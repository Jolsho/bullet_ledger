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

use crate::peer_net::ping_pong::handle_ping;
use crate::{BLOCKCHAIN, SOCIAL};
use crate::server::NetServer;
use crate::utils::errors::{NetResult, NetError}; 
use crate::utils::msg::{NetMsg, NetMsgCode};

use super::connection::PeerConnection;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PacketCode {
    None = 0,
    Ping = 1,

    NegotiationSyn = 2,
    NegotiationAck = 3,

    Social = 4,
    Blockchain = 5,
}

pub fn code_from_u8(dig: u8) -> PacketCode {
    match dig {
        1 => PacketCode::Ping,
        2 => PacketCode::NegotiationSyn,
        3 => PacketCode::NegotiationAck,

        4 => PacketCode::Social,
        5 => PacketCode::Blockchain,

        _ => PacketCode::None,
    }
}


#[derive(PartialEq, Eq, Debug, Clone)]
pub enum HandlerRes {
    Read((u16, Handler)),
    Write(NetMsg),
    None,
}

pub type Handler = fn(&mut PeerConnection, &mut NetServer<PeerConnection>) -> NetResult<HandlerRes>;

pub fn code_switcher(conn: &mut PeerConnection, server: &mut NetServer<PeerConnection>) -> NetResult<HandlerRes> {
    match conn.read_header.code {
        PacketCode::Ping => handle_ping(conn, server),

        PacketCode::NegotiationSyn | 
        PacketCode::NegotiationAck => conn.handle_negotiation(server),


        PacketCode::Blockchain => forward_2(BLOCKCHAIN, conn, server),
        PacketCode::Social => forward_2(SOCIAL, conn, server),


        PacketCode::None => Err(NetError::Other(format!("PacketCode::None"))),
    }
}

pub fn forward_2(
    token: Token,
    conn: &mut PeerConnection, 
    server: &mut NetServer<PeerConnection>
) -> NetResult<HandlerRes> {
    let mut msg = server.collect_internal(&token);
    std::mem::swap(&mut *msg.body, &mut conn.read_buf);
    msg.code = NetMsgCode::External(conn.read_header.code);

    server.enqueue_internal((token, msg));

    Err(NetError::Other(format!("bad TRX")))
}
