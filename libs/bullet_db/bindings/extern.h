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

// LEDGER
void* open(
    const char* path, 
    size_t cache_size,
    size_t map_size,
    const char* tag,
    const void* secret
);

// DATABASE
void* db_open(const char* path, size_t map_size);
void db_close(void* handle);
void db_start_trx(void* handle);
void db_end_trx(void* handle, int rc);
int db_put(void* handle, const void* key_data, size_t key_size, const void* value_data, size_t value_size);
int db_get(void* handle, const void* key_data, size_t key_size, void** value_data, size_t* value_size);
int db_delete(void* handle, const void* key_data, size_t key_size);
int db_exists(void* handle, const void* key_data, size_t key_size);

} // extern "C"
