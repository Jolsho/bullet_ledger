#include <list>
#include <unordered_map>
#include <optional>

template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : cap(capacity) {}

    // Get returns a pointer to the value, or nullptr if not found
    Value* get(const Key& key) {
        auto it = map.find(key);
        if (it == map.end())
            return nullptr;

        // Move to front (MRU)
        order.splice(order.begin(), order, it->second);
        return &it->second->second;  // return pointer
    }

    std::optional<std::tuple<Key, Value>> put(const Key& key, const Value& value) {
        auto it = map.find(key);
        if (it != map.end()) {
            // Update existing
            it->second->second = value;
            order.splice(order.begin(), order, it->second);
            return;
        }

        // New entry
        order.emplace_front(key, value);
        map[key] = order.begin();

        // Evict if above capacity
        if (map.size() > cap) {
            std::tuple<Key, Value> last = order.end();
            --last;
            map.erase(last->first);
            order.pop_back();
            return last;
        }
        return std::nullopt;
    }

    // Remove and take ownership
    std::optional<Value> remove(const Key& key) {
        auto it = map.find(key);
        if (it == map.end()) return std::nullopt;

        Value val = std::move(it->second->second);
        order.erase(it->second);
        map.erase(it);
        return val;
    }

private:
    size_t cap;

    // Most-recently-used at front
    std::list<std::pair<Key, Value>> order;
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> map;
};

