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

#include "alloc.h"
#include "branch.h"
#include "leaf.h"

NodeAllocator::NodeAllocator(
    std::string path, 
    size_t cache_size, 
    size_t map_size
) :
    db_(path.data(), map_size),
    cache_(cache_size)
{
}

NodeAllocator::~NodeAllocator() {
    std::vector<byte> root_bytes = root_->to_bytes();
    const NodeId root_id = root_->get_id();

    db_.start_txn();
    int rc = db_.put(
        root_id.get_full(), root_id.size(), 
        root_bytes.data(), root_bytes.size()
    );
    db_.end_txn(rc);
}

int NodeAllocator::cache_node(Node* node) {

    auto put_res = cache_.put(node->get_id(), node);
    if (put_res.has_value()) {

        // move the tuple
        auto [id, node] = put_res.value();

        std::vector<byte> node_bytes = node->to_bytes();
        int rc = db_.put(
            id.get_full(), id.size(), 
            node_bytes.data(), node_bytes.size()
        );

        delete node;

        return rc;
    }
    return OK;
}

int NodeAllocator::recache(const NodeId &old_id, Node* node) {
    Node* entry = cache_.remove(old_id);
    return cache_node(node);
}


Result<Node*, int> NodeAllocator::load_node(const NodeId* id) {

    if (Node** node = cache_.get(id)) return *node;

    void* ptr; size_t size;
    int rc = db_.get(id->get_full(), id->size(), &ptr, &size);
    if (rc == 0) {
        ByteSlice raw_node(reinterpret_cast<byte*>(ptr), size);

        Node* node_ptr;

        if (raw_node[0] == BRANCH)
            node_ptr = create_branch(id, &raw_node);
        else
            node_ptr = create_leaf(id, &raw_node);

        // Insert into cache (cache now owns the unique_ptr)
        cache_node(node_ptr);

        return node_ptr;
    }
    return rc;
}

Result<Node*, int> NodeAllocator::delete_node(const NodeId &id) {

    Node* entry = cache_.remove(id);
    if (!entry) {
        Result<Node*, int> res = load_node(&id);
        if (res.is_ok()) {
            entry = cache_.remove(id);
        } else {
            return res.unwrap_err();
        }
    }
    if (entry) {
        int rc = db_.del(id.get_full(), id.size());
        if (rc != 0) return rc;
    }
    return entry;
}

int NodeAllocator::rename_value(const NodeId& old_id, const NodeId& new_id) {
        void* data; size_t size;

        int res = db_.get(old_id.get_full(), old_id.size(), &data, &size);
        if (res != 0) return res;

        return db_.put(new_id.get_full(), new_id.size(), data, size);

}
