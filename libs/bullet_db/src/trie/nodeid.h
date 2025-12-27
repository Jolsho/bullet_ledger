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
#include <cstdint>
#include <array>
#include "blst.h"

const size_t ID_SIZE = 8 + 2;

class NodeId {
private:
    byte buff_[ID_SIZE];
public:
    NodeId();
    NodeId(const NodeId* other);
    NodeId(uint64_t node_id, uint16_t block_id);

    bool operator ==(const NodeId& other) const noexcept;

    ~NodeId();
    uint16_t get_block_id() const;
    void set_block_id(uint16_t id);

    uint64_t get_node_id() const;
    void set_node_id(uint64_t id);

    const byte* get_full() const;
    std::array<byte, ID_SIZE> get_full_array() const;

    void from_bytes(const byte* buff);
    size_t size() const;

    uint64_t derive_child_id(const byte nib) const;
};


struct NodeIdHash {
    size_t operator()(const NodeId& k) const noexcept {
        auto array = k.get_full_array();
        uint64_t h = 1469598103934665603ull; // FNV-1a
        for (byte b : array) {
            h ^= static_cast<unsigned char>(b);
            h *= 1099511628211ull;
        }
        return static_cast<size_t>(h);
    }
};

