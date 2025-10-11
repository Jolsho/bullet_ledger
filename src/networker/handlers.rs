use std::net::{SocketAddr, TcpStream};
use std::os::{fd::AsRawFd, unix::io::FromRawFd};
use nix::sys::socket::SockProtocol;
use nix::unistd;
use nix::{errno::Errno, sys::{epoll::EpollEvent, socket::{connect, socket, AddressFamily, SockFlag, SockType, SockaddrIn}}};

use crate::networker::epoll_flags_write;
use crate::networker::{connection::{ConnDirection, ConnState, Connection}, epoll_flags, header::PacketCode, ConsMap, NetMan};

pub fn dial_outbound(addr: &SocketAddr) -> nix::Result<TcpStream> {
    // Create a non-blocking socket
    let fd = socket(
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
    match connect(fd.as_raw_fd(), &nix_addr) {
        Ok(_) => {} // Connected immediately
        Err(Errno::EINPROGRESS) => {} // Connection in progress
        Err(e) => {
            // Connection failed
            let _ = unistd::close(fd);
            return Err(e);
        }
    }

    // Convert the raw file descriptor into a TcpStream
    let stream = unsafe { TcpStream::from_raw_fd(fd.as_raw_fd()) };

    // Return the TcpStream
    Ok(stream)
}


pub fn handle_new_connection(
    stream: TcpStream, 
    _addr: &SocketAddr,
    net_man: &mut NetMan,
    conns: &mut ConsMap,
    dir: ConnDirection,
) { 

    if stream.set_nonblocking(true).is_err() ||
    stream.set_nodelay(true).is_err() {
        return
    }

    let flags = match dir {
        ConnDirection::Outbound => epoll_flags_write(),
        ConnDirection::Inbound => epoll_flags(),
    };
    let ev = EpollEvent::new(flags, stream.as_raw_fd() as u64);

    if net_man.epoll.add(&stream, ev).is_err() {
        return
    }

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

pub fn code_switcher(conn: &mut Connection, net_man: &mut NetMan) -> ConnState {
    match conn.header.code {

        PacketCode::PingPong => handle_ping_pong(conn, net_man),

        PacketCode::None => ConnState::Failed(format!("PacketCode::None")),
        
    }
}

pub fn handle_ping_pong(conn: &mut Connection, _net_man: &mut NetMan) -> ConnState {
    let res = b"Pong";
    let expected = b"Ping";
    let read = conn.read(expected.len());
    if read == expected {
        conn.queue_write(res);
        ConnState::Writing(None)

    } else {
        ConnState::Failed("Bad ping".to_string())
    }
}
