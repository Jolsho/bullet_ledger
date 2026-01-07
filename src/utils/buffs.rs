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
use std::ops::{Deref, DerefMut};

use crate::peer_net::header::{HEADER_LEN, PREFIX_LEN};


#[derive(PartialEq, Eq, Debug, Clone)]
pub struct WriteBuffer {
    buf: Vec<u8>,
}

impl WriteBuffer {
    pub fn new(cap: usize) -> Self {
        let mut s = Self { 
            buf: Vec::with_capacity(cap), 
        };
        s.reset();

        s
    }
    pub fn from_vec(buf: Vec<u8>) -> Self {
        Self { buf }
    }
    pub fn release_buffer(&mut self) -> Vec<u8> {
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.buf);
        read_buf
    }

    // resize back to PREFIX_LEN + HEADER_LEN
    pub fn reset(&mut self) {
        self.buf.resize(PREFIX_LEN + HEADER_LEN, 0);
    }
}

impl Deref for WriteBuffer {
    type Target = Vec<u8>;
    fn deref(&self) -> &Self::Target {
        &self.buf
    }
}
impl DerefMut for WriteBuffer {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.buf
    }
}
