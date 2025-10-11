use ringbuf::traits::Split;

use crate::config::load_config;
use crate::core::{start_core, msg::CoreMsg};
use crate::msging::{MsgQ, Ring};
use crate::networker::{start_networker, netman::NetMsg};
use crate::trxs::Trx;

mod trxs;
mod db;
mod tests;
mod core;
mod networker;
mod crypto;
mod msging;
mod shutdown;
mod config;

pub const NETWORKER: i32 = 1;
pub const CORE: i32 = 2;
pub const TRX_POOL: i32 = 3;

fn main() {
    let config = load_config("config.toml");
    let trx_ring = Ring::<Trx>::new(512);
    let (trx_prod, trx_con) = trx_ring.split();

    let net_to_core =  MsgQ::<CoreMsg>::new(128, NETWORKER).unwrap();
    let (net_core_prod, net_core_cons) = net_to_core.split().unwrap();

    let core_to_net =  MsgQ::<NetMsg>::new(128, CORE).unwrap();
    let (core_net_prod, core_net_cons) = core_to_net.split().unwrap();

    let core_handle = start_core(config.core.clone(), 
        trx_prod, net_core_cons, core_net_prod
    );
    let net_handle = start_networker(config.network.clone(),
        trx_con, net_core_prod, core_net_cons,
    );

    core_handle.join().unwrap();
    net_handle.join().unwrap();
}
