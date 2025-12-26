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
#include "alloc.h"
#include <string>

struct Gadgets {
    KZGSettings settings;
    NodeAllocator alloc;

    Gadgets(
        KZGSettings settings_,
        std::string path,
        size_t cache_size,
        size_t map_size
    )
        : settings(std::move(settings_))
        , alloc(path, cache_size, map_size)
    {}
};

using Gadgets_ptr = std::shared_ptr<Gadgets>;

Gadgets_ptr init_gadgets(
    size_t degree, 
    const blst_scalar &s, 
    std::string tag,
    std::string path,
    size_t cache_size,
    size_t map_size
);
