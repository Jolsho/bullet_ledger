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

use crate::spsc::SpscQueue;

mod tests;
mod blockchain;
mod peer_net;
mod config;
mod rpc;
mod server;
mod utils;
mod spsc;
mod social;

const NETWORKER: Token = Token(707070);
const BLOCKCHAIN: Token = Token(717171);
const RPC: Token = Token(727272);
const SOCIAL: Token = Token(737373);


fn main() {
    let config = config::load_config("config.toml");
    let buffer_size = Some(config.peer.max_buffer_size.clone());

    // PEER_NET <-> BLOCKCHAIN
    let (to_blockchain_net, from_net_blockchain) = SpscQueue::new(256, buffer_size).unwrap();
    let (to_net_blockchain, from_blockchain_net) = SpscQueue::new(256, buffer_size).unwrap();


    // PEER_NET <-> RPC
    let (to_net_rpc, from_rpc_net) = SpscQueue::new(128, buffer_size).unwrap();
    let (to_rpc_net, from_net_rpc) = SpscQueue::new(128, buffer_size).unwrap();

    // PEER_NET <-> SOCIAL
    let (to_net_social, from_social_net) = SpscQueue::new(128, buffer_size).unwrap();
    let (to_social_net, from_net_social) = SpscQueue::new(128, buffer_size).unwrap();

    // RPC <-> SOCIAL
    let (to_social_rpc, from_rpc_social) = SpscQueue::new(128, buffer_size).unwrap();
    let (to_rpc_social, from_social_rpc) = SpscQueue::new(128, buffer_size).unwrap();


    // BLOCKCHAIN <-> RPC
    let (to_rpc_blockchain, from_blockchain_rpc) = SpscQueue::new(128, buffer_size).unwrap();
    let (to_blockchain_rpc, from_rpc_blockchain) = SpscQueue::new(128, buffer_size).unwrap();


    // START BLOCKCHAIN
    let blockchain_handle = blockchain::start_blockchain(
        config.blockchain.clone(), 
        vec![
            (to_net_blockchain, NETWORKER),
            (to_rpc_blockchain, RPC),
        ],
        vec![
            (from_net_blockchain, NETWORKER),
            (from_rpc_blockchain, RPC)
        ],
    ).unwrap();


    // START RPC
    let rpc_handle = rpc::start_rpc(
        config.rpc.clone(), 
        vec![
            (to_net_rpc, NETWORKER),
            (to_blockchain_rpc, BLOCKCHAIN),
            (to_social_rpc, SOCIAL),
        ],
        vec![
            (from_net_rpc, NETWORKER),
            (from_blockchain_rpc, BLOCKCHAIN),
            (from_social_rpc, SOCIAL),
        ],
    ).unwrap();

    // START SOCIAL
    let social_handle = social::start_social(
        config.social.clone(), 
        vec![
            (to_net_social, NETWORKER),
            (to_rpc_social, RPC),
        ],
        vec![
            (from_rpc_social, RPC),
            (from_net_social, NETWORKER),
        ],
    ).unwrap();


    // START NETWORKER
    let net_handle = peer_net::start_peer_networker(
        config.peer.clone(), 
        vec![
            (to_blockchain_net, BLOCKCHAIN),
            (to_rpc_net, RPC),
            (to_social_net, SOCIAL),
        ],
        vec![
            (from_blockchain_net, BLOCKCHAIN), 
            (from_rpc_net, RPC),
            (from_social_net, SOCIAL),
        ],
    ).unwrap();


    let _ = net_handle.join().unwrap();
    let _ = blockchain_handle.join().unwrap();
    let _ = social_handle.join().unwrap();
    let _ = rpc_handle.join().unwrap();
}
