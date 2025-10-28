use std::{cell::RefCell, error, rc::Rc};
use lru::LruCache;
use crate::core::ledger::{branch::BranchNode, ext::ExtNode, leaf::{derive_value_hash, Leaf}, lmdb::DB, node::{Hash, Node, NodeID}};

pub mod node;
pub mod branch;
pub mod leaf;
pub mod ext;
pub mod lmdb;

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
    cache: LruCache<u64, Node>,
}

impl Ledger { 

    pub fn new(path: &str, cap: usize) -> Result<Self, Box<dyn error::Error>> {
        let mut l = Self {
            db: DB::new(path),
            root: None,
            cache: LruCache::new(std::num::NonZeroUsize::new(cap).unwrap()),
        };

        let root_id = 1u64.to_le_bytes();
        if let Some(root) = l.load_node(&root_id) {
            l.root = Some(root);
        } else {
            let root = BranchNode::new(Some(root_id));
            l.root = Some(Node::Branch(root));
        }
        Ok(l)
    }

    fn load_node(&mut self, id: &NodeID) -> Option<Node> {
        let num_id = u64::from_le_bytes(id.clone());
        if let Some(node) = self.cache.get(&num_id) {
            return Some(node.clone());
        }

        if let Ok(raw_node) = self.db.get(id) {
            if let Ok(node) = Node::from_bytes(id.clone(), &raw_node) {
                if let Some((old_key, old_node)) = self.cache.push(num_id, node.clone()) {
                    if old_key != num_id {
                        println!("expired {old_key}");
                        self.db.put(&old_key.to_le_bytes(), old_node.to_bytes()).unwrap();
                    }
                }
                return Some(node);
            }
        }
        return None;
    }

    pub fn cache_node(&mut self, id: u64, node: Node) {
        if let Some((old_id, old_node)) = self.cache.push(id, node) {
            self.db.put(&old_id.to_le_bytes(), old_node.to_bytes()).unwrap();
        }
    }

    pub fn delete_node(&mut self, id: &NodeID) {
        let num_id = u64::from_le_bytes(*id);
        self.cache.pop_entry(&num_id);
        self.db.delete(id).unwrap();
    }

    pub fn new_cached_leaf(&mut self, id: u64) -> Rc<RefCell<Leaf>> {
        let l = Leaf::new(Some(id.to_le_bytes()));
        self.cache_node(id, Node::Leaf(l.clone()));
        l
    }

    pub fn new_cached_branch(&mut self, id: u64) -> Rc<RefCell<BranchNode>> {
        let b = BranchNode::new(Some(id.to_le_bytes()));
        self.cache_node(id, Node::Branch(b.clone()));
        b
    }

    pub fn new_cached_ext(&mut self, id: u64) -> Rc<RefCell<ExtNode>> {
        let b = ExtNode::new(Some(id.to_le_bytes()));
        self.cache_node(id, Node::Extension(b.clone()));
        b
    }

    pub fn value_exists(&mut self, value_hash: &Hash) -> bool {
        self.db.start_trx();
        let res = self.db.exists(value_hash);
        self.db.end_trx();
        res
    }

    pub fn get_value(&mut self, key: &[u8]) -> Option<Vec<u8>> {
        let mut res = None;
        if let Some(root) = self.root.take() {
            let nibs = get_nibbles(key);

            self.db.start_trx();

            if let Some(val_hash) = root.search(self, &nibs) {
                if let Ok(value) = self.db.get(&val_hash) {
                    println!("got: {}", hex::encode(&value));
                    res = Some(value);
                }
            }
            self.root = Some(root);
            
            self.db.end_trx();
        }
        res
    }

    pub fn put(&mut self, key: &[u8], value: Vec<u8>) -> Option<Hash> {
        let hash = derive_value_hash(&value);
        let mut root_hash = None;

        // if definetely doesnt exist
        if !self.value_exists(&hash){ 
            if let Some(mut root) = self.root.take() {
                let nibs = get_nibbles(key);

                self.db.start_trx();

                if let Some(new_root_hash) = root.put(self, &nibs, key, &hash) {
                    root_hash = Some(new_root_hash);

                    println!("put: {}", hex::encode(&value));
                    // save value
                    self.db.put(&hash, value).unwrap();

                }

                if root_hash.is_some() {
                    // save new root node
                    self.db.put(&root.get_id(), root.to_bytes()).unwrap();
                }

                self.db.end_trx();

                self.root = Some(root);
            }
        }
        return root_hash;
    }

    pub fn remove(&mut self, key: &[u8]) -> Option<Hash> {
        let mut root_hash = None;
        if let Some(mut root) = self.root.take() {
            let nibs = get_nibbles(key);
            self.db.start_trx();

            if let Some(new_root_hash) = root.remove(self, &nibs) {
                root_hash = Some(new_root_hash);
            }
            self.root = Some(root);

            self.db.end_trx();
        }
        return root_hash;
    }
}
