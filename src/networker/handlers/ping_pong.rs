use mio::Token;

use crate::networker::connection::Connection;
use crate::networker::utils::{NetError, NetMsgCode, NetResult};
use crate::networker::NetMan;
use super::{HandlerRes, PacketCode};
use core::error;
use std::{net::SocketAddr, time::Duration};

use crate::msging::MsgProd;
use crate::networker::utils::NetMsg;

pub fn send_ping(
    sender: &mut MsgProd<NetMsg>, 
    remote_addr: String, 
    remote_pub_key: [u8; 32],
    from: Token,
    index: Option<usize>,
) -> Result<(), Box<dyn error::Error>> {

    let ip:SocketAddr = remote_addr.parse()?;
    let mut msg = sender.collect();
    msg.addr = Some(ip);
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
    Ok(())
}

pub fn handle_ping(
    conn: &mut Connection, 
    _net_man: &mut NetMan
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

        let mut msg = conn.get_new_msg();
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
    conn: &mut Connection, 
    _net_man: &mut NetMan
) -> NetResult<HandlerRes> {
    let res = b"Pong";
    let read = conn.read(res.len());

    if read == b"Pong" {
        Ok(HandlerRes::None)

    } else {
        Err(NetError::Other(format!("bad pong {}", hex::encode(read))))
    }
}

