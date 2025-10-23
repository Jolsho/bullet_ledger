use std::{cell::RefCell, rc::Rc, usize};
use crate::core::{ledger::{branch::BranchNode, leaf::Leaf, node::{NodePointer, EXT}, Ledger}, utils::Hash};

pub struct ExtNode { 
    hash: Rc<RefCell<Hash>>,
    path: Vec<u8>,
    child: Rc<RefCell<Hash>>,
}
impl ExtNode {
    pub fn new() -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            hash: Rc::new(RefCell::new(Hash::default())), 
            path: Vec::new(), 
            child: Rc::new(RefCell::new(Hash::default())), 
        }))
    }
    pub fn path_len(&self) -> usize { self.path.len() }
    pub fn get_nibble(&self) -> u8 { self.path[0] }
    pub fn get_hash(&self) -> Rc<RefCell<Hash>> { self.hash.clone() }
    pub fn set_path(&mut self, path: Vec<u8>) { self.path = path; }
    pub fn set_child(&mut self, child: &Rc<RefCell<Hash>>) { self.child = child.clone(); }

    pub fn cut_prefix(&mut self, n: usize) -> Vec<u8> {
        let mut suffix = self.path.split_off(n);
        std::mem::swap(&mut suffix, &mut self.path);
        suffix
    }

    /// returns ok if self.path is included in nibs.
    /// if not it returns how much of self.path is in nibs.
    pub fn in_path(&self, nibs: &[u8]) -> Result<(), usize> {
        let mut count = 0usize;
        for (i, nib) in self.path.iter().enumerate() {
            if i > nibs.len() || *nib != nibs[i] {
                return Err(count);
            }
            count += 1;
        }
        return Ok(());
    }


    pub fn get_next(&self, nibs: &[u8]) -> Option<Rc<RefCell<Hash>>> {
        if self.in_path(nibs).is_ok() {
            return Some(self.child.clone());
        }
        None
    }

    pub fn derive_hash(&self) -> Rc<RefCell<Hash>> {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.child.borrow().0);
        let hash = hasher.finalize();
        self.hash.borrow_mut().copy_from_slice(hash.as_bytes());
        self.hash.clone()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1+32+32+self.path_len());
        buff.extend_from_slice(&[EXT]);
        buff.extend_from_slice(&self.hash.borrow().0);
        buff.extend_from_slice(&self.child.borrow().0);
        buff.extend_from_slice(&self.path);
        buff
    }

    pub fn from_bytes(bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = Hash::default();
        hash.copy_from_slice(&bytes[..32]);

        let mut child = Hash::default();
        child.copy_from_slice(&bytes[32..64]);

        let mut path = Vec::<u8>::new();
        path.extend_from_slice(&bytes[64..]);
        Rc::new(RefCell::new(
            Self { 
                hash: Rc::new(RefCell::new(hash)), 
                path, 
                child: Rc::new(RefCell::new(child)), 
            }
        ))
    }
    pub fn put(&mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8], 
        key: &[u8], 
        val: Vec<u8>, 
    ) -> Option<Rc<RefCell<Hash>>> {
        if let Some(next_hash) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(hash) = next_node.put(ledger, &nibbles[self.path_len()..], key,val) {

                    self.set_child(&hash);
                    ledger.db.delete(&self.get_hash().borrow().0).unwrap();

                    let new_hash = self.derive_hash();
                    ledger.db.put(new_hash.borrow().0, self.to_bytes()).unwrap();

                    return Some(new_hash);
                }
            }
            return None;
        } else {

            let mut branch = BranchNode::new();
            // if there are more than one nibble left...
            // we have to insert a new extension that will point to a new leaf
            // else just create the new leaf and point to it directly
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

                branch.insert(&nibbles[0], &ext_hash);

            } else {
                branch.insert(&nibbles[0], &leaf_hash);
            }
            

            // if the self.path shares some nibbles with search key
                // need to cut shared prefix from self and use it to create a new extension
                // the new extension will connect prev to new_branch
                // then new branch will connect to current ext and new leaf
            // else just connect ext to new_branch and new_branch back to prev
            let count = self.in_path(&nibbles[self.path_len()..]).unwrap_err();

            if count > 0 {
                // shared path
                let cut_prefix = self.cut_prefix(count);

                let old_ext_hash = self.get_hash();
                let new_old_ext_hash = self.derive_hash();

                // insert new old_ext hash
                ledger.db.put(new_old_ext_hash.borrow().0, self.to_bytes()).unwrap();

                // delete old ext hash
                let _ = ledger.db.delete(old_ext_hash.borrow().0);

                // create new extension
                let new_ext_full = ExtNode::new();
                let mut new_ext = new_ext_full.borrow_mut();
                new_ext.set_path(cut_prefix);

                // current extension will be pointed to by new branch
                branch.insert(&self.get_nibble(), &self.get_hash());

                // derive hash of new_branch and insert into db
                let branch_hash = branch.derive_hash();

                ledger.db.put(branch_hash.borrow().clone(), branch.to_bytes()).unwrap();

                // point new_ext to new_branch
                new_ext.set_child(&branch_hash);

                // derive hash of new_ext and insert into db
                let new_ext_hash = new_ext.derive_hash();
                ledger.db.put(new_ext_hash.borrow().0, new_ext.to_bytes()).unwrap();

                // previous_branch will point to new_ext
                return Some(new_ext_hash);
            } else {

                // branch points to current ext
                branch.insert(&self.get_nibble(), &self.get_hash());

                // derive new hash and put into db
                let branch_hash = branch.derive_hash();
                ledger.db.put(branch_hash.borrow().clone(), branch.to_bytes()).unwrap();

                // previous_branch will point to new_branch instead of ext
                return Some(branch_hash);
            }
        }
    }

    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Rc<RefCell<Hash>>> {
        if let Some(next_hash) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_hash) {
                if let Some(hash) = next_node.remove(ledger, &nibbles[self.path_len()..]) {
                    if *hash.borrow() != Hash::ZERO {
                        self.set_child(&hash);
                        ledger.db.delete(&self.get_hash().borrow().0).unwrap();

                        let new_hash = self.derive_hash();
                        ledger.db.put(new_hash.borrow().0, self.to_bytes()).unwrap();

                        return Some(new_hash);
                    } else {
                        if ledger.db.delete(&self.hash.borrow().0).is_err() { return None; }
                        ledger.cache.demote(&self.hash.borrow());
                        return Some(hash);
                    }
                }
            }
        }
        None
    }
}

