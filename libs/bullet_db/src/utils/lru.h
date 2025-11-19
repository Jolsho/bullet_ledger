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
#include <list>
#include <unordered_map>
#include <optional>
#include <memory>

template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : cap(capacity) {}

    // Get returns a raw pointer to the value, or nullptr if not found
    Value* get(const Key& key) {
        auto it = map.find(key);
        if (it == map.end())
            return nullptr;

        // Move to front (MRU)
        order.splice(order.begin(), order, it->second);
        return it->second->second.get();  // raw pointer, does NOT transfer ownership
    }

    // Put a new value (wrapped automatically in unique_ptr)
    std::optional<std::tuple<Key, std::unique_ptr<Value>>> put(const Key& key, std::unique_ptr<Value> value) {
        auto it = map.find(key);
        if (it != map.end()) {
            // Update existing
            it->second->second = std::move(value);
            order.splice(order.begin(), order, it->second);
            return std::nullopt;
        }

        // New entry
        order.emplace_front(key, std::move(value));
        map[key] = order.begin();

        // Evict if above capacity
        if (map.size() > cap) {
            auto last = --order.end();
            map.erase(last->first);
            std::tuple<Key, std::unique_ptr<Value>> evicted {last->first, std::move(last->second)};
            order.pop_back();
            return evicted;
        }

        return std::nullopt;
    }

    // Remove and take ownership
    std::unique_ptr<Value> remove(const Key& key) {
        auto it = map.find(key);
        if (it == map.end()) return nullptr;

        std::unique_ptr<Value> val = std::move(it->second->second);
        order.erase(it->second);
        map.erase(it);
        return val;
    }

private:
    size_t cap;

    // Most-recently-used at front
    std::list<std::pair<Key, std::unique_ptr<Value>>> order;
    std::unordered_map<Key, typename std::list<std::pair<Key, std::unique_ptr<Value>>>::iterator> map;
};

