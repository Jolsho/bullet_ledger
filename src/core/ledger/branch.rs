use std::{cell::RefCell, rc::Rc, usize};
use crate::core::ledger::{ext::ExtNode, leaf::Leaf, node::{Hash, NodePointer, BRANCH}, Ledger};


pub struct BranchNode { 
    hash: Hash,
    children: [Option<Hash>; 16],
    count: u8,
}

impl BranchNode {
    pub fn new() -> Self {
        Self { 
            count: 0,
            hash: [0u8;32],
            children: std::array::from_fn(|_| None),
        }
    }
    pub fn get_hash(&self) -> Hash { self.hash.clone() }
    pub fn get_next(&self, nib: &u8) -> Option<&Hash> {
        if let Some(hash) = &self.children[*nib as usize] {
            return Some(&hash);
        }
        None
    }

    const ZERO_HASH: [u8;32] = [0u8; 32];
    pub fn from_bytes(bytes: &[u8]) -> NodePointer<Self> {
        let mut s = BranchNode::new();
        for i in 0..17usize {
            let start = 32 * i;
            let end =  32 * (i + 1);
            if i == 0 {
                s.hash.copy_from_slice(&bytes[start..end]);
            } else {
                if bytes[start..end] != Self::ZERO_HASH {
                    let mut hash = [0u8;32];
                    hash.copy_from_slice(&bytes[start..end]);
                    s.children[i-1] = Some(hash);
                    s.count += 1;
                } else {
                    s.children[i-1] = None;
                }
            }
        }
        Rc::new(RefCell::new(s))
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1+(17*32));
        buff.extend_from_slice(&[BRANCH]);
        buff.extend_from_slice(&self.hash);

        let zero_buffer = [0u8;32];
        for child_o in self.children.iter() {
            if let Some(child) = child_o {
                buff.extend_from_slice(child);
            } else {
                buff.extend_from_slice(&zero_buffer);
            }
        }
        buff
    }

    pub fn derive_hash(&mut self) -> Hash {
        let mut hasher = blake3::Hasher::new();
        for child_o in self.children.iter() {
            if let Some(child) = child_o {
                hasher.update(child);
            }
        }
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        *hash.as_bytes()
    }

    pub fn insert(&mut self, nib: &u8, hash: &Hash) {
        if let Some(child) = &mut self.children[*nib as usize] {
            child.copy_from_slice(hash);
        } else {
            self.children[*nib as usize] = Some(hash.clone());
            self.count += 1;
        }
    }
    
    pub fn delete_child(&mut self, nib: &u8) { 
        if self.children[*nib as usize].is_some() {
            self.children[*nib as usize] = None;
            self.count -= 1;
        }
    }

    pub fn put(&mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8],
        key: &[u8], 
        val: Vec<u8>, 
    ) -> Option<Hash> {
        if let Some(next_hash) = self.get_next(&nibbles[0]) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(new_hash) = next_node.put(ledger, &nibbles[1..], key, val) {
                    self.insert(&nibbles[0], &new_hash);
                }
            }
        } else {
            let l = Leaf::new();
            let mut leaf = l.borrow_mut();
            leaf.set_value(val);
            let leaf_hash = leaf.derive_hash(key);

            ledger.db.put(leaf_hash, leaf.to_bytes()).unwrap();

            if nibbles.len() > 1 {

                let e = ExtNode::new();
                let mut ext = e.borrow_mut();
                ext.set_path(&nibbles[1..]);
                ext.set_child(&leaf_hash);
                let ext_hash = ext.derive_hash();

                ledger.db.put(ext_hash, ext.to_bytes()).unwrap();

                self.insert(&nibbles[0], &ext_hash);

            } else {
                self.insert(&nibbles[0], &leaf_hash);
            }
        }
        // delete old self
        ledger.db.delete(&self.get_hash()).unwrap();

        // derive new self and save
        let new_hash = self.derive_hash();
        ledger.db.put(new_hash, self.to_bytes()).unwrap();

        return Some(new_hash);
    }

    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Hash> {
        if let Some(next_hash) = self.get_next(&nibbles[0]) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(hash) = next_node.remove(ledger, &nibbles[1..]) {

                    ledger.db.delete(self.get_hash()).unwrap();

                    if hash == Self::ZERO_HASH {
                        self.delete_child(&nibbles[0]);
                    } else {
                        self.insert(&nibbles[0], &hash);
                    }
                    
                    let mut new_hash = Self::ZERO_HASH;

                    if self.count > 0 {
                        new_hash = self.derive_hash();
                        ledger.db.put(new_hash, self.to_bytes()).unwrap();
                    }

                    return Some(new_hash);
                }
            }
        }
        println!(" branch lacking child \nnibs::{}", hex::encode(nibbles));
        None
    }
}

