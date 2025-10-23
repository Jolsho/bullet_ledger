use std::{cell::RefCell, error, rc::Rc};
use lru::LruCache;
use rocksdb::{DB};
use crate::core::{ledger::{branch::BranchNode, leaf::derive_value_hash, node::Node}, utils::Hash};

pub mod node;
pub mod branch;
pub mod leaf;
pub mod ext;

pub fn get_nibbles(key: &[u8]) -> Vec<u8> {
    let mut nibbles = Vec::with_capacity(key.len() * 2);
    for &byte in key {
        let high_nibble = byte >> 4;
        let low_nibble = byte & 0x0F;
        nibbles.push(high_nibble);
        nibbles.push(low_nibble);
    }
    nibbles
}

pub struct Ledger {
    db: DB,
    root: Option<Node>,
    cache: LruCache<Hash, Node>,
}

impl Ledger { 

    pub fn new(path: &str, cap: usize) -> Result<Self, Box<dyn error::Error>> {
        let mut l = Self {
            db: DB::open_default(path)?,
            root: None,
            cache: LruCache::new(std::num::NonZeroUsize::new(cap).unwrap()),
        };

        let mut hasher = blake3::Hasher::new();
        hasher.update(b"root-bullet-ledger");
        let root_hash_id: [u8;32] = hasher.finalize().into();

        if let Ok(Some(raw_root_hash)) = l.db.get(root_hash_id) {
            let root_hash = Rc::new(RefCell::new(Hash::default()));

            root_hash.borrow_mut().copy_from_slice(&raw_root_hash);

            if let Some(root) = l.load_node(root_hash) {
                l.root = Some(root);
            } 

        } else {
            let mut root = BranchNode::new();
            let root_hash = root.derive_hash();
            l.db.put(root_hash_id, root_hash.borrow().0).unwrap();
            l.db.put(root_hash.borrow().0, root.to_bytes()).unwrap();
            l.root = Some(Node::Branch(Rc::new(RefCell::new(root))));
        }
        Ok(l)
    }

    fn load_node(&mut self, hash: Rc<RefCell<Hash>>) -> Option<Node> {
        let hash_ref = hash.borrow();
        if let Some(node) = self.cache.get(&*hash_ref) {
            return Some(node.clone());
        }

        if let Ok(Some(raw_node)) = self.db.get(&hash_ref.0) {
            if let Ok(node) = Node::from_bytes(&raw_node) {
                self.cache.push(hash_ref.clone(), node.clone());
                return Some(node);
            }
        }
        return None;
    }

    pub fn value_exists(&mut self, hash: &[u8;32]) -> bool {
        if let Ok(Some(_)) = self.db.get_pinned(hash) {
            return true;
        }
        return false;
    }

    pub fn get(&mut self, key: &[u8]) -> Option<Vec<u8>> {
        let mut res = None;
        if let Some(root) = self.root.take() {
            res = root.search(self, &get_nibbles(key));
            self.root = Some(root);
        }
        res
    }

    pub fn put(&mut self, key: &[u8], value: Vec<u8>) -> Option<Hash> {
        let hash = derive_value_hash(&value);
        let mut root_hash = None;

        // if definetely doesnt exist
        if !self.db.key_may_exist(&hash.borrow().0) || !self.value_exists(&hash.borrow().0){ 
            if let Some(mut root) = self.root.take() {
                let nibs = get_nibbles(key);
                if let Some(new_root_hash) = root.put(self, &nibs, key, value) {
                    root_hash = Some(new_root_hash.borrow().clone());
                }
                self.root = Some(root);
            }
        }
        return root_hash;
    }

    pub fn remove(&mut self, key: &[u8]) -> Option<Hash> {
        let mut root_hash = None;
        if let Some(mut root) = self.root.take() {
            let nibs = get_nibbles(key);
            if let Some(new_root_hash) = root.remove(self, &nibs) {
                root_hash = Some(new_root_hash.borrow().clone());
            }
            self.root = Some(root);
        }
        return root_hash;
    }
}
