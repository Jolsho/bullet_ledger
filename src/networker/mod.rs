use std::error;
use std::net::{SocketAddr, TcpListener, TcpStream};
use std::os::fd::{AsFd, AsRawFd, RawFd};
use std::thread::JoinHandle;
use nix::sys::epoll::{EpollEvent, EpollFlags};
use nix::{errno::Errno, unistd};
use nix::sys::socket::{AddressFamily, SockFlag, SockType, SockaddrIn, SockProtocol};

use crate::{CORE, TRX_POOL, shutdown, config::NetworkConfig};
use crate::msging::{MsgCons, MsgProd, Poller };

use connection::{ConnDirection, Connection};
use utils::{NetMsg, NetError, NetResult, Messengers, Mappings, epoll_flags_write, epoll_flags};
use netman::NetMan;

pub mod handlers;
pub mod connection;
pub mod netman;
pub mod header;
pub mod utils;
pub mod peers;

pub const OUTBOUND_CHANS: usize = 3;

pub fn start_networker(
    config: NetworkConfig, to_core: MsgProd<NetMsg>, froms: Vec<(MsgCons<NetMsg>, i32)>,
) -> Result<JoinHandle<()>, Box<dyn error::Error>> {

    // REGISTER OUTBOUND MESSENGERS WITH EPOLL AND INSERT INTO MAPPING
    let mut messengers = Messengers::with_capacity(OUTBOUND_CHANS);
    let mut epoll = Poller::new()?;

    for (chan, code) in froms {
        epoll.listen_to(&chan)?;
        messengers.insert(code, chan);
    }

    // BUILD THE NETWORK MANAGER AND CONNECTION MAPPING
    let mut net_man = NetMan::new(config, to_core, epoll)?;
    let mut maps = Mappings::new(net_man.config.max_connections);

    // CREATE THE LISTENER
    let listener = TcpListener::bind(net_man.config.bind_addr.clone())?;
    listener.set_nonblocking(true)?;

    // REGISTER LISTENER WITH EPOLL
    let event = EpollEvent::new(EpollFlags::EPOLLIN, listener.as_raw_fd() as u64);
    net_man.epoll.add(&listener, event)?;

    // BUFFER FOR RETURNED EVENTS FROM EPOLL
    let mut events = vec![EpollEvent::empty(); net_man.config.event_buffer_size];
    let listener_fd = listener.as_raw_fd();

    Ok(std::thread::spawn(move || {
        loop {
            // GLOBAL VAR OF ACTIVE STATUS
            if shutdown::should_shutdown() { break; }

            // GET THE NEXT DEADLINE FOR ACTIVE CONNECTIONS
            // OR DEFAULT DEADLINE FROM NOW
            let timeout = net_man.handle_timeouts(
                &mut maps, &mut messengers
            );

            // WAIT FOR EVENTS UNTIL DEADLINE
            let n = net_man.epoll.wait(&mut events, timeout);
            if n.is_err() { continue; }

            // FOR EACH EPOLL EVENT
            for ev in &events[..n.unwrap()] {
                let fd = ev.data() as RawFd;
                let flags = ev.events();

                match fd {
                    // HANDLE NEW CONNECTIONS
                    fd if fd == listener_fd => {
                        if let Ok((stream, addr)) = listener.accept() {
                            let zero_pub_key = [0u8;32];
                            new_connection( 
                                stream, addr, zero_pub_key,
                                &mut net_man, &mut maps,
                                ConnDirection::Inbound,
                            )
                        }
                    }

                    // HANDLE OUTBOUND FROM INTERNAL MESSENGERS
                    CORE | TRX_POOL => {
                        if let Some(from) = messengers.get_mut(&fd) {
                            let _ = from.read_event(); // clear epoll event

                            // POP MSGS
                            while let Some(mut msg) = from.pop() {
                                if msg.code.is_internal() {
                                    net_man.handle_internal_msg(&mut msg);
                                    from.recycle(msg);
                                } else if let Err(msg) = outbound_msg(msg, &mut net_man, &mut maps) {
                                    // if you cant enqueue, or get a valid connection,
                                    // something is very wrong, and you should just give up
                                    from.recycle(msg);
                                }
                            }
                        }
                    }

                    // HANDLE ACTIVE CONNECTIONS
                    _ => active_connection(
                        fd, flags, 
                        &mut net_man, 
                        &mut maps, 
                        &mut messengers
                    ),
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
    pub_key: [u8; 32],
    net_man: &mut NetMan,
    maps: &mut Mappings,
    dir: ConnDirection,
) { 
    if net_man.peers.is_banned(&addr).is_err() {
        println!("BLOCKED: {addr}");
        return
    }

    if stream.set_nonblocking(true).is_err() ||
    stream.set_nodelay(true).is_err() { return }

    let flags = match dir {
        ConnDirection::Outbound => epoll_flags_write(),
        ConnDirection::Inbound => epoll_flags(),
    };
    let ev = EpollEvent::new(flags, stream.as_raw_fd() as u64);

    if net_man.epoll.add(&stream, ev).is_err() { return; }

    match Connection::new(stream, addr.clone(), net_man, pub_key, dir) {
        Ok(new_conn) => {
            let fd = new_conn.as_fd().as_raw_fd();
            maps.conns.insert(fd, new_conn);
            maps.addrs.insert(addr, fd);
            net_man.update_timeout(&fd, maps);
        }
        Err(_) => {}
    }
}


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
    let nix_sockaddr = match addr {
        SocketAddr::V4(v4_addr) => {
            let ip = v4_addr.ip().octets(); // Returns [u8; 4]
            SockaddrIn::new(ip[0], ip[1], ip[2], ip[3], v4_addr.port())
        },
        SocketAddr::V6(_) => {
            return Err(Errno::ECONNABORTED);
        },
    };

    // Start Connection
    match nix::sys::socket::connect(fd.as_raw_fd(), &nix_sockaddr) {
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


pub fn outbound_msg(mut msg: Box<NetMsg>, net_man: &mut NetMan, maps: &mut Mappings) -> Result<(), Box<NetMsg>> {
    let mut conn: Option<&mut Connection> = None;
    let mut sfd: Option<&i32> = None;
    let addr = msg.addr.take();

    // If have stream fd use that
    // otherwise get it
    if msg.stream_fd > 0 {
        sfd = Some(&msg.stream_fd);
    } else if addr.is_some() {
        sfd = maps.addrs.get(&addr.unwrap())
    }

    // GET ACTIVE CONNECTION IF HAVE FD
    if sfd.is_some() {
        conn = maps.conns.get_mut(sfd.unwrap())
    }

    // IF NOT EXISTS DIAL AND CREATE NEW CONNECTION
    if conn.is_none() && addr.is_some() {
        let addr = addr.unwrap();
        if let Some(pub_key) = msg.pub_key.take() {

            // MAKE SURE NOT A BAD ACTOR
            if !net_man.peers.is_banned(&addr).is_err() {

                // INITIATE THE DIAL
                if let Ok(stream) = dial_outbound(&addr) {

                    // CREATE NEW CONN OBJECT
                    let stream_fd = stream.as_raw_fd();
                    new_connection( 
                        stream, addr, pub_key,
                        net_man, maps,
                        ConnDirection::Outbound,
                    );
                    conn = maps.conns.get_mut(&stream_fd);
                }
            }
        }
    }

    // ENQUEUE MSG IN CONNECTION QUEUE
    match conn {
        Some(connection) => connection.enqueue_msg(msg, net_man),
        None => Err(msg),
    }
}


pub fn active_connection(
    fd: i32, flags: EpollFlags,
    net_man: &mut NetMan, 
    maps: &mut Mappings, 
    messengers: &mut Messengers
) {
    // GET ACTIVE CONN
    if let Some(conn) = maps.conns.get_mut(&fd) {
        let mut res: NetResult<bool> = Ok(false);

        // ENSURE IT HASN'T ERRORED
        match conn.check_socket_error() {
            Ok(_) => {
                // HANDLE READABLE SOCKET
                if flags.contains(EpollFlags::EPOLLIN) {
                    res = conn.on_readable(net_man);
                }

                // HANDLE QUEUED OUTBOUND MSGS
                if flags.contains(EpollFlags::EPOLLOUT) {
                    res = conn.on_writable(net_man, messengers);
                }
            }

            Err(e) => res = Err(NetError::SocketFailed(e.to_string())),
        };

        if res.is_err() {
            // IF ERRORED REMOVE CONN AND RECYCLE ITS PARTS
            if let Some(mut c) = maps.conns.remove(&fd) {
                let error = res.unwrap_err();
                println!("Connection Closed {:?}", error.clone());

                // IF ITS BAD BEHAVIOUR RECORD IT
                let _ = net_man.peers.record_behaviour(&c.addr, error);

                c.strip_and_delete(net_man, messengers);
            }

        } else if res.unwrap() {
            // IF ALL GOOD AND DID_WORK == TRUE RESET TIMEOUT DEADLINE
            net_man.update_timeout(&fd, maps);
        }
    }
}
