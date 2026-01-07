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

use std::thread::JoinHandle;
use mio::{Events, Token};
use core::error;

use crate::{RPC, NETWORKER};
use crate::server::from_internals_from_vec;
use crate::spsc::{Consumer, Producer};
use crate::utils::{shutdown::should_shutdown, msg::NetMsg};
use crate::config::BlockchainConfig;

use schnorr::TrxGenerators; 
use trxs::Trx;
use codes::*;
use ledger::Ledger;
use blockchain::Blockchain;

pub mod consensus;
pub mod execution;
pub mod priority;
pub mod schnorr;
pub mod trxs;
pub mod ledger;
pub mod codes;
pub mod blockchain;

pub type Hash = [u8; 32];


pub fn start_blockchain(
    config: BlockchainConfig,
    tos: Vec<(Producer<NetMsg>, Token)>,
    froms: Vec<(Consumer<NetMsg>, Token)>,
) -> Result<JoinHandle<Result<(),()>>, Box<dyn error::Error>> {
    Ok(std::thread::spawn(move || {
        let mut events = Events::with_capacity(config.event_len);

        let mut blockchain = Blockchain::new(config, tos)
            .map_err(|_| ())?;

        let mut from_internals = from_internals_from_vec(&mut blockchain.poll, froms).map_err(|_| ())?;

        // Start polling
        loop {
            if should_shutdown() { break; }
            blockchain.consensus.poll();
            if blockchain.poll.poll(&mut events, blockchain.poll_timeout).is_err() { continue; }

            for ev in events.iter() {
                let token = ev.token();
                if let Some(from) = from_internals.get_mut(&token) {
                    if let Some(mut msg) = from.pop() {
                        match token {
                            NETWORKER => from_net(&mut blockchain, &mut msg),
                            RPC => from_rpc(&mut blockchain, &mut msg),
                            _ => {}
                        }

                        let _ = from.recycle(msg);
                    }
                }
            }
        }
        Ok(())
    }))
}

pub fn from_rpc(blockchain: &mut Blockchain, m: &mut NetMsg) {}

pub fn from_net(blockchain: &mut Blockchain, m: &mut NetMsg) {
    match blockchain_code_from_u8(m.body[0]){
        BlockchainCodes::NewTrx => {
            // get trx by type
            match blockchain.pool.get_value(m.body[1] as u8) {
                Some(Trx::Ephemeral(mut trx)) => {

                    if trx.unmarshal(&mut m.body[2..]).is_ok() && 
                    blockchain.ledger.db_exists(&trx.sender_init.0).is_ok() && 
                    blockchain.ledger.db_exists(&trx.receiver_init.0).is_ok() &&
                    trx.is_valid(&blockchain.gens).is_ok() {          

                        // TODO -- if other trx is in pool we append this one
                        // because its valid once that gets processed but not before

                        blockchain.pool.insert(Trx::Ephemeral(trx));
                    } else {
                        blockchain.pool.recycle_value(Trx::Ephemeral(trx));
                    }
                }
                Some(Trx::Hidden(mut trx)) => { }
                Some(Trx::Regular(mut trx)) => { }
                None => {}
            }
        },

        BlockchainCodes::NewBlock => { },

        BlockchainCodes::None => { },

        _ => {}
    }
}
