use std::{collections::HashMap, error, os::fd::AsRawFd, thread::JoinHandle, usize};
use mio::{net::TcpListener, Events, Interest, Poll, Token};
use crate::config::ServerConfig; 
use crate::msging::MsgProd;
use crate::networker::utils::{NetMsg, NetResult};
use crate::rpc::{connection::RpcConn, server::Server};
use crate::shutdown;

pub mod server;
pub mod connection;

pub type ClientMap = HashMap<Token, Box<RpcConn>>;

const LISTENER:Token = Token(69);
pub fn start_rpc(
    config: ServerConfig, 
    mut to_net: MsgProd<NetMsg>,
) -> Result<JoinHandle<()>, Box<dyn error::Error>> {

    // Initialize a poll instance (epoll/kqueue)
    let poll = Poll::new()?;
    let mut events = Events::with_capacity(config.event_buffer_size);

    // Track streams by token
    let mut connections = ClientMap::with_capacity(config.max_connections);

    let mut listener = TcpListener::bind(config.bind_addr.parse()?)?;
    poll.registry().register(&mut listener, LISTENER, Interest::READABLE)?;

    let mut server = Server::new(config, poll)?;

    Ok(std::thread::spawn(move || {
        // Poll loop
        loop {
            if shutdown::should_shutdown() { break; }

            let timeout = server.handle_timeouts(&mut connections);

            if server.poll.poll(&mut events, Some(timeout)).is_err() {
                continue;
            }

            for event in events.iter() {
                let token = event.token();
                if token == LISTENER {
                    if let Ok((stream, addr)) = listener.accept() {
                        if server.allowed.is_banned(&addr).is_ok() {
                            let token_fd = Token(stream.as_raw_fd() as usize);
                            let conn = server.get_conn(stream);

                            if let Some(c) = connections.insert(token_fd, conn) {
                                server.put_conn(c);
                            }
                        }
                    }
                }

                let mut res: NetResult<bool> = Ok(false);
                if let Some(conn) = connections.get_mut(&token) {
                    if event.is_writable() {
                        res = conn.handle_write(&mut server);
                    }
                    if res.is_ok() && event.is_readable() {
                        res = conn.handle_read(&mut to_net);
                    }
                }
                if res.is_err() {
                    let (_, c) = connections.remove_entry(&token).unwrap();
                    server.put_conn(c);
                } else {
                    server.update_timeout(&token, &mut connections);
                }
            }
        }
    }))
}
