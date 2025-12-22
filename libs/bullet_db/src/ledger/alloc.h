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
#include <string>

class NodeAllocator {
public:
    NodeAllocator(
        std::string path,
        size_t cache_size,
        size_t map_size
    );
    ~NodeAllocator();

    Node* root_;
    LRUCache<NodeId, Node*, NodeIdHash> cache_;
    BulletDB db_;

    Result<Node*, int> load_node(const NodeId* id);
    int recache(const NodeId& old_id, Node* node);
    int cache_node(Node* node);
    Result<Node*, int> delete_node(const NodeId& id);

    int rename_value(const NodeId& old_id, const NodeId& new_id);
};

