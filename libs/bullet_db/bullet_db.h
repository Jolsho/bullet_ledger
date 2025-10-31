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
    MDB_env* env;
    MDB_txn* txn;
    MDB_dbi dbi;
    std::vector<uint64_t> keys;
    void* root;
    std::array<void*, 256> l1;
    std::unordered_map<uint64_t, void*> l2;
    std::unordered_map<uint64_t, void*> l3;

    BulletDB(const char* path, size_t map_size);
    ~BulletDB();
    int put(const void* key_data, size_t key_size, const void* value_data, size_t value_size);
    int get(const void* key_data, size_t key_size, void** value_data, size_t* value_size);
    void* mut_get(const void* key, size_t key_size, size_t value_size);
    int del(const void* key_data, size_t key_size);
    int exists(const void* key_data, size_t key_size);
    std::vector<uint64_t> flatten_sort_l2();
};

extern "C" {

void* lmdb_open(const char* path, size_t map_size);
void lmdb_close(void* handle);
void lmdb_start_trx(void* handle);
void lmdb_end_trx(void* handle, int rc);
int lmdb_put(void* handle, const void* key_data, size_t key_size, const void* value_data, size_t value_size);
int lmdb_get(void* handle, const void* key_data, size_t key_size, void** value_data, size_t* value_size);
void* mutable_get(void* handle, const void* key, size_t key_size, size_t value_size);
int lmdb_delete(void* handle, const void* key_data, size_t key_size);
int lmdb_exists(void* handle, const void* key_data, size_t key_size);

} // extern "C"

