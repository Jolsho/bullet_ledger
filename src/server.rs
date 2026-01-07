/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

use mio::net::{TcpListener, TcpStream};
use mio::{Events, Interest, Poll, Token};
use std::collections::HashMap;
use std::collections::VecDeque;
use std::collections::BinaryHeap;
use std::error;
use std::{io, os::fd::AsRawFd};
use std::net::SocketAddr;
use std::time::{Duration, Instant};
use crate::utils::keys::load_keys;
use crate::spsc::{Msg, Consumer, Producer};
use crate::utils::errors::{NetError, NetResult};
use crate::utils::msg::NetMsg;

pub trait TcpConnection: Sized + Send {
    fn new(server: &mut NetServer<Self>) -> Box<Self>;
    fn reset(&mut self, server: &mut NetServer<Self>);
    fn initialize(&mut self, stream: TcpStream, addr: SocketAddr, pub_key: Option<[u8;32]>,  server: &mut NetServer<Self>);
    fn enable_writable(&mut self, poll: &mut Poll) -> io::Result<()>;
    fn disable_writable(&mut self, poll: &mut Poll) -> io::Result<()>;
    fn on_readable(&mut self, server: &mut NetServer<Self>) -> NetResult<bool>;
    fn on_writable(&mut self, server: &mut NetServer<Self>) -> NetResult<bool>;
    fn get_last_deadline(&self) -> Instant;
    fn set_last_deadline(&mut self, deadline: Instant);
    fn enqueue_msg(&mut self, msg: NetMsg, server: &mut NetServer<Self>) -> Result<(), NetMsg>;
    fn get_addr(&self) -> SocketAddr;
    fn get_stream(&mut self) -> &mut TcpStream;
    fn initiate_negotiation(&mut self, server: &mut NetServer<Self>) -> NetResult<()>;
}

pub trait NetServerConfig {
    fn get_buffers_cap(&self) -> usize;
    fn get_max_connections(&self) -> usize;
    fn get_buffer_size(&self) -> usize;
    fn get_idle_polltimeout(&self) -> u64;
    fn get_idle_timeout(&self) -> u64;
    fn get_bind_addr(&self) -> io::Result<SocketAddr>;
    fn get_events_cap(&self) -> usize;
    fn get_con_q_cap(&self) -> usize;
    fn get_key_path(&self) -> String;
}

///////////////////////////////////////////////////
///////////     INTERNAL MSGING     //////////////
///////////////////////////////////////////////// 

pub type ToInternals = HashMap<Token, Producer<NetMsg>>;
pub fn to_internals_from_vec(
    mut tos: Vec<(Producer<NetMsg>, Token)>
) -> ToInternals {
    let mut to_internals = ToInternals::with_capacity(tos.len());
    while tos.len() > 0 {
        let (chan, t) = tos.pop().unwrap();
        to_internals.insert(t, chan);
    }
    to_internals
}
pub type FromInternals = HashMap<Token, Consumer<NetMsg>>;
pub fn from_internals_from_vec(
    poll: &mut Poll,
    mut froms: Vec<(Consumer<NetMsg>, Token)>
) -> io::Result<FromInternals> {
    let mut from_internals = FromInternals::with_capacity(froms.len());
    while froms.len() > 0 {
        let (mut chan, t) = froms.pop().unwrap();
        poll.registry().register(&mut chan, t.clone(), Interest::READABLE)?;
        from_internals.insert(t, chan);
    }
    Ok(from_internals)
}
pub type ConnMapping<C> = HashMap<Token, Box<C>>;


///////////////////////////////////////////////////
///////////     NETSERVER    /////////////////////
///////////////////////////////////////////////// 

pub struct NetServer<C: TcpConnection> {

    // TCP
    listener: TcpListener,
    pub poll: Poll,
    time_table: BinaryHeap<TimeoutEntry>,
    pub token_map: HashMap<SocketAddr, Token>,
    pub pub_key: [u8;32],
    pub priv_key: [u8;32],
    connections: Vec<Box<C>>,
    buffs: Vec<Vec<u8>>,

