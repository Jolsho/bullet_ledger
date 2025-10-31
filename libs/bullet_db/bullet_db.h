// bullet_db.h
#include <cstdint>
#include <cstdio>
#include <lmdb.h>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <array>

extern "C" {

struct BulletDB {
    MDB_env* env;
    MDB_txn* txn;
    MDB_dbi dbi;
    std::vector<uint64_t> keys;
    void* root;
    std::array<void*, 256> l1;
    std::unordered_map<uint64_t, void*> l2;
    std::unordered_map<uint64_t, void*> l3;
};

BulletDB* lmdb_open(const char* path, size_t map_size);
void lmdb_close(BulletDB* handle);
void lmdb_start_trx(BulletDB* handle);
void lmdb_end_trx(BulletDB* handle, int rc);
int lmdb_put(BulletDB* handle, const void* key_data, size_t key_size, const void* value_data, size_t value_size);
int lmdb_get(BulletDB* handle, const void* key_data, size_t key_size, void** value_data, size_t* value_size);
void* mutable_get(BulletDB* handle, const void* key, size_t key_size, size_t value_size);
int lmdb_delete(BulletDB* handle, const void* key_data, size_t key_size);
int lmdb_exists(BulletDB* handle, const void* key_data, size_t key_size);

} // extern "C"

