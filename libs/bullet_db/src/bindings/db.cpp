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


// db.cpp
#include <lmdb.h>
#include "db.h"
#include "extern.h"

extern "C" {

extern const int SUCCESS         = MDB_SUCCESS;
extern const int NOTFOUND        = MDB_NOTFOUND;
extern const int ALREADY_EXISTS  = MDB_KEYEXIST;

extern const int TXN_FULL        = MDB_TXN_FULL;
extern const int MAP_FULL        = MDB_MAP_FULL;
extern const int DBS_FULL        = MDB_DBS_FULL;
extern const int READERS_FULL    = MDB_READERS_FULL;

extern const int PAGE_NOTFOUND   = MDB_PAGE_NOTFOUND;
extern const int CORRUPTED       = MDB_CORRUPTED;
extern const int PANIC           = MDB_PANIC;
extern const int VERSION_MISMATCH= MDB_VERSION_MISMATCH;

extern const int INVALID         = MDB_INVALID;
extern const int MAP_RESIZED     = MDB_MAP_RESIZED;

}

void* db_open(const char* path, size_t map_size) {
    return new BulletDB(path, map_size);
}
void db_close(void* handle) {
    delete static_cast<BulletDB*>(handle);
}

void db_start_trx(void* handle) {
    auto h = static_cast<BulletDB*>(handle);
    mdb_txn_begin(h->env_, nullptr, 0, &h->txn_);
}

void db_end_trx(void* handle, int rc) {
    auto h = static_cast<BulletDB*>(handle);
    if (rc == 0) mdb_txn_commit(h->txn_);
    else mdb_txn_abort(h->txn_);
}

int db_put(void* handle, 
             const void* key_data, size_t key_size, 
             const void* value_data, size_t value_size) {
    return static_cast<BulletDB*>(handle) 
        ->put(key_data, key_size, value_data, value_size);
}

int db_get(void* handle, 
             const void* key_data, size_t key_size, 
             void** value_data, size_t* value_size) {
    return static_cast<BulletDB*>(handle)
        ->get_raw(key_data, key_size, value_data, value_size);

}


int db_delete(void* handle, 
                const void* key_data, size_t key_size) {
    return static_cast<BulletDB*>(handle) 
        ->del(key_data, key_size);
}

int lmdb_exists(void* handle, const void* key_data, size_t key_size) {
    return static_cast<BulletDB*>(handle) 
        ->exists(key_data, key_size);
}


