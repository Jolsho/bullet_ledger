// bullet_db.cpp
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <lmdb.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "bullet_db.h"

BulletDB::BulletDB(const char* path, size_t map_size) {
    keys.reserve(10);
    mdb_env_create(&env);
    mdb_env_set_mapsize(env, map_size);
    mdb_env_open(env, path, 0, 0600);

    MDB_txn* txn;
    mdb_txn_begin(env, nullptr, 0, &txn);
    mdb_dbi_open(txn, nullptr, 0, &dbi);
    mdb_txn_commit(txn);

    // 12,000 == 2,000 * 3 * 2
    // 2,000 trxns per block
    // 3 accounts touched per trx on avg
    // 2 nodes loaded == 1 in l2, 1 in l3
    l2.reserve(10);
    l3.reserve(10);
    // 12,000 * (8 + 8 + 50)bytes ==  792KB
    //      uint_64 + void* + overhead
}

BulletDB::~BulletDB() {
    // TODO -- save state??
    mdb_dbi_close(env, dbi);
    mdb_env_close(env);
}
int BulletDB::put(const void* key_data, size_t key_size, const void* value_data, size_t value_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value{ value_size, (void*)(value_data) };

    return mdb_put(txn, dbi, &key, &value, 0);
}

int BulletDB::get(const void* key_data, size_t key_size, void** value_data, size_t* value_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;

    int rc = mdb_get(txn, dbi, &key, &value);
    if (rc == 0) {
        *value_size = value.mv_size;
        *value_data = malloc(value.mv_size);
        memcpy(*value_data, value.mv_data, value.mv_size);
    }
    return rc;
}

void* BulletDB::mut_get(const void* key, size_t key_size, size_t value_size) {
    MDB_val k{ key_size, const_cast<void*>(key) };
    MDB_val v{ value_size, nullptr };
    int rc = mdb_put(txn, dbi, &k, &v, MDB_RESERVE);
    return rc == 0 ? v.mv_data : nullptr;
}

int BulletDB::del(const void* key_data, size_t key_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    return mdb_del(txn, dbi, &key, nullptr);
}

int BulletDB::exists(const void* key_data, size_t key_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;
    return mdb_get(txn, dbi, &key, &value);
}



std::vector<uint64_t> BulletDB::flatten_sort_l2() {

    // large array for previously used node_ids.
    // 12,000 nodes/block * 32 slots/epoch == 384,000 nodes/epoch
    // 8 bytes/node_id  * 384,000 node_id/epoch == 3.07MB / epoch

    std::vector<uint64_t> keys; // MAX size is 3.07MB
    keys.reserve(l2.size() + l3.size());

    // copy l2
    for (const auto& [key, value] : l2)
        keys.push_back(key);

    // copy l3
    for (const auto& [key, value] : l3)
        keys.push_back(key);

    auto middle = keys.begin() + l2.size();

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
