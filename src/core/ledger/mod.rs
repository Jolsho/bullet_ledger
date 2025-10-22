use std::{cell::RefCell, collections::VecDeque, error, rc::Rc};
use lru::LruCache;
use rocksdb::{DB};
use crate::core::{ledger::node::Node, utils::Hash};

pub mod node;

pub fn get_nibbles(key: &[u8]) -> Vec<u8> {
    let mut nibbles = Vec::with_capacity(key.len() * 2); // each byte gives 2 nibbles
    for &byte in key {
        let high_nibble = byte >> 4;
        let low_nibble = byte & 0x0F;
        nibbles.push(high_nibble);
        nibbles.push(low_nibble);
    }
    nibbles
}

pub struct Ledger {
    db: Rc<DB>,
    touched: VecDeque<Rc<Hash>>,
    root: Rc<RefCell<Node>>,
    cache: LruCache<Rc<Hash>, Rc<RefCell<Node>>>,
}

pub enum GetResult {
    Node(Rc<RefCell<Node>>),
    Value(Vec<u8>),
    None,
}

impl Ledger { 

    pub fn new(path: &str, cap: usize) -> Result<Self, Box<dyn error::Error>> {
        Ok(Self {
            db: Rc::new(DB::open_default(path)?),
            touched: VecDeque::with_capacity(100),
            root: Rc::new(RefCell::new(Node::new())),
            cache: LruCache::new(std::num::NonZeroUsize::new(cap).unwrap()),
        })
    }

    pub fn get_node(&mut self, hash: Rc<Hash>) -> GetResult {
        if let Some(n) = self.cache.get(&hash) {
            return GetResult::Node(n.clone());
        }
        if let Ok(Some(raw_node)) = self.db.get(&hash.0) {
            let node = Rc::new(RefCell::new(Node::new()));
            if node.borrow_mut().from_bytes(&raw_node).is_ok() {
                self.cache.push(hash, node.clone());
                return GetResult::Node(node);
            } else {
                return GetResult::Value(raw_node);
            }
        }
        GetResult::None
    }

    pub fn get_value(&mut self, key: &[u8]) -> Option<Vec<u8>> {
        let mut node_ref = Some(self.root.clone());
        for nib in get_nibbles(key) {
            let node = node_ref.take().unwrap();
            if let Some(hash) =  node.borrow_mut().get_hash(nib) {
                match self.get_node(hash) {
                    GetResult::Node(next_node) => node_ref = Some(next_node),
                    GetResult::Value(val) => return Some(val),
                    GetResult::None => return None,
                }
            } else { break; }
        }
        None
    }
}
