use nix::poll::PollTimeout;
use core::error;
use std::net::Ipv4Addr;
use std::{collections::BinaryHeap, time::Instant};
use crate::config::NetworkConfig;
use crate::crypto::montgomery::load_keys;
use crate::msging::{MsgProd, Poller};
use crate::networker::utils::{next_deadline, NetManCode, NetMsg, NetMsgCode};
use crate::networker::{peers::PeerMan, Mappings, Messengers};

pub struct NetMan {
    pub epoll: Poller,
    pub buffs: Vec<Vec<u8>>,
    time_table: BinaryHeap<TimeoutEntry>,

    pub to_core: MsgProd<NetMsg>,
    pub config: NetworkConfig,

    pub peers: PeerMan,

    pub pub_key: [u8;32],
    pub priv_key: [u8;32],
}

impl NetMan {
    pub fn new(
        config: NetworkConfig,
        to_core: MsgProd<NetMsg>,
        epoll: Poller,
    ) -> Result<Self, Box<dyn error::Error>> {
        let (pub_key, priv_key) = load_keys(&config.key_path)?;
        let peers = PeerMan::new(
            config.db_path.clone(), 
            config.score_threshold,
            config.bootstraps.clone(),
        )?;

        Ok(Self { 
            pub_key, priv_key,
            buffs: Vec::with_capacity(config.net_man_buffers_cap),
            time_table: BinaryHeap::with_capacity(config.max_connections),
            epoll, to_core, config, peers
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


    //////////////////////////////////////////////////////////////////////////////
    ////       INTERNAL MESSAGING      //////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    pub fn handle_internal_msg(&mut self, msg: &mut Box<NetMsg>) {
        match msg.code {
            NetMsgCode::Internal(NetManCode::AddPeer) => {
                let mut raw_ip = [0u8; 4];
                raw_ip.copy_from_slice(&msg.body[..4]);
                let ip = Ipv4Addr::from_bits(u32::from_le_bytes(raw_ip));
                let _ = self.peers.add_peer(&ip);
            }
            NetMsgCode::Internal(NetManCode::RemovePeer) => {
                let mut raw_ip = [0u8; 4];
                raw_ip.copy_from_slice(&msg.body[..4]);
                let ip = Ipv4Addr::from_bits(u32::from_le_bytes(raw_ip));
                let _ = self.peers.remove_peer(&ip);

            }
            _ => {},
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    ////       TIMEOUTS      ////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    pub fn handle_timeouts(&mut self, 
        maps: &mut Mappings,
        messengers: &mut Messengers,
    ) -> PollTimeout {
        let now = Instant::now();
        while let Some(entry) = self.time_table.peek() {
            if entry.when > now && maps.conns.contains_key(&entry.fd) {
                // next deadline is in the future -> return time until it
                let until = entry.when.saturating_duration_since(now);
                return PollTimeout::try_from(until).unwrap_or_else(|_| 
                    PollTimeout::from(self.config.idle_polltimeout)
                );
            }

            let entry = self.time_table.pop().unwrap();
            if let Some(c) = maps.conns.get(&entry.fd) {
                if c.last_deadline == entry.when {
                    println!("EXPIRED {}", entry.fd);

                    if let Some(mut c) = maps.conns.remove(&entry.fd) {
                        let _ = maps.addrs.remove(&c.addr);
                        c.strip_and_delete(self, messengers);
                    }
                }
            }
        }
        return PollTimeout::from(self.config.idle_polltimeout);
    }

    pub fn update_timeout(&mut self, fd: &i32, maps: &mut Mappings) {
        if let Some(conn) = maps.conns.get_mut(fd) {
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
