// SPDX-License-Identifier: GPL-2.0-only

use std::{ffi::{c_void, CString}, fs, path::Path};

#[repr(C)]
struct BulletDB {
    _private: [u8; 0],
}

unsafe extern "C" {
    fn lmdb_open(path: *const std::os::raw::c_char, map_size: usize) -> *mut BulletDB;
    fn lmdb_close(handle: *mut BulletDB);
    fn lmdb_start_trx(handle: *mut BulletDB);
    fn lmdb_end_trx(handle: *mut BulletDB, rc: i32);
    fn lmdb_delete(handle: *mut BulletDB, key_data: *const c_void, key_size: usize) -> i32;
    fn lmdb_exists(handle: *mut BulletDB, key_data: *const c_void, key_size: usize) -> i32;
    fn lmdb_get(handle: *mut BulletDB,
                key_data: *const c_void, key_size: usize,
                value_data: *mut *mut c_void, value_size: *mut usize) -> i32;

    fn lmdb_put(handle: *mut BulletDB,
                key_data: *const c_void, key_size: usize,
                value_data: *const c_void, value_size: usize) -> i32;
}

pub struct DB {
    handle: *mut BulletDB,
    in_txn: bool,
}

impl Drop for DB {
    fn drop(&mut self) {
        unsafe { 
            lmdb_close(self.handle) 
        };
    }
}


impl DB {
    pub fn new(path: &str) -> Self {
        let path_obj = Path::new(path);
        if !path_obj.exists() {
            fs::create_dir_all(path_obj).expect("Failed to create LMDB directory");
        }
        let handle = unsafe {

            let map_size: usize = 1 * 1024 * 1024 * 1024; // 100 GB
            // TODO -- eventually need to check to see if needs to be larger
            // like if there is already a database


            let c_path = CString::new(path).expect("CString::new failed");
            lmdb_open(c_path.as_ptr(), map_size)
        };

        if handle.is_null() {
            panic!("lmdb_open failed");
        }

        Self { 
            handle, 
            in_txn: false, 
        }
    }

    pub fn start_trx(&mut self) {
        if self.in_txn {
            self.end_trx();
        }
        self.in_txn = true;
        unsafe { 
            lmdb_start_trx(self.handle); 
        };
    }

    pub fn abort_trx(&mut self, error: i32) {
        if self.in_txn {
            unsafe { 
                lmdb_end_trx(self.handle, error); 
            }
            self.in_txn = false;
        }
    }

    pub fn end_trx(&mut self) {
        if self.in_txn {
            unsafe { 
                lmdb_end_trx(self.handle, SUCCESS); 
            }
            self.in_txn = false;
        }
    }

    pub fn exists(&self, key: &[u8]) -> bool {
        unsafe {
            SUCCESS == lmdb_exists(
                self.handle, 
                key.as_ptr() as *const _, 
                key.len()
            )
        }
    }

    pub fn delete(&mut self, key: &[u8]) -> Result<(), LmdbError> {
        unsafe {
            let rc = lmdb_exists(
                self.handle, 
                key.as_ptr() as *const _, 
                key.len()
            );
            if rc == SUCCESS || rc == NOTFOUND {
                Ok(())
            } else {
                Err(LmdbError::from_rc(rc))
            }
        }
    }


    pub fn get(&self, key: &[u8]) -> Result<Vec<u8>, LmdbError> {
        let mut value_ptr: *mut c_void = std::ptr::null_mut();
        let mut value_size: usize = 0;

        unsafe {
            let rc = lmdb_get(self.handle, key.as_ptr() as *const _, key.len(),
                              &mut value_ptr, &mut value_size);
            if rc == SUCCESS {
                let slice = std::slice::from_raw_parts(value_ptr as *const u8, value_size);
                return Ok(slice.to_vec());

            } else {
                return Err(LmdbError::from_rc(rc));
            }
        }
    }

    pub fn put(&self, key: &[u8], value: Vec<u8>) -> Result<(), LmdbError> {
        unsafe {
            let rc = lmdb_put(
                self.handle,
                key.as_ptr() as *const c_void,
                key.len(),
                value.as_ptr() as *const c_void,
                value.len(),
            );
            if rc != SUCCESS {
                return Err(LmdbError::from_rc(rc));
            }
            Ok(())
        }
    }
}

unsafe extern "C" {
    pub static SUCCESS: i32;
    pub static NOTFOUND: i32;
    pub static ALREADY_EXISTS: i32;

    pub static TXN_FULL: i32;
    pub static MAP_FULL: i32;
    pub static DBS_FULL: i32;
    pub static READERS_FULL: i32;

    pub static PAGE_NOTFOUND: i32;
    pub static CORRUPTED: i32;
    pub static PANIC: i32;
    pub static VERSION_MISMATCH: i32;

    pub static INVALID: i32;
    pub static MAP_RESIZED: i32;
}



#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LmdbError {
    Success,
    NotFound,
    AlreadyExists,
    TxnFull,
    MapFull,
    DbsFull,
    ReadersFull,
    PageNotFound,
    Corrupted,
    Panic,
    VersionMismatch,
    Invalid,
    MapResized,
    Unknown(i32), // fallback for any unrecognized codes
}

impl LmdbError {
    /// Convert an LMDB return code into the enum
    pub fn from_rc(rc: i32) -> Self {
        unsafe {
            match rc {
                c if c == SUCCESS => LmdbError::Success,
                c if c == NOTFOUND => LmdbError::NotFound,
                c if c == ALREADY_EXISTS => LmdbError::AlreadyExists,
                c if c == TXN_FULL => LmdbError::TxnFull,
                c if c == MAP_FULL => LmdbError::MapFull,
                c if c == DBS_FULL => LmdbError::DbsFull,
                c if c == READERS_FULL => LmdbError::ReadersFull,
                c if c == PAGE_NOTFOUND => LmdbError::PageNotFound,
                c if c == CORRUPTED => LmdbError::Corrupted,
                c if c == PANIC => LmdbError::Panic,
                c if c == VERSION_MISMATCH => LmdbError::VersionMismatch,
                c if c == INVALID => LmdbError::Invalid,
                c if c == MAP_RESIZED => LmdbError::MapResized,
                other => LmdbError::Unknown(other),
            }
        }
    }
}
