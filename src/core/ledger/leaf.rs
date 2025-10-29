use crate::core::ledger::{node::{IsNode, Hash, NodeID, NodePointer, LEAF}, Ledger};
use std::{cell::RefCell, rc::Rc};

pub fn derive_value_hash(bytes: &[u8]) -> Hash {
    let mut hasher = blake3::Hasher::new();
    hasher.update(bytes);
    let hash = hasher.finalize();
    *hash.as_bytes()
}

pub struct Leaf {
    id: NodeID,
    hash: Hash,
    value_hash: Hash,
}

impl IsNode for Leaf {
    fn get_id(&self) -> &NodeID { &self.id }
    fn get_hash(&self) -> &Hash { &self.hash }
}

impl Leaf {
    pub fn new(id: Option<NodeID>) -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            id: id.unwrap_or(NodeID::default()),
            hash: Hash::default(), 
            value_hash: Hash::default(),
        }))
    }

    pub fn get_value_hash(&self) -> &Hash { &self.value_hash }
    pub fn set_value_hash(&mut self, val_hash: &Hash) { 
        self.value_hash.copy_from_slice(val_hash);
    }
    pub fn set_id(&mut self, id: &NodeID) { 
        self.id.copy_from_slice(id);
    }

    pub fn derive_hash(&mut self, key: &[u8]) -> Hash {
        let mut hasher = blake3::Hasher::new();
        hasher.update(key);
        hasher.update(&self.value_hash);
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        hash.into()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1 + 32 + 32);
        buff.extend_from_slice(&[LEAF]);
        buff.extend_from_slice(&self.hash);
        buff.extend_from_slice(&self.value_hash);
        buff
    }

    pub fn from_bytes(id: NodeID, bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = [0u8;32];
        hash.copy_from_slice(&bytes[..32]);

        let mut value_hash = [0u8;32];
        value_hash.copy_from_slice(&bytes[32..64]);

        Rc::new(RefCell::new(Self {
            id, hash, value_hash,
        }))
    }

    pub fn remove(&mut self, ledger: &mut Ledger) -> Option<(Hash, Option<Vec<u8>>)> {
        ledger.delete_node(self.get_id());
        self.hash.fill(0);
        Some((self.hash, None))
    }
}
