
use crate::networker::connection::PeerConnection;
use crate::networker::utils::NetMsgCode;
use crate::server::NetServer;
use crate::CORE;

use super::utils::{NetResult, NetError};
use super::utils::NetMsg;

pub mod ping_pong;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PacketCode {
    None = 0,
    Ping = 1,

    NegotiationSyn = 2,
    NegotiationAck = 3,

    NewTrx = 4,
    NewBlock = 5,
}

pub fn code_from_u8(dig: u8) -> PacketCode {
    match dig {
        1 => PacketCode::Ping,
        2 => PacketCode::NegotiationSyn,
        3 => PacketCode::NegotiationAck,
        4 => PacketCode::NewTrx,
        5 => PacketCode::NewBlock,
        _ => PacketCode::None,
    }
}


#[derive(PartialEq, Eq, Debug, Clone)]
pub enum HandlerRes {
    Read((u16, Handler)),
    Write(Box<NetMsg>),
    None,
}

pub type Handler = fn(&mut PeerConnection, &mut NetServer<PeerConnection>) -> NetResult<HandlerRes>;

pub fn code_switcher(conn: &mut PeerConnection, server: &mut NetServer<PeerConnection>) -> NetResult<HandlerRes> {
    match conn.read_header.code {
        PacketCode::Ping => ping_pong::handle_ping(conn, server),

        PacketCode::NegotiationSyn | 
        PacketCode::NegotiationAck => conn.handle_negotiation(server),

        PacketCode::NewTrx |
        PacketCode::NewBlock => forward_2_core(conn, server),

        PacketCode::None => Err(NetError::Other(format!("PacketCode::None"))),
    }
}

pub fn forward_2_core(
    conn: &mut PeerConnection, 
    server: &mut NetServer<PeerConnection>
) -> NetResult<HandlerRes> {
    let mut msg = server.collect_internal(&CORE);
    std::mem::swap(&mut *msg.body, &mut conn.read_buf);
    msg.code = NetMsgCode::External(conn.read_header.code);

    server.enqueue_internal((CORE, msg));

    Err(NetError::Other(format!("bad TRX")))
}
