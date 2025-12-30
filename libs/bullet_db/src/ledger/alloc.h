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
#include "node.h"
#include "db.h"
#include "lru.h"
#include "result.h"

struct Gadgets;

class NodeAllocator {
public:
    NodeAllocator(
        std::string path,
        size_t cache_size,
        size_t map_size
    );
    ~NodeAllocator();

    std::shared_mutex mux_;
    LRUCache<NodeId, std::shared_ptr<Node>, NodeIdHash> cache_;
    BulletDB db_;
    std::shared_ptr<Gadgets> gadgets_;

    void set_gadgets(std::shared_ptr<Gadgets> gadgets);

    Result<std::shared_ptr<Node>, int> load_node(
        const NodeId* id, 
        bool needs_lock = false
    );
    Result<std::shared_ptr<Node>, int> delete_node(
        const NodeId& id,
        bool needs_lock = false
    );
    int recache(const NodeId *old_id, const NodeId *new_id, bool needs_lock = false);
    Node_ptr cache_node(std::shared_ptr<Node> node, bool needs_lock = false);
    void persist_node(Node* node);
};
