use std::collections::HashMap;
use std::io;
use std::net::TcpListener;
use std::os::fd::{AsRawFd, RawFd};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};
use nix::sys::epoll::{EpollEvent, EpollFlags};

use crate::config::NetworkConfig;
use crate::core::msg::CoreMsg;
use crate::msging::{MsgCons, MsgProd, Poller, RingCons};
use crate::networker::connection::{ConnDirection, Connection};
use crate::networker::handlers::{dial_outbound, handle_new_connection};
use crate::networker::netman::{NetMan, NetMsg};
use crate::shutdown::should_shutdown;
use crate::trxs::Trx;
use crate::{CORE, TRX_POOL};

pub mod handlers;
pub mod connection;
pub mod netman;
pub mod header;

type ConsMap = HashMap<i32, Connection>;
pub fn next_deadline(timeout: u64) -> Instant {
    Instant::now() + Duration::from_secs(timeout)
}
pub fn epoll_flags() -> EpollFlags {
    EpollFlags::EPOLLIN | EpollFlags::EPOLLHUP | EpollFlags::EPOLLERR
}
pub fn epoll_flags_write() -> EpollFlags {
    epoll_flags() | EpollFlags::EPOLLOUT
}

const OUTBOUND_CHANS: usize = 3;
type Messengers = HashMap<i32, MsgCons<NetMsg>>;


pub fn start_networker(
    config: NetworkConfig, trx_con: RingCons<Trx>, 
    to_core: MsgProd<CoreMsg>, from_core: MsgCons<NetMsg>,
) ->JoinHandle<()> {

    // REGISTER OUTBOUND MESSENGERS WITH EPOLL AND INSERT INTO MAPPING
    let mut messengers = Messengers::with_capacity(OUTBOUND_CHANS);
    let mut epoll = Poller::new().unwrap();
    epoll.listen_to(&from_core).unwrap();
    messengers.insert(CORE, from_core);

    // BUILD THE NETWORK MANAGER AND CONNECTION MAPPING
    let mut net_man = NetMan::new(trx_con, to_core, epoll, config).unwrap();
    let mut conns = ConsMap::with_capacity(net_man.config.max_connections);

    // CREATE THE LISTENER
    let listener = TcpListener::bind(net_man.config.bind_addr.clone()).unwrap();
    listener.set_nonblocking(true).unwrap();

    // REGISTER LISTENER WITH EPOLL
    let event = EpollEvent::new(EpollFlags::EPOLLIN, listener.as_raw_fd() as u64);
    net_man.epoll.add(&listener, event).unwrap();

    // BUFFER FOR RETURNED EVENTS FROM EPOLL
    let mut events = vec![EpollEvent::empty(); net_man.config.event_buffer_size];
    let listener_fd = listener.as_raw_fd();

    std::thread::spawn(move || {
        loop {
            // GLOBAL VAR OF ACTIVE STATUS
            if should_shutdown() { break; }

            // GET THE NEXT DEADLINE FOR ACTIVE CONNECTIONS
            // OR DEFAULT DEADLINE FROM NOW
            let timeout = net_man.handle_timeouts(&mut conns);

            // WAIT FOR EVENTS UNTIL DEADLINE
            let n = net_man.epoll.wait(&mut events, timeout);
            if n.is_err() { continue; }

            for ev in &events[..n.unwrap()] {
                let fd = ev.data() as RawFd;
                let flags = ev.events();

                match fd {
                    // HANDLE NEW CONNECTIONS
                    fd if fd == listener_fd => {
                        if let Ok((stream, addr)) = listener.accept() {
                            // println!("new_conn: {}", stream.as_raw_fd());
                            handle_new_connection( 
                                stream, &addr, 
                                &mut net_man, &mut conns,
                                ConnDirection::Inbound,
                            )
                        }
                    }

                    // HANDLE OUTBOUND FROM INTERNAL MESSENGERS
                    CORE | TRX_POOL => {
                        if let Some(from) = messengers.get_mut(&fd) {
                            let _ = from.read_event();
                            while let Some(mut msg) = from.pop() {
                                let mut conn = conns.get_mut(&msg.stream_fd);
                                if conn.is_none() {
                                    if let Some(addr) = msg.addr.take() {
                                        if let Ok(stream) = dial_outbound(&addr) {
                                            let stream_fd = stream.as_raw_fd();
                                            handle_new_connection( 
                                                stream, &addr, 
                                                &mut net_man, &mut conns,
                                                ConnDirection::Outbound,
                                            );
                                            conn = conns.get_mut(&stream_fd);
                                        }
                                    }
                                }
                                if conn.is_some() {
                                    if let Err(msg) = conn.unwrap()
                                        .queue_msg(msg, &mut net_man.epoll) 
                                    {
                                        from.recycle(msg);
                                    }
                                } else {
                                    from.recycle(msg);
                                }
                            }
                        }
                    }

                    // HANDLE ACTIVE CONNECTIONS
                    _ => {
                        if let Some(conn) = conns.get_mut(&fd) {
                            let mut res: io::Result<bool> = Ok(false);
                            match conn.check_socket_error() {
                                Ok(_) => {
                                    if flags.contains(EpollFlags::EPOLLIN) {
                                        res = conn.on_readable(&mut net_man);
                                    }

                                    if flags.contains(EpollFlags::EPOLLOUT) {
                                        res = conn.on_writable(
                                            &mut net_man,
                                            &mut messengers,
                                        );
                                    }
                                }
                                Err(e) => res = Err(e),
                            };
                            if res.is_err() {
                                let _ = net_man.epoll.delete(&conn.stream);
                                if let Some(c) = conns.remove(&fd) {
                                    net_man.put_buff(c.read_buf);
                                    net_man.put_buff(c.write_buf);
                                }

                            } else if res.unwrap() {
                                net_man.update_timeout(&fd, &mut conns);
                            }
                        }
                    }
                }
            }
        }
    })
}
