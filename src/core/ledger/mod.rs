use lru::LruCache;

use {lmdb::DB, node::Node};

mod node;
mod branch;
mod leaf;
mod ext;
mod lmdb;
mod ledger;

pub type Hash = [u8;32];

pub fn derive_value_hash(bytes: &[u8]) -> Hash {
    let mut hasher = blake3::Hasher::new();
    hasher.update(bytes);
    let hash = hasher.finalize();
    *hash.as_bytes()
}

pub struct Ledger {
    db: DB,
    root: Option<Node>,
    cache: LruCache<u64, Node>,
}

