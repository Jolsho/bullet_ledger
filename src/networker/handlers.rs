use crate::networker::connection::{Connection, NetError, NetResult};
use crate::networker::netman::NetMsg;
use crate::networker::{NetMan, header::PacketCode};

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum HandlerRes {
    Read((u16, Handler)),
    Write(Box<NetMsg>),
    None,
}

pub type Handler = fn(&mut Connection, &mut super::NetMan) -> NetResult<HandlerRes>;

pub fn code_switcher(conn: &mut Connection, net_man: &mut NetMan) -> NetResult<HandlerRes> {
    match conn.read_header.code {
        PacketCode::PingPong => handle_ping_pong(conn, net_man),

        PacketCode::NegotiationSyn | 
        PacketCode::NegotiationAck => conn.handle_negotiation(net_man),

        PacketCode::None => Err(NetError::Other(format!("PacketCode::None"))),
    }
}

pub fn handle_ping_pong(
    conn: &mut Connection, 
    _net_man: &mut NetMan
) -> NetResult<HandlerRes> {
    let res = b"Pong";
    let read = conn.read(res.len());
    if read == b"Ping" {
        println!("recieved PING");
        if let Some(msg) = conn.local_net_msgs.pop() {

            return Ok(HandlerRes::Write(msg))
        }
        return Err(NetError::Other("No local_net_msgs".to_string()));

    } else if read == b"Pong" {
        println!("received PONG");
        Ok(HandlerRes::None)

    } else {
        Err(NetError::Other("Bad ping".to_string()))
    }
}
