// SPDX-License-Identifier: GPL-2.0-only

#pragma once
#include <cstdint>
#include <cstdio>
#include <lmdb.h>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <array>

class BulletDB {
public:
    MDB_env* env_;
    MDB_txn* txn_;
    MDB_dbi dbi_;
    std::vector<uint64_t> keys_;

    void* root_;                                // 256^0 == 1           | NODE
    std::array<void*, 256> l1_;                 // 256^1 == 256         | NODES
    std::unordered_map<uint64_t, void*> l2_;    // 256^2 == 65,536      | NODES
    std::unordered_map<uint64_t, void*> l3_;    // 256^3 == 16,777,216  | LEAVES


    BulletDB(const char* path, size_t map_size);
    ~BulletDB();
    void start_txn();
    void end_txn(int rc = 0);
    int put(const void* key_data, size_t key_size, 
            const void* value_data, size_t value_size);
    int get(const void* key_data, size_t key_size, 
            void** value_data, size_t* value_size);
    void* mut_get(const void* key, size_t key_size, 
                  size_t value_size);
    int del(const void* key_data, size_t key_size);
    int exists(const void* key_data, size_t key_size);
    std::vector<uint64_t> flatten_sort_l2();

};
