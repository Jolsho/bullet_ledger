use crate::core::{
    ledger::{node::{NodePointer, LEAF}, Ledger},
    utils::Hash,
};
use std::{cell::RefCell, rc::Rc, usize};

pub fn derive_value_hash(bytes: &[u8]) -> Rc<RefCell<Hash>> {
    let mut hasher = blake3::Hasher::new();
    hasher.update(bytes);
    let hash = hasher.finalize();
    Rc::new(RefCell::new(Hash(*hash.as_bytes())))
}

pub struct Leaf {
    hash: Rc<RefCell<Hash>>,
    value: Vec<u8>,
}

impl Leaf {
    pub fn new() -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            hash: Rc::new(RefCell::new(Hash::ZERO)), 
            value: Vec::with_capacity(0),
        }))
    }

    pub fn get_hash(&self) -> Rc<RefCell<Hash>> {
        self.hash.clone()
    }
    pub fn get_value(&self) -> Vec<u8> {
        // THIS IS BIG CLONE?? TODO
        self.value.clone()
    }

    pub fn set_value(&mut self, val: Vec<u8>) {
        self.value = val;
    }

    pub fn derive_hash(&self) -> Rc<RefCell<Hash>> {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.value);
        let hash = hasher.finalize();
        self.hash.borrow_mut().copy_from_slice(hash.as_bytes());
        self.hash.clone()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1 + 32 + 8 + self.value.len());
        buff.extend_from_slice(&[LEAF]);
        buff.extend_from_slice(&self.hash.borrow().0);
        buff.extend_from_slice(&self.value.len().to_le_bytes());
        buff.extend_from_slice(&self.value);
        buff
    }

    pub fn from_bytes(bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = Hash::ZERO;
        hash.copy_from_slice(&bytes[..32]);

        let mut val_len = [0u8; 8];
        val_len.copy_from_slice(&bytes[32..40]);
        let len = usize::from_le_bytes(val_len);

        let mut value = Vec::with_capacity(len);
        value.extend_from_slice(&bytes[40..]);

        Rc::new(RefCell::new(Self {
            hash: Rc::new(RefCell::new(hash)),
            value,
        }))
    }

    pub fn remove(&mut self, ledger: &mut Ledger) -> Option<Rc<RefCell<Hash>>> {
        if ledger.db.delete(&self.hash.borrow().0).is_err() {
            return None;
        }
        ledger.cache.demote(&self.hash.borrow());
        self.hash.borrow_mut().0.fill(0);
        Some(self.hash.clone())
    }
}
