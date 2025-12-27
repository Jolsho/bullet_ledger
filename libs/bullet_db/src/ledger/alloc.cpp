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

#include "branch.h"
#include "leaf.h"
#include "node.h"
#include <cassert>
#include <cstddef>
#include <cstdio>

NodeAllocator::NodeAllocator(
    std::string path, 
    size_t cache_size, 
    size_t map_size
) :
    db_(path.data(), map_size),
    cache_(cache_size),
    gadgets_(nullptr)
{
}

NodeAllocator::~NodeAllocator() {}

void NodeAllocator::persist_node(Node* node) {

    const NodeId* id = node->get_id();

    std::vector<byte> bytes = node->to_bytes();

    void* trx = db_.start_txn();
    int rc = db_.put(
        id->get_full(), id->size(), 
        bytes.data(), bytes.size(), 
        trx
    );
    assert(rc == OK);
    db_.end_txn(trx, rc);
}

void NodeAllocator::set_gadgets(std::shared_ptr<Gadgets> gadgets) { 
    gadgets_ = gadgets; 
}

Node_ptr NodeAllocator::cache_node(
    Node_ptr node, 
    bool needs_lock
) {
    if (needs_lock) mux_.lock();
    auto put_res = cache_.put(node->get_id(), node);
    if (needs_lock) mux_.unlock();

    if (put_res.has_value()) {
        // put_res can have an evicted node but
        // since destructor triggers pending persistence 
        // we dont need to persist the evicted here
        auto [id, n] = put_res.value();
        return n;
    }
    return nullptr;
}

int NodeAllocator::recache(
    const NodeId *old_id, 
    const NodeId *new_id, 
    bool needs_lock
) {
    if (needs_lock) mux_.lock();
    Node_ptr entry = cache_.remove(old_id);
    if (needs_lock) mux_.unlock();
    if (entry == nullptr) return NOT_EXIST_RECACHE;
    // if its not in cache we can just return OK
    // it will be deconstructed and therefore persisted...

    std::vector<byte> node_bytes = entry->to_bytes();
    void* trx = db_.start_txn();
    int rc = db_.put(
        old_id->get_full(), old_id->size(), 
        node_bytes.data(), node_bytes.size(), 
        trx
    );
    db_.end_txn(trx, rc);
    if (rc != OK) return rc;

    entry->set_id(new_id);
    Node_ptr maybe_evicted = cache_node(entry, needs_lock);

    return OK;
}


Result<Node_ptr, int> NodeAllocator::load_node(
    const NodeId* id, 
    bool needs_lock
) {

    if (needs_lock) mux_.lock_shared();
    Node_ptr* node = cache_.get(id);
    if (needs_lock) mux_.unlock_shared();
    if (node) return *node;

    std::vector<byte> out;
    void* out_data = nullptr;
    size_t size = 0;

    void* trx = db_.start_rd_txn();
    int rc = db_.get_raw(id->get_full(), id->size(), &out_data, &size, trx);
    if (rc == 0) {
        out.resize(size);
        std::memcpy(out.data(), out_data, size);
        free(out_data);  // avoid memory leak
    }
    db_.end_txn(trx, rc);

    if (rc == 0) {
        ByteSlice raw_node(out.data(), out.size());
        Node_ptr node_ptr;

        if (raw_node[0] == BRANCH)
            node_ptr = create_branch(gadgets_, id, &raw_node);
        else
            node_ptr = create_leaf(gadgets_, id, &raw_node);

        // Insert into cache (cache now owns the unique_ptr)
        cache_node(node_ptr, needs_lock);

        return node_ptr;
    }

    return rc;
}

Result<Node_ptr, int> NodeAllocator::delete_node(
    const NodeId &id,
    bool need_lock
) {
    if (need_lock) mux_.lock();

    Node_ptr entry = cache_.remove(id);
    if (!entry) {
        Result<Node_ptr, int> res = load_node(&id);
        if (res.is_ok()) {
            entry = cache_.remove(id);
        } else {
            return res.unwrap_err();
        }
    }
    if (entry) {
        void* trx = db_.start_txn();
        int rc = db_.del(id.get_full(), id.size(), trx);
        db_.end_txn(trx, rc);
        if (rc != OK) return rc;
    }
    return entry;
}
