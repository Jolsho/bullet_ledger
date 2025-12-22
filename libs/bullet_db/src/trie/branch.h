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
#include "node.h"

class Branch_i : public Node {
public:
    virtual void insert_child(
        const byte& nib,
        const Commitment* new_commit,
        const Gadgets* gadgets,
        const uint16_t block_id
    ) = 0;

    virtual Result<void*, int> change_id(
        uint64_t node_id,
        uint16_t block_id,
        Gadgets* gadgets
    ) = 0;
};

Branch_i* create_branch(const NodeId* id, const ByteSlice* buff);
