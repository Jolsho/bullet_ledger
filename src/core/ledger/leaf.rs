use crate::core::ledger::{node::{Hash, NodePointer, LEAF}, Ledger};
use std::{cell::RefCell, rc::Rc, usize};

pub fn derive_value_hash(bytes: &[u8]) -> Hash {
    let mut hasher = blake3::Hasher::new();
    hasher.update(bytes);
    let hash = hasher.finalize();
    *hash.as_bytes()
}

pub struct Leaf {
    hash: Hash,
    value: Vec<u8>,
}

impl Leaf {
    pub fn new() -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            hash: [0u8;32], 
            value: Vec::with_capacity(0),
        }))
    }

    pub fn get_value(&self) -> Vec<u8> { self.value.clone() }
    pub fn set_value(&mut self, val: Vec<u8>) { self.value = val; }

    pub fn derive_hash(&mut self, key: &[u8]) -> Hash {
        let mut hasher = blake3::Hasher::new();
        hasher.update(key);
        hasher.update(&self.value);
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        *hash.as_bytes()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1 + 32 + 8 + self.value.len());
        buff.extend_from_slice(&[LEAF]);
        buff.extend_from_slice(&self.hash);
        buff.extend_from_slice(&self.value.len().to_le_bytes());
        buff.extend_from_slice(&self.value);
        buff
    }

    pub fn from_bytes(bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = [0u8;32];
        hash.copy_from_slice(&bytes[..32]);

        let mut val_len = [0u8; 8];
        val_len.copy_from_slice(&bytes[32..40]);
        let len = usize::from_le_bytes(val_len);

        let mut value = Vec::with_capacity(len);
        value.extend_from_slice(&bytes[40..]);

        Rc::new(RefCell::new(Self {
            hash: hash,
            value,
        }))
    }

    pub fn remove(&mut self, ledger: &mut Ledger) -> Option<Hash> {
        ledger.db.delete(&self.hash).unwrap();
        ledger.cache.demote(&self.hash);
        self.hash.fill(0);
        Some(self.hash)
    }
}
