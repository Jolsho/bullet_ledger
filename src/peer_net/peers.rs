// SPDX-License-Identifier: GPL-2.0-only

use std::{collections::HashMap, fs, io::{self, Read, Seek, Write}, net::{Ipv4Addr, SocketAddr}, os::unix::fs::MetadataExt};

use crate::utils::{NetError, NetResult};

pub struct PeerMan {
    file: fs::File,
    peers: HashMap<[u8;4], u16>,
    threshold: u16,
}

impl Drop for PeerMan {
    fn drop(&mut self) {
        let _ = self.file.seek(io::SeekFrom::Start(0));
        let _ = self.file.set_len((self.peers.len() * 6) as u64 + 8);

        let _ = self.file.write_all(&self.peers.len().to_le_bytes());

        for (ip, score) in self.peers.iter() {
            let _ = self.file.write_all(ip);
            let _ = self.file.write_all(&score.to_le_bytes());
        }
    }
}

impl PeerMan {
    pub fn new(db_path: String, threshold: usize, initial_ips: Vec<[u8;4]>) -> io::Result<Self > {
        let mut file = fs::OpenOptions::new()
            .write(true).read(true)
            .create(true).open(&db_path)?;

        let mut peer_count = [0u8; 8];
        if file.metadata().unwrap().size() < 8 {
            file.set_len(8)?;
        }
        let _ = file.read_exact(&mut peer_count)?;
        
        let peer_count = usize::from_le_bytes(peer_count);

        let mut peers = HashMap::with_capacity(100 + peer_count + initial_ips.len());

        for i in 0..peer_count {
            let mut ip = [0u8; 4];
            let mut score = [0u8; 2];
            let cursor = i * 6;
            file.seek_relative(cursor as i64)?;
            file.read_exact(&mut ip)?;
            file.read_exact(&mut score)?;
            peers.insert(ip, u16::from_le_bytes(score));
        }

        for ip in initial_ips {
            if !peers.contains_key(&ip) {
                peers.insert(ip, 0);
            }
        }

        let s = Self { file, peers, 
            threshold: threshold as u16,
        };
        Ok(s)
    }

    pub fn add_peer(&mut self, ip: &Ipv4Addr) {
        let ip = ip.octets();
        if !self.peers.contains_key(&ip) {
            self.peers.insert(ip, 0);
        }
    }

    pub fn remove_peer(&mut self, ip: &Ipv4Addr) {
        self.peers.remove(&ip.octets());
    }

    pub fn is_banned(&mut self, addr: &SocketAddr) -> NetResult<()> {

        if let SocketAddr::V4(ip) = addr {
            if let Some(score) = self.peers.get(&ip.ip().octets()) {
                if *score < self.threshold {
                    return Ok(());
                }
            }
        }
        Err(NetError::Unauthorized)
    }

    pub fn record_behaviour(&mut self, addr: &SocketAddr, error: NetError) -> NetResult<()> {
        if let SocketAddr::V4(ip) = addr {
            if let Some(score) = self.peers.get_mut(&ip.ip().octets()) {
                *score += error.to_score() as u16;
                return Ok(());
            }
        }
        return Err(NetError::Unauthorized);
    }
}
