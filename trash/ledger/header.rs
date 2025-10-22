use crate::core::{ledger::{pages::LEAF_ENTRY_LEN, PAGE_SIZE}, utils::Hash};
use super::LEAF_ENTRY_COUNT;

pub const PAGE_HEADER_LEN: usize = 96;
pub const PAGE_ID_LEN: usize = 8;

pub const PAGE_ID_O: usize = 0;
const IS_LEAF_O: usize = 8;
const PARENT_O: usize = 9;
const BODY_RANGE_O: usize = 17;
const HASH_O: usize = 25;
const TOTAL_O: usize = 57;
const MAP_O: usize = 59;

pub fn fill_page_header(
    page: &mut[u8], 
    parent: u64,
    page_id: u64,
    is_leaf: bool,
    count: u16,
) {
    set_parent(page, parent);
    set_page_id(page, page_id);
    set_is_leaf(page, is_leaf);
    set_occupied_count(page, count);

    let bitmap = get_map(page);
    for i in 0..count as usize {
        set_bit(bitmap, i, true);
    }
}

pub fn get_page_offset(buff: &mut [u8]) -> usize {
    let id = [0u8; PAGE_ID_LEN];
    buff[PAGE_ID_O..PAGE_ID_O+PAGE_ID_LEN].copy_from_slice(&id);
    PAGE_SIZE * usize::from_le_bytes(id)
}
pub fn set_page_id(buff: &mut [u8], page_id: u64) {
    buff[PAGE_ID_O..PAGE_ID_O+PAGE_ID_LEN].copy_from_slice(&page_id.to_le_bytes());
}

pub fn is_leaf(buff: &mut [u8]) -> bool {
    let mut is_leaf = [0u8; 1];
    is_leaf.copy_from_slice(&buff[IS_LEAF_O..IS_LEAF_O + 1]);
    is_leaf[0] as u8 == 1
}
pub fn set_is_leaf(buff: &mut [u8], is: bool) {
    let mut is_leaf = [0u8; 1];
    if is { is_leaf[0] = 1; }
    is_leaf.copy_from_slice(&buff[IS_LEAF_O..IS_LEAF_O + 1]);
}

pub fn get_body<'a>(buff: &'a mut [u8]) -> &'a mut [u8] {
    let end_buf = [0u8;8];
    buff[BODY_RANGE_O..BODY_RANGE_O+8].copy_from_slice(&end_buf);
    &mut buff[PAGE_HEADER_LEN..usize::from_le_bytes(end_buf)]
}

pub fn increment_body_range(page: &mut[u8]) {
    let mut end_buf = [0u8;8];
    end_buf.copy_from_slice(&page[BODY_RANGE_O..BODY_RANGE_O+8]);
    let mut end = usize::from_le_bytes(end_buf);
    end += LEAF_ENTRY_LEN;
    page[BODY_RANGE_O..BODY_RANGE_O+8].copy_from_slice(&end.to_le_bytes());
}


pub fn get_parent(buff: &mut [u8]) -> u64 {
    let mut parent_bytes = [0u8; PAGE_ID_LEN];
    parent_bytes.copy_from_slice(&buff[PARENT_O..PARENT_O + PAGE_ID_LEN]);
    u64::from_le_bytes(parent_bytes)
}

pub fn set_parent(buff: &mut [u8], parent: u64) {
    buff[PARENT_O..PARENT_O + PAGE_ID_LEN].copy_from_slice(&parent.to_le_bytes());
}

pub fn get_hash(buff: &mut [u8]) -> Hash {
    let mut hash = [0u8; 32];
    hash.copy_from_slice(&buff[HASH_O..HASH_O + 32]);
    Hash(hash)
}

pub fn get_occupied_count(buff: &mut [u8]) -> u16 {
    let mut count = [0u8; 2];
    count.copy_from_slice(&buff[TOTAL_O..TOTAL_O + 2]);
    u16::from_le_bytes(count)
}

pub fn set_occupied_count(buff: &mut [u8], total: u16) {
    buff[TOTAL_O..TOTAL_O + 2].copy_from_slice(&total.to_le_bytes());
}

pub fn increment_occupied(page: &mut[u8], by: u16) {
    let mut count = get_occupied_count(page);
    count = count.saturating_add(by);
    set_occupied_count(page, count);
}

pub fn decrement_occupied(page: &mut[u8], by: u16) {
    let mut count = get_occupied_count(page);
    count = count.saturating_sub(by);
    set_occupied_count(page, count);
}

/// Sets the bit at `index` in `bitmap` to `value` (true = 1, false = 0)
pub fn set_bit(bitmap: &mut [u8], index: usize, value: bool) {
    let byte_index = index / 8;
    let bit_index = index % 8;

    if byte_index >= bitmap.len() {
        panic!("Bit index {} out of range (bitmap len = {})", index, bitmap.len() * 8);
    }

    if value {
        bitmap[byte_index] |= 1 << bit_index;  // set bit
    } else {
        bitmap[byte_index] &= !(1 << bit_index); // clear bit
    }
}

/// Returns the bit value at `index` as a bool
pub fn get_bit(bitmap: &[u8], index: usize) -> bool {
    let byte_index = index / 8;
    let bit_index = index % 8;

    if byte_index >= bitmap.len() {
        panic!("Bit index {} out of range (bitmap len = {})", index, bitmap.len() * 8);
    }

    (bitmap[byte_index] >> bit_index) & 1 == 1
}

pub fn get_map(page: &mut[u8],) -> &mut [u8] {
    let byte_count = (LEAF_ENTRY_COUNT + 7) / 8;
    &mut page[MAP_O..MAP_O + byte_count]
}

pub fn set_map(buff: &mut [u8], map: &[u8]) {
    let byte_count = (LEAF_ENTRY_COUNT + 7) / 8;
    buff[MAP_O..MAP_O + byte_count].copy_from_slice(map);
}

/// Find contiguous clean (occupied) intervals in *record indexes*
/// Returns Vec<(start_index, end_index)> where end is exclusive.
pub fn derive_clean_intervals(map: &mut [u8]) -> Vec<(usize, usize)> {
    let mut clean_intervals = Vec::new();
    let mut start: Option<usize> = None;

    for i in 0..map.len() * 8 {
        if get_bit(map, i) {
            if start.is_none() {
                start = Some(i);
            }
        } else if let Some(s) = start.take() {
            clean_intervals.push((s, i)); // [s, i)
        }
    }

    if let Some(s) = start {
        clean_intervals.push((s, map.len())); // trailing region
    }
    clean_intervals
}

