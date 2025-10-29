use crate::core::ledger::{node::{Hash, NodeID, NodePointer, LEAF}, Ledger};
use std::{cell::RefCell, collections::VecDeque, rc::Rc};

pub fn derive_value_hash(bytes: &[u8]) -> Hash {
    let mut hasher = blake3::Hasher::new();
    hasher.update(bytes);
    let hash = hasher.finalize();
    *hash.as_bytes()
}

pub struct Leaf {
    id: NodeID,
    pub path: VecDeque<u8>,
    hash: Hash,
    value_hash: Hash,
}

impl Leaf {
    pub fn new(id: Option<NodeID>) -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            path: VecDeque::with_capacity(64),
            id: id.unwrap_or(NodeID::default()),
            hash: Hash::default(), 
            value_hash: Hash::default(),
        }))
    }
    pub fn get_id(&self) -> &NodeID { &self.id }
    pub fn set_id(&mut self, id: &NodeID) { self.id.copy_from_slice(id); }
    pub fn get_hash(&self) -> &Hash { &self.hash }
    pub fn set_path(&mut self, path: &[u8]) {
        self.path.clear();
        path.iter().for_each(|&nib| self.path.push_back(nib));
    }

    pub fn get_value_hash(&self) -> &Hash { &self.value_hash }
    pub fn set_value_hash(&mut self, val_hash: &Hash) { 
        self.value_hash.copy_from_slice(val_hash);
    }

    pub fn derive_hash(&mut self, key: &[u8; 32]) -> Hash {
        let mut hasher = blake3::Hasher::new();
        hasher.update(key);
        hasher.update(&self.value_hash);
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        hash.into()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1 + 32 + 32 + 8 + 64);
        buff.extend_from_slice(&[LEAF]);
        buff.extend_from_slice(&self.hash);
        buff.extend_from_slice(&self.value_hash);
        buff.extend_from_slice(&self.path.len().to_le_bytes());
        let (path1, path2) = self.path.as_slices();
        buff.extend_from_slice(&path1);
        buff.extend_from_slice(&path2);
        buff
    }

    pub fn from_bytes(id: NodeID, bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = [0u8;32];
        hash.copy_from_slice(&bytes[..32]);

        let mut value_hash = [0u8;32];
        value_hash.copy_from_slice(&bytes[32..64]);

        let mut path_len = [0u8; 8];
        path_len.copy_from_slice(&bytes[64..72]);
        let len = usize::from_le_bytes(path_len);
        let mut path = VecDeque::with_capacity(len);
        path.extend(&bytes[72..72+len]);

        Rc::new(RefCell::new(Self {
            id, hash, value_hash, path,
        }))
    }

    pub fn in_path(&self, nibs: &[u8]) -> Result<(), usize> {
        let matched = self.path.iter().zip(nibs)
            .take_while(|(a, b)| a == b)
            .count();
        if matched == self.path.len() {
            Ok(())
        } else {
            Err(matched)
        }
    }

    pub fn search(&self, nibs: &[u8]) -> Option<Hash> {
        let matched = self.path.iter().zip(nibs)
            .take_while(|(a, b)| a == b)
            .count();
        if matched == self.path.len() {
            Some(self.value_hash)
        } else {
            None
        }
    }
    pub fn put(&mut self, 
        ledger: &mut Ledger, 
        mut nibbles: &[u8], 
        key: &[u8; 32], 
        val_hash: &Hash
    ) -> Option<Hash> {
        let res = self.in_path(&nibbles);
        if res.is_ok() {
            self.set_value_hash(val_hash);
            return Some(self.derive_hash(key));
        }
        let shared_path_count = res.unwrap_err();

        let self_id = u64::from_le_bytes(self.id);

        ledger.delete_node(self.get_id());

        let mut branch_id = self_id;

        let mut e_op = None;
        if shared_path_count > 0 {
            branch_id = self_id * 16;

            let e = ledger.new_cached_ext(self_id);
            let mut ext = e.borrow_mut();

            let mut shared_path = Vec::with_capacity(shared_path_count);
            while shared_path.len() < shared_path_count {
                shared_path.push(self.path.pop_front().unwrap());
            }
            ext.set_path(&shared_path);

            nibbles = &nibbles[shared_path_count..];

            e_op = Some(e.clone());
        }

        let b = ledger.new_cached_branch(branch_id);
        let mut branch = b.borrow_mut();

        // derive a new self
        let self_nib = self.path.pop_front().unwrap();
        let new_self_id = (branch_id * 16) + self_nib as u64;
        let self_leaf = ledger.new_cached_leaf(new_self_id);
        let mut new_self = self_leaf.borrow_mut();
        std::mem::swap(&mut new_self.path, &mut self.path);
        new_self.set_value_hash(self.get_value_hash());
        branch.insert(&self_nib, &new_self.derive_hash(key));

        // derive a new leaf
        let new_leaf_id = (branch_id * 16) + nibbles[0] as u64;
        let l = ledger.new_cached_leaf(new_leaf_id);
        let mut leaf = l.borrow_mut();
        leaf.set_path(&nibbles[1..]);
        leaf.set_value_hash(val_hash);
        branch.insert(&nibbles[0], &leaf.derive_hash(key));

        if shared_path_count > 0 {
            let ext = e_op.unwrap();
            ext.borrow_mut().set_child(&branch.derive_hash());
            Some(ext.borrow_mut().derive_hash())

        } else {
            Some(branch.derive_hash())
        }
    }

    pub fn remove(&mut self, ledger: &mut Ledger) -> Option<(Hash, Option<Vec<u8>>)> {
        ledger.delete_node(self.get_id());
        self.hash.fill(0);
        Some((self.hash, None))
    }
}
