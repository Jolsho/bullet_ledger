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


use std::ffi::{CString, c_void};
use std::ptr::NonNull;
use nix::libc;

use ffi::*;
use crate::utils::errors::InternalError;

mod ffi;

#[derive(Debug)]
pub struct Ledger {
    inner: NonNull<c_void>,
}

type Result<T> = std::result::Result<T, InternalError>;

impl Ledger {
    pub fn open(
        path: &str,
        cache_size: usize,
        map_size: usize,
        tag: &str,
        secret: Option<[u8; 32]>,
    ) -> Result<Self> {
        let path = CString::new(path).unwrap();
        let tag = CString::new(tag).unwrap();

        let mut out: *mut c_void = std::ptr::null_mut();

        let rc = unsafe {
            ledger_open(
                &mut out,
                path.as_ptr(),
                cache_size,
                map_size,
                tag.as_ptr(),
                secret.map_or(std::ptr::null_mut(), |mut s| s.as_mut_ptr()),
                secret.map_or(0, |s| s.len()),
            )
        };

        if rc != 0 {
            return Err(InternalError::Ledger(rc));
        }

        let inner = NonNull::new(out).expect("ledger_open returned null");
        Ok(Self { inner })
    }

    pub fn create_account(
        &self,
        key: &[u8],
        block_hash: Option<&Hash>,
        prev_block_hash: Option<&Hash>,
    ) -> Result<()> {
        let rc = unsafe {
            ledger_create_account(
                self.inner.as_ptr(),
                key.as_ptr(),
                key.len(),
                block_hash.map_or(std::ptr::null(), |h| h),
                prev_block_hash.map_or(std::ptr::null(), |h| h),
            )
        };

        if rc != 0 {
            Err(InternalError::Ledger(rc))
        } else {
            Ok(())
        }
    }

    pub fn put(
        &self,
        key: &[u8],
        value_hash: &Hash,
        val_idx: u8,
        block_hash: Option<&Hash>,
        prev_block_hash: Option<&Hash>,
    ) -> Result<()> {
        let rc = unsafe {
            ledger_put(
                self.inner.as_ptr(),
                key.as_ptr(), key.len(),
                value_hash,
                val_idx,
                block_hash.map_or(std::ptr::null(), |h| h),
                prev_block_hash.map_or(std::ptr::null(), |h| h),
            )
        };

        if rc != 0 {
            Err(InternalError::Ledger(rc))
        } else {
            Ok(())
        }
    }

    pub fn finalize(
        &self,
        block_hash: &Hash,
    ) -> Result<Vec<u8>> {
        let mut out: *mut c_void = std::ptr::null_mut();
        let mut out_size: usize = 0;

        let rc = unsafe {
            ledger_finalize(
                self.inner.as_ptr(),
                block_hash,
                &mut out,
                &mut out_size,
            )
        };

        if rc != 0 {
            return Err(InternalError::Ledger(rc));
        }

        assert!(!out.is_null());
        let bytes = unsafe {
            std::slice::from_raw_parts(out as *const u8, out_size)
        }.to_vec();

        // IMPORTANT:
        // You MUST free `out` if the C++ side expects it.
        // Replace this with the correct deallocator.
        unsafe {
            libc::free(out);
        }

        Ok(bytes)
    }

    pub fn db_put(
        &self,
        key: &[u8],
        value: &[u8],
    ) -> Result<()> {
        let rc = unsafe {
            ledger_db_store_value(
                self.inner.as_ptr(),
                key.as_ptr(), key.len(),
                value.as_ptr(), value.len()
            )
        };
        if rc != 0 { 
            return Err(InternalError::Ledger(rc));
        }
        Ok(())
    }

    pub fn db_remove(
        &self,
        key: &[u8]
    ) -> Result<()> {
        let rc = unsafe {
            ledger_db_delete_value(
                self.inner.as_ptr(),
                key.as_ptr(), key.len()
            )
        };
        if rc != 0 { 
            return Err(InternalError::Ledger(rc));
        }
        Ok(())
    }

    pub fn db_get(
        &self,
        key: &[u8]
    ) -> Result<Vec<u8>> {
        let mut out: *mut c_void = std::ptr::null_mut();
        let mut out_size: usize = 0;

        let rc = unsafe {
            ledger_db_get_value(
                self.inner.as_ptr(),
                key.as_ptr(), key.len(),
                &mut out, &mut out_size,
            )
        };

        if rc != 0 {
            return Err(InternalError::Ledger(rc));
        }

        assert!(!out.is_null());
        let bytes = unsafe {
            std::slice::from_raw_parts(out as *const u8, out_size)
        }.to_vec();

        // IMPORTANT:
        // You MUST free `out` if the C++ side expects it.
        // Replace this with the correct deallocator.
        unsafe {
            libc::free(out);
        }

        Ok(bytes)
    }

    pub fn db_exists(
        &self, key: &[u8]
    ) -> Result<()> {
        let rc = unsafe {
            ledger_db_value_exists(
                self.inner.as_ptr(),
                key.as_ptr(), key.len()
            )
        };
        if rc != 0 { 
            return Err(InternalError::Ledger(rc));
        }
        Ok(())
    }
}

