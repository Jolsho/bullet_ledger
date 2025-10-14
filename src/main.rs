use crate::config::load_config;
use crate::core::start_core;
use crate::msging::MsgQ;
use crate::networker::{start_networker, utils::NetMsg};

mod trxs;
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

    // Channel from networker to core.
    let net_to_core =  MsgQ::<NetMsg>::new(128, NETWORKER).unwrap();
    let (to_core, from_net) = net_to_core.split().unwrap();

    // Channel core to networker.
    let core_to_net =  MsgQ::<NetMsg>::new(128, CORE).unwrap();
    let (to_net, from_core) = core_to_net.split().unwrap();

    let core_handle = start_core(
        config.core.clone(), from_net, to_net
    ).unwrap();

    let net_handle = start_networker(
        config.network.clone(), to_core, 
        vec![(from_core, CORE)],
    ).unwrap();

    core_handle.join().unwrap();
    net_handle.join().unwrap();
}
