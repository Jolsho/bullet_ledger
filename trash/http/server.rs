use std::collections::BinaryHeap;
use std::error;
use std::time::Instant;
use nix::poll::PollTimeout;

use crate::http::ClientMap;
use crate::networker::peers::PeerMan;
use crate::networker::utils::next_deadline;
use crate::msging::Poller;

pub struct ServerConfig {
    pub max_connections: usize,
    pub bind_addr: String,
    pub db_path: String,
    pub peer_threshold: usize,
    pub bootstraps: Vec<[u8; 4]>,
    pub event_buffer_size: usize,
    pub idle_polltimeout: u16,
    pub idle_timeout: u64,
    pub buffers_cap: usize,
    pub max_buffer_size: usize,
}

pub struct Server {
    pub config: ServerConfig,
    pub epoll: Poller,
    pub allowed: PeerMan,
    time_table: BinaryHeap<TimeoutEntry>,
    pub buffs: Vec<Vec<u8>>,
}

impl Server {
    pub fn new(config: ServerConfig) -> Result<Self, Box<dyn error::Error>> {
        Ok(Self{ 
            allowed: PeerMan::new(
                config.db_path.clone(), 
                config.peer_threshold, 
                config.bootstraps.clone()
            )?, 
            time_table: BinaryHeap::with_capacity(config.max_connections),
            buffs: Vec::with_capacity(config.buffers_cap),
            epoll: Poller::new()?,
            config,
        })
    }

    pub fn get_buff(&mut self) -> Vec<u8> {
        let mut buf = self.buffs.pop();
        if buf.is_none() {
            buf = Some(Vec::with_capacity(self.config.max_buffer_size));
        }
        buf.unwrap()
    }
    pub fn put_buff(&mut self, buff: Vec<u8>) { self.buffs.push(buff); }

    pub fn handle_timeouts(&mut self, clients: &mut ClientMap) -> PollTimeout {
        let now = Instant::now();
        while let Some(entry) = self.time_table.peek() {
            if entry.when > now && clients.contains_key(&entry.fd) {
                // next deadline is in the future -> return time until it
                let until = entry.when.saturating_duration_since(now);
                return PollTimeout::try_from(until).unwrap_or_else(|_| 
                    PollTimeout::from(self.config.idle_polltimeout)
                );
            }

            let entry = self.time_table.pop().unwrap();
            if let Some(c) = clients.get(&entry.fd) {
                if c.last_deadline == entry.when {
                    println!("EXPIRED {}", entry.fd);

                    if let Some(mut c) = clients.remove(&entry.fd) {
                        c.strip_and_delete(self);
                    }
                }
            }
        }
        return PollTimeout::from(self.config.idle_polltimeout);
    }

    pub fn update_timeout(&mut self, fd: &i32, clients: &mut ClientMap) {
        if let Some(conn) = clients.get_mut(fd) {
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
