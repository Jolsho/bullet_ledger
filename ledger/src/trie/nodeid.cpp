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

#include "nodeid.h"
#include <algorithm>
#include <cstring>
#include <format>

const size_t PATH_OFF = 0;
const size_t PATH_SIZE = 13;

const size_t LEVEL_OFF = PATH_OFF + PATH_SIZE;
const size_t LEVEL_SIZE = 1;

const size_t BLOCK_ID_OFF = LEVEL_OFF + LEVEL_SIZE;
const size_t BLOCK_ID_SIZE = 2;

NodeId::NodeId() {
    std::memset(buff_, 0, ID_SIZE);
}

NodeId::NodeId(const NodeId* other) { *this = *other; }

NodeId::NodeId(const Hash* key, uint8_t level, uint16_t block_id) {
    std::memcpy(buff_, key->h, PATH_SIZE);
    buff_[LEVEL_OFF] = level;
    std::memcpy(&buff_[BLOCK_ID_OFF], &block_id, BLOCK_ID_SIZE);
}

NodeId::NodeId(const std::vector<byte>* key, uint16_t block_id) {
    std::memset(buff_, 0, ID_SIZE);
    if (key) {
        buff_[LEVEL_OFF] = std::min(key->size(), PATH_SIZE);
        std::memcpy(buff_, key->data(), buff_[LEVEL_OFF]);
    }
    std::memcpy(&buff_[BLOCK_ID_OFF], &block_id, BLOCK_ID_SIZE);
}

NodeId::~NodeId() {}

bool NodeId::operator==(const NodeId& other) const noexcept {
    return std::memcmp(buff_, other.get_full(), ID_SIZE) == 0;
}

std::string NodeId::to_string() const {

    // first PATH_SIZE bytes → hex string
    std::string hex;
    hex.reserve(PATH_SIZE * 2);
    for (int i = 0; i < PATH_SIZE; ++i) {
        hex += std::format("{:02X}", buff_[i]);
    }

    std::uint8_t u8 = buff_[LEVEL_OFF];

    std::uint16_t u16;
    std::memcpy(&u16, &buff_[BLOCK_ID_OFF], 2);

    return std::format(
        "id={}, u8={}, u16={}",
        hex, u8, u16
    );
}


uint16_t NodeId::get_block_id() const { 
    uint16_t id;
    std::memcpy(&id, buff_ + BLOCK_ID_OFF, BLOCK_ID_SIZE);
    return id;
}

void NodeId::set_block_id(uint16_t id) {
    std::memcpy(buff_ + BLOCK_ID_OFF, &id, BLOCK_ID_SIZE);
}

uint8_t NodeId::get_level() const { return buff_[LEVEL_OFF]; }
void NodeId::set_level(uint8_t level) { buff_[LEVEL_OFF] = level; }
void NodeId::increment_level() { buff_[LEVEL_OFF] += 1; }

void NodeId::set_child_nibble(byte nib) { 
    buff_[buff_[LEVEL_OFF]] = nib; 
}
void NodeId::set_self_nibble(byte nib) { 
    buff_[buff_[LEVEL_OFF] - 1] = nib; 
}

int NodeId::cmp(const Hash* b) {
    return std::memcmp(buff_, b->h, buff_[LEVEL_OFF]);
}

size_t NodeId::size() const { return ID_SIZE; }

const byte* NodeId::get_full() const { 
    return buff_; 
}

std::array<byte, ID_SIZE> NodeId::get_full_array() const {
    std::array<byte, ID_SIZE> buffer;
    std::memcpy(buffer.data(), buff_, ID_SIZE);
    return buffer; 
}
