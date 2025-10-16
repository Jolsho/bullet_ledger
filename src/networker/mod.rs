use std::net::{Ipv4Addr, SocketAddr};
use std::{error, io};
use std::thread::JoinHandle;
use mio::Token;

use crate::config::PeerServerConfig;
use crate::networker::connection::PeerConnection;
use crate::networker::utils::{NetError, NetManCode, NetMsgCode};
use crate::server::{FromInternals, NetServer, ToInternals};
use crate::CORE;
use crate::msging::{MsgCons, MsgProd};

use utils::NetMsg;

pub mod handlers;
pub mod connection;
pub mod header;
pub mod utils;
pub mod peers;

pub fn start_peer_networker(
    config: PeerServerConfig, to_core: MsgProd<NetMsg>, mut froms: Vec<(MsgCons<NetMsg>, Token)>,
) -> Result<JoinHandle<io::Result<()>>, Box<dyn error::Error>> {

    let mut to_internals = ToInternals::with_capacity(1);
    to_internals.insert(CORE, to_core);

    let mut from_internals = FromInternals::with_capacity(froms.len());
    while froms.len() > 0 {
        let (chan, t) = froms.pop().unwrap();
        from_internals.insert(t, chan);
    }

    let peers = peers::PeerMan::new(
        config.db_path.clone(), 
        config.peer_threshold, 
        config.bootstraps.clone()
    )?;

    let mut server = NetServer::<PeerConnection>::new(&config, to_internals)?;
    Ok(std::thread::spawn(move || {

        let allow_connection = |addr: &SocketAddr| {
            peers.is_banned(addr).is_ok()
        };

        let handle_errored = |e: NetError, addr: &SocketAddr, _s: &mut NetServer<PeerConnection> | {
            let _ = peers.record_behaviour(addr, e);
        };

        let handle_internal = |msg: Box<NetMsg>, _server: &mut NetServer<PeerConnection> | {
            if let NetMsgCode::Internal(code) = &msg.code {
                match *code {
                    NetManCode::AddPeer | NetManCode::RemovePeer => {
                        let mut raw_ip = [0u8; 4];
                        let count = msg.body.len() / 4;
                        let mut cursor = 0;
                        for _ in 0..count {
                            raw_ip.copy_from_slice(&msg.body[cursor..cursor+4]);
                            let ip = Ipv4Addr::from_bits(u32::from_le_bytes(raw_ip));
                            
                            if *code == NetManCode::AddPeer {
                                let _ = peers.add_peer(&ip);
                            } else {
                                let _ = peers.remove_peer(&ip);
                            }
                            cursor += 4
                        }
                    }
                    _ => {},
                }
            }
            Some(msg) //RECYCLE
        };

        server.start( from_internals, handle_internal, allow_connection, handle_errored,)
    }))
}
