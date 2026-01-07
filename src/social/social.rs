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

use std::error;
use std::time::Duration;

use mio::{Poll, Token};

use crate::config::SocialConfig;
use crate::server::ToInternals;
use crate::spsc::Producer; 
use crate::utils::msg::NetMsg;


pub struct Social {
    to_internals: ToInternals,

    pub poll: Poll,
    pub poll_timeout: Option<Duration>,

    config: SocialConfig,
}

impl Social {
    pub fn new(
        config: SocialConfig, 
        mut tos: Vec<(Producer<NetMsg>, Token)>,
    ) -> Result<Self, Box<dyn error::Error>> {

        let mut to_internals = ToInternals::with_capacity(tos.len());
        while tos.len() > 0 {
            let (chan, t) = tos.pop().unwrap();
            to_internals.insert(t, chan);
        }

        Ok(Self { 
            poll: Poll::new()?,
            poll_timeout: Some(Duration::from_secs(config.idle_polltimeout)),
            to_internals,
            config,
        })
    }
}
