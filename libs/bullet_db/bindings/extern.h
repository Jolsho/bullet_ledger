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
