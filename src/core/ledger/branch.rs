use std::{cell::RefCell, rc::Rc, usize};
use crate::core::ledger::{node::{Hash, IsNode, Node, NodeID, BRANCH}, Ledger};


pub struct BranchNode { 
    id: NodeID,
    hash: Hash,
    children: [Option<(Hash, NodeID)>; 16],
    count: u8,
}
impl IsNode for BranchNode {
    fn get_id(&self) -> &NodeID { &self.id }
    fn get_hash(&self) -> &Hash { &self.hash }
}

impl BranchNode {
    pub fn new(id: Option<NodeID>) -> Rc<RefCell<Self>> {
        Rc::new(RefCell::new(Self { 
            count: 0,
            id: id.unwrap_or(NodeID::default()),
            hash: [0u8;32],
            children: std::array::from_fn(|_| None),
        }))
    }
    pub fn change_id(&mut self, id: &NodeID, ledger: &mut Ledger) {
        let num = u64::from_le_bytes(*id);
        self.id.copy_from_slice(id);

        for (i, child) in self.children.iter_mut().enumerate() {
            if let Some((_, child_id)) = child {
                let should_be = num * 16 + i as u64;
                if u64::from_le_bytes(*child_id) != should_be {
                    // load child_node and delete it from cache/db
                    let node = ledger.load_node(child_id).unwrap();
                    ledger.delete_node(child_id);

                    // change its children recursively
                    *child_id = should_be.to_le_bytes();
                    node.change_id(child_id, ledger);

                    // re-cache updated child once its children have been updated
                    ledger.cache_node(should_be, node);
                }
            }
        }
    }

    pub fn get_next(&self, nib: &u8) -> Option<&(Hash, NodeID)> {
        if let Some(hash) = &self.children[*nib as usize] {
            return Some(&hash);
        }
        None
    }

    const ZERO_HASH: [u8;32] = [0u8; 32];
    pub fn from_bytes(id: NodeID, bytes: &[u8]) -> Rc<RefCell<Self>> {
        let b = BranchNode::new(Some(id));

        let p = u64::from_le_bytes(b.borrow().id.clone());

        for i in 0..17usize {
            let start = 32 * i;
            let end =  32 * (i + 1);
            if i == 0 {
                b.borrow_mut().hash.copy_from_slice(&bytes[start..end]);

            } else {

                if bytes[start..end] != Self::ZERO_HASH {
                    let mut hash = [0u8;32];
                    hash.copy_from_slice(&bytes[start..end]);

                    let child_id = (p * 16) + (i - 1) as u64;

                    b.borrow_mut().children[i-1] = Some((hash, child_id.to_le_bytes()));
                    b.borrow_mut().count += 1;
                } else {

                    b.borrow_mut().children[i-1] = None;
                }
            }
        }
        b
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buff = Vec::with_capacity(1+(17*32));
        buff.extend_from_slice(&[BRANCH]);
        buff.extend_from_slice(&self.hash);

        let zero_buffer = [0u8;32];
        for child_o in self.children.iter() {
            if let Some(child) = child_o {
                buff.extend_from_slice(&child.0);
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
                hasher.update(&child.0);
            }
        }
        let hash = hasher.finalize();
        self.hash.copy_from_slice(hash.as_bytes());
        *hash.as_bytes()
    }

    pub fn insert(&mut self, nib: &u8, hash: &Hash, id: &NodeID) {
        if let Some(child) = &mut self.children[*nib as usize] {
            child.0.copy_from_slice(hash);
            child.1.copy_from_slice(id);
        } else {
            self.children[*nib as usize] = Some((hash.clone(), id.clone()));
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
        val_hash: &Hash, 
    ) -> Option<Hash> {
        if let Some((_, next_id)) = self.get_next(&nibbles[0]) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some(new_hash) = next_node.put(ledger, &nibbles[1..], key, val_hash) {
                    self.insert(&nibbles[0], &new_hash, &next_node.get_id());
                }
            }
        } else {
            let child_id = (u64::from_le_bytes(self.id) * 16) + nibbles[0] as u64;

            if nibbles.len() > 1 {
                // extension adopts child id
                let e = ledger.new_cached_ext(child_id);
                let mut ext = e.borrow_mut();
                ext.set_path(&nibbles[1..]);

                // leaf is grandchild
                let l = ledger.new_cached_leaf(child_id * 16);
                let mut leaf = l.borrow_mut();
                leaf.set_value_hash(val_hash);

                // set leaf as child
                ext.set_child(&leaf.derive_hash(key), &leaf.get_id());

                // insert extension
                self.insert(&nibbles[0], &ext.derive_hash(), ext.get_id());
            } else {

                // set leaf as direct child
                let l = ledger.new_cached_leaf(child_id);
                let mut leaf = l.borrow_mut();
                leaf.set_value_hash(val_hash);

                self.insert(&nibbles[0], &leaf.derive_hash(key), leaf.get_id());
            }
        }

