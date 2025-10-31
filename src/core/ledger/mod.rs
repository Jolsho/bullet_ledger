use lru::LruCache;

use {bullet_db::DB, node::Node};

mod node;
mod branch;
mod leaf;
mod ext;
mod bullet_db;
mod ledger;

pub type Hash = [u8;32];

pub fn derive_leaf_hash(key: &[u8], hash: &Hash) -> Hash {
    let mut hasher = blake3::Hasher::new();
    hasher.update(key);
    hasher.update(hash);
    let hash = hasher.finalize();
    *hash.as_bytes()
}
pub fn derive_value_hash(value: &[u8]) -> Hash {
    let mut hasher = blake3::Hasher::new();
    hasher.update(value);
    let hash = hasher.finalize();
    *hash.as_bytes()
}

pub struct Ledger {
    db: DB,
    root: Option<Node>,
    cache: LruCache<u64, Node>,
}

