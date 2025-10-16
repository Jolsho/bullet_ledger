use std::io;
use std::net::SocketAddr;
use std::{error, thread::JoinHandle };

use mio::Token;

use crate::config::ServerConfig; 
use crate::msging::{MsgCons, MsgProd};
use crate::utils::{NetError, NetMsg};
use crate::rpc::connection::RpcConn;
use crate::server::{to_internals_from_vec, NetServer};
use crate::{CORE, NETWORKER};

pub mod connection;

pub fn start_rpc(config: ServerConfig, 
    tos: Vec<(MsgProd<NetMsg>, Token)>,
    froms: Vec<(MsgCons<NetMsg>, Token)>,
) -> Result<JoinHandle<io::Result<()>>, Box<dyn error::Error>> {

    let to_internals = to_internals_from_vec(tos);

    let mut server = NetServer::<RpcConn>::new(&config, to_internals)?;
    Ok(std::thread::spawn(move || {
        let allow_connection = |_addr: &SocketAddr| { true };

        let handle_errored = |_e: NetError, _addr: &SocketAddr, _s: &mut NetServer<RpcConn> | {};

        let handle_internal = |msg: Box<NetMsg>, server: &mut NetServer<RpcConn> | {
            let token = msg.from_code;

            if token == NETWORKER {
                let new_msg = server.collect_internal(&CORE);
                server.enqueue_internal((CORE, new_msg));
            }
            Some(msg) //RECYCLE
        };

        server.start( froms, handle_internal, allow_connection, handle_errored,)
    }))
}
