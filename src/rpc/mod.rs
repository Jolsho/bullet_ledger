/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

use std::io;
use std::net::SocketAddr;
use std::{error, thread::JoinHandle };

use mio::Token;

use crate::config::ServerConfig; 
use crate::spsc::{Consumer, Producer};
use crate::utils::{NetError, NetMsg};
use crate::rpc::connection::RpcConn;
use crate::server::{to_internals_from_vec, NetServer};
use crate::{CORE, NETWORKER};

pub mod connection;

pub fn start_rpc(config: ServerConfig, 
    tos: Vec<(Producer<NetMsg>, Token)>,
    froms: Vec<(Consumer<NetMsg>, Token)>,
) -> Result<JoinHandle<io::Result<()>>, Box<dyn error::Error>> {

    let to_internals = to_internals_from_vec(tos);

    let mut server = NetServer::<RpcConn>::new(&config, to_internals)?;
    Ok(std::thread::spawn(move || {
        let allow_connection = |_addr: &SocketAddr| { true };

        let handle_errored = |_e: NetError, _addr: &SocketAddr, _s: &mut NetServer<RpcConn> | {};

        let handle_internal = |msg: NetMsg, server: &mut NetServer<RpcConn> | {
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
