use std::time::Duration;
use mio::{Poll, Token};
use core::error;

use crate::server::ToInternals;
use crate::spsc::Producer;
use crate::utils::random::random_b32;
use crate::utils::msg::NetMsg;
use crate::config::BlockchainConfig;

use super::priority::TrxPool;
use super::consensus::Consensus;
use super::schnorr::TrxGenerators;


pub struct Blockchain {
    pub to_internals: ToInternals,

    pub ledger: super::Ledger,

    pub poll: Poll,
    pub poll_timeout: Option<Duration>,

    pub pool: TrxPool,
    pub gens: TrxGenerators,
    pub consensus: Consensus,
    pub config: BlockchainConfig,
}

impl Blockchain {
    pub fn new(
        config: BlockchainConfig, 
        mut tos: Vec<(Producer<NetMsg>, Token)>,
    ) -> Result<Self, Box<dyn error::Error>> {

        let mut to_internals = ToInternals::with_capacity(tos.len());
        while tos.len() > 0 {
            let (chan, t) = tos.pop().unwrap();
            to_internals.insert(t, chan);
        }

        let ledger = super::Ledger::open(
            &config.ledger_path, 
            config.ledger_cache_size,
            config.ledger_map_size,
            &config.ledger_tag,
            Some(random_b32())
        )?;

        Ok(Self { 
            consensus: Consensus::new(config.epoch_interval),
            pool: TrxPool::new(config.pool_cap), 
            gens: TrxGenerators::new("bullet_ledger", config.bullet_count),
            poll: Poll::new()?,
            poll_timeout: Some(Duration::from_secs(config.idle_polltimeout)),
            to_internals,
            ledger,
            config,
        })
    }
}
