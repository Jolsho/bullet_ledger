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
#include "blst.h"
#include "result.h"
#include <cstdint>
#include <cstring>
#include <span>

const size_t BLOCK_ID_OFFSET = 2;

const size_t HEADER_SIZE = 2 + 2;
const size_t TRXS_OFFSET = HEADER_SIZE;

struct Block {
    byte* raw_block;
    size_t raw_size;

    byte* cursor;
};

uint16_t get_block_id(Block* block) {

    uint16_t block_id;
    const byte* cursor = block->raw_block + BLOCK_ID_OFFSET;
    std::memcpy(&block_id, cursor, sizeof(block_id));

    return block_id;
}

byte* get_trxs_start(Block* block) {
    return block->raw_block + TRXS_OFFSET;
}

Result<const std::span<byte>, int> next_trx(Block* block) {
    size_t trx_length;
    size_t len_size = sizeof(trx_length);
    std::memcpy(&trx_length, block->cursor, len_size);

    // if (!valid_trx_length(block->cursor + len_size, trx_length))
    //     return INVALID_TRX_LENGTH;

    std::span<byte> section(block->cursor, trx_length);
    block->cursor += len_size;
    return section;
}
