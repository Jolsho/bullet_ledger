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

#include <cstddef>
#include "ledger.h"

extern "C" {
extern const int SECRET_SIZE = 32;
}

void* open(
    const char* path, 
    size_t cache_size,
    size_t map_size,
    const char* tag,
    const void* secret
) {
    blst_scalar s;
    if (secret) {
        blst_scalar_from_le_bytes(&s, reinterpret_cast<const byte*>(secret), SECRET_SIZE);
    }
    return new Ledger(path, cache_size, map_size, tag, s);
}
