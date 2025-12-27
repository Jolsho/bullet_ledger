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
#include "hashing.h"
#include "nodeid.h"
#include "state_types.h"
#include "polynomial.h"
#include <cstdint>
#include <memory>
#include <vector>

class Node;
using Node_ptr = std::shared_ptr<Node>;

class Node {
public:
    virtual ~Node() = default;

    virtual const NodeId* get_id() = 0;
    virtual void set_id(const NodeId &id) = 0;

    virtual const Commitment* get_commitment() const = 0;
    virtual void set_commitment(const Commitment &c) = 0;

    virtual const byte get_type() const = 0;
    virtual const bool should_delete() const = 0;

    virtual const NodeId* get_next_id(byte nib) = 0;
    virtual std::vector<byte> to_bytes() const = 0;

    virtual int change_id(
        uint64_t node_id, 
        uint16_t block_id
    ) = 0;

    virtual int put(
        const Hash* key,
        const Hash* val_hash,
        uint16_t new_block_id,
        int i
    ) = 0;

    virtual int remove(
        const Hash* key,
        uint16_t new_block_id,
        int i
    ) = 0;

    virtual int delete_account(
        const Hash* key,
        uint16_t new_block_id,
        int i
    ) = 0;

    virtual int generate_proof(
        const Hash* key,
        std::vector<Polynomial> &Fxs,
        std::vector<blst_p1> &Cs,
        int i
    ) = 0;


    virtual int finalize(
        const uint16_t block_id,
        Commitment *out,
        const size_t start = 0, 
        size_t end = 0,
        Polynomial* Fx = nullptr
    ) = 0;

    virtual int prune(const uint16_t block_id) = 0;

    virtual int justify() = 0;
};

