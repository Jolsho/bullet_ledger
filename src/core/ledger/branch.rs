use std::{cell::RefCell, rc::Rc, usize};
use crate::core::{ledger::{ext::ExtNode, leaf::Leaf, node::{NodePointer, BRANCH}, Ledger}, utils::Hash};

pub struct BranchNode { 
    hash: Rc<RefCell<Hash>>,
    children: [Option<Rc<RefCell<Hash>>>; 16],
    count: u8,
}

impl BranchNode {
    pub fn new() -> Self {
        Self { 
            count: 0,
            hash: Rc::new(RefCell::new(Hash::default())),
            children: std::array::from_fn(|_| None),
        }
    }
    pub fn get_hash(&self) -> Rc<RefCell<Hash>> { self.hash.clone() }
    pub fn get_next(&self, nib: &u8) -> Option<Rc<RefCell<Hash>>> {
        if let Some(hash) = &self.children[*nib as usize] {
            return Some(hash.clone());
        }
        None
    }

    pub fn from_bytes(bytes: &[u8]) -> NodePointer<Self> {
        let mut s = BranchNode::new();
        for i in 0..17usize {
            let start = 32 * i;
            let end =  32 * (i + 1);
            if i == 0 {
                s.hash.borrow_mut().copy_from_slice(&bytes[start..end]);
            } else {
                let mut hash = Hash::default();
                hash.copy_from_slice(&bytes[start..end]);
                s.children[i-1] = Some(Rc::new(RefCell::new(hash)));
            }
        }
        Rc::new(RefCell::new(s))
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1+(17*32));
        buff.extend_from_slice(&[BRANCH]);

        let zero_buffer = [0u8;32];
        for child_o in self.children.iter() {
            if let Some(child) = child_o {
                buff.extend_from_slice(&child.borrow().0);
            } else {
                buff.extend_from_slice(&zero_buffer);
            }
        }
        buff
    }

    pub fn derive_hash(&mut self) -> Rc<RefCell<Hash>> {
        let mut hasher = blake3::Hasher::new();
        for child_o in self.children.iter() {
            if let Some(child) = child_o {
                hasher.update(&child.borrow().0);
            }
        }
        let hash = hasher.finalize();
        self.hash.borrow_mut().copy_from_slice(hash.as_bytes());
        self.hash.clone()
    }

    pub fn insert(&mut self, nib: &u8, hash: &Rc<RefCell<Hash>>) {
        if let Some(child) = &self.children[*nib as usize] {
            child.borrow_mut().copy_from_slice(&hash.borrow().0);
        } else {
            self.children[*nib as usize] = Some(hash.clone());
        }
    }

    pub fn put(&mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8],
        key: &[u8], 
        val: Vec<u8>, 
    ) -> Option<Rc<RefCell<Hash>>> {
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
            let leaf_hash = leaf.derive_hash();

            ledger.db.put(leaf_hash.borrow().0, leaf.to_bytes()).unwrap();

            if nibbles.len() > 1 {
                let e = ExtNode::new();
                let mut ext = e.borrow_mut();
                ext.set_path(nibbles[1..].to_vec());
                ext.set_child(&leaf_hash);
                let ext_hash = ext.derive_hash();

                ledger.db.put(ext_hash.borrow().0, ext.to_bytes()).unwrap();

                self.insert(&nibbles[0], &ext_hash);

            } else {
                self.insert(&nibbles[0], &leaf_hash);
            }
        }
        // delete old self
        ledger.db.delete(&self.get_hash().borrow().0).unwrap();

        // derive new self and save
        let new_hash = self.derive_hash();
        ledger.db.put(new_hash.borrow().0, self.to_bytes()).unwrap();

        return Some(new_hash);
    }

    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Rc<RefCell<Hash>>> {
        if let Some(next_hash) = self.get_next(&nibbles[0]) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(hash) = next_node.remove(ledger, &nibbles[1..]) {
                    self.insert(&nibbles[0], &hash);
                    return Some(self.derive_hash());
                }
            }
        }
        None
    }
}

