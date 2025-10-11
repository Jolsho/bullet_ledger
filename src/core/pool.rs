use std::{cell::RefCell, collections::HashMap, ops::Deref, rc::Rc};
use ringbuf::{traits::{Consumer, Producer}, HeapRb};
use crate::{msging::{Ring, RingProd}, trxs::Trx};

#[derive(PartialEq, Eq, Hash, Clone, Copy)]
pub struct Hash(pub [u8;32]);
impl Hash {
    pub const ZERO: Hash = Hash([0u8; 32]);
    pub fn copy_from(&mut self, buff: &[u8;32]) {
        self.0.copy_from_slice(buff);
    }
}


////////////////////////////////////////////////////////


#[derive(Clone)]
pub struct PoolEntry {
    pub trx: Option<Box<Trx>>,
    next: Option<PoolEntryPointer>,
    prev: Option<PoolEntryPointer>,
}
impl PoolEntry {
    pub fn default() -> Self {
        PoolEntry { trx: None, next: None, prev: None }
    }
    pub fn get_fee_value(&self) -> u64 { 
        match &self.trx {
            Some(trx) => trx.fee_value,
            _ => 0
        }
    }
}


////////////////////////////////////////////////////////


#[derive(Clone)]
pub struct PoolEntryPointer(Rc<RefCell<PoolEntry>>);
impl Deref for PoolEntryPointer {
    type Target = Rc<RefCell<PoolEntry>>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
impl PoolEntryPointer {
    pub fn default() -> Self { Self(Rc::new(RefCell::new(PoolEntry::default()))) }
    pub fn set_next(&mut self, next: Option<PoolEntryPointer>) { self.borrow_mut().next = next; }
    pub fn set_prev(&mut self, prev: Option<PoolEntryPointer>) { self.borrow_mut().prev = prev; }

    pub fn insert_into_list(&mut self, prev: Option<PoolEntryPointer>, pool: &mut TrxPool) {
        match prev {
            Some(mut p_ref) => {
                let prev_next = p_ref.borrow().next.clone();
                if let Some(n) = &prev_next {
                    n.borrow_mut().prev = Some(self.clone());
                }
                self.set_next(prev_next);
                self.set_prev(Some(p_ref.clone()));
                p_ref.set_next(Some(self.clone()));
            }
            None => {
                if let Some(head) = &pool.head {
                    head.borrow_mut().prev = Some(self.clone());
                }
                self.set_next(pool.head.clone());
                pool.head = Some(self.clone());
            }
        }
    }

    pub fn remove_from_list(&mut self, pool: &mut TrxPool) {
        let mut next = self.borrow_mut().next.take();
        let mut prev = self.borrow_mut().prev.take();

        if let Some(prev) = &mut prev {
            prev.set_next(next.clone());
        } else {
            pool.head = next.clone();
        }

        if let Some(next) = &mut next {
            next.set_prev(prev.clone());
        } else {
            pool.tail = prev.clone();
        }
    }
}


////////////////////////////////////////////////////////


pub struct TrxPool {
    trxs:   RingProd<Trx>,
    keys:   Ring<Hash>,
    entries:HeapRb<PoolEntryPointer>,

    pending:HashMap<Box<Hash>, PoolEntryPointer>,
    head:   Option<PoolEntryPointer>,
    tail:   Option<PoolEntryPointer>,

}
impl TrxPool {
    pub fn new(capacity: usize, trxs: RingProd<Trx>) -> Self  {
        let mut s = Self { 
            trxs,
            keys: HeapRb::new(capacity),
            entries: HeapRb::new(capacity),

            pending: HashMap::with_capacity(capacity),
            head: None, tail: None,
        };

        for _ in 0..capacity {
            s.put_entry(PoolEntryPointer::default());
            let _ = s.keys.try_push(Box::new(Hash::ZERO));
        };
        s
    }

    pub fn length(&self) -> usize { self.pending.len() }

    #[cfg(test)]
    pub fn get_head(&self) -> Option<Box<Trx>> {
        let p = self.head.clone().unwrap();
        p.borrow_mut().trx.clone()
    }

    fn get_entry(&mut self, trx: Option<Box<Trx>>) -> PoolEntryPointer { 
        let mut e = self.entries.try_pop();
        if e.is_none() {
            e = Some(PoolEntryPointer::default());
        }
        let e = e.unwrap();
        e.borrow_mut().trx = trx;
        e
    }

    fn put_entry(&mut self, entry: PoolEntryPointer) { 
        if let Some(trx) = entry.borrow_mut().trx.take() {
            if let Err(t) = self.trxs.try_push(trx) {
                drop(t);
            }
        }
        if let Err(e) = self.entries.try_push(entry) {
            drop(e);
        }
    }

    fn get_key(&mut self) -> Box<Hash> {
        let mut key = self.keys.try_pop();
        if key.is_none() {
            key = Some(Box::new(Hash::ZERO));
        }
        key.unwrap()
    }

    pub fn remove_entry(&mut self, key: Box<Hash>) {
        if let Some((k, mut v)) = self.pending.remove_entry(&key) {
            v.remove_from_list(self);
            self.put_entry(v);
            let _ = self.keys.try_push(k);
        }
        let _ = self.keys.try_push(key);
    }

    pub fn remove_batch(&mut self, keys: &Vec<Hash>) {
        for key in keys.iter() {
            if let Some((k, mut v)) = self.pending.remove_entry(key) {
                v.remove_from_list(self);
                self.put_entry(v);
                let _ = self.keys.try_push(k);
            }
        }
    }

    pub fn insert(&mut self, trx: Box<Trx>) {
        // Get a key for the pending map.
        // make sure it doesnt already exist.
        let mut key = self.get_key();
        key.copy_from(&trx.hash);
        if self.pending.contains_key(&key) { 
            if let Err(t) = self.trxs.try_push(trx) {
                drop(t);
            }
            let _ = self.keys.try_push(key);
            return; 
        }

        // Build Pool Entry Pointer
        let mut entry = self.get_entry(Some(trx));

        // Linear search to find insertion point based on fee
        let mut curr = self.head.clone();
        let mut prev: Option<PoolEntryPointer> = None;
        while let Some(node) = curr.clone() {
            let node_fee = node.borrow().get_fee_value();
            let entry_fee = entry.borrow().get_fee_value();

            if node_fee >= entry_fee {
                prev = curr.clone();
                curr = node.borrow().next.clone();
            } else {
                break;
            }
        }

        // Pop last if at capacity
        if self.length() == self.pending.capacity() {
            if let Some(tail) = self.tail.take() {

                let mut key = self.get_key();
                if let Some(trx) = &tail.borrow_mut().trx {
                    key.copy_from(&trx.hash);
                }
                self.remove_entry(key);
            }
        }

        // Insert into hashmap and list
        entry.insert_into_list(prev, self);
        self.pending.insert(key, entry);
    }
}
