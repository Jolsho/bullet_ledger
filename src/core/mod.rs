use std::io;
use std::{thread::JoinHandle, time::Duration};
use curve25519_dalek::{ristretto::CompressedRistretto, RistrettoPoint};
use mio::{Events, Poll, Token};
use sha2::{Digest, Sha256};
use core::error;

use crate::trxs::Trx;
use crate::RPC;
use crate::server::{from_internals_from_vec, ToInternals};
use crate::spsc::{Consumer, Producer};
use crate::utils::{should_shutdown, NetMsg, NetMsgCode};
use crate::config::CoreConfig;
use crate::crypto::{TrxGenerators, schnorr::SchnorrProof};
use crate::{peer_net::handlers::PacketCode, NETWORKER};

use {priority::TrxPool, ledger::Ledger, consensus::Consensus};

pub mod consensus;
pub mod execution;
pub mod priority;
pub mod utils;
pub mod ledger;

pub struct Core {
    to_internals: ToInternals,

    ledger: Ledger,

    poll: Poll,
    poll_timeout: Option<Duration>,

    pool: TrxPool,
    gens: TrxGenerators,
    consensus: Consensus,
    config: CoreConfig,
}

impl Core {
    pub fn new(
        config: CoreConfig, 
        mut tos: Vec<(Producer<NetMsg>, Token)>,
    ) -> Result<Self, Box<dyn error::Error>> {

        let mut to_internals = ToInternals::with_capacity(tos.len());
        while tos.len() > 0 {
            let (chan, t) = tos.pop().unwrap();
            to_internals.insert(t, chan);
        }


        Ok(Self { 
            consensus: Consensus::new(config.epoch_interval),
            pool: TrxPool::new(config.pool_cap), 
            gens: TrxGenerators::new("bullet_ledger", config.bullet_count),
            poll: Poll::new()?,
            ledger: Ledger::new(&config.db_path, config.db_cache_size)?,
            poll_timeout: Some(Duration::from_secs(config.idle_polltimeout)),
            to_internals,
            config,
        })
    }

    pub fn poll(&mut self, events: &mut Events) -> io::Result<()> {
        self.consensus.poll();
        self.poll.poll(events, self.poll_timeout)?;
        Ok(())
    }
}

pub fn start_core(
    config: CoreConfig,
    tos: Vec<(Producer<NetMsg>, Token)>,
    froms: Vec<(Consumer<NetMsg>, Token)>,
) -> Result<JoinHandle<Result<(),()>>, Box<dyn error::Error>> {
    Ok(std::thread::spawn(move || {
        let mut events = Events::with_capacity(config.event_len);
        let mut core = Core::new(config, tos).map_err(|_| ())?;
        let mut from_internals = from_internals_from_vec(&mut core.poll, froms).map_err(|_| ())?;
        // Start polling
        loop {
            if should_shutdown() { break; }
            if core.poll(&mut events).is_err() { continue; }

            for ev in events.iter() {
                let token = ev.token();
                if let Some(from) = from_internals.get_mut(&token) {
                    if let Some(mut msg) = from.pop() {
                        match token {
                            NETWORKER => from_net(&mut core, &mut msg),
                            RPC => from_rpc(&mut core, &mut msg),
                            _ => {}
                        }

                        from.recycle(msg);
                    }
                }
            }
        }
        Ok(())
    }))
}

pub fn from_rpc(core: &mut Core, m: &mut NetMsg) {}

pub fn from_net(core: &mut Core, m: &mut NetMsg) {
    match m.code {
        NetMsgCode::External(PacketCode::NewTrx) => {
            // get trx by type
            match core.pool.get_value(m.body[0] as u8) {
                Some(Trx::Ephemeral(mut trx)) => {
                    if trx.unmarshal(&mut m.body[1..]).is_ok() && 
                    core.ledger.value_exists(&trx.sender_init.0) &&                     
                    core.ledger.value_exists(&trx.receiver_init.0) &&
                    trx.is_valid(&core.gens).is_ok() {          

                        // TODO -- if other trx is in pool we append this one
                        // because its valid once that gets processed but not before

                        core.pool.insert(Trx::Ephemeral(trx));
                    } else {
                        core.pool.recycle_value(Trx::Ephemeral(trx));
                    }
                }
                Some(Trx::Hidden(mut trx)) => { }
                Some(Trx::Regular(mut trx)) => { }
                None => {}
            }
        },

        NetMsgCode::External(PacketCode::NewBlock) => {
            let mut cursor = 32;
            let mut validator_commit = [0u8; 32];
            validator_commit.copy_from_slice(&m.body[..cursor]);
            let validator = CompressedRistretto::from_slice(&validator_commit).unwrap();

            if core.consensus.is_validator(&validator) {

                let mut v_proof = SchnorrProof::default();
                v_proof.from_bytes(&mut m.body[cursor..cursor+96]);
                cursor += 96;

                let context_hash: [u8;32] = Sha256::digest(&m.body[cursor..]).into();

                if !v_proof.verify(&core.gens, &validator, &context_hash) { return; }

                let mut fee_commit = RistrettoPoint::default();
                let mut k = core.pool.get_key();

                for _ in 0..core.config.trxs_per_block {
                    k.copy_from_slice(&m.body[cursor..cursor+32]);
                    cursor += 32;

                    if let Some((trx, _)) = core.pool.get(&k) {
                        trx.execute(
                            &mut core.ledger, 
                            &mut core.gens, 
                            &mut fee_commit,
                        );
                    }

                    core.pool.remove_one(&k);
                }

                if let Some(root_hash) = core.ledger.put(validator.as_bytes(), fee_commit.compress().as_bytes().to_vec()) {
                }
            }
        },

        _ => {}
    }
}
