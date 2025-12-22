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
#include "result.h"
#include "state_types.h"
#include "polynomial.h"
#include <cstdint>
#include <vector>


struct Gadgets;

class Node {
public:
    virtual ~Node() = default;

    virtual const NodeId* get_id() = 0;
    virtual const Commitment* get_commitment() const = 0;
    virtual const byte get_type() const = 0;
    virtual const bool should_delete() const = 0;

    virtual const NodeId* get_next_id(ByteSlice& nibbles) = 0;
    virtual std::vector<byte> to_bytes() const = 0;

    virtual const Commitment* derive_commitment(Gadgets*) = 0;

    virtual Result<Hash, int> search(
        Gadgets*, 
        ByteSlice nibbles
    ) = 0;


    virtual int put(
        Gadgets*,
        ByteSlice nibbles,
        const Hash& key,
        const Hash& val_hash,
        uint16_t new_block_id
    ) = 0;

    virtual int remove(
        Gadgets*,
        ByteSlice nibbles,
        const Hash& key,
        uint16_t new_block_id
    ) = 0;

    virtual int delete_account(
        Gadgets*,
        ByteSlice nibbles,
        const Hash& key,
        uint16_t new_block_id
    ) = 0;

    virtual int generate_proof(
        Gadgets*,
        const Hash& key,
        ByteSlice nibbles,
        std::vector<Polynomial>& Fxs
    ) = 0;


    virtual Result<const Commitment*, int> finalize(
        Gadgets*,
        const uint16_t block_id
    ) = 0;

    virtual int prune(
        Gadgets*,
        const uint16_t block_id
    ) = 0;

    virtual int justify(Gadgets*) = 0;

};
