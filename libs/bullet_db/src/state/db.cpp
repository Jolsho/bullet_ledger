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

#include <algorithm>
#include <lmdb.h>
#include "db.h"

BulletDB::BulletDB(const char* path, size_t map_size) {
    mdb_env_create(&env_);
    mdb_env_set_mapsize(env_, map_size);
    mdb_env_open(env_, path, 0, 0600);

    MDB_txn* txn;
    mdb_txn_begin(env_, nullptr, 0, &txn);
    mdb_dbi_open(txn, nullptr, 0, &dbi_);
    mdb_txn_commit(txn);

    // 12,000 == 2,000 * 3 * 2
    // 2,000 trxns per block
    // 3 accounts touched per trx on avg
    // 2 nodes loaded == 1 in l2, 1 in l3
    l2_.reserve(10);
    l3_.reserve(10);
    // 12,000 * (8 + 8 + 50)bytes ==  792KB
    //      uint_64 + void* + overhead
    keys_.reserve(10);
}

BulletDB::~BulletDB() {
    // TODO -- save state??
    mdb_dbi_close(env_, dbi_);
    mdb_env_close(env_);
}

void BulletDB::start_txn() { 
    mdb_txn_begin(env_, nullptr, 0, &txn_); 
}

void BulletDB::end_txn(int rc) {
    if (rc == 0) mdb_txn_commit(txn_);
    else mdb_txn_abort(txn_);
}

int BulletDB::put(
    const void* key_data, size_t key_size, 
    const void* value_data, size_t value_size
) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value{ value_size, (void*)(value_data) };

    return mdb_put(txn_, dbi_, &key, &value, 0);
}

int BulletDB::get(
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

void* BulletDB::mut_get(
    const void* key, size_t key_size, 
    size_t value_size
) {
    MDB_val k{ key_size, const_cast<void*>(key) };
    MDB_val v{ value_size, nullptr };
    int rc = mdb_put(txn_, dbi_, &k, &v, MDB_RESERVE);
    return rc == 0 ? v.mv_data : nullptr;
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

std::vector<uint64_t> BulletDB::flatten_sort_l2() {

    // large array for previously used node_ids.
    // 12,000 nodes/block * 32 slots/epoch == 384,000 nodes/epoch
    // 8 bytes/node_id  * 384,000 node_id/epoch == 3.07MB / epoch

    std::vector<uint64_t> keys; // MAX size is 3.07MB
    keys.reserve(l2_.size() + l3_.size());

    // copy l2
    for (const auto& [key, value] : l2_)
        keys.push_back(key);

    // copy l3
    for (const auto& [key, value] : l3_)
        keys.push_back(key);

    auto middle = keys.begin() + l2_.size();

    // sort l2 partition
    std::sort(keys.begin(), middle);

    // sort l3 parition
    std::sort(middle, keys.end());

    // TODO -- then write this thing to some place.
    // maybe just have a seperate file for these.
    // call these snapshots.
    // you have hash and  length of keys.
    // just iterate by length until hash matches
    // and if it doesnt exist insert into first free space
    // maybe just mark out 3.5MB per set of keys,
    // so you can iterate without reading length.
    // and that means you can always fit a snapshot

    return keys;
}
