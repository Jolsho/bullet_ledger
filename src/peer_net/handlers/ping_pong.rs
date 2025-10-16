use mio::Token;
use std::{net::SocketAddr, time::Duration};

use super::{HandlerRes, PacketCode};
use super::super::connection::PeerConnection;
use crate::utils::{NetError, NetMsgCode, NetResult, NetMsg};

use crate::{server::NetServer, msging::MsgProd};

pub fn send_ping(
    sender: &mut MsgProd<NetMsg>, 
    remote_addr: SocketAddr, 
    remote_pub_key: [u8; 32],
    from: Token,
    index: Option<usize>,
) {

    let mut msg = sender.collect();
    msg.addr = Some(remote_addr);
    msg.pub_key = Some(remote_pub_key.clone());
    msg.code = NetMsgCode::External(PacketCode::Ping);
    msg.from_code = from;
    msg.body.extend_from_slice(b"Ping");
    if let Some(idx) = index {
        msg.body.extend_from_slice(&idx.to_le_bytes());
    }
    msg.handler = Some(handle_pong);
    while let Err(m) = sender.push(msg) {
        // this case is not possible...I think...
        // so for peace of mind I'm going to leave it.
        msg = m;
        std::thread::sleep(Duration::from_millis(5));
    }
}

pub fn handle_ping(
    conn: &mut PeerConnection, 
    server: &mut NetServer<PeerConnection>
) -> NetResult<HandlerRes> {
    let res = b"Pong";
    let read = conn.read(res.len());

    if read == b"Ping" {

        #[cfg(test)]
        if conn.read_buf.len() == conn.read_pos + 8 {
            let mut idx_bytes = [0u8; 8];
            idx_bytes.copy_from_slice(&conn.read(8));
            let idx = usize::from_le_bytes(idx_bytes);
            println!("R Ping: {idx}");
        }

        let mut msg = server.get_new_msg();
        msg.fill_fd_and_id(conn);
        msg.id = conn.read_header.msg_id;
        msg.code = NetMsgCode::External(PacketCode::Ping);
        msg.body.extend_from_slice(res);

        return Ok(HandlerRes::Write(msg))

    } else {
        Err(NetError::Other(
            format!("bad ping {} != {}", 
                hex::encode(b"Ping"), 
                hex::encode(read),
            )
        ))
    }
}

pub fn handle_pong(
    conn: &mut PeerConnection, 
    _server: &mut NetServer<PeerConnection>
) -> NetResult<HandlerRes> {
    let res = b"Pong";
    let read = conn.read(res.len());

    if read == b"Pong" {
        Ok(HandlerRes::None)

    } else {
        Err(NetError::Other(format!("bad pong {}", hex::encode(read))))
    }
}

