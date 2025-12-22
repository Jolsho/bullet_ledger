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
#include "state_types.h"

const size_t NODE_ID_OFF = 0;
const size_t NODE_ID_SIZE = 8;

const size_t BLOCK_ID_OFF = NODE_ID_OFF + NODE_ID_SIZE;
const size_t BLOCK_ID_SIZE = 2;

NodeId::NodeId() {
    std::memset(buff_, 0, ID_SIZE);
}

NodeId::NodeId(const NodeId* other) {
    from_bytes(other->get_full());
}

NodeId::NodeId(uint64_t node_id, uint16_t block_id) {
    std::memcpy(buff_, &node_id, NODE_ID_SIZE);
    std::memcpy(buff_ + BLOCK_ID_OFF, &block_id, BLOCK_ID_SIZE);
}

NodeId::~NodeId() { free(buff_); }

bool NodeId::operator==(const NodeId& other) const noexcept {
    return std::memcmp(buff_, other.get_full(), ID_SIZE) == 0;
}


uint16_t NodeId::get_block_id() const { 
    uint16_t id;
    std::memcpy(&id, buff_ + BLOCK_ID_OFF, BLOCK_ID_SIZE);
    return id;
}

uint64_t NodeId::get_node_id() const { 
    uint64_t id;
    std::memcpy(&id, buff_ + NODE_ID_OFF, NODE_ID_SIZE);
    return id;
}
void NodeId::set_node_id(uint64_t id) {
    std::memcpy(buff_ + NODE_ID_OFF, &id, NODE_ID_SIZE);
}

size_t NodeId::size() const { return ID_SIZE; }

uint64_t NodeId::derive_child_id(const byte nib) const {

    uint64_t id;
    std::memcpy(&id, buff_ + NODE_ID_OFF, NODE_ID_SIZE);
    id += nib;
    id *= BRANCH_ORDER;

    return id;
}

const byte* NodeId::get_full() const { 
    return buff_; 
}

std::array<byte, ID_SIZE> NodeId::get_full_array() const {
    std::array<byte, ID_SIZE> buffer;
    std::memcpy(buffer.data(), buff_, ID_SIZE);
    return buffer; 
}

void NodeId::from_bytes(const byte* buff) {
    if (buff != nullptr) {
        std::memcpy(buff_, buff, ID_SIZE);
    }
}
