use std::{collections::HashSet, fs::{self, File}, io, os::unix::fs::MetadataExt, usize};
use memmap2::{MmapMut, MmapOptions};
use zeroize::Zeroize;
use crate::core::{ledger::{header::*, pages::*},  utils::Hash};

mod header;
mod pages;

pub struct Ledger {
    file: File,
    mmap: MmapMut,
    free_pages: Vec<u64>,
    page_buffer: Option<Vec<u8>>,
}

const FSM_PAGE: u64 = 2;
const ROOT_ID: usize = 3;

impl Ledger {
    fn open(path: String) -> io::Result<Self> {
        let file = fs::OpenOptions::new()
                .create(true).write(true)
                .read(true).open(&path)?;

        let mut s = Self { 
            page_buffer: Some(Vec::with_capacity(PAGE_SIZE)),
            free_pages: Vec::new(), 
            mmap: unsafe { MmapOptions::new().map_mut(&file)? },
            file, 
        };

        let body = get_body(get_page_mut(&mut s.mmap, FSM_PAGE));
        let mut i = 0;
        while i < body.len() {
            let mut id_buff = [0u8; 8];
            id_buff.copy_from_slice(&body[i..i+8]);
            s.free_pages.push(u64::from_le_bytes(id_buff));
            i += 8;
        }
        Ok(s)
    }

    pub fn get_page_mut( &mut self, page_id: u64) -> &mut [u8] {
        get_page_mut(&mut self.mmap, page_id)
    }

    pub fn get_new_page(&mut self) -> u64 {
        let mut new_page = self.free_pages.pop();
        if new_page.is_none() {
            let size = self.file.metadata().unwrap().size();
            let page_size = PAGE_SIZE as u64;
            let _ = self.file.set_len(size + (10 * page_size));
            for i in 1..10 {
                self.free_pages.push(size+(i * page_size)/ page_size);
            }
            new_page = Some(size / page_size);
        }
        new_page.unwrap()
    }

    pub fn exists(&mut self, target: &mut Hash) -> bool {
        if let Ok((_, (_, is_dirty))) = bin_search(self, target) {
            !is_dirty
        } else {
            false
        }
    }

    pub fn insert(&mut self, target: &mut Hash, altered: &mut HashSet<u64>) {
        let entry = new_entry(target);
        while let Err((page_id, (offset, is_dirty))) = bin_search(self, target) {

            altered.insert(page_id);

            let page = self.get_page_mut(page_id);

            if is_dirty {
                page[offset..offset+LEAF_ENTRY_LEN].copy_from_slice(&entry);
                break;

            } else {
                let occupied = get_occupied_count(page) as usize;
                if  occupied < LEAF_ENTRY_LEN {
                    shift_and_insert(page, &entry, offset, LEAF_ENTRY_COUNT, LEAF_ENTRY_LEN);
                    break;

                } else {
                    split_and_insert(self, page_id, LEAF_ENTRY_LEN, &entry, altered);
                }
            }
        }
    }

    pub fn remove(&mut self, target: &mut Hash, altered: &mut HashSet<u64>) {
        if let Ok((page_id, (offset, is_dirty))) = bin_search(self, target) {
            if !is_dirty {
                let leaf = self.get_page_mut(page_id);
                mark_dirty(&mut leaf[offset..LEAF_ENTRY_LEN]);
                decrement_occupied(leaf, 1);

                if get_occupied_count(leaf) == 0 {
                    let mut parent = get_parent(leaf);
                    leaf.zeroize();
                    self.free_pages.push(page_id);

                    // Remove from parent
                    loop {
                        let node = get_page_mut(&mut self.mmap, parent);
                        if get_occupied_count(node) == 1 {
                            // if last child remove parent from grandparent
                            let next_parent = get_parent(node);
                            node.zeroize();
                            self.free_pages.push(parent);
                            parent = next_parent;
                        } else {

                            altered.insert(parent);
                            node.copy_within(offset+NODE_ENTRY_LEN.., offset);
                            decrement_occupied(node, 1);
                            break;
                        }
                    }
                } else {
                    altered.insert(page_id);
                }
            }
        }
    }
}
