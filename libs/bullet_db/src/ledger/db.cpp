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

#include "db.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

BulletDB::BulletDB(const char* path, size_t map_size) {
    assert(mdb_env_create(&env_) == 0);
    assert(mdb_env_set_mapsize(env_, map_size) == 0);
    assert(mdb_env_open(env_, path, 0, 0600) == 0);

    MDB_txn* txn;
    assert(mdb_txn_begin(env_, nullptr, 0, &txn) == 0);
    assert(mdb_dbi_open(txn, nullptr, 0, &dbi_) == 0);
    assert(mdb_txn_commit(txn) == 0);
    active_txn_ = false;
}

BulletDB::~BulletDB() {
    // TODO -- save state??
    mdb_dbi_close(env_, dbi_);
    mdb_env_close(env_);
}

void BulletDB::start_txn() { 
    if (active_txn_) return;
    assert(mdb_txn_begin(env_, nullptr, 0, &txn_) == 0); 
    active_txn_ = true;
}

void BulletDB::end_txn(int rc) {
    if (!active_txn_) return;
    if (rc == 0) assert(mdb_txn_commit(txn_) == 0);
    else mdb_txn_abort(txn_);
    active_txn_ = false;
}

int BulletDB::put(
    const void* key_data, size_t key_size, 
    const void* value_data, size_t value_size
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value{ value_size, (void*)(value_data) };

    return mdb_put(txn_, dbi_, &key, &value, 0);
}
int BulletDB::get_raw(
    const void* key_data, size_t key_size, 
    void** value_data, size_t* value_size
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;

    int rc = mdb_get(txn_, dbi_, &key, &value);
    if (rc == 0) {
        *value_size = value.mv_size;
        *value_data = malloc(value.mv_size);
        memcpy(*value_data, value.mv_data, value.mv_size);
    }
    return rc;
}

int BulletDB::get(
    const void* key_data, size_t key_size, 
    std::vector<std::byte> &out
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;

    int rc = mdb_get(txn_, dbi_, &key, &value);
    if (rc == 0) {
        out.resize(value.mv_size);
        std::memcpy(out.data(), value.mv_data, value.mv_size);
    }
    return rc;
}

int BulletDB::del( const void* key_data, size_t key_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    return mdb_del(txn_, dbi_, &key, nullptr);
}

int BulletDB::exists(const void* key_data, size_t key_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;
    return mdb_get(txn_, dbi_, &key, &value);
}
