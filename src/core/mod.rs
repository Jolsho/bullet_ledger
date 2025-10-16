use core::error;
use std::time::Duration;
use std::{thread::JoinHandle};
use mio::{Events, Poll, Token};

use crate::core::db::Ledger;
use crate::server::{from_internals_from_vec, ToInternals};
use crate::utils::{NetMsgCode,NetMsg};
use crate::{config::CoreConfig, crypto::TrxGenerators, trxs::Trx};
use crate::spsc::{Consumer, Producer};
use crate::{peer_net::handlers::PacketCode, NETWORKER};
use {consensus::Consensus, priority::PriorityPool};

pub mod consensus;
pub mod execution;
pub mod priority;
pub mod db;

pub type TrxPool = PriorityPool<Box<Hash>, Box<Trx>>;

#[derive(PartialEq, Eq, Hash, Clone, Copy, PartialOrd, Ord)]
pub struct Hash(pub [u8;32]);
impl Hash {
    pub const ZERO: Hash = Hash([0u8; 32]);
    pub fn copy_from_slice(&mut self, buff: &[u8]) {
        self.0.copy_from_slice(buff);
    }
}
impl Default for Hash {
    fn default() -> Self { Hash::ZERO }
}


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////


pub struct Core {
    to_internals: ToInternals,
    poll: Poll,
    ledger: Ledger,

    pool: TrxPool,
    gens: TrxGenerators,
    consensus: Consensus,
}

impl Core {
    pub fn new(
        config: &CoreConfig, 
        mut tos: Vec<(Producer<NetMsg>, Token)>,
    ) -> Result<Self, Box<dyn error::Error>> {

        let mut to_internals = ToInternals::with_capacity(tos.len());
        while tos.len() > 0 {
            let (chan, t) = tos.pop().unwrap();
            to_internals.insert(t, chan);
        }

        Ok(Self { 
            consensus: Consensus::new(),
            pool: TrxPool::new(config.pool_cap), 
            gens: TrxGenerators::new("bullet_ledger", config.bullet_count),
            poll: Poll::new()?,
            ledger: Ledger::new(config.db_path.clone())?,
            to_internals,
        })
    }
}

pub fn start_core(
    config: CoreConfig,
    tos: Vec<(Producer<NetMsg>, Token)>,
    froms: Vec<(Consumer<NetMsg>, Token)>,
) -> Result<JoinHandle<()>, Box<dyn error::Error>> {

    let mut core = Core::new(&config, tos)?;

    let mut from_internals = from_internals_from_vec(&mut core.poll, froms)?;

    let mut events = Events::with_capacity(config.event_len);
    let polltimeout = Duration::from_millis(config.idle_polltimeout);

    Ok(std::thread::spawn(move || {
        // Start polling
        loop {
            if core.poll.poll(&mut events, Some(polltimeout)).is_err() {
                continue;
            }

            for ev in events.iter() {
                let token = ev.token();
                match token {
                    NETWORKER => { // Msg from Networker
                        if let Some(from) = from_internals.get_mut(&token) {
                            if let Some(mut msg) = from.pop() {
                                handle_from_net(&mut core, &mut msg);
                                from.recycle(msg);
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
    }))
}


/*
*   Engine? NOT pool? => 
*       Engine entails validation if need be.
*       Execution client...
*           Validates Trxs as they come in...
*           Processes Commands from Consensus
*               like remove batch...
*           Must update state in DB
*
*   Consensus => Own Message Types
*       Needs to communicate with DB for block history...
*           Maybe... probably not
*
*/
pub fn handle_from_net(
    core: &mut Core,
    m: &mut NetMsg
) {
    match m.code {
        NetMsgCode::External(PacketCode::NewTrx) => {
            let mut trx = core.pool.get_value();
            std::mem::swap(&mut trx.buffer, &mut *m.body);

            if trx.unmarshal_internal().is_err() || 
                !trx.is_valid(&core.gens)
            {
                core.pool.recycle_value(trx);
            } else {
                core.pool.insert(trx);
            }
        },

        NetMsgCode::External(PacketCode::NewBlock) => {
            const BLOCK_SIZE:usize = 3; // TODO -- what is this

            let mut keys = Vec::with_capacity(BLOCK_SIZE);
            for i in 0..BLOCK_SIZE {
                if i + 32 > m.body.len() { break; }
                let mut k = core.pool.get_key();
                k.copy_from_slice(&m.body[i..i+32]);
                keys.push(k);
            }

            core.pool.remove_batch(keys)
        },
        _ => {}
    }
}



