// SPDX-License-Identifier: GPL-2.0-only

use std::{cell::RefCell, collections::VecDeque, rc::Rc, usize};

use crate::core::ledger::{branch::{BranchNode, ORDER}, derive_leaf_hash, derive_value_hash};

use super::{ Ledger, Hash,  node::{NodeID, NodePointer, EXT} };

pub struct ExtNode {
    id: NodeID,
    hash: Hash,
    pub path: VecDeque<u8>,
    child: (Hash, NodeID),
}


impl ExtNode {
    pub fn new(id: Option<NodeID>) -> Rc<RefCell<Self>> {

        let mut child_id = NodeID::default();
        if let Some(i) = id {
            let num = u64::from_le_bytes(i);
            child_id.copy_from_slice(&(num * ORDER).to_le_bytes());
        }

        Rc::new(RefCell::new(Self {
            id: id.unwrap_or(NodeID::default()),
            hash: Hash::default(),
            path: VecDeque::new(),
            child: (Hash::default(), child_id),
        }))
    }

    pub fn get_id(&self) -> &NodeID { &self.id }
    pub fn get_hash(&self) -> &Hash { &self.hash }

    pub fn set_path(&mut self, path: &[u8]) {
        self.path.clear();
        path.iter().for_each(|&nib| self.path.push_back(nib));
    }

    /// ENSURE SELF IS DELETED FROM CACHE BEFORE CALLING
    pub fn change_id(&mut self, id: &NodeID, ledger: &mut Ledger) {
        let num = u64::from_le_bytes(*id);
        self.id.copy_from_slice(id);

        if u64::from_le_bytes(self.child.1) != num * ORDER {
            // load child_node and delete it from cache/db
            let node = ledger.load_node(&self.child.1).unwrap();
            ledger.delete_node(&self.child.1);

            // change its children recursively
            self.child.1 = (num * ORDER).to_le_bytes();
            node.change_id(&self.child.1, ledger);

            // re-cache updated child once its children have been updated
            ledger.cache_node(num * ORDER, node);
        }
    }

    pub fn get_child(&self) -> &(Hash, NodeID) { &self.child }
    pub fn set_child(&mut self, hash: &Hash) { self.child.0.copy_from_slice(hash); }

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

    pub fn get_next(&self, nibs: &[u8]) -> Option<&NodeID> {
        if self.in_path(nibs).is_ok() {
            return Some(&self.child.1);
        }
        None
    }

    pub fn derive_hash(&mut self) -> Hash {
        let mut hasher = blake3::Hasher::new();
        hasher.update(&self.child.0);
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
        let child_id = (p * ORDER).to_le_bytes();

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

    pub fn search(&self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Hash> {
        if let Some(next_id) = self.get_next(&nibbles) {
            if let Some(next_node) = ledger.load_node(next_id) {
                return next_node.search(ledger, &nibbles[self.path.len()..]);
            }
            //println!("ERROR::EXT::SEARCH::FAILED_LOAD,  ID: {} KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
        }

        //println!("ERROR::EXT::SEARCH::NO_ID, KEY: {}", hex::encode(nibbles));
        return None;
    }
    pub fn virtual_put(
        &mut self,
        ledger: &mut Ledger,
        nibbles: &[u8],
        key: &[u8; 32],
        val_hash: &Hash,
    ) -> Option<Hash> {
        if let Some(next_id) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some(hash) = next_node.put(ledger, &nibbles[self.path.len()..], key, val_hash, true) {
                    return Some(derive_value_hash(&hash));
                }
            }
            return None;
        } else {

            // if there is more than one nibble left after removing shared prefix...
            // insert new extension that will point to new leaf from branch
            // else remove/update old self, create new leaf, and point to it directly from branch
            let shared_path_end = self.in_path(&nibbles).unwrap_err();

            // load branch
            let b = BranchNode::new(None);
            let mut branch = b.borrow_mut();

            let (_prefix, nibbles) = nibbles.split_at(shared_path_end);

            let leaf_hash = derive_leaf_hash(key, val_hash);

            branch.insert(&nibbles[0], &leaf_hash);

            // if self.path shares nothing with new key
            // remove old self id
            // connect new_branch to self_copy using popped nibble from self
            // return new_branch to parent
            if shared_path_end == 0 {
                let nib = self.path.front().unwrap();

                // handle edge case where pathlen becomes 0.
                if self.path.len() == 1 {
                    branch.insert(nib, &self.child.0);
                } else {
                    branch.insert(nib, &derive_value_hash(&self.child.0));
                }

                // parent will point to new_branch instead of self
                return Some(branch.derive_hash());
            }

            // if there is path remaining use it to create new extension
            // new extension will connect new_branch to self.child
            // else point branch to self.child directly.

            let nib = &self.path[shared_path_end];
            if self.path.len() > shared_path_end {
                // new branch points to new extension
                branch.insert(nib, &derive_value_hash(&self.child.0));
            } else {
                // point to child
                branch.insert(nib, &self.child.0);
            }
            // derive final virtual state
            return Some(derive_value_hash(&branch.derive_hash()));
        }
    }

