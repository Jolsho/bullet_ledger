use std::cell::RefCell;
use std::net::{Ipv4Addr, SocketAddr};
use std::rc::Rc;
use std::{error, io};
use std::thread::JoinHandle;
use mio::Token;

use crate::config::PeerServerConfig;
use crate::peer_net::connection::PeerConnection;
use crate::utils::{NetError, NetManCode, NetMsg, NetMsgCode};
use crate::server::{to_internals_from_vec, NetServer};
use crate::spsc::{Consumer, Producer};

pub mod handlers;
pub mod connection;
pub mod header;
pub mod peers;

pub fn start_peer_networker(
    config: PeerServerConfig, 
    tos: Vec<(Producer<NetMsg>, Token)>, 
    froms: Vec<(Consumer<NetMsg>, Token)>,
) -> Result<JoinHandle<io::Result<()>>, Box<dyn error::Error>> {

    let to_internals = to_internals_from_vec(tos);

    let peers = peers::PeerMan::new(
        config.db_path.clone(), 
        config.peer_threshold, 
        config.bootstraps.clone()
    )?;

    let mut server = NetServer::<PeerConnection>::new(&config, to_internals)?;

    Ok(std::thread::spawn(move || {

        let peers = Rc::new(RefCell::new(peers));

        let allow_connection = |addr: &SocketAddr| {
            peers.borrow_mut().is_banned(addr).is_ok()
        };

        let handle_errored = |e: NetError, addr: &SocketAddr, _s: &mut NetServer<PeerConnection> | {
            let _ = peers.borrow_mut().record_behaviour(addr, e);
        };

        let handle_internal = |msg: NetMsg, _s: &mut NetServer<PeerConnection> | {

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
                                let _ = peers.borrow_mut().add_peer(&ip);
                            } else {
                                let _ = peers.borrow_mut().remove_peer(&ip);
                            }
                            cursor += 4
                        }
                    }
                    _ => {},
                }
            }
            Some(msg) //RECYCLE
        };

        server.start(froms, handle_internal, allow_connection, handle_errored,)
    }))
}