    // INTERNAL MSGING 
    net_msgs: Vec<NetMsg>,
    internal_msgs: VecDeque<(Token, NetMsg)>,
    to_internals: ToInternals,

    // CONFIG
    pub buffer_size: usize,
    pub idle_timeout: u64,
    pub conn_q_cap: usize,
    idle_polltimeout: u64,
    events_cap: usize,
    max_connections: usize,
}


pub const LISTENER:Token = Token(696969);

impl<C: TcpConnection> NetServer<C> {

    pub fn new<T: NetServerConfig>(config: &T, to_internals: ToInternals) -> Result<Self, Box<dyn error::Error>> {
        let poll = Poll::new()?;
        let mut listener = TcpListener::bind(config.get_bind_addr()?)?;
        poll.registry().register(&mut listener, LISTENER, Interest::READABLE)?;

        let (pub_key, priv_key) = load_keys(&config.get_key_path())?;

        Ok(Self { 
            listener, poll, to_internals, 
            pub_key, priv_key, 
            time_table: BinaryHeap::with_capacity(config.get_max_connections()),

            token_map: HashMap::with_capacity(config.get_max_connections()),
            connections: Vec::with_capacity(config.get_max_connections()),

            buffs: Vec::with_capacity(config.get_buffers_cap()),
            net_msgs: Vec::with_capacity(config.get_buffers_cap()),
            internal_msgs: VecDeque::with_capacity(config.get_buffers_cap()),

            buffer_size: config.get_buffer_size(),
            idle_timeout: config.get_idle_timeout(),
            idle_polltimeout: config.get_idle_polltimeout(),
            events_cap: config.get_events_cap(),
            max_connections: config.get_max_connections(),
            conn_q_cap: config.get_con_q_cap(),
        })
    }

    //////////////////////////////////////////////////////////////////////////
    ////       POLLING       ////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////

    pub fn start<A, B, D>(&mut self,
        froms: Vec<(Consumer<NetMsg>, Token)>,
        mut handle_from_internal: A,
        mut allow_connection: B,
        mut handle_errored: D,
    )-> io::Result<()> 
    where 
        A: FnMut(NetMsg, &mut Self) -> Option<NetMsg>,
        B: FnMut(&SocketAddr) -> bool,
        D: FnMut(NetError, &SocketAddr, &mut Self),
    { 
        let mut maps = ConnMapping::<C>::with_capacity(self.max_connections);

        let mut from_internals = from_internals_from_vec(&mut self.poll, froms)?;

        let mut events = Events::with_capacity(self.events_cap);
        
        // Poll loop
        loop {
            if crate::utils::shutdown::should_shutdown() { break; }

            let timeout = self.handle_timeouts(&mut maps);
            if self.poll.poll(&mut events, Some(timeout)).is_err() { 
                self.internal_try_push();
                continue; 
            }

            for event in events.iter() {
                self.internal_try_push();

                let token = event.token();

                // HANDLING NEW CONNECTIONS
                if token == LISTENER {
                    if let Ok((stream, addr)) = self.listener.accept() {
                        if allow_connection(&addr) {
                            let _ = self.handle_new(
                                stream, addr, None, 
                                &mut maps, Self::INBOUND,
                            );
                        }
                    }
                    continue;
                }

                // HANDLING MSGS FROM INTERNAL ACTORS
                if let Some(from) = from_internals.get_mut(&token) {
                    let _ = from.read_event(); // clear epoll event
                    // POP MSGS
                    while let Some(msg) = from.pop() {
                        if msg.code.is_internal() {
                            if let Some(msg) = handle_from_internal(msg, self) {
                                let _ = from.recycle(msg);
                            }
                        } else {
                            if let Err(msg) = self.handle_outbound(msg, &mut maps) {
                                let _ = from.recycle(msg);
                            } else {
                                // conn will send msg back to server
                                // so to prevent overloading server with msgs
                                // pop one and recycle it to from
                                let _ = from.recycle(self.get_new_msg());
                            }
                        }
                    }
                    continue;
                }

                // ACTIVE CONNECTION HANDLING
                let mut res: NetResult<bool> = Ok(false);
                if let Some(conn) = maps.get_mut(&token) {
                    if event.is_error() {
                        res = Err(NetError::SocketFailed);
                    }
                    if res.is_ok() && event.is_writable() {
                        res = conn.on_writable(self);
                    }
                    if res.is_ok() && event.is_readable() {
                        res = conn.on_readable(self);
                    }
                }

                if res.is_err() {
                    let c = maps.remove(&token).unwrap();
                    handle_errored(res.unwrap_err(), &c.get_addr(), self);
                    self.put_conn(c);

                } else {
                    self.update_timeout(&token, &mut maps);
                }
            }
        }
        Ok(())
    }


