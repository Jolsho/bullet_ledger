// SPDX-License-Identifier: GPL-2.0-only

use std::{cell::RefCell, rc::Rc };
use super::{ Hash, branch::BranchNode, ext::ExtNode, leaf::Leaf, Ledger};

#[derive(Clone)]
pub enum Node {
    Branch(NodePointer<BranchNode>),
    Extension(NodePointer<ExtNode>),
    Leaf(NodePointer<Leaf>),
}

pub type NodePointer<T> = Rc<RefCell<T>>;
pub type NodeID = [u8;8];

pub const BRANCH:u8 = 69;
pub const EXT:u8 = 70;
pub const LEAF:u8 = 71;

impl Node {
    pub fn from_bytes(id: NodeID, bytes: &[u8]) -> Result<Self,()> {
        Ok(match bytes[0] as u8 {
            BRANCH => Node::Branch(BranchNode::from_bytes(id, &bytes[1..])),
            LEAF => Node::Leaf(Leaf::from_bytes(id, &bytes[1..])),
            EXT => Node::Extension(ExtNode::from_bytes(id, &bytes[1..])),
            _ => return Err(()),
        })
    }
    pub fn get_id(&self) -> NodeID { 
        match &self {
            Node::Branch(b) => b.borrow().get_id().clone(),
            Node::Extension(e) => e.borrow().get_id().clone(),
            Node::Leaf(l) => l.borrow().get_id().clone(),
        }
    }

    pub fn change_id(&self, id: &NodeID, ledger: &mut Ledger) { 
        match &self {
            Node::Branch(b) => b.borrow_mut().change_id(id, ledger),
            Node::Extension(e) => e.borrow_mut().change_id(id, ledger),
            Node::Leaf(l) => l.borrow_mut().set_id(id),
        };
    }

    pub fn get_hash(&self) -> Hash { 
        match &self {
            Node::Branch(b) => b.borrow().get_hash().clone(),
            Node::Extension(e) => e.borrow().get_hash().clone(),
            Node::Leaf(l) => l.borrow().get_hash().clone(),
        }
    }

    pub fn derive_hash(&self) -> Hash { 
        match &self {
            Node::Branch(b) => b.borrow_mut().derive_hash(),
            Node::Extension(e) => e.borrow_mut().derive_hash(),
            Node::Leaf(l) => l.borrow().get_hash().clone(),
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        match &self {
            Node::Branch(b) => b.borrow().to_bytes(),
            Node::Extension(e) => e.borrow().to_bytes(),
            Node::Leaf(l) => l.borrow().to_bytes(),
        }
    }

    pub fn search(&self, ledger: &mut Ledger, key: &[u8]) -> Option<Hash> {
        return match &self {
            Node::Branch(b) => b.borrow_mut().search(ledger, key),
            Node::Extension(e) => e.borrow_mut().search(ledger, key),
            Node::Leaf(l) => l.borrow_mut().search(key),
        }
    }

    pub fn put(&mut self, 
        ledger: &mut Ledger, 
        nibbles: &[u8], 
        key: &[u8; 32], 
        val_hash: &Hash,
        is_virtual: bool,
    ) -> Option<Hash> {
        if !is_virtual {
            match self {
                Node::Branch(b) => b.borrow_mut().put(ledger, nibbles, key, val_hash),
                Node::Extension(e) => e.borrow_mut().put(ledger, nibbles, key, val_hash),
                Node::Leaf(l) => l.borrow_mut().put(ledger, nibbles, key, val_hash),
            }
        } else {
            match self {
                Node::Branch(b) => b.borrow_mut().virtual_put(ledger, nibbles, key, val_hash),
                Node::Extension(e) => e.borrow_mut().virtual_put(ledger, nibbles, key, val_hash),
                Node::Leaf(l) => l.borrow_mut().virtual_put(nibbles, key, val_hash),
            }
        }
    }


    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<(Hash,Option<Vec<u8>>)> {
        match self {
            Node::Branch(b) => b.borrow_mut().remove(ledger, nibbles),
            Node::Extension(e) => e.borrow_mut().remove(ledger, nibbles),
            Node::Leaf(l) => l.borrow_mut().remove(ledger)
        }
    }
}
