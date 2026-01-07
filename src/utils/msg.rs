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
use std::net::SocketAddr;
use std::ops::{Deref, DerefMut};

use mio::Token;

use crate::utils::random::random_b2;
use crate::peer_net::handlers::PacketCode;
use crate::peer_net::{connection::PeerConnection, handlers::Handler};
use crate::spsc::Msg;
use crate::utils::buffs::WriteBuffer;


#[derive(PartialEq, Eq, Debug, Clone)]
pub enum NetManCode {
    None,
    AddPeer,
    RemovePeer,
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum NetMsgCode {
    Internal(NetManCode),
    External(PacketCode),
}
impl NetMsgCode {
    pub fn is_internal(&self) -> bool {
        match self {
            NetMsgCode::Internal(_) => true,
            _ => false,
        }
    }
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct NetMsg(Box<NetMsgInner>);
impl Deref for NetMsg {
    type Target = Box<NetMsgInner>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
impl DerefMut for NetMsg {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}
impl Msg for NetMsg {
    fn new(default_cap: Option<usize>) -> Self {
        NetMsg(Box::new(NetMsgInner::new(default_cap)))
    }
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct NetMsgInner {
    pub id: u16,
    pub from_code: Token,
    pub stream_token: Token,

    pub addr: Option<SocketAddr>,
    pub pub_key: Option<[u8;32]>,

    pub code: NetMsgCode,
    pub body: WriteBuffer,
    pub handler: Option<Handler>,
}

impl NetMsgInner {
    pub fn new(cap: Option<usize>) -> Self { 
        let mut s = Self { 
            id: u16::from_le_bytes(random_b2()),
            code: NetMsgCode::Internal(NetManCode::None),
            stream_token: Token(0), 
            from_code: Token(0),
            addr: None,
            body: WriteBuffer::new(cap.unwrap()),
            handler: None,
            pub_key: None,
        };
        s.reset();
        s
    }
    pub fn fill_fd_and_id(&mut self, conn: &mut PeerConnection) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_token = conn.token;
        self.from_code = Token(0);
        self.addr = None;
        self.pub_key = None;
    }
    pub fn reset(&mut self) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_token =  Token(0);
        self.from_code = Token(0);
        self.addr = None;
        self.pub_key = None;
        self.body.reset();
    }

    pub fn fill_for_internal(&mut self, from_code: Token, code: NetManCode, buff: &[u8]) {
        self.reset();
        self.from_code = from_code;
        self.code = NetMsgCode::Internal(code);
        self.body.extend_from_slice(buff);
    }
}
