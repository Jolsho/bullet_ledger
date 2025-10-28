// lmdb_wrapper.cpp
#include <cstdio>
#include <lmdb.h>
#include <cstring>
#include <cstdlib>

extern "C" {

struct LMDBHandle {
    MDB_env* env;
    MDB_txn* txn;
    MDB_dbi dbi;
};

LMDBHandle* lmdb_open(const char* path, size_t map_size) {
    LMDBHandle* handle = new LMDBHandle();
    mdb_env_create(&handle->env);
    mdb_env_set_mapsize(handle->env, map_size);
    mdb_env_open(handle->env, path, 0, 0600);

    MDB_txn* txn;
    mdb_txn_begin(handle->env, nullptr, 0, &txn);
    mdb_dbi_open(txn, nullptr, 0, &handle->dbi);
    mdb_txn_commit(txn);

    return handle;
}

void lmdb_close(LMDBHandle* handle) {
    mdb_dbi_close(handle->env, handle->dbi);
    mdb_env_close(handle->env);
    delete handle;
}

void lmdb_start_trx(LMDBHandle* handle) {
    mdb_txn_begin(handle->env, nullptr, 0, &handle->txn);
}

void lmdb_end_trx(LMDBHandle* handle, int rc) {
    if (rc == 0) mdb_txn_commit(handle->txn);
    else mdb_txn_abort(handle->txn);
}


int lmdb_put(LMDBHandle* handle, const void* key_data, size_t key_size,
             const void* value_data, size_t value_size) {

    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value{ value_size, (void*)(value_data) };

    return mdb_put(handle->txn, handle->dbi, &key, &value, 0);
}

int lmdb_get(LMDBHandle* handle, const void* key_data, size_t key_size,
             void** value_data, size_t* value_size) {

    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;

    int rc = mdb_get(handle->txn, handle->dbi, &key, &value);
    if (rc == 0) {
        *value_size = value.mv_size;
        *value_data = malloc(value.mv_size);
        memcpy(*value_data, value.mv_data, value.mv_size);
    }
    return rc;
}

int lmdb_delete(LMDBHandle* handle, const void* key_data, size_t key_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    return mdb_del(handle->txn, handle->dbi, &key, nullptr);
}

int lmdb_exists(LMDBHandle* handle, const void* key_data, size_t key_size) {
    MDB_val key{ key_size, (void*)(key_data) };
    MDB_val value;
    return mdb_get(handle->txn, handle->dbi, &key, &value);
}

} // extern "C"

