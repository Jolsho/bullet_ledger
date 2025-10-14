use std::collections::{BinaryHeap, HashMap};
use std::hash::Hash;
use std::usize;

pub trait DeriveStuff<K>
where
    K: Clone + Eq + Hash + Ord + Default,
{
    fn fill_key(&self, key: &mut K);
    fn get_comperator(&self) -> u64;
}

pub struct PriorityPool<K, V> {
    map: HashMap<K, (V, u64)>, // value + priority
    heap: BinaryHeap<(u64, K)>,
    capacity: usize,

    values:   Vec<V>,
    keys:   Vec<K>,
}

impl<K, V> PriorityPool<K, V>
where
    K: Clone + Eq + Hash + Ord + Default,
    V: Default + DeriveStuff<K>,
{
    pub fn new(capacity: usize) -> Self {
        Self { 
            map: HashMap::with_capacity(capacity), 
            heap: BinaryHeap::with_capacity(capacity),
            values: Vec::with_capacity(capacity),
            keys: Vec::with_capacity(capacity),
            capacity,
        }
    }

    pub fn len(&self) -> usize { self.map.len() }
    pub fn contains(&self, key: &K) -> bool { self.map.contains_key(key) }

    pub fn peek(&mut self) -> Option<&(u64, K)> {
        self.clean();
        self.heap.peek()
    }

    pub fn pop(&mut self) -> Option<V> {
        while self.heap.len() > 0 {
            if let Some((_, k1)) = self.heap.pop() {
                if let Some((k2, (v, _))) = self.map.remove_entry(&k1) {
                    self.recycle_key(k2);
                    return Some(v);

                } else {
                    self.recycle_key(k1);
                    continue;
                }
            }
        }
        self.clean();
        return None
    }

    pub fn clean(&mut self) {
        while self.heap.len() > 0 {
            if let Some((_p, k)) = self.heap.peek() {
                if !self.contains(&k) {
                    let (_p, k) = self.heap.pop().unwrap();
                    self.recycle_key(k);
                } else {
                    break
                }
            }
        }
    }

    pub fn insert(&mut self, val: V) {
        let mut key1 = self.get_key();
        val.fill_key(&mut key1);

        let mut key2 = self.get_key();
        val.fill_key(&mut key2);

        let priority = val.get_comperator();

        if let Some((v,_)) = self.map.insert(key1, (val, priority)) {
            self.recycle_key(key2);
            self.recycle_value(v);
            return;
        }
        self.heap.push((priority, key2));

        // Evict lowest priority if over capacity
        while self.map.len() > self.capacity {
            if let Some((_, k)) = self.heap.pop() {
                if let Some((v,_)) = self.map.remove(&k) {
                    self.recycle_value(v);
                }
                self.recycle_key(k);
            }
        }
    }

    pub fn remove_batch(&mut self, mut keys: Vec<K>) {
        while keys.len() > 0 {
            self.remove_one(keys.pop().unwrap());
        }
        self.clean();
    }

    pub fn remove_one(&mut self, key: K) {
        if let Some((k, (v, _))) = self.map.remove_entry(&key) {
            self.recycle_value(v);
            self.recycle_key(k);
            self.recycle_key(key);
        }
    }

    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////

    pub fn get_value(&mut self) -> V {
        let mut v = self.values.pop();
        if v.is_none() {
            v = Some(V::default());
        }
        v.unwrap()
    }

    pub fn recycle_value(&mut self, v: V) {
        if self.values.len() < self.values.capacity() {
            self.values.push(v)
        } else {
            drop(v);
        }
    }

    pub fn get_key(&mut self) -> K {
        let mut k = self.keys.pop();
        if k.is_none() {
            k = Some(K::default());
        }
        k.unwrap()
    }

    pub fn recycle_key(&mut self, key: K) {
        if self.keys.len() < self.keys.capacity() {
            self.keys.push(key)
        }
    }
}
