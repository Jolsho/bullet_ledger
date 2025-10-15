use serde::Deserialize;
use std::fs;
use toml;

#[derive(Debug, Deserialize)]
pub struct Config {
    pub network: NetworkConfig,
    pub core: CoreConfig,
    pub server: ServerConfig,
}

#[derive(Debug, Deserialize, Clone)]
pub struct NetworkConfig {
    pub idle_timeout: u64,
    pub max_connections: usize,
    pub event_buffer_size: usize,
    pub idle_polltimeout: u64,
    pub net_man_buffers_cap: usize,
    pub max_buffer_size: usize,
    pub in_out_q_size: usize,
    pub key_path: String,
    pub db_path: String,
    pub score_threshold: usize,
    pub bootstraps: Vec<[u8;4]>,

    pub bind_addr: String,
}


#[derive(Debug, Deserialize, Clone)]
pub struct CoreConfig {
    pub idle_polltimeout: u64,
    pub event_len: usize,
    pub pool_cap: usize,
    pub bullet_count: usize,
    pub db_path: String,
}

#[derive(Debug, Deserialize, Clone)]
pub struct ServerConfig {
    pub max_connections: usize,
    pub bind_addr: String,
    pub db_path: String,
    pub peer_threshold: usize,
    pub bootstraps: Vec<[u8; 4]>,
    pub event_buffer_size: usize,
    pub idle_polltimeout: u64,
    pub idle_timeout: u64,
    pub buffers_cap: usize,
    pub max_buffer_size: usize,
}

pub fn load_config(path: &str) -> Config {
    let data = fs::read_to_string(path)
        .expect("failed to read config file");
    toml::from_str(&data)
        .expect("failed to parse config file")
}
