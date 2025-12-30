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

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : cap(capacity) {}

    Value* get(const Key& key) {
        auto it = map.find(key);
        if (it == map.end())
            return nullptr;

        order.splice(order.begin(), order, it->second);
        return &it->second->second;
    }

    std::optional<std::tuple<Key, Value>> put(const Key& key, Value value) {
        auto it = map.find(key);
        if (it != map.end()) {
            it->second->second = value;
            order.splice(order.begin(), order, it->second);
            return std::nullopt;
        }

        order.emplace_front(key, value);
        map.emplace(key, order.begin());

        if (map.size() > cap) {
            auto last = std::prev(order.end());
            map.erase(last->first);
            auto evicted = std::tuple<Key, Value>{last->first, last->second};
            order.pop_back();
            return evicted;
        }

        return std::nullopt;
    }

    Value remove(const Key& key) {
        auto it = map.find(key);
        if (it == map.end())
            return Value{};

        Value val = it->second->second;
        order.erase(it->second);
        map.erase(it);
        return val;
    }

private:
    size_t cap;
    std::list<std::pair<Key, Value>> order;
    std::unordered_map<Key,
        typename std::list<std::pair<Key, Value>>::iterator,
        Hash> map;
};

