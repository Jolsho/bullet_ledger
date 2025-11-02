// extern.cpp
#include <lmdb.h>
#include "../src/bullet_db.h"
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

void* lmdb_open(const char* path, size_t map_size) {
    return new BulletDB(path, map_size);
}
void lmdb_close(void* handle) {
    delete static_cast<BulletDB*>(handle);
}

void lmdb_start_trx(void* handle) {
    auto h = static_cast<BulletDB*>(handle);
    mdb_txn_begin(h->env_, nullptr, 0, &h->txn_);
}

void lmdb_end_trx(void* handle, int rc) {
    auto h = static_cast<BulletDB*>(handle);
    if (rc == 0) mdb_txn_commit(h->txn_);
    else mdb_txn_abort(h->txn_);
}

int lmdb_put(void* handle, 
             const void* key_data, size_t key_size, 
             const void* value_data, size_t value_size) {
    return static_cast<BulletDB*>(handle) 
        ->put(key_data, key_size, value_data, value_size);
}

int lmdb_get(void* handle, 
             const void* key_data, size_t key_size, 
             void** value_data, size_t* value_size) {
    return static_cast<BulletDB*>(handle)
        ->get(key_data, key_size, value_data, value_size);

}
void* mutable_get(void* handle, 
                  const void* key, size_t key_size, 
                  size_t value_size) {
    return static_cast<BulletDB*>(handle) 
        ->mut_get(key, key_size, value_size);
}


int lmdb_delete(void* handle, 
                const void* key_data, size_t key_size) {
    return static_cast<BulletDB*>(handle) 
        ->del(key_data, key_size);
}

int lmdb_exists(void* handle, const void* key_data, size_t key_size) {
    return static_cast<BulletDB*>(handle) 
        ->exists(key_data, key_size);
}

