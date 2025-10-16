use std::io;
use std::net::SocketAddr;
use std::{error, thread::JoinHandle };

use crate::config::ServerConfig; 
use crate::msging::MsgProd;
use crate::networker::utils::{NetError, NetMsg};
use crate::rpc::connection::RpcConn;
use crate::server::{FromInternals, NetServer, ToInternals};
use crate::{CORE, NETWORKER};

pub mod connection;

pub fn start_rpc(config: ServerConfig, to_net: MsgProd<NetMsg>
) -> Result<JoinHandle<io::Result<()>>, Box<dyn error::Error>> {
    let mut to_internals = ToInternals::with_capacity(1);
    to_internals.insert(NETWORKER, to_net);

    let from_internals = FromInternals::with_capacity(0);

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

        server.start( from_internals, handle_internal, allow_connection, handle_errored,)
    }))
}
