use std::{collections::HashMap, rc::Rc};
use crate::core::utils::Hash;

pub struct Node { 
    children: HashMap<u8, Rc<Hash>>,
}

impl Node {
    pub fn new() -> Self {
        Self { 
            children: HashMap::with_capacity(16),
        }
    }

    pub fn get_hash(&self, nib: u8) -> Option<Rc<Hash>> {
        if let Some(hash) = self.children.get(&nib) {
            return Some(hash.clone());
        }
        None
    }

    pub fn from_bytes(&mut self, bytes: &[u8]) -> Result<(),()> {
        Ok(())
    }
}
