/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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

