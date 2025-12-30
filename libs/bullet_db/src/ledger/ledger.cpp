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

#include "ledger.h"
#include "branch.h"
#include "state_types.h"

Ledger::Ledger(
    std::string path, 
    size_t cache_size, 
    size_t map_size,
    std::string tag,
    blst_scalar secret_sk
) : 
    gadgets_(init_gadgets(
        BRANCH_ORDER, 
        secret_sk, tag, 
        path, cache_size, map_size
    ))
{
    shard_prefix_.reserve(32);
}

Ledger::~Ledger() {}

const Gadgets_ptr Ledger::get_gadgets() const { return gadgets_; }


Result<Node_ptr, int> Ledger::get_root( 
    uint16_t block_id, 
    uint16_t prev_block_id
) {

    NodeId id(ROOT_NODE_ID, block_id);
    Result<Node_ptr, int> res = gadgets_->alloc.load_node(&id);
    if (res.is_err() && res.unwrap_err() == MDB_NOTFOUND) {

        // fetch prev_blocks root
        NodeId prev_root_id(ROOT_NODE_ID, prev_block_id);
        Node_ptr prev_root_node = nullptr;

        Result<Node_ptr, int> res = gadgets_->alloc.load_node(&prev_root_id);
        if (res.is_err() && res.unwrap_err() == MDB_NOTFOUND) {
            prev_root_node = create_branch(gadgets_, &prev_root_id, nullptr);
            gadgets_->alloc.cache_node(prev_root_node);
        } else {
            prev_root_node = res.unwrap();
        }

        std::vector<byte> cannon_bytes = prev_root_node->to_bytes();
        ByteSlice bytes(cannon_bytes);

        Node_ptr node = create_branch(gadgets_, &id, &bytes);
        gadgets_->alloc.cache_node(node);

        return node;

    } else if (res.is_err()) {
        return res.unwrap_err();
    }
    return res.unwrap();
}

bool Ledger::in_shard(const Hash h) {
    size_t matched = 0;
    size_t path_size = shard_prefix_.size();

    while (matched < path_size && matched < 32) {
        if (shard_prefix_[matched] != h.h[matched]) {
            break;
        }
        ++matched;
    }
    return matched == path_size;
}

int Ledger::store_value( 
    const Hash &key_hash,
    const ByteSlice& value,
    uint16_t block_id
) {

    // determine leaf value node id and then insert value with block_id
    uint64_t node_id = 1;
    for (int i{}; i < 6; i++) {
        node_id *= BRANCH_ORDER;
        node_id += key_hash.h[i];
    }

    NodeId id(node_id, block_id);

    void* trx = gadgets_->alloc.db_.start_txn();
    int rc = gadgets_->alloc.db_.put(
        id.get_full(), id.size(), 
        value.data(), value.size(), 
        trx
    );
    gadgets_->alloc.db_.end_txn(trx, rc);

    return rc;
}

int Ledger::delete_value( 
    const Hash &key_hash,
    uint16_t block_id
) {

    // determine leaf value node id and then insert value with block_id
    uint64_t node_id = 1;
    for (int i{}; i < 6; i++) {
        node_id *= BRANCH_ORDER;
        node_id += key_hash.h[i];
    }

    NodeId id(node_id, block_id);

    void* trx = gadgets_->alloc.db_.start_txn();
    int rc = gadgets_->alloc.db_.del(
        id.get_full(), id.size(), 
        trx
    );
    gadgets_->alloc.db_.end_txn(trx, rc);
    if (rc == MDB_NOTFOUND) return NOT_EXIST;
    return rc;
}

int Ledger::put(
    const ByteSlice &key, 
    const Hash &val_hash, 
    uint8_t idx,
    uint16_t block_id,
    uint16_t prev_block_id
) {
    Hash key_hash;
    derive_hash(key_hash.h, key);
    key_hash.h[32-1] = idx;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    Result<Node_ptr, int> root = get_root(block_id, prev_block_id);
    if (root.is_err()) return root.unwrap_err();

    return root.unwrap()->put(
        &key_hash, &val_hash, 
        block_id, 0
    );
}

int Ledger::replace(
    const ByteSlice &key, 
    const Hash &val_hash, 
    const Hash &prev_val_hash, 
    uint8_t idx,
    uint16_t block_id,
    uint16_t prev_block_id
) {
    Hash key_hash;
    derive_hash(key_hash.h, key);
    key_hash.h[32-1] = idx;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    Result<Node_ptr, int> root = get_root(block_id, prev_block_id);
    if (root.is_err()) return root.unwrap_err();

    return root.unwrap()->replace(
        &key_hash, &val_hash, &prev_val_hash,
        block_id, 0
    );
}


int Ledger::create_account(
    const ByteSlice &key, 
    uint16_t block_id
) {
    Hash key_hash;
    derive_hash(key_hash.h, key);
    key_hash.h[32-1] = 0;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    Result<Node_ptr, int> root = get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    return root.unwrap()->create_account(&key_hash, block_id, 0);
}

int Ledger::delete_account(
    const ByteSlice &key, 
    uint16_t block_id
) {
    Hash key_hash;
    derive_hash(key_hash.h, key);
    key_hash.h[32-1] = 0;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    Result<Node_ptr, int> root = get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    return root.unwrap()->delete_account(&key_hash, block_id, 0);
}
