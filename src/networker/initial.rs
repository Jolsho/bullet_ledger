use std::{os::fd::AsRawFd, net::{SocketAddr, TcpStream}};
use nix::{errno::Errno, sys::epoll::EpollEvent, unistd};
use nix::sys::socket::{AddressFamily, SockFlag, SockType, SockaddrIn, SockProtocol};

use crate::networker::connection::{ConnDirection, Connection};
use crate::networker::{epoll_flags, epoll_flags_write, ConsMap, NetMan};

/// Dials the address and returns stream.
/// Stream may or may not be connected when returned.
pub fn dial_outbound(addr: &SocketAddr) -> nix::Result<TcpStream> {
    // Create a non-blocking socket
    let fd = nix::sys::socket::socket(
        AddressFamily::Inet,
        SockType::Stream,
        SockFlag::SOCK_NONBLOCK,
        Some(SockProtocol::Tcp),
    )?;

    // Convert SocketAddr to SockAddrIn
    let nix_addr = match addr {
        SocketAddr::V4(v4_addr) => {
            let ip = v4_addr.ip().octets(); // Returns [u8; 4]
            SockaddrIn::new(ip[0], ip[1], ip[2], ip[3], v4_addr.port())
        },
        SocketAddr::V6(_) => {
            return Err(Errno::ECONNABORTED);
        },
    };

    // Start Connection
    match nix::sys::socket::connect(fd.as_raw_fd(), &nix_addr) {
        Ok(_) => {} // Connected immediately
        Err(Errno::EINPROGRESS) => {} // Connection in progress
        Err(e) => {
            // Connection failed
            let _ = unistd::close(fd);
            return Err(e);
        }
    }

    // Return the TcpStream
    Ok(TcpStream::from(fd))
}


/// configure stream, register fd with epoll, create connection, 
/// add connection to cons, and set timeout deadline.
pub fn new_connection(
    stream: TcpStream, 
    _addr: &SocketAddr,
    net_man: &mut NetMan,
    conns: &mut ConsMap,
    dir: ConnDirection,
) { 
    if stream.set_nonblocking(true).is_err() ||
    stream.set_nodelay(true).is_err() { return }

    let flags = match dir {
        ConnDirection::Outbound => epoll_flags_write(),
        ConnDirection::Inbound => epoll_flags(),
    };
    let ev = EpollEvent::new(flags, stream.as_raw_fd() as u64);

    if net_man.epoll.add(&stream, ev).is_err() { return; }

    match Connection::new(stream, net_man, dir) {
        Ok(new_conn) => {
            let fd = new_conn.stream.as_raw_fd();
            conns.insert(fd, new_conn);
            net_man.update_timeout(&fd, conns);
        }
        Err(stream) => {
            net_man.epoll.delete(&stream).ok();
        }
    }
}
