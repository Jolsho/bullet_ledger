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

use std::os::raw::{c_char, c_int, c_uchar};
use std::ffi::c_void;

#[repr(C)]
#[derive(Copy, Clone)]
pub struct Hash {
    // IMPORTANT:
    // This must exactly match hashing.h
    // Replace with the real definition if different
    pub h: [u8; 32],
}
unsafe extern "C" {
    pub fn ledger_open(
        out: *mut *mut c_void,
        path: *const c_char,
        cache_size: usize,
        map_size: usize,
        tag: *const c_char,
        secret: *mut c_uchar,
        secret_size: usize,
    ) -> c_int;

    pub fn ledger_get_SRS(
        ledger: *mut c_void,
        out: *mut *mut c_void,
        out_size: *mut usize,
    ) -> c_int;

    pub fn ledger_set_SRS(
        ledger: *mut c_void,
        setup: *const c_uchar,
        setup_size: usize,
    ) -> c_int;

    pub fn ledger_create_account(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        block_hash: *const Hash,
        prev_block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_delete_account(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        block_hash: *const Hash,
        prev_block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_put(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        value_hash: *const Hash,
        val_idx: u8,
        block_hash: *const Hash,
        prev_block_hash: *const Hash,
    ) -> c_int;

    pub fn replace(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        value_hash: *const Hash,
        val_idx: u8,
        prev_value_hash: *const Hash,
        block_hash: *const Hash,
        prev_block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_remove(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        val_idx: u8,
        block_hash: *const Hash,
        prev_block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_finalize(
        ledger: *mut c_void,
        block_hash: *const Hash,
        out: *mut *mut c_void,
        out_size: *mut usize,
    ) -> c_int;

    pub fn ledger_prune(
        ledger: *mut c_void,
        block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_justify(
        ledger: *mut c_void,
        block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_generate_existence_proof(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        val_idx: u8,
        out: *mut *mut c_void,
        out_size: *mut usize,
        block_hash: *const Hash,
    ) -> c_int;

    pub fn ledger_validate_proof(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        value_hash: *const Hash,
        val_idx: u8,
        proof: *const c_uchar,
        proof_size: usize,
    ) -> c_int;

    pub fn ledger_db_store_value(
        ledger: *mut c_void,
        key_hash: *const c_uchar,
        key_hash_size: usize,
        value: *const c_uchar,
        value_size: usize,
    ) -> c_int;

    pub fn ledger_db_delete_value(
        ledger: *mut c_void,
        key_hash: *const c_uchar,
        key_hash_size: usize,
    ) -> c_int;

    pub fn ledger_db_get_value(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
        out: *mut *mut c_void,
        out_size: *mut usize,
    ) -> c_int;

    pub fn ledger_db_value_exists(
        ledger: *mut c_void,
        key: *const c_uchar,
        key_size: usize,
    ) -> c_int;
}

