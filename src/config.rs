// SPDX-License-Identifier: GPL-2.0-only

use serde::Deserialize;
use std::{fs, io};
use toml;

use crate::server::NetServerConfig;

pub fn load_config(path: &str) -> Config {
    let data = fs::read_to_string(path)
        .expect("failed to read config file");
    toml::from_str(&data)
        .expect("failed to parse config file")
}

#[derive(Debug, Deserialize)]
pub struct Config {
    pub core: CoreConfig,
    pub server: ServerConfig,
    pub peer: PeerServerConfig,
}

#[derive(Debug, Deserialize, Clone)]
pub struct CoreConfig {
    pub idle_polltimeout: u64,
    pub event_len: usize,
    pub pool_cap: usize,
    pub bullet_count: usize,
    pub db_path: String,
    pub db_cache_size: usize,
    pub trxs_per_block: usize,
    pub epoch_interval: u64,
}

#[derive(Debug, Deserialize, Clone)]
pub struct PeerServerConfig {
    pub bind_addr: String,
    pub db_path: String,
    pub key_path: String,
    pub bootstraps: Vec<[u8; 4]>,
    pub max_connections: usize,
    pub buffers_cap: usize,
    pub max_buffer_size: usize,
    pub conn_q_cap: usize,
    pub idle_timeout: u64,
    pub idle_polltimeout: u64,
    pub poll_event_cap: usize,
    pub peer_threshold: usize,
}

impl NetServerConfig for PeerServerConfig {
    fn get_buffers_cap(&self) -> usize { self.buffers_cap }
    fn get_buffer_size(&self) -> usize { self.max_buffer_size }
    fn get_idle_timeout(&self) -> u64 { self.idle_timeout }
    fn get_max_connections(&self) -> usize { self.max_connections }
    fn get_idle_polltimeout(&self) -> u64 { self.idle_polltimeout }
    fn get_bind_addr(&self) -> std::io::Result<std::net::SocketAddr> {
        self.bind_addr.parse().map_err(|_| 
            io::Error::other("parse bind_addr failed".to_string())
        )
    }
    fn get_events_cap(&self) -> usize { self.poll_event_cap }
    fn get_key_path(&self) -> String { self.key_path.clone() }
    fn get_con_q_cap(&self) -> usize { self.conn_q_cap }
}


#[derive(Debug, Deserialize, Clone)]
pub struct ServerConfig {
    pub bind_addr: String,
    pub key_path: String,
    pub max_connections: usize,
    pub buffers_cap: usize,
    pub max_buffer_size: usize,
    pub conn_q_cap: usize,
    pub poll_event_cap: usize,
    pub idle_timeout: u64,
    pub idle_polltimeout: u64,
}

impl NetServerConfig for ServerConfig {
    fn get_buffers_cap(&self) -> usize { self.buffers_cap }
    fn get_buffer_size(&self) -> usize { self.max_buffer_size }
    fn get_idle_timeout(&self) -> u64 { self.idle_timeout }
    fn get_max_connections(&self) -> usize { self.max_connections }
    fn get_idle_polltimeout(&self) -> u64 { self.idle_polltimeout }
    fn get_bind_addr(&self) -> std::io::Result<std::net::SocketAddr> {
        self.bind_addr.parse().map_err(|_| 
            io::Error::other("parse bind_addr failed".to_string())
        )
    }
    fn get_events_cap(&self) -> usize { self.poll_event_cap }
    fn get_key_path(&self) -> String { self.key_path.clone() }
    fn get_con_q_cap(&self) -> usize { self.conn_q_cap }
}

