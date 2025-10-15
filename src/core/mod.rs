use core::error;
use std::time::Duration;
use std::{thread::JoinHandle};
use mio::{Events, Interest, Poll};

use crate::core::db::Ledger;
use crate::networker::utils::NetMsgCode;
use crate::{config::CoreConfig, crypto::TrxGenerators, trxs::Trx};
use crate::msging::{MsgCons, MsgProd};
use crate::{networker::{utils::NetMsg, handlers::PacketCode}, NETWORKER};
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
    poll: Poll,
    to_net: MsgProd<NetMsg>,
    ledger: Ledger,

    pool: TrxPool,
    gens: TrxGenerators,
    consensus: Consensus,
}

impl Core {
    pub fn new(
        config: &CoreConfig, 
        to_net: MsgProd<NetMsg>,
    ) -> Result<Self, Box<dyn error::Error>> {
        Ok(Self { 
            consensus: Consensus::new(),
            pool: TrxPool::new(config.pool_cap), 
            gens: TrxGenerators::new("bullet_ledger", config.bullet_count),
            poll: Poll::new()?,
            to_net, 
            ledger: Ledger::new(config.db_path.clone())?,
        })
    }
}

pub fn start_core(
    config: CoreConfig,
    mut from_net: MsgCons<NetMsg>,
    to_net: MsgProd<NetMsg>,
) -> Result<JoinHandle<()>, Box<dyn error::Error>> {

    let mut core = Core::new(&config, to_net)?;
    core.poll.registry().register(&mut from_net, NETWORKER, Interest::READABLE)?;

    let mut events = Events::with_capacity(config.event_len);
    let polltimeout = Duration::from_millis(config.idle_polltimeout);

    Ok(std::thread::spawn(move || {
        // Start polling
        loop {
            if core.poll.poll(&mut events, Some(polltimeout)).is_err() {
                continue;
            }

            for ev in events.iter() {
                match ev.token() {
                    NETWORKER => { // Msg from Networker
                        if let Some(mut m) = from_net.pop() {
                            handle_from_net( &mut core, &mut m);
                            from_net.recycle(m);
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
                return;
            }
            core.pool.insert(trx);
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



