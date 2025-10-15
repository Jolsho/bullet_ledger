use mio::{Poll, Token};
use core::error;
use std::net::Ipv4Addr;
use std::time::Duration;
use std::{collections::BinaryHeap, time::Instant};
use crate::config::NetworkConfig;
use crate::crypto::montgomery::load_keys;
use crate::msging::MsgProd;
use crate::networker::utils::{next_deadline, NetManCode, NetMsg, NetMsgCode};
use crate::networker::{peers::PeerMan, Mappings, Messengers};

pub struct NetMan {
    pub poll: Poll,
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
        poll: Poll,
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
            poll, to_core, config, peers
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

    // TODO -- might want to send msg to RPC for example...
    // so maybe give ownership return Option<Box<NetMsg>>
    pub fn handle_internal_msg(&mut self, msg: &mut Box<NetMsg>) {
        if let NetMsgCode::Internal(code) = &msg.code {
            match code {
                NetManCode::AddPeer | NetManCode::RemovePeer => {
                    let mut raw_ip = [0u8; 4];
                    let count = msg.body.len() / 4;
                    let mut cursor = 0;
                    for _ in 0..count {
                        raw_ip.copy_from_slice(&msg.body[cursor..cursor+4]);
                        let ip = Ipv4Addr::from_bits(u32::from_le_bytes(raw_ip));
                        
                        if *code == NetManCode::AddPeer {
                            let _ = self.peers.add_peer(&ip);
                        } else {
                            let _ = self.peers.remove_peer(&ip);
                        }
                        cursor += 4
                    }
                }
                _ => {},
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////
    ////       TIMEOUTS      ////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    pub fn handle_timeouts(&mut self, 
        maps: &mut Mappings,
        messengers: &mut Messengers,
    ) -> Duration {
        let now = Instant::now();
        while let Some(entry) = self.time_table.peek() {
            if entry.when > now && maps.conns.contains_key(&entry.token) {
                // next deadline is in the future -> return time until it
                return entry.when.saturating_duration_since(now);
            }

            let entry = self.time_table.pop().unwrap();
            if let Some(c) = maps.conns.get(&entry.token) {
                if c.last_deadline == entry.when {
                    println!("EXPIRED {:?}", entry.token);

                    if let Some(mut c) = maps.conns.remove(&entry.token) {
                        let _ = maps.addrs.remove(&c.addr);
                        c.strip_and_delete(self, messengers);
                    }
                }
            }
        }
        return Duration::from_millis(self.config.idle_polltimeout);
    }

    pub fn update_timeout(&mut self, token: &Token, maps: &mut Mappings) {
        if let Some(conn) = maps.conns.get_mut(token) {
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
