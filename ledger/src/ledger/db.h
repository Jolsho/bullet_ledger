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

#pragma once
#include <lmdb.h>
#include <shared_mutex>
#include <vector>

class BulletDB {
public:
    MDB_env* env_;
    MDB_dbi dbi_;
    int count_;
    std::shared_mutex mux_;

    BulletDB(const char* path, size_t map_size);
    ~BulletDB();
    void* start_txn();
    void* start_rd_txn();

    void end_txn(void* trx, int rc = 0);

    int put(const void* key_data, size_t key_size, 
            const void* value_data, size_t value_size,
            void* trx
        );
    int get(
        const void* key_data, size_t key_size, 
        std::vector<std::byte> &out, 
        void* trx
    );
    int get_raw(const void* key_data, size_t key_size,
            void** value_data, size_t* value_size,
             void* trx
    );

    int del(const void* key_data, size_t key_size,  void* trx);
    int exists(const void* key_data, size_t key_size,  void* trx);
    std::vector<uint64_t> flatten_sort_l2();
};
