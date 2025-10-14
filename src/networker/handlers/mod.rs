
use crate::networker::utils::NetMsgCode;

use super::connection::Connection;
use super::utils::{NetResult, NetError};
use super::{utils::NetMsg, NetMan};

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

pub type Handler = fn(&mut Connection, &mut super::NetMan) -> NetResult<HandlerRes>;

pub fn code_switcher(conn: &mut Connection, net_man: &mut NetMan) -> NetResult<HandlerRes> {
    match conn.read_header.code {
        PacketCode::Ping => ping_pong::handle_ping(conn, net_man),

        PacketCode::NegotiationSyn | 
        PacketCode::NegotiationAck => conn.handle_negotiation(net_man),

        PacketCode::NewTrx |
        PacketCode::NewBlock => forward_2_core(conn, net_man),

        PacketCode::None => Err(NetError::Other(format!("PacketCode::None"))),
    }
}

pub fn forward_2_core(
    conn: &mut Connection, 
    net_man: &mut NetMan
) -> NetResult<HandlerRes> {
    let mut msg = net_man.to_core.collect();
    std::mem::swap(&mut *msg.body, &mut conn.read_buf);
    msg.code = NetMsgCode::External(conn.read_header.code);

    if let Err(_msg) = net_man.to_core.push(msg) {
        // TODO -- I dont know what to do here...
    }

    Err(NetError::Other(format!("bad TRX")))
}
