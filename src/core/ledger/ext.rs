use std::{cell::RefCell, collections::VecDeque, rc::Rc, usize};

use crate::core::ledger::{
    Ledger,
    node::{EXT, Hash, IsNode, NodeID, NodePointer},
};

pub struct ExtNode {
    id: NodeID,
    hash: Hash,
    path: VecDeque<u8>,
    child: (Hash, NodeID),
}

impl IsNode for ExtNode {
    fn get_id(&self) -> &NodeID {
        &self.id
    }
    fn get_hash(&self) -> &Hash {
        &self.hash
    }
}

impl ExtNode {
    pub fn new(id: Option<NodeID>) -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self {
            id: id.unwrap_or(NodeID::default()),
            hash: Hash::default(),
            path: VecDeque::new(),
            child: (Hash::default(), NodeID::default()),
        }))
    }
    pub fn path_len(&self) -> usize {
        self.path.len()
    }

    pub fn set_path(&mut self, path: &[u8]) {
        self.path.clear();
        self.path.extend(path.iter().copied());
    }
    pub fn change_id(&mut self, id: &NodeID, ledger: &mut Ledger) {
        let num = u64::from_le_bytes(*id);
        self.id.copy_from_slice(id);
        if u64::from_le_bytes(self.child.1) != num * 16 {
            // load child_node and delete it from cache/db
            let node = ledger.load_node(&self.child.1).unwrap();
            ledger.delete_node(&self.child.1);

            // change its children recursively
            self.child.1 = (num * 16).to_le_bytes();
            node.change_id(&self.child.1, ledger);

            // re-cache updated child once its children have been updated
            ledger.cache_node(num * 16, node);
        }
    }
    pub fn get_child(&self) -> &(Hash, NodeID) {
        &self.child
    }
    pub fn set_child(&mut self, hash: &Hash, id: &NodeID) {
        self.child.0.copy_from_slice(hash);
        self.child.1.copy_from_slice(id);
    }

    pub fn cut_prefix(&mut self, n: usize) -> Vec<u8> {
        let prefix_iter = self.path.drain(..n);
        Vec::from_iter(prefix_iter)
    }
    pub fn cut_remaining(&mut self, n: usize) -> Vec<u8> {
        let suffix_iter = self.path.drain(n..);
        Vec::from_iter(suffix_iter)
    }
    pub fn path_pop_front(&mut self) -> Option<u8> {
        self.path.pop_front()
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

    pub fn get_next(&self, nibs: &[u8]) -> Option<&NodeID> {
        if self.in_path(nibs).is_ok() {
            return Some(&self.child.1);
        }
        None
    }

    pub fn derive_hash(&mut self) -> Hash {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.child.0);
        hasher.update(&self.child.1);
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        *hash.as_bytes()
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1 + 32 + 32 + 8 + 64);
        buff.extend_from_slice(&[EXT]);
        buff.extend_from_slice(&self.hash);
        buff.extend_from_slice(&self.child.0);
        buff.extend_from_slice(&self.path.len().to_le_bytes());
        let (path1, path2) = self.path.as_slices();
        buff.extend_from_slice(&path1);
        buff.extend_from_slice(&path2);
        buff
    }

    pub fn from_bytes(id: NodeID, bytes: &[u8]) -> NodePointer<Self> {
        let mut hash = Hash::default();
        hash.copy_from_slice(&bytes[..32]);

        let mut child_hash = Hash::default();
        child_hash.copy_from_slice(&bytes[32..64]);

        let p = u64::from_le_bytes(id.clone());
        let child_id = (p * 16).to_le_bytes();

        let mut path_len = [0u8; 8];
        path_len.copy_from_slice(&bytes[64..72]);
        let len = usize::from_le_bytes(path_len);
        let mut path = VecDeque::with_capacity(len);
        path.extend(&bytes[72..72+len]);
        Rc::new(RefCell::new(Self {
            id,
            hash,
            path,
            child: (child_hash, child_id),
        }))
    }

    pub fn put(
        &mut self,
        ledger: &mut Ledger,
        nibbles: &[u8],
        key: &[u8],
        val_hash: &Hash,
    ) -> Option<Hash> {
        if let Some(next_id) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some(hash) = next_node.put(ledger, &nibbles[self.path_len()..], key, val_hash) {
                    self.set_child(&hash, &next_node.get_id());
                    return Some(self.derive_hash());
                }
            }
            return None;
        } else {
            let self_id = u64::from_le_bytes(self.id);
            let mut branch_id = self_id * 16;

            // if there is more than one nibble left after removing shared prefix...
            // insert new extension that will point to new leaf from branch
            // else remove/update old self, create new leaf, and point to it directly from branch
            let res = self.in_path(&nibbles);
            if res.is_ok() { return None; }
            let shared_path_count = res.unwrap_err();

            if shared_path_count == 0 {
                // delete old id self if exists
                // hold onto reference to handle later though
                ledger.delete_node(self.get_id());

                branch_id = self_id;
            }

            // load self.child and invalidate its cache entry
            // have to put this here(odd spot)
            // because it can collide with a new child id
            let child = ledger.load_node(&self.child.1).unwrap();
            ledger.delete_node(&self.child.1);


            // load branch
            let b = ledger.new_cached_branch(branch_id);
            let mut branch = b.borrow_mut();

            let (_prefix, nibbles) = nibbles.split_at(shared_path_count);

            let new_branch_child_id = (branch_id * 16) + nibbles[0] as u64;

            if nibbles.len() > 1 {
                // this extension will connect branch to leaf
                // therefore it adopts the first_grand_child_id from leaf
                // and derives a new id for the leaf

                let e = ledger.new_cached_ext(new_branch_child_id);
                let mut ext = e.borrow_mut();

                let l = ledger.new_cached_leaf(new_branch_child_id * 16);
                let mut leaf = l.borrow_mut();
                leaf.set_value_hash(val_hash);

                ext.set_path(&nibbles[1..]);
                ext.set_child(&leaf.derive_hash(key), leaf.get_id());

                branch.insert(&nibbles[0], &ext.derive_hash(), ext.get_id());
            } else {
                let l = ledger.new_cached_leaf(new_branch_child_id);
                let mut leaf = l.borrow_mut();
                leaf.set_value_hash(val_hash);

                branch.insert(&nibbles[0], &leaf.derive_hash(key), leaf.get_id());
            }

            // if self.path shares nothing with new key
            // remove old self id
            // connect new_branch to self_copy using popped nibble from self
            // return new_branch to parent
            if shared_path_count == 0 {
                let nib = self.path_pop_front().unwrap();

                // handle edge case where pathlen becomes 0.
                let new_child_id: u64;
                if self.path_len() == 0 {

                    new_child_id = (branch_id * 16) + nib as u64;
                    let new_id = new_child_id.to_le_bytes();
                    child.change_id(&new_id, ledger);
                    branch.insert(&nib, &child.derive_hash(), &new_id);

                } else {

                    let new_id = (branch_id * 16) + nib as u64;
                    let new_self = ledger.new_cached_ext(new_id);
                    let mut new_self_mut = new_self.borrow_mut();

                    // give old child an updated id
                    new_child_id = new_id * 16;
                    let new_child_id_bytes = new_child_id.to_le_bytes();
                    child.change_id(&new_child_id_bytes, ledger);

                    new_self_mut.set_child(&child.get_hash(), &new_child_id_bytes);
                    new_self_mut.set_path(&self.cut_remaining(0));
                    branch.insert(&nib, &new_self_mut.derive_hash(), new_self_mut.get_id());
                }

                ledger.cache_node(new_child_id, child);

                // previous_branch will point to new_branch instead of self
                return Some(branch.derive_hash());
            }

            // if there is path remaining use it to create new extension
            // new extension will connect new_branch to self.child
            // else point branch to self.child directly.

            let remaining = self.cut_remaining(shared_path_count);
            let (&nib, remaining_path) = remaining.split_first().unwrap();

            let new_child_id: u64;
            if remaining_path.len() > 0 {
                let new_ext_id = (branch_id * 16) + nib as u64;

                // create new extension to point to self.child
                let new_ext_full = ledger.new_cached_ext(new_ext_id);
                let mut new_ext = new_ext_full.borrow_mut();
                new_ext.set_path(&remaining_path);

                // derive new child id
                new_child_id = new_ext_id * 16;
                let new_id = new_child_id.to_le_bytes();
                child.change_id(&new_id, ledger);

                // set new child id
                new_ext.set_child(&child.get_hash(), &new_id);

                // new branch points to new extension
                branch.insert(&nib, &new_ext.derive_hash(), new_ext.get_id());
            } else {
                // derive and set new child id
                new_child_id = (branch_id * 16) + nib as u64;

                let new_id = new_child_id.to_le_bytes();
                child.change_id(&new_id, ledger);

                // point to child
                branch.insert(&nib, &child.get_hash(), &new_id);
            }

            // have to recache old_child
            ledger.cache_node(new_child_id, child);

            // set self to point to branch return self
            self.set_child(&branch.derive_hash(), branch.get_id());
            return Some(self.derive_hash());
        }
    }

    const ZERO_HASH: [u8; 32] = [0u8; 32];
    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Hash> {
        if let Some(next_id) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some(hash) = next_node.remove(ledger, &nibbles[self.path_len()..]) {
                    if hash != Self::ZERO_HASH {
                        self.child.0.copy_from_slice(&hash);
                        return Some(self.derive_hash());
                    } else {
                        ledger.delete_node(self.get_id());
                        return Some(hash);
                    }
                } else {
                    println!("ERROR::EXT::REMOVE::RECV_NONE,     ID: {:10},  KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
                }
            } else {
                println!("ERROR::EXT::REMOVE::FAIL_LOAD,  ID: {:10},  KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
            }
        } else {
            println!("ERROR::EXT::REMOVE::NO_ID,  KEY: {}", hex::encode(nibbles));
        }
        None
    }
}