        // derive new self
        return Some(self.derive_hash());
    }

    pub fn get_last_child(&self) -> (u8, &Option<(Hash, NodeID)>) {
        for (i, c) in self.children.iter().enumerate() {
            if c.is_some() {
                return (i as u8, c)
            }
        }
        return (20, &None);
    }

    pub fn remove( 
        &mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8]
    ) -> Option<(Hash, Option<Vec<u8>>)> {

        if let Some((_, next_id)) = self.get_next(&nibbles[0]) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some((hash, _)) = next_node.remove(ledger, &nibbles[1..]) {

                    if hash == Self::ZERO_HASH {
                        self.delete_child(&nibbles[0]);
                    } else {
                        self.insert(&nibbles[0], &hash, &next_node.get_id());
                    }

                    let mut hash_to_parent = Self::ZERO_HASH;

                    if self.count == 1 {
                        let parent_id = u64::from_le_bytes(*self.get_id()) / 16;
                        let parent_node = ledger.load_node(&parent_id.to_le_bytes());
                        if parent_node.is_none() {
                            return Some((self.derive_hash(), None));
                        }
                        let parent_node = parent_node.unwrap();

                        let (nib, child) = self.get_last_child();
                        let child_node = ledger.load_node(&child.unwrap().1).unwrap();
                        let mut path_to_parent = Vec::new();

                        if let Node::Extension(_) = parent_node {
                            ledger.delete_node(self.get_id());
                            // extend parent by last child nibble
                            path_to_parent.push(nib);

                            if let Node::Extension(c) = child_node {
                                let mut child_ext = c.borrow_mut();
                                ledger.delete_node(child_ext.get_id());

                                // add ext path to parent 
                                let _ = child_ext.cut_remaining(0).into_iter()
                                    .map(|nib| path_to_parent.push(nib));

                                // load in grnd_child
                                let (grnd_child_hash, grnd_child_id) = child_ext.get_child();
                                let grnd_child = ledger.load_node(grnd_child_id).unwrap();

                                // change id of grndchild to match self
                                ledger.delete_node(grnd_child_id);
                                grnd_child.change_id(self.get_id(), ledger);
                                ledger.cache_node(u64::from_le_bytes(grnd_child.get_id()), grnd_child);

                                // connect parent to grnd_child
                                hash_to_parent = *grnd_child_hash;

                            } else {
                                ledger.delete_node(&child_node.get_id());

                                // change id of child to match self
                                child_node.change_id(self.get_id(), ledger);

                                // connect parent to child
                                hash_to_parent = child_node.derive_hash();

                                ledger.cache_node(u64::from_le_bytes(child_node.get_id()), child_node);
                            }

                            return Some((hash_to_parent, Some(path_to_parent)));

                        } else if let Node::Extension(c) = child_node {
                            ledger.delete_node(self.get_id());
                            {
                                let mut child_ext = c.borrow_mut();
                                child_ext.prepend_path(nib);

                                // change id of child to match self
                                ledger.delete_node(child_ext.get_id());
                                child_ext.change_id(self.get_id(), ledger);

                                // connect parent to child
                                hash_to_parent = child_ext.derive_hash();
                            }

                            let child_id = u64::from_le_bytes(*c.borrow().get_id());
                            ledger.cache_node(child_id, Node::Extension(c));

                            return Some((hash_to_parent, None));
                        }
                    } 

                    if self.count > 0 {
                        hash_to_parent = self.derive_hash();
                    } else {
                        ledger.delete_node(self.get_id());
                    }

                    return Some((hash_to_parent, None));

                } else {
                    println!("ERROR::BRANCH::REMOVE::RECV_NONE,  ID: {:10},  KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
                }
            } else {
                println!("ERROR::BRANCH::REMOVE::FAIL_LOAD,  ID: {:10},  KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
            }
        } else {
            println!("ERROR::BRANCH::REMOVE::NO_ID,                       KEY: {}", hex::encode(nibbles));
            println!("count {}", self.count);
        }
        None
    }
}

