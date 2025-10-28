use std::{cell::RefCell, rc::Rc, usize};
use crate::core::ledger::{node::{Hash, IsNode, NodeID, BRANCH}, Ledger};


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
        for (i, child_o) in self.children.iter().enumerate() {
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

    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Hash> {
        // TODO -- if parent is extension and self.count == 1... compact parent further
        // ALSO if self.count == 1 and that child is an extension grow that extension

        if let Some((_, next_id)) = self.get_next(&nibbles[0]) {
            if let Some(mut next_node) = ledger.load_node(next_id) {
                if let Some(hash) = next_node.remove(ledger, &nibbles[1..]) {

                    if hash == Self::ZERO_HASH {
                        self.delete_child(&nibbles[0]);
                    } else {
                        self.insert(&nibbles[0], &hash, &next_node.get_id());
                    }
                    
                    let mut new_hash = Self::ZERO_HASH;

                    if self.count > 0 {
                        new_hash = self.derive_hash();
                    } else {
                        ledger.delete_node(self.get_id());
                    }

                    return Some(new_hash);
                } else {
                    println!("ERROR::BRANCH::REMOVE::RECV_NONE,  ID: {:10},  KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
                }
            } else {
                println!("ERROR::BRANCH::REMOVE::FAIL_LOAD,  ID: {:10},  KEY: {}", u64::from_le_bytes(*next_id), hex::encode(nibbles));
            }
        } else {
            println!("ERROR::BRANCH::REMOVE::NO_ID, KEY: {}", hex::encode(nibbles));
        }
        None
    }
}

