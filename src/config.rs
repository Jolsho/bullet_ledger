use serde::Deserialize;
use std::fs;
use toml;

#[derive(Debug, Deserialize)]
pub struct Config {
    pub network: NetworkConfig,
    pub database: DatabaseConfig,
    pub core: CoreConfig,
}

#[derive(Debug, Deserialize, Clone)]
pub struct NetworkConfig {
    pub idle_timeout: u64,
    pub max_connections: usize,
    pub event_buffer_size: usize,
    pub idle_polltimeout: u16,
    pub net_man_buffers_cap: usize,
    pub max_buffer_size: usize,
    pub in_out_q_size: usize,
    pub key_path: String,

    pub bind_addr: String,
}

#[derive(Debug, Deserialize, Clone)]
pub struct DatabaseConfig {
    pub uri: String,
    pub pool_size: usize,
}

#[derive(Debug, Deserialize, Clone)]
pub struct CoreConfig {
    pub idle_polltimeout: u16,
    pub init_trx_len: usize,
    pub event_len: usize,
    pub pool_cap: usize,
}

pub fn load_config(path: &str) -> Config {
    let data = fs::read_to_string(path)
        .expect("failed to read config file");
    toml::from_str(&data)
        .expect("failed to parse config file")
}
