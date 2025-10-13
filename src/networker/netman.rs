use nix::poll::PollTimeout;
use std::net::SocketAddr;
use std::ops::{Deref, DerefMut};
use std::os::fd::{AsFd, AsRawFd, RawFd};
use std::{collections::BinaryHeap, time::Instant};
use crate::config::NetworkConfig;
use crate::core::msg::CoreMsg;
use crate::crypto::random_b2;
use crate::msging::{MsgProd, Poller, RingCons};
use crate::networker::connection::Connection;
use crate::networker::handlers::Handler;
use crate::networker::header::{PacketCode, HEADER_LEN, PREFIX_LEN};
use crate::trxs::Trx;
use crate::networker::{next_deadline, ConsMap};

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct WriteBuffer {
    buf: Vec<u8>,
}

impl WriteBuffer {
    pub fn new() -> Self {
        let mut s = Self { 
            buf: Vec::with_capacity(DEFAULT_BUFFER_SIZE), 
        };
        s.reset();

        s
    }
    pub fn from_vec(buf: Vec<u8>) -> Self {
        Self { buf }
    }
    pub fn release_buffer(&mut self) -> Vec<u8> {
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.buf);
        read_buf
    }
    // resize back to PREFIX_LEN + HEADER_LEN
    pub fn reset(&mut self) {
        self.buf.resize(PREFIX_LEN + HEADER_LEN, 0);
    }
}

impl Deref for WriteBuffer {
    type Target = Vec<u8>;
    fn deref(&self) -> &Self::Target {
        &self.buf
    }
}
impl DerefMut for WriteBuffer {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.buf
    }
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct NetMsg {
    pub id: u16,
    pub from_code: i32,
    pub stream_fd: RawFd,

    pub addr: Option<SocketAddr>,
    pub pub_key: Option<[u8;32]>,

    pub code: PacketCode,
    pub body: WriteBuffer,
    pub handler: Option<Handler>,
}
const DEFAULT_BUFFER_SIZE:usize = 1024;

impl Default for NetMsg {
    fn default() -> Self { 
        Self { 
            id: u16::from_le_bytes(random_b2()),
            code: PacketCode::None,
            stream_fd: -1, 
            from_code: 0,
            addr: None,
            body: WriteBuffer::new(),
            handler: None,
            pub_key: None,
        }
    }
}

impl NetMsg {
    pub fn fill_fd_and_id(&mut self, conn: &mut Connection) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_fd = conn.as_fd().as_raw_fd();
        self.from_code = 0;
        self.addr = None;
        self.pub_key = None;
    }
    pub fn reset(&mut self) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_fd = -1;
        self.from_code = 0;
        self.addr = None;
        self.pub_key = None;
        self.body.reset();

    }
}

pub struct NetMan {
    pub epoll: Poller,
    pub buffs: Vec<Vec<u8>>,
    time_table: BinaryHeap<TimeoutEntry>,

    pub to_core: MsgProd<CoreMsg>,
    pub trx_con: RingCons<Trx>, 
    pub config: NetworkConfig,

    pub pub_key: [u8;32],
    pub priv_key: [u8;32],
}

impl NetMan {
    pub fn new(
        trx_con: RingCons<Trx>, 
        to_core: MsgProd<CoreMsg>,
        epoll: Poller,
        config: NetworkConfig,
        pub_key: [u8; 32],
        priv_key: [u8; 32],
    ) -> Self {
        Self { 
            priv_key, pub_key,
            buffs: Vec::with_capacity(config.net_man_buffers_cap),
            time_table: BinaryHeap::with_capacity(config.max_connections),
            trx_con, epoll, to_core, config,
        }
    }
    
    pub fn get_buff(&mut self) -> Vec<u8> {
        let mut buf = self.buffs.pop();
        if buf.is_none() {
            buf = Some(Vec::with_capacity(self.config.max_buffer_size));
        }
        buf.unwrap()
    }
    pub fn put_buff(&mut self, buff: Vec<u8>) { self.buffs.push(buff); }

    //////////////////////////////////////////////////////////////////////////////
    ////       TIMEOUTS      ////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    pub fn handle_timeouts(&mut self, conns: &mut ConsMap) -> PollTimeout {
        let now = Instant::now();
        while let Some(entry) = self.time_table.peek() {
            if entry.when > now && conns.contains_key(&entry.fd) {
                // next deadline is in the future -> return time until it
                let until = entry.when.saturating_duration_since(now);
                return PollTimeout::try_from(until).unwrap_or_else(|_| 
                    PollTimeout::from(self.config.idle_polltimeout)
                );
            }

            let entry = self.time_table.pop().unwrap();
            if let Some(c) = conns.get(&entry.fd) {
                if c.last_deadline == entry.when {
                    println!("EXPIRED {}", entry.fd);

                    if let Some(mut c) = conns.remove(&entry.fd) {
                        c.strip_and_delete(self);
                    }
                }
            }
        }
        return PollTimeout::from(self.config.idle_polltimeout);
    }

    pub fn update_timeout(&mut self, fd: &i32, conns: &mut ConsMap) {
        if let Some(conn) = conns.get_mut(fd) {
            conn.last_deadline = next_deadline(self.config.idle_timeout);
            self.time_table.push(TimeoutEntry { 
                when: conn.last_deadline.clone(), 
                fd: fd.clone(),
            });
        }
    }
}

pub struct TimeoutEntry {
    when: Instant,
    fd: i32,
}

// Implement Ord reversed so the smallest Instant comes out first
impl Eq for TimeoutEntry {}
impl PartialEq for TimeoutEntry { fn eq(&self, other: &Self) -> bool { self.when == other.when } }
impl PartialOrd for TimeoutEntry { fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> { Some(other.when.cmp(&self.when)) } }
impl Ord for TimeoutEntry { fn cmp(&self, other: &Self) -> std::cmp::Ordering { other.when.cmp(&self.when) } }