    pub fn put(
        &mut self,
        ledger: &mut Ledger,
        nibbles: &[u8],
        key: &[u8; 32],
        val_hash: &Hash,
    ) -> Option<Hash> {
        if let Some(next_id) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some(hash) = next_node.put(ledger, &nibbles[self.path.len()..], key, val_hash, false) {
                    self.set_child(&hash);
                    return Some(self.derive_hash());
                }
            }
            return None;
        } else {
            let self_id = u64::from_le_bytes(self.id);
            let mut branch_id = self_id * ORDER;

            // if there is more than one nibble left after removing shared prefix...
            // insert new extension that will point to new leaf from branch
            // else remove/update old self, create new leaf, and point to it directly from branch
            let shared_path_end = self.in_path(&nibbles).unwrap_err();
            if shared_path_end == 0 {
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

            let (_prefix, nibbles) = nibbles.split_at(shared_path_end);

            let new_branch_child_id = (branch_id * ORDER) + nibbles[0] as u64;
            let l = ledger.new_cached_leaf(new_branch_child_id);
            let mut leaf = l.borrow_mut();
            leaf.set_value_hash(val_hash);
            leaf.set_path(&nibbles[1..]);

            branch.insert(&nibbles[0], &leaf.derive_hash(key));

            // if self.path shares nothing with new key
            // remove old self id
            // connect new_branch to self_copy using popped nibble from self
            // return new_branch to parent
            if shared_path_end == 0 {
                let nib = self.path.pop_front().unwrap();

                // handle edge case where pathlen becomes 0.
                let new_child_id: u64;
                if self.path.len() == 0 {

                    new_child_id = (branch_id * ORDER) + nib as u64;
                    child.change_id(&new_child_id.to_le_bytes(), ledger);
                    branch.insert(&nib, &self.child.0);

                } else {

                    let new_id = (branch_id * ORDER) + nib as u64;
                    let new_self = ledger.new_cached_ext(new_id);
                    let mut new_self_mut = new_self.borrow_mut();

                    // give old child an updated id
                    new_child_id = new_id * ORDER;
                    let new_child_id_bytes = new_child_id.to_le_bytes();
                    child.change_id(&new_child_id_bytes, ledger);

                    new_self_mut.set_child(&self.child.0);
                    new_self_mut.set_path(&Vec::from_iter(self.path.drain(..)));
                    branch.insert(&nib, &new_self_mut.derive_hash());
                }

                ledger.cache_node(new_child_id, child);

                // parent will point to new_branch instead of self
                return Some(branch.derive_hash());
            }

            // if there is path remaining use it to create new extension
            // new extension will connect new_branch to self.child
            // else point branch to self.child directly.

            let remaining = Vec::from_iter(self.path.drain(shared_path_end .. ));
            let (&nib, remaining_path) = remaining.split_first().unwrap();

            let new_child_id: u64;
            if remaining_path.len() > 0 {
                let new_ext_id = (branch_id * ORDER) + nib as u64;

                // create new extension to point to self.child
                let new_ext_full = ledger.new_cached_ext(new_ext_id);
                let mut new_ext = new_ext_full.borrow_mut();
                new_ext.set_path(&remaining_path);

                // derive new child id
                new_child_id = new_ext_id * ORDER;
                let new_id = new_child_id.to_le_bytes();
                child.change_id(&new_id, ledger);

                // set new child id
                new_ext.set_child(&child.get_hash());

                // new branch points to new extension
                branch.insert(&nib, &new_ext.derive_hash());
            } else {
                // derive and set new child id
                new_child_id = (branch_id * ORDER) + nib as u64;

                let new_id = new_child_id.to_le_bytes();
                child.change_id(&new_id, ledger);

                // point to child
                branch.insert(&nib, &child.get_hash());
            }

            // have to recache old_child
            ledger.cache_node(new_child_id, child);

            // set self to point to branch return self
            self.set_child(&branch.derive_hash());
            return Some(self.derive_hash());
        }
    }

    const ZERO_HASH: [u8; 32] = [0u8; 32];
    pub fn remove(
        &mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8]
    ) -> Option<(Hash, Option<Vec<u8>>)> {

        if let Some(next_id) = self.get_next(&nibbles) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some((hash, mut path_ext)) = next_node.remove(ledger, &nibbles[self.path.len()..]) {

                    if let Some(new_path) = path_ext.take() {
                        new_path.into_iter().for_each(|nib| {
                            self.path.push_back(nib)
                        });
                    }

                    if hash != Self::ZERO_HASH {
                        self.child.0.copy_from_slice(&hash);

                        return Some((self.derive_hash(), None));
                    } else {

                        ledger.delete_node(self.get_id());
                        return Some((hash, path_ext));
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
