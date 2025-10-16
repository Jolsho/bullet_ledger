use mio::Token;

use crate::config::load_config;
use crate::core::start_core;
use crate::msging::MsgQ;
use crate::networker::start_peer_networker;
use crate::networker::utils::NetMsg;
use crate::rpc::start_rpc;

mod trxs;
mod tests;
mod core;
mod networker;
mod crypto;
mod msging;
mod shutdown;
mod config;
mod rpc;
mod server;

const NETWORKER: Token = Token(707070);
const CORE: Token = Token(717171);
const RPC: Token = Token(727272);

fn main() {
    let config = load_config("config.toml");

    // Channel from networker to core.
    let net_to_core =  MsgQ::<NetMsg>::new(128).unwrap();
    let (to_core_net, from_net_core) = net_to_core.split().unwrap();

    // Channel core to networker.
    let core_to_net =  MsgQ::<NetMsg>::new(128).unwrap();
    let (to_net_core, from_core_net) = core_to_net.split().unwrap();

    // Channel RPC to networker.
    let rpc_to_net =  MsgQ::<NetMsg>::new(128).unwrap();
    let (to_net_rpc, from_rpc_net) = rpc_to_net.split().unwrap();



    // START CORE
    let core_handle = start_core(
        config.core.clone(), 
        from_net_core, to_net_core
    ).unwrap();


    // START RPC
    let rpc_handle = start_rpc(
        config.server.clone(), 
        to_net_rpc
    ).unwrap();


    // START NETWORKER
    let net_handle = start_peer_networker(
        config.peer.clone(), to_core_net, 
        vec![
            (from_core_net, CORE), 
            (from_rpc_net, RPC)
        ],
    ).unwrap();


    core_handle.join().unwrap();
    let _ = net_handle.join().unwrap();
    let _ = rpc_handle.join().unwrap();
}
