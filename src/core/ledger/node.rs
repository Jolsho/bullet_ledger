use std::{cell::RefCell, rc::Rc };
use crate::core::{ledger::{branch::BranchNode, ext::ExtNode, leaf::Leaf, Ledger}};

#[derive(Clone)]
pub enum Node {
    Branch(NodePointer<BranchNode>),
    Extension(NodePointer<ExtNode>),
    Leaf(NodePointer<Leaf>),
}

pub type NodePointer<T> = Rc<RefCell<T>>;
pub type Hash = [u8;32];

pub const BRANCH:u8 = 69;
pub const EXT:u8 = 70;
pub const LEAF:u8 = 71;

impl Node {

    pub fn from_bytes(bytes: &[u8]) -> Result<Self,()> {
        Ok(match bytes[0] as u8 {
            BRANCH => Node::Branch(BranchNode::from_bytes(&bytes[1..])),
            LEAF => Node::Leaf(Leaf::from_bytes(&bytes[1..])),
            EXT => Node::Extension(ExtNode::from_bytes(&bytes[1..])),
            _ => return Err(()),
        })
    }

    pub fn search(&self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Vec<u8>> {
        match &self {
            Node::Branch(b) => {
                let branch = b.borrow_mut();
                if let Some(next_hash) = branch.get_next(&nibbles[0]) {
                    if let Some(next_node) = ledger.load_node(next_hash) {
                        return next_node.search(ledger, &nibbles[1..]);
                    }
                }
            }
            Node::Extension(e) => {
                let ext = e.borrow_mut();
                if let Some(next_hash) = ext.get_next(&nibbles) {
                    if let Some(next_node) = ledger.load_node(next_hash) {
                        return next_node.search(ledger, &nibbles[ext.path_len()..]);
                    }
                }
            }
            Node::Leaf(leaf) => return Some(leaf.borrow().get_value())
        }
        return None;
    }

    pub fn put(&mut self, ledger: &mut Ledger, nibbles: &[u8], 
        key: &[u8], val: Vec<u8>,
    ) -> Option<Hash> {
        match self {
            Node::Branch(b) => b.borrow_mut().put(ledger, nibbles, key, val),
            Node::Extension(e) => e.borrow_mut().put(ledger, nibbles, key, val),
            Node::Leaf(_) => None,
        }
    }

    pub fn remove(&mut self, ledger: &mut Ledger, nibbles: &[u8]) -> Option<Hash> {
        match self {
            Node::Branch(b) => b.borrow_mut().remove(ledger, nibbles),
            Node::Extension(e) => e.borrow_mut().remove(ledger, nibbles),
            Node::Leaf(l) => l.borrow_mut().remove(ledger)
        }
    }
}
