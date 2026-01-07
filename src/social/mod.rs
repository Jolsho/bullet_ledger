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

use std::{error, thread::JoinHandle};

use mio::{Events, Token};

use crate::config::SocialConfig;
use crate::{NETWORKER, RPC};
use crate::server::from_internals_from_vec;
use crate::spsc::{Consumer, Producer}; 
use crate::utils::msg::NetMsg;
use crate::utils::shutdown::should_shutdown;

pub mod social;
pub mod handlers;
use social::*;

pub fn start_social(
    config: SocialConfig, 
    tos: Vec<(Producer<NetMsg>, Token)>, 
    froms: Vec<(Consumer<NetMsg>, Token)>,
) -> Result<JoinHandle<Result<(),()>>, Box<dyn error::Error>> {
    Ok(std::thread::spawn(move || {
        let mut events = Events::with_capacity(config.event_len);

        let mut node = Social::new(config, tos)
            .map_err(|_| ())?;

        let mut from_internals = from_internals_from_vec(&mut node.poll, froms).map_err(|_| ())?;

        // Start polling
        loop {
            if should_shutdown() { break; }
            if node.poll.poll(&mut events, node.poll_timeout).is_err() { continue; }

            for ev in events.iter() {
                let token = ev.token();
                if let Some(from) = from_internals.get_mut(&token) {
                    if let Some(mut msg) = from.pop() {
                        match token {
                            NETWORKER => from_net(&mut node, &mut msg),
                            RPC => from_rpc(&mut node, &mut msg),
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

pub fn from_rpc(node: &mut Social, m: &mut NetMsg) {}

pub fn from_net(node: &mut Social, m: &mut NetMsg) {}
