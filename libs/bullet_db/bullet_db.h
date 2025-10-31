// bullet_db.h
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
    void* root_;
    std::array<void*, 256> l1_;
    std::unordered_map<uint64_t, void*> l2_;
    std::unordered_map<uint64_t, void*> l3_;

    BulletDB(const char* path, size_t map_size);
    ~BulletDB();
    int put(const void* key_data, size_t key_size, const void* value_data, size_t value_size);
    int get(const void* key_data, size_t key_size, void** value_data, size_t* value_size);
    void* mut_get(const void* key, size_t key_size, size_t value_size);
    int del(const void* key_data, size_t key_size);
    int exists(const void* key_data, size_t key_size);
    std::vector<uint64_t> flatten_sort_l2();
};

