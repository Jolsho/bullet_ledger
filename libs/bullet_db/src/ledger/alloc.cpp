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

void NodeAllocator::set_gadgets(std::shared_ptr<Gadgets> gadgets) { 
    gadgets_ = gadgets; 
}

int NodeAllocator::cache_node(Node_ptr node) {
    auto put_res = cache_.put(node->get_id(), node);
    // put_res can have an evicted node but
    // since destructor triggers persistence 
    // we dont need to persist the evicted here
    return OK;
}

int NodeAllocator::recache(const NodeId *old_id, const NodeId *new_id) {
    Node_ptr entry = cache_.remove(old_id);
    if (entry == nullptr) return NOT_EXIST_RECACHE;
    // if its not in cache we can just return OK
    // it will be deconstructed and therefore persisted...

    std::vector<byte> node_bytes = entry->to_bytes();
    int rc = db_.put(
        entry->get_id()->get_full(), entry->get_id()->size(), 
        node_bytes.data(), node_bytes.size()
    );
    if (rc != OK) return rc;

    entry->set_id(new_id);
    return cache_node(entry);
}


Result<Node_ptr, int> NodeAllocator::load_node(const NodeId* id) {

    if (Node_ptr* node = cache_.get(id)) return *node;

    std::vector<std::byte> out;
    int rc = db_.get(id->get_full(), id->size(), out);
    if (rc == 0) {
        ByteSlice raw_node(reinterpret_cast<byte*>(out.data()), out.size());

        Node_ptr node_ptr;

        if (raw_node[0] == BRANCH)
            node_ptr = create_branch(gadgets_, id, &raw_node);
        else
            node_ptr = create_leaf(gadgets_, id, &raw_node);

        // Insert into cache (cache now owns the unique_ptr)
        int rc = cache_node(node_ptr);
        if (rc != OK) {
            printf("FAILED CACHE\n");
            return rc;
        }

        return node_ptr;
    }

    return rc;
}

Result<Node_ptr, int> NodeAllocator::delete_node(const NodeId &id) {

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
        int rc = db_.del(id.get_full(), id.size());
        if (rc != 0) return rc;
    }
    return entry;
}

int NodeAllocator::rename_value(const NodeId& old_id, const NodeId& new_id) {

    std::vector<std::byte> out;
    int res = db_.get(old_id.get_full(), old_id.size(), out);
    if (res != 0) return res;

    return db_.put(new_id.get_full(), new_id.size(), out.data(), out.size());
}

int NodeAllocator::persist_node(Node* node) {
    const NodeId* id = node->get_id();
    std::vector<byte> node_bytes = node->to_bytes();
    return db_.put(
        id->get_full(), id->size(), 
        node_bytes.data(), node_bytes.size()
    );
}
