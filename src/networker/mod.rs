use std::error;
use std::net::{SocketAddr};
use std::os::fd::AsRawFd;
use std::thread::JoinHandle;
use mio::event::Event;
use mio::net::{TcpListener, TcpStream};
use mio::{Events, Interest, Poll, Token};

use crate::RPC;
use crate::{CORE, shutdown, config::NetworkConfig};
use crate::msging::{MsgCons, MsgProd};

use connection::{ConnDirection, Connection};
use utils::{NetMsg, NetError, NetResult, Messengers, Mappings};
use netman::NetMan;

pub mod handlers;
pub mod connection;
pub mod netman;
pub mod header;
pub mod utils;
pub mod peers;

pub const OUTBOUND_CHANS: usize = 3;

const LISTENER:Token = Token(69);

pub fn start_networker(
    config: NetworkConfig, to_core: MsgProd<NetMsg>, froms: Vec<(MsgCons<NetMsg>, Token)>,
) -> Result<JoinHandle<()>, Box<dyn error::Error>> {

    // REGISTER OUTBOUND MESSENGERS WITH POLL AND INSERT INTO MAPPING
    let mut messengers = Messengers::with_capacity(OUTBOUND_CHANS);
    let poll = Poll::new()?;

    for (mut chan, token) in froms {
        poll.registry().register(&mut chan, token, Interest::READABLE)?;
        messengers.insert(token, chan);
    }

    // BUILD THE NETWORK MANAGER AND CONNECTION MAPPING
    let mut net_man = NetMan::new(config, to_core, poll)?;
    let mut maps = Mappings::new(net_man.config.max_connections);

    // CREATE THE LISTENER
    let mut listener = TcpListener::bind(net_man.config.bind_addr.clone().parse()?)?;

    // REGISTER LISTENER WITH EPOLL
    net_man.poll.registry().register(&mut listener, LISTENER, Interest::READABLE)?;

    // BUFFER FOR RETURNED EVENTS FROM EPOLL
    let mut events = Events::with_capacity(net_man.config.event_buffer_size);

    Ok(std::thread::spawn(move || {
        loop {
            // GLOBAL VAR OF ACTIVE STATUS
            if shutdown::should_shutdown() { break; }

            // GET THE NEXT DEADLINE FOR ACTIVE CONNECTIONS
            // OR DEFAULT DEADLINE FROM NOW
            let timeout = net_man.handle_timeouts(
                &mut maps, &mut messengers
            );

            if net_man.poll.poll(&mut events, Some(timeout)).is_err() {
                continue;
            };

            // FOR EACH EPOLL EVENT
            for ev in events.iter() {
                let token = ev.token();

                match token {
                    // HANDLE NEW CONNECTIONS
                    LISTENER => {
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
                    CORE | RPC => {
                        if let Some(from) = messengers.get_mut(&token) {
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
                        &ev,
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
    mut stream: TcpStream, 
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

    if stream.set_nodelay(true).is_err() { return }

    let flags = match dir {
        ConnDirection::Outbound => Interest::READABLE | Interest::WRITABLE,
        ConnDirection::Inbound => Interest::READABLE,
    };

    let token_fd = Token(stream.as_raw_fd() as usize);
    if net_man.poll.registry().register(&mut stream, token_fd, flags).is_err() { return; }

    match Connection::new(stream, addr.clone(), net_man, pub_key, dir) {
        Ok(new_conn) => {
            maps.conns.insert(token_fd, new_conn);
            maps.addrs.insert(addr, token_fd);
            net_man.update_timeout(&token_fd, maps);
        }
        Err(_) => {}
    }
}

pub fn outbound_msg(mut msg: Box<NetMsg>, net_man: &mut NetMan, maps: &mut Mappings) -> Result<(), Box<NetMsg>> {
    let mut conn: Option<&mut Connection> = None;
    let mut sfd: Option<&Token> = None;
    let addr = msg.addr.take();

    // If have stream fd use that
    // otherwise get it
    if msg.stream_token.0 > 0 {
        sfd = Some(&msg.stream_token);
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
                if let Ok(stream) = TcpStream::connect(addr) {

                    // CREATE NEW CONN OBJECT
                    let stream_fd = Token(stream.as_raw_fd() as usize);
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
    event: &Event,
    net_man: &mut NetMan, 
    maps: &mut Mappings, 
    messengers: &mut Messengers
) {
    let token = event.token();
    // GET ACTIVE CONN
    if let Some(conn) = maps.conns.get_mut(&token) {
        let mut res: NetResult<bool> = Ok(false);

        // ENSURE IT HASN'T ERRORED
        if event.is_error() {
            res = Err(NetError::SocketFailed);
        }

        // HANDLE READABLE SOCKET
        if event.is_readable() {
            res = conn.on_readable(net_man);
        }

        // HANDLE QUEUED OUTBOUND MSGS
        if event.is_writable() {
            res = conn.on_writable(net_man, messengers);
        }

        if res.is_err() {
            // IF ERRORED REMOVE CONN AND RECYCLE ITS PARTS
            if let Some(mut c) = maps.conns.remove(&token) {
                let error = res.unwrap_err();
                println!("Connection Closed {:?}", error.clone());

                // IF ITS BAD BEHAVIOUR RECORD IT
                let _ = net_man.peers.record_behaviour(&c.addr, error);

                c.strip_and_delete(net_man, messengers);
            }

        } else if res.unwrap() {
            // IF ALL GOOD AND DID_WORK == TRUE RESET TIMEOUT DEADLINE
            net_man.update_timeout(&token, maps);
        }
    }
}
