use nix::poll::PollTimeout;
use std::net::SocketAddr;
use std::os::fd::RawFd;
use std::{collections::BinaryHeap, time::Instant};
use crate::config::NetworkConfig;
use crate::core::msg::CoreMsg;
use crate::msging::{MsgProd, Poller, RingCons};
use crate::networker::header::PacketCode;
use crate::trxs::Trx;
use crate::networker::{next_deadline, ConsMap};

pub struct NetMsg {
    pub stream_fd: RawFd,
    pub addr: Option<SocketAddr>,
    pub code: PacketCode,
    pub from_code: i32,
    pub body: Box<Vec<u8>>,
}
const DEFAULT_BUFFER_SIZE:usize = 1024;

impl Default for NetMsg {
    fn default() -> Self { 
        Self { 
            code: PacketCode::None,
            stream_fd: -1, 
            from_code: 0,
            addr: None,
            body: Box::new(Vec::with_capacity(DEFAULT_BUFFER_SIZE))
        }
    }
}

pub struct NetMan {
    pub epoll: Poller,
    time_table: BinaryHeap<TimeoutEntry>,
    pub buffs: Vec<Vec<u8>>,

    pub to_core: MsgProd<CoreMsg>,
    pub trx_con: RingCons<Trx>, 
    pub config: NetworkConfig,
}

impl NetMan {
    pub fn new(
        trx_con: RingCons<Trx>, 
        to_core: MsgProd<CoreMsg>,
        epoll: Poller,
        config: NetworkConfig,
    ) -> nix::Result<Self> {
        Ok(Self { 
            buffs: Vec::with_capacity(config.net_man_buffers_cap),
            time_table: BinaryHeap::with_capacity(config.max_connections),
            trx_con, epoll, to_core, config,
        })
    }

    pub fn get_buff(&mut self) -> Vec<u8> {
        let mut buf = self.buffs.pop();
        if buf.is_none() {
            buf = Some(Vec::with_capacity(self.config.max_buffer_size));
        }
        buf.unwrap()
    }

    pub fn put_buff(&mut self, buff: Vec<u8>) {
        self.buffs.push(buff);
    }

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
                    if let Some(c) = conns.remove(&entry.fd) {
                        let _ = self.epoll.delete(&c.stream);
                        self.put_buff(c.read_buf);
                        self.put_buff(c.write_buf);
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
