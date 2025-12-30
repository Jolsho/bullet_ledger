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
#include <lmdb.h>

BulletDB::BulletDB(const char* path, size_t map_size) {
    assert(mdb_env_create(&env_) == 0);
    assert(mdb_env_set_mapsize(env_, map_size) == 0);
    assert(mdb_env_open(env_, path, 0, 0600) == 0);

    void* trx = start_txn();
    assert(mdb_dbi_open((MDB_txn*)trx, nullptr, 0, &dbi_) == 0);
    end_txn(trx);
}

BulletDB::~BulletDB() {
    mdb_dbi_close(env_, dbi_);
    mdb_env_close(env_);
}

void* BulletDB::start_txn() { 
    MDB_txn* tx;
    assert(mdb_txn_begin(env_, nullptr, 0, &tx) == 0); 
    return tx;
}

void* BulletDB::start_rd_txn() { 
    MDB_txn* tx;
    assert(mdb_txn_begin(env_, nullptr, MDB_RDONLY, &tx) == 0); 
    return tx;
}

void BulletDB::end_txn(void* trx, int rc) {
    if (rc == 0) 
        assert(mdb_txn_commit((MDB_txn*)trx) == 0);
    else 
        mdb_txn_abort((MDB_txn*)trx);
}

int BulletDB::put(
    const void* key_data, size_t key_size, 
    const void* value_data, size_t value_size, 
    void* trx
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value{ value_size, (void*)(value_data) };

    return mdb_put((MDB_txn*)trx, dbi_, &key, &value, 0);
}

int BulletDB::get_raw(
    const void* key_data, size_t key_size, 
    void** value_data, size_t* value_size,
    void* trx
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;

    int rc = mdb_get((MDB_txn*)trx, dbi_, &key, &value);
    if (rc == 0) {
        *value_size = value.mv_size;
        *value_data = malloc(value.mv_size);
        memcpy(*value_data, value.mv_data, value.mv_size);
    }
    return rc;
}

int BulletDB::get(
    const void* key_data, size_t key_size, 
    std::vector<std::byte> &out,
    void* trx
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;

    int rc = mdb_get((MDB_txn*)trx, dbi_, &key, &value);
    if (rc == 0) {
        out.resize(value.mv_size);
        std::memcpy(out.data(), value.mv_data, value.mv_size);
    }
    return rc;
}

int BulletDB::del(const void* key_data, size_t key_size, void* trx) {
    MDB_val key{ key_size, (void*)(key_data) };
    return mdb_del((MDB_txn*)trx, dbi_, &key, nullptr);
}

int BulletDB::exists(const void* key_data, size_t key_size, void* trx) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;
    return mdb_get((MDB_txn*)trx, dbi_, &key, &value);
}
