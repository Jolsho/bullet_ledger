use std::collections::BinaryHeap;
use std::error;
use std::time::{Duration, Instant};
use mio::net::TcpStream;
use mio::{Poll, Token};

use crate::config::ServerConfig;
use crate::networker::peers::PeerMan;
use crate::networker::utils::next_deadline;
use crate::rpc::connection::RpcConn;
use crate::rpc::ClientMap;

pub struct Server {
    pub config: ServerConfig,
    pub poll: Poll,
    pub allowed: PeerMan,
    time_table: BinaryHeap<TimeoutEntry>,
    pub buffs: Vec<Vec<u8>>,
    pub conns: Vec<Box<RpcConn>>,
}

impl Server {
    pub fn new(config: ServerConfig, poll: Poll) -> Result<Self, Box<dyn error::Error>> {
        Ok(Self{ 
            allowed: PeerMan::new(
                config.db_path.clone(), 
                config.peer_threshold, 
                config.bootstraps.clone()
            )?, 
            time_table: BinaryHeap::with_capacity(config.max_connections),
            buffs: Vec::with_capacity(config.buffers_cap),
            conns: Vec::with_capacity(config.max_connections),
            config, poll
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


    pub fn get_conn(&mut self, stream: TcpStream) -> Box<RpcConn> {
        let mut conn = self.conns.pop();
        if conn.is_none() {
            conn = Some(Box::new(RpcConn::new(self)));
        }
        let mut conn = conn.unwrap();
        conn.initialize(stream, self);
        conn
    }

    pub fn put_conn(&mut self, mut conn: Box<RpcConn>) { 
        conn.reset(self);
        self.conns.push(conn); 
    }


    //////////////////////////////////////////////////////////////////////////////
    ////       TIMEOUTS      ////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    pub fn handle_timeouts(&mut self, 
        maps: &mut ClientMap,
    ) -> Duration {
        let now = Instant::now();
        while let Some(entry) = self.time_table.peek() {
            if entry.when > now && maps.contains_key(&entry.token) {
                // next deadline is in the future -> return time until it
                return entry.when.saturating_duration_since(now);
            }

            let entry = self.time_table.pop().unwrap();
            if let Some(c) = maps.get(&entry.token) {
                if c.last_deadline == entry.when {
                    println!("EXPIRED {:?}", entry.token);

                    if let Some(c) = maps.remove(&entry.token) {
                        self.put_conn(c);
                    }
                }
            }
        }
        return Duration::from_millis(self.config.idle_polltimeout);
    }

    pub fn update_timeout(&mut self, token: &Token, maps: &mut ClientMap) {
        if let Some(conn) = maps.get_mut(token) {
            conn.last_deadline = next_deadline(self.config.idle_timeout);
            self.time_table.push(TimeoutEntry { 
                when: conn.last_deadline.clone(), 
                token: token.clone(),
            });
        }
    }
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
