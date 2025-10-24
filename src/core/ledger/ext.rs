use std::{cell::RefCell, collections::VecDeque, rc::Rc, usize};
use crate::core::ledger::{branch::BranchNode, leaf::Leaf, node::{Hash, NodePointer, EXT}, Ledger};

pub struct ExtNode { 
    hash: Hash,
    path: VecDeque<u8>,
    child: Hash,
}
impl ExtNode {
    pub fn new() -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            hash: Hash::default(), 
            path: VecDeque::new(), 
            child: Hash::default(), 
        }))
    }
    pub fn path_len(&self) -> usize { self.path.len() }
    pub fn get_hash(&self) -> &Hash { &self.hash }
    pub fn set_path(&mut self, path: &[u8]) { 
        self.path.clear();
        self.path.extend(path.iter().copied());
    }
    pub fn set_child(&mut self, child: &Hash) { 
        self.child.copy_from_slice(child); 
    }

    pub fn cut_prefix(&mut self, n: usize) -> Vec<u8> {
        let prefix_iter = self.path.drain(..n);
        Vec::from_iter(prefix_iter)
    }

    /// returns ok if self.path is included in nibs.
    /// if not it returns how much of self.path is in nibs.
    pub fn in_path(&self, nibs: &[u8]) -> Result<(), usize> {

        let mut count = 0usize;
        for (i, nib) in self.path.iter().enumerate() {
            if i > nibs.len() - 1 || *nib != nibs[i] {
                return Err(count);
            }
            count += 1;
        }
        return Ok(());
    }


    pub fn get_next(&self, nibs: &[u8]) -> Option<&Hash> {
        if self.in_path(nibs).is_ok() {
            return Some(&self.child);
        }
        None
    }

    pub fn derive_hash(&mut self) -> Hash {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.child);
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        *hash.as_bytes()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1+32+32+self.path_len());
        buff.extend_from_slice(&[EXT]);
        buff.extend_from_slice(&self.hash);
        buff.extend_from_slice(&self.child);
        let (path1, path2) = self.path.as_slices();
        buff.extend_from_slice(&path1);
        buff.extend_from_slice(&path2);
        buff
    }

    pub fn from_bytes(bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = Hash::default();
        hash.copy_from_slice(&bytes[..32]);

        let mut child = Hash::default();
        child.copy_from_slice(&bytes[32..64]);

        let mut path = VecDeque::with_capacity(bytes.len() - 64);
        path.extend(&bytes[64..]);
        Rc::new(RefCell::new(
            Self { hash, path, child, }
        ))
    }

    pub fn put(&mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8], 
        key: &[u8], 
        val: Vec<u8>, 
    ) -> Option<Hash> {
        if let Some(next_hash) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(hash) = next_node.put(ledger, &nibbles[self.path_len()..], key,val) {

                    ledger.db.delete(self.get_hash()).unwrap();

                    self.set_child(&hash);
                    let new_hash = self.derive_hash();
                    ledger.db.put(new_hash, self.to_bytes()).unwrap();

                    return Some(new_hash);
                }
            }
            return None;
        } else {

            let mut branch = BranchNode::new();

            // if there is more than one nibble left after removing shared prefix...
            // insert  new extension that will point to new leaf
            // else just create new leaf and point to it directly
            let l = Leaf::new();
            let mut leaf = l.borrow_mut();
            leaf.set_value(val);
            let leaf_hash = leaf.derive_hash(key);

            ledger.db.put(leaf_hash, leaf.to_bytes()).unwrap();

            let shared_path_count = self.in_path(&nibbles).unwrap_err();

            let (_prefix, nibbles) = nibbles.split_at(shared_path_count);

            if nibbles.len() > 1 {
                let e = ExtNode::new();
                let mut ext = e.borrow_mut();
                ext.set_path(&nibbles[1..]);
                ext.set_child(&leaf_hash);
                let ext_hash = ext.derive_hash();

                ledger.db.put(ext_hash, ext.to_bytes()).unwrap();

                branch.insert(&nibbles[0], &ext_hash);
            } else {

                branch.insert(&nibbles[0], &leaf_hash);
            }

                
            // delete old ext hash
            let old_ext_hash = self.get_hash();
            ledger.db.delete(old_ext_hash).unwrap();

            if shared_path_count == 0 {
                println!("nibs:: {}", hex::encode(&nibbles));
                // just connect ext to new_branch and new_branch back to prev

                // branch points to current ext
                let new_ext_hash = self.derive_hash();
                branch.insert(&self.cut_prefix(1)[0], &new_ext_hash);

                ledger.db.put(new_ext_hash, self.to_bytes()).unwrap();

                // derive new hash and put into db
                let branch_hash = branch.derive_hash();
                ledger.db.put(branch_hash, branch.to_bytes()).unwrap();

                // previous_branch will point to new_branch instead of ext
                return Some(branch_hash);

            } else {
                // if the self.path shares some nibbles with search key
                // need to cut shared prefix from self and use it to create a new extension
                // the new extension will connect prev to new_branch
                // then new branch will connect to current ext and new leaf
                
                // shared path
                let prefix = self.cut_prefix(shared_path_count);

                let nib = self.cut_prefix(1)[0];

                if self.path_len() > 0 {

                    // insert new old_ext hash
                    let old_ext_hash = self.derive_hash();
                    ledger.db.put(old_ext_hash, self.to_bytes()).unwrap();

                    // current extension will be pointed to by new branch
                    branch.insert(&nib, &old_ext_hash);
                } else {

                    // just point to old leaf
                    branch.insert(&nib, &self.child);
                }

                // derive hash of new_branch and insert into db
                let branch_hash = branch.derive_hash();
                ledger.db.put(branch_hash, branch.to_bytes()).unwrap();

                // if this extension shares a partial path with new branch
                    // create a new extension which 
                // else just return branch hash
                if prefix.len() > 0 {

                    // create new extension
                    let new_ext_full = ExtNode::new();
                    let mut new_ext = new_ext_full.borrow_mut();
                    new_ext.set_path(&prefix);


                    // point new_ext to new_branch
                    new_ext.set_child(&branch_hash);

                    // derive hash of new_ext and insert into db
                    let new_ext_hash = new_ext.derive_hash();
                    ledger.db.put(new_ext_hash, new_ext.to_bytes()).unwrap();

                    // previous_branch will point to new_ext
                    return Some(new_ext_hash);

                } else {

                    // previous_branch will point to new_branch
                    return Some(branch_hash);
                }
            }
        }
    }

    const ZERO_HASH: [u8;32] = [0u8; 32];
    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Hash> {
        if let Some(next_hash) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(hash) = next_node.remove(ledger, &nibbles[self.path_len()..]) {
                    if hash != Self::ZERO_HASH {
                        self.set_child(&hash);
                        ledger.db.delete(self.get_hash()).unwrap();

                        let new_hash = self.derive_hash();
                        ledger.db.put(new_hash, self.to_bytes()).unwrap();

                        return Some(new_hash);
                    } else {

                        ledger.db.delete(self.hash).unwrap();
                        ledger.cache.demote(&self.hash);
                        return Some(hash);
                    }
                }
            }
        }
        println!("\n extension lacking child \nnibs::  {}\npath::{}", hex::encode(nibbles), hex::encode(self.path.as_slices().0));
        None
    }
}

