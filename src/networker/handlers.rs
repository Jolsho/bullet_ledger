use crate::networker::connection::{ConnState, Connection};
use crate::networker::{NetMan, header::PacketCode};

pub fn code_switcher(conn: &mut Connection, net_man: &mut NetMan) -> ConnState {
    match conn.header.code {
        PacketCode::PingPong => handle_ping_pong(conn, net_man),
        PacketCode::None => ConnState::Failed(format!("PacketCode::None")),
    }
}

pub fn handle_ping_pong(conn: &mut Connection, _net_man: &mut NetMan) -> ConnState {
    let res = b"Pong";
    let read = conn.read(res.len());
    if read == b"Ping" {
        println!("recieved PING");
        conn.queue_write(res);
        ConnState::Writing(None)

    } else if read == b"Pong" {
        println!("received PONG");
        ConnState::ReadingHeader(None)

    } else {
        ConnState::Failed("Bad ping".to_string())
    }
}
