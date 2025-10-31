// extern.h
#include <cstddef>

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
