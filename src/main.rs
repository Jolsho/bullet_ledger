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

use mio::Token;

use crate::config::load_config;
use crate::core::start_core;
use crate::peer_net::start_peer_networker;
use crate::rpc::start_rpc;
use crate::spsc::SpscQueue;

mod trxs;
mod tests;
mod core;
mod peer_net;
mod crypto;
mod config;
mod rpc;
mod server;
mod utils;
mod spsc;

const NETWORKER: Token = Token(707070);
const CORE: Token = Token(717171);
const RPC: Token = Token(727272);


fn main() {
    let config = load_config("config.toml");
    let buffer_size = Some(config.peer.max_buffer_size.clone());

    // PEER_NET && CORE
    let (to_core_net, from_net_core) = SpscQueue::new(256, buffer_size).unwrap();
    let (to_net_core, from_core_net) = SpscQueue::new(256, buffer_size).unwrap();


    // PEER_NET && RPC
    let (to_net_rpc, from_rpc_net) = SpscQueue::new(128, buffer_size).unwrap();
    let (to_rpc_net, from_net_rpc) = SpscQueue::new(128, buffer_size).unwrap();


    // CORE && RPC
    let (to_rpc_core, from_core_rpc) = SpscQueue::new(128, buffer_size).unwrap();
    let (to_core_rpc, from_rpc_core) = SpscQueue::new(128, buffer_size).unwrap();


    // START CORE
    let core_handle = start_core(
        config.core.clone(), 
        vec![
            (to_net_core, NETWORKER),
            (to_rpc_core, RPC),
        ],
        vec![
            (from_net_core, NETWORKER),
            (from_rpc_core, RPC)
        ],
    ).unwrap();


    // START RPC
    let rpc_handle = start_rpc(
        config.server.clone(), 
        vec![
            (to_net_rpc, NETWORKER),
            (to_core_rpc, CORE),
        ],
        vec![
            (from_net_rpc, NETWORKER),
            (from_core_rpc, CORE),
        ],
    ).unwrap();


    // START NETWORKER
    let net_handle = start_peer_networker(
        config.peer.clone(), 
        vec![
            (to_core_net, CORE),
            (to_rpc_net, RPC),
        ],
        vec![
            (from_core_net, CORE), 
            (from_rpc_net, RPC)
        ],
    ).unwrap();


    let _ = core_handle.join().unwrap();
    let _ = net_handle.join().unwrap();
    let _ = rpc_handle.join().unwrap();
}