    //////////////////////////////////////////////////////////////////////////////
    ////       TCP CONNECTION STUFF    //////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    fn get_conn(&mut self, 
        stream: TcpStream, 
        addr: SocketAddr, 
        pub_key: Option<[u8;32]>,
    ) -> Box<C> {
        let mut conn = self.connections.pop();
        if conn.is_none() {
            conn = Some(C::new(self));
        }
        let mut conn = conn.unwrap();
        conn.initialize(stream, addr, pub_key, self);
        conn
    }

    fn put_conn(&mut self, mut conn: Box<C>) { 
        conn.reset(self);
        self.connections.push(conn); 
    }

    
    pub fn get_buff(&mut self) -> Vec<u8> {
        let mut buf = self.buffs.pop();
        if buf.is_none() {
            buf = Some(Vec::with_capacity(self.buffer_size));
        }
        buf.unwrap()
    }

    pub fn put_buff(&mut self, buff: Vec<u8>) { self.buffs.push(buff); }


    pub fn handle_outbound(&mut self, mut msg: NetMsg, maps: &mut ConnMapping<C>) -> Result<(), NetMsg> {
        let mut conn: Option<&mut Box<C>> = None;
        let mut sfd: Option<&Token> = None;
        let addr = msg.addr.take();

        // If have stream fd use that
        // otherwise get it
        if msg.stream_token.0 > 0 {
            sfd = Some(&msg.stream_token);
        } else if addr.is_some() {
            sfd = self.token_map.get(&addr.unwrap())
        }

        // GET ACTIVE CONNECTION IF HAVE FD
        if sfd.is_some() {
            conn = maps.get_mut(sfd.unwrap())
        }

        // IF NOT EXISTS DIAL AND CREATE NEW CONNECTION
        if conn.is_none() && addr.is_some() {
            let addr = addr.unwrap();

            // INITIATE THE DIAL
            if let Ok(stream) = TcpStream::connect(addr) {

                let token_fd = self.handle_new(
                    stream, addr, msg.pub_key.take(), maps, Self::OUTBOUND
                ); 

                if token_fd.is_ok() {
                    conn = maps.get_mut(&token_fd.unwrap());
                } else {
                    return Err(msg);
                }

            }
        }

        // ENQUEUE MSG IN CONNECTION QUEUE
        match conn {
            Some(connection) => connection.enqueue_msg(msg, self),
            None => Err(msg),
        }
    }

    const OUTBOUND: bool = true;
    const INBOUND: bool = false;
    pub fn handle_new(&mut self, 
        mut stream: TcpStream, 
        addr: SocketAddr, 
        pub_key: Option<[u8;32]>, 
        maps: &mut ConnMapping<C>,
        is_outbound: bool,
    ) -> NetResult<Token> {

        stream.set_nodelay(true).map_err(|e|
            NetError::Other(e.to_string())
        )?;
        let token_fd = Token(stream.as_raw_fd() as usize);

        let mut flags = Interest::READABLE;
        if is_outbound {
            flags = flags | Interest::WRITABLE;
        }

        // REGISTER WITH SERVER
        self.poll.registry().register(&mut stream, token_fd, flags)
            .map_err(|e| NetError::Other(e.to_string()))?;

        // CREATE NEW CONN OBJECT
        let mut new_conn = self.get_conn(stream, addr.clone(), pub_key);

        // ALLOW TO PRIME INTERNAL STATE FOR NEGOTIATION
        if is_outbound {
            if new_conn.initiate_negotiation(self).is_err() {
                self.poll.registry().deregister(new_conn.get_stream())
                    .map_err(|e| NetError::Other(e.to_string()))?;
            }
        }

        // INSERT AND UPDATE DEADLINE
        if let Some(c) = maps.insert(token_fd, new_conn) {
            self.put_conn(c);
        } else {
            let _ = self.token_map.insert(addr, token_fd.clone());
            self.update_timeout(&token_fd, maps);
        }
        Ok(token_fd)
    }




    //////////////////////////////////////////////////////////////////////////////
    ////       INTERNAL MSGING      /////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    pub fn get_new_msg(&mut self) -> NetMsg {
        let mut msg = self.net_msgs.pop(); 
        if msg.is_none() {
            msg = Some(NetMsg::new(Some(self.buffer_size)));
        }
        let mut msg = msg.unwrap();
        msg.body.reset();
        msg
    }
    pub fn put_msg(&mut self, msg: NetMsg) { self.net_msgs.push(msg); }

    /// Enqueues a msg in self.internal_msgs which is then flushed
    /// every maximum of default idle_polltimeout.
    pub fn enqueue_internal(&mut self, msg: (Token, NetMsg)) {
        self.internal_msgs.push_back(msg);
    }

    /// Get a NetMsg from the specifiec internal MsgProd<NetMsg>
    pub fn collect_internal(&mut self, token: &Token) -> NetMsg {
        if let Some(to) = self.to_internals.get_mut(token) {
            return to.collect();
        } else {
            return self.get_new_msg();
        }
    }

    /// TRYING TO MAKE SURE MSGS ARE BEING DISPATCHED TO OTHER ACTORS
    /// Pops from self.internal_msgs until len == 0 or we run into a cycle
    /// if msg cant be sent now it is pushed to back of self.internal_msgs
    /// that is what creates a cycle
    fn internal_try_push(&mut self) {
        let mut start: Option<u16> = None;
        while self.internal_msgs.len() > 0 {

            let (to, msg) = self.internal_msgs.pop_front().unwrap();

            if let Some(msg_prod) = self.to_internals.get_mut(&to) {
                if let Err(msg) = msg_prod.try_push(msg) {

                    if start.is_none() { 
                        start = Some(msg.id);

                    } else if msg.id == start.unwrap() {
                        self.internal_msgs.push_front((to, msg));
                        break;
                    }
                    self.internal_msgs.push_back((to, msg));
                }
            } else {
                self.put_msg(msg);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    ////       TIMEOUTS      ////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    fn handle_timeouts(&mut self, maps: &mut ConnMapping<C>) -> Duration  {
        let now = Instant::now();
        while let Some(entry) = self.time_table.peek() {
            if entry.when > now && maps.contains_key(&entry.token) {
                // next deadline is in the future -> return time until it
                return entry.when.saturating_duration_since(now);
            }

            let entry = self.time_table.pop().unwrap();
            if let Some(c) = maps.get(&entry.token) {
                if c.get_last_deadline() == entry.when {
                    println!("EXPIRED {:?}", entry.token);

                    if let Some(mut c) = maps.remove(&entry.token) {
                        c.reset(self);
                    }
                }
            }
        }
        return Duration::from_millis(self.idle_polltimeout);
    }

    fn update_timeout(&mut self, token: &Token, maps: &mut ConnMapping<C>) {
        if let Some(conn) = maps.get_mut(token) {
            conn.set_last_deadline(next_deadline(self.idle_timeout));
            self.time_table.push(TimeoutEntry { 
                when: conn.get_last_deadline().clone(), 
                token: token.clone(),
            });
        }
    }
}

pub fn next_deadline(timeout: u64) -> Instant {
    Instant::now() + Duration::from_secs(timeout)
}

pub struct TimeoutEntry {
    when: Instant,
    token: Token,
}

// Implement Ord reversed so the smallest Instant comes out first
impl Eq for TimeoutEntry {}
impl PartialEq for TimeoutEntry { fn eq(&self, other: &Self) -> bool { self.when == other.when } }
impl PartialOrd for TimeoutEntry { fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> { Some(other.when.cmp(&self.when)) } }
impl Ord for TimeoutEntry { fn cmp(&self, other: &Self) -> std::cmp::Ordering { other.when.cmp(&self.when) } }


