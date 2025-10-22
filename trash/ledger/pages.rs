use std::cmp::Ordering;
use super::*;

/////////////////////////////////////////////////////////////
///////     ENTRIES      ///////////////////////////////////
///////////////////////////////////////////////////////////

pub const KEY_LEN: usize = 32;
pub const NODE_ENTRY_LEN: usize = KEY_LEN + PAGE_ID_LEN;
pub const LEAF_ENTRY_LEN: usize = KEY_LEN + ENTRY_META_LEN;

const ENTRY_META_LEN: usize = 1;

pub const NODE_ENTRY_COUNT: usize = (PAGE_SIZE as usize - PAGE_HEADER_LEN) / NODE_ENTRY_LEN;
pub const LEAF_ENTRY_COUNT: usize = (PAGE_SIZE as usize - PAGE_HEADER_LEN) / LEAF_ENTRY_LEN;

pub fn is_dirty(entry: &[u8]) -> bool {
    if let Some(&last_byte) = entry.last() {
        // Check if the most significant bit (bit 7) is set
        last_byte & 0b1000_0000 != 0
    } else {
        false
    }
}

pub fn mark_dirty(entry: &mut [u8]) {
    if let Some(last_byte) = entry.last_mut() {
        // Flip the most significant bit (bit 7)
        *last_byte ^= 0b1000_0000;
    }
}

pub fn new_entry(key: &mut Hash) -> [u8; LEAF_ENTRY_LEN] {
    let mut entry = [0u8; LEAF_ENTRY_LEN];
    entry.copy_from_slice(&key.0);
    entry
}


/////////////////////////////////////////////////////////////
///////     PAGES      /////////////////////////////////////
///////////////////////////////////////////////////////////

pub const PAGE_SIZE: usize = 4096;

pub fn get_page_mut(
    mmap: &mut MmapMut, 
    page_id: u64
) -> &mut [u8] {

    let start = page_id as usize * PAGE_SIZE;
    let end = start + PAGE_SIZE;
    &mut mmap[start..end]
}

///////     SEARCH      /////////////////////////////////////

pub fn bin_search(
    ledger: &mut Ledger, 
    target: &Hash
) -> Result<(u64, (usize, bool)), (u64, (usize, bool))> {

    let mut page_id = ROOT_ID as u64; 
    loop {
        let page = ledger.get_page_mut(page_id);
        if is_leaf(page) {
            match bin_search_leaf(page, &target.0) {
                Ok(res) => return Ok((page_id, res)),
                Err(res) => return Err((page_id, res)),
            }
        } else {
            (page_id, _) = bin_search_node(page, &target.0);
        }
    }
}

fn bin_search_leaf(
    page: &mut [u8],
    target: &[u8; KEY_LEN],
) -> Result<(usize, bool), (usize, bool)> {
    let body = get_body(page);
    let mut key_buff = [0u8; KEY_LEN];

    let mut left = 0;
    let mut right = LEAF_ENTRY_COUNT; // exclusive upper bound

    while left < right {
        let mid = (left + right) / 2;
        let offset = mid * LEAF_ENTRY_LEN;

        key_buff.copy_from_slice(&body[offset..offset + KEY_LEN]);

        match key_buff.cmp(target) {
            std::cmp::Ordering::Equal => {
                return Ok((
                    offset + PAGE_HEADER_LEN,
                    is_dirty(&mut body[offset..offset + LEAF_ENTRY_LEN]),
                ));
            }
            std::cmp::Ordering::Less => {
                left = mid + 1;
            }
            std::cmp::Ordering::Greater => {
                // No need to break — just adjust right safely
                if mid == 0 {
                    right = 0;
                } else {
                    right = mid;
                }
            }
        }
    }

    // At this point, `left` is the insertion position
    let offset = left * LEAF_ENTRY_LEN;
    Err((
        offset + PAGE_HEADER_LEN,
        is_dirty(&mut body[offset..offset + LEAF_ENTRY_LEN]),
    ))
}


/// Performs a binary search on an internal (non-leaf) node.
/// Returns the page ID associated with the found or closest key.
fn bin_search_node(
    page: &mut [u8],
    target: &[u8; KEY_LEN],
) -> (u64, usize) {

    let body = get_body(page);
    let mut left = 0;
    let mut right = NODE_ENTRY_COUNT;
    let mut result_offset = 0;
    let mut key_buff = [0u8; KEY_LEN];

    while left < right {
        let mid = (left + right) / 2;
        let entry_offset = mid * NODE_ENTRY_LEN;

        let key_slice = &body[entry_offset..entry_offset + KEY_LEN];
        key_buff.copy_from_slice(key_slice);

        match key_buff.cmp(target) {
            std::cmp::Ordering::Equal => {
                result_offset = entry_offset + KEY_LEN;
                break;
            }
            std::cmp::Ordering::Less => left = mid + 1,
            std::cmp::Ordering::Greater => {
                if mid == 0 {
                    break;
                }
                right = mid - 1;
            }
        }
    }

    // Read 4-byte page ID after the key
    let id_offset = result_offset + KEY_LEN;
    let mut id_bytes = [0u8; PAGE_ID_LEN];
    id_bytes.copy_from_slice(&body[id_offset..id_offset + PAGE_ID_LEN]);
    (u64::from_le_bytes(id_bytes), result_offset)
}


