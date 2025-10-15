use std::collections::HashMap;
use std::error;
use std::net::{SocketAddr, TcpListener, TcpStream};
use std::os::fd::{AsFd, AsRawFd, RawFd};
use std::thread::JoinHandle;
use nix::sys::epoll::{EpollEvent, EpollFlags};

use crate::http::client::Client;
use crate::http::server::{Server, ServerConfig};
use crate::networker::utils::{epoll_flags, NetError, NetMsg, NetResult};
use crate::{msging::MsgProd, shutdown};

pub mod server;
pub mod client;
pub mod chunk;
pub mod handlers;
pub mod utils;

pub type ClientMap = HashMap<i32, Client>;

pub fn start_networker(
    config: ServerConfig, _to_net: MsgProd<NetMsg>,
) -> Result<JoinHandle<()>, Box<dyn error::Error>> {

    // BUILD THE SERVER MANAGER AND CONNECTION MAPPING
    let mut server = Server::new(config)?;
    let mut clients = ClientMap::with_capacity(server.config.max_connections);

    // CREATE THE LISTENER
    let listener = TcpListener::bind(server.config.bind_addr.clone())?;
    listener.set_nonblocking(true)?;

    // REGISTER LISTENER WITH EPOLL
    let event = EpollEvent::new(EpollFlags::EPOLLIN, listener.as_raw_fd() as u64);
    server.epoll.add(&listener, event)?;

    // BUFFER FOR RETURNED EVENTS FROM EPOLL
    let mut events = vec![EpollEvent::empty(); server.config.event_buffer_size];
    let listener_fd = listener.as_raw_fd();

    Ok(std::thread::spawn(move || {
        loop {
            // GLOBAL VAR OF ACTIVE STATUS
            if shutdown::should_shutdown() { break; }

            // GET THE NEXT DEADLINE FOR ACTIVE CONNECTIONS
            // OR DEFAULT DEADLINE FROM NOW
            let timeout = server.handle_timeouts(&mut clients);

            // WAIT FOR EVENTS UNTIL DEADLINE
            let n = server.epoll.wait(&mut events, timeout);
            if n.is_err() { continue; }

            // FOR EACH EPOLL EVENT
            for ev in &events[..n.unwrap()] {
                let fd = ev.data() as RawFd;
                let flags = ev.events();

                match fd {
                    // HANDLE NEW CONNECTIONS
                    fd if fd == listener_fd => {
                        if let Ok((stream, addr)) = listener.accept() {
                            new_connection(stream, addr, &mut server, &mut clients)
                        }
                    }

                    // HANDLE ACTIVE CONNECTIONS
                    _ => active_client(fd, flags, &mut server, &mut clients),
                }
            }
        }
    }))
}


/// configure stream, register fd with epoll, create connection, 
/// add connection to cons, and set timeout deadline.
pub fn new_connection(
    stream: TcpStream, 
    addr: SocketAddr,
    server: &mut Server,
    clients: &mut ClientMap,
) { 
    if server.allowed.is_banned(&addr).is_err() {
        println!("BLOCKED: {addr}");
        return
    }

    if stream.set_nonblocking(true).is_err() ||
    stream.set_nodelay(true).is_err() { return }

    let ev = EpollEvent::new(epoll_flags(), stream.as_raw_fd() as u64);

    if server.epoll.add(&stream, ev).is_err() { return; }

    let new_cli =  Client::new(stream, addr.clone(), server);
    let fd = new_cli.as_fd().as_raw_fd();
    clients.insert(fd, new_cli);
    server.update_timeout(&fd, clients);
}

pub fn active_client(
    fd: i32, flags: EpollFlags,
    server: &mut Server, 
    clients: &mut ClientMap, 
) {
    // GET ACTIVE CONN
    if let Some(client) = clients.get_mut(&fd) {
        let mut res: NetResult<bool> = Ok(false);

        // ENSURE IT HASN'T ERRORED
        match client.check_socket_error() {
            Ok(_) => {
                // HANDLE READABLE SOCKET
                if flags.contains(EpollFlags::EPOLLIN) {
                    res = client.on_readable(server);
                }

                // HANDLE QUEUED OUTBOUND MSGS
                if flags.contains(EpollFlags::EPOLLOUT) {
                    res = client.on_writable(server);
                }
            }

            Err(e) => res = Err(NetError::SocketFailed(e.to_string())),
        };

        if res.is_err() {
            // IF ERRORED REMOVE CONN AND RECYCLE ITS PARTS
            if let Some(mut c) = clients.remove(&fd) {
                let error = res.unwrap_err();
                println!("Connection Closed {:?}", error.clone());

                // IF ITS BAD BEHAVIOUR RECORD IT
                let _ = server.allowed.record_behaviour(&c.addr, error);

                c.strip_and_delete(server);
            }

        } else if res.unwrap() {
            // IF ALL GOOD AND DID_WORK == TRUE RESET TIMEOUT DEADLINE
            server.update_timeout(&fd, clients);
        }
    }
}
