/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

use std::collections::{BinaryHeap, HashMap};
use std::hash::Hash;
use super::Hash as LocalHash;
use super::trxs::Trx;

pub type TrxPool = PriorityPool<Box<LocalHash>, Trx>;

pub trait DeriveStuff<K>
where
    K: Clone + Eq + Hash + Ord + Default,
{
    fn fill_key(&self, key: &mut K);
    fn get_comperator(&self) -> u64;
    fn get_value_id(&self) -> &u8;
    fn new(id: u8) -> Self;
}

pub struct PriorityPool<K, V> {
    map:        HashMap<K, (V, u64)>, // value + priority
    heap:       BinaryHeap<(u64, K)>,
    capacity:   usize,
    values:     HashMap<u8, Vec<V>>,
    keys:       Vec<K>,
}

impl<K, V> PriorityPool<K, V>
where
    K: Clone + Eq + Hash + Ord + Default,
    V: DeriveStuff<K>,
{
    pub fn new(capacity: usize) -> Self {
        Self { 
            map: HashMap::with_capacity(capacity), 
            heap: BinaryHeap::with_capacity(capacity),
            values: HashMap::with_capacity(capacity),
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

    pub fn get(&mut self, key: &K) -> Option<&mut (V, u64)> {
        self.map.get_mut(key)
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

    pub fn remove_one(&mut self, key: &K) {
        if let Some((k, (v, _))) = self.map.remove_entry(&key) {
            self.recycle_value(v);
            self.recycle_key(k);
        }
    }

    ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////

    pub fn get_value(&mut self, id: u8) -> Option<V> {
        let vec = match self.values.get_mut(&id) {
            Some(vec) => vec,
            None => {
                self.values.insert(id, Vec::with_capacity(100));
                self.values.get_mut(&id).unwrap()
            }
        };
        let mut v = vec.pop();
        if v.is_none() {
            v = Some(V::new(id));
        }
        v
    }

    pub fn recycle_value(&mut self, v: V) {
        if self.values.len() < self.values.capacity() {
            match self.values.get_mut(v.get_value_id()) {
                Some(vec) => vec.push(v),
                None => {
                    let mut vec = Vec::with_capacity(100);
                    let id = v.get_value_id().clone();
                    vec.push(v);
                    self.values.insert(id, vec);
                }
            }
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