///////     INSERTION      /////////////////////////////////////

pub fn insert_node_child(
    ledger: &mut Ledger, 
    node_num: u64, 
    key: [u8; KEY_LEN],
    new_page_num: u64, 
    altered: &mut HashSet<u64>,
) {
    let page = ledger.get_page_mut(node_num);

    let mut e = [0u8; NODE_ENTRY_LEN];
    e[..KEY_LEN].copy_from_slice(&key);
    e[KEY_LEN..].copy_from_slice(&new_page_num.to_le_bytes());

    let count = get_occupied_count(page) as usize;
    if count < NODE_ENTRY_COUNT {

        let (_, offset) = bin_search_node(page, &key);
        shift_and_insert(page, &e, offset, NODE_ENTRY_COUNT, NODE_ENTRY_LEN);
    } else {
        split_and_insert(ledger, node_num, NODE_ENTRY_LEN, &e, altered);
    }
}

pub fn shift_and_insert(
    page: &mut [u8], 
    entry: &[u8], 
    offset: usize,
    entry_cap: usize,
    entry_size: usize,
) {
    let mut map = get_map(page).to_vec();
    let intervals = derive_clean_intervals(&mut map);

    // find the interval that contains offset
    for (left, right) in intervals {
        if left <= offset && offset < right {

            if right < entry_cap {
                let end = right * entry_size;
                page.copy_within(offset..end, offset + entry_size);
                set_bit(&mut map, right, true);

            } else {
                let start = left * entry_size;
                page.copy_within(start..offset, start - entry_size);
                set_bit(&mut map, left-1, true);
            }

            // write it
            page[offset..offset + entry_size].copy_from_slice(entry);
            increment_occupied(page, 1);
            set_map(page, &mut map);
            return
        }
    }
}

pub fn split_and_insert(
    ledger: &mut Ledger,
    page_id: u64,
    entry_len: usize,
    new_entry: &[u8],
    altered: &mut HashSet<u64>,
) {

    fn insert_new_entry(page: &mut [u8], new_entry: &[u8]) {
        let mut key = [0u8; KEY_LEN];
        key.copy_from_slice(&new_entry[..KEY_LEN]);
        if is_leaf(page) {
            if let Err((offset, _)) = bin_search_leaf(page, &key) {
                shift_and_insert(page, new_entry, offset, LEAF_ENTRY_COUNT, LEAF_ENTRY_LEN);
            }
        } else {
            let (_, offset) = bin_search_node(page, &key);
            shift_and_insert(page, new_entry, offset, NODE_ENTRY_COUNT, NODE_ENTRY_LEN);
        }
    }

    // obtain ownership of tmp page buffer
    let mut tmp = ledger.page_buffer.take().unwrap();

    // load full page, get parent and partition
    let page = ledger.get_page_mut(page_id);
    let parent = get_parent(page);
    let body = get_body(page);
    let part = body.len() / 2;
    let new_count = (part / entry_len) as u16;

    // copy from part to end into tmp buffer
    tmp[..part].copy_from_slice(&body[part..]);

    // update page with new count
    set_occupied_count(page, new_count);

    // save new key that differentiates the new leaves
    let mut new_key = [0u8; KEY_LEN];
    new_key.copy_from_slice(&tmp[..KEY_LEN]);

    // if new entry is less than that key inser it into current page
    // else set is greater and wait to insert into new page
    let mut is_greater = false;
    match new_entry.cmp(&new_key) {
        Ordering::Greater => is_greater = true,
        Ordering::Less => {
        }
        _ => {}
    }

    // get new page and copy from tmp
    let new_page_id = ledger.get_new_page();
    let page = ledger.get_page_mut(new_page_id);
    fill_page_header(page, parent, new_page_id, true, new_count);
    let body = get_body(page);
    body.copy_from_slice(&tmp);

    altered.insert(new_page_id);

    if is_greater {
        if new_entry.cmp(&new_key) == Ordering::Greater {
            let mut key = [0u8; KEY_LEN];
            key.copy_from_slice(&new_entry[..KEY_LEN]);
            if is_leaf(page) {
                if let Err((offset, _)) = bin_search_leaf(page, &key) {
                    shift_and_insert(page, new_entry, offset, LEAF_ENTRY_COUNT, LEAF_ENTRY_LEN);
                }
            } else {
                let (_, offset) = bin_search_node(page, &key);
                shift_and_insert(page, new_entry, offset, NODE_ENTRY_COUNT, NODE_ENTRY_LEN);
            }
        }
    }

    altered.insert(parent);

    // add new key to parent and recursively split if neccessary
    insert_node_child(ledger, parent, new_key, new_page_id, altered);

    ledger.page_buffer = Some(tmp);
}

