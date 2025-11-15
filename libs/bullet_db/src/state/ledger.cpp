#include "nodes.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>

LedgerState::LedgerState(std::string path, size_t cache_size, size_t map_size) : 
    db_(path.data(), map_size), 
    cache_(cache_size) {
}

Ledger::Ledger(std::string path, size_t cache_size, size_t map_size) : 
    state_(path, cache_size, map_size) 
{
    uint64_array root_id = u64_to_array(1);
    state_.db_.start_txn();
    if (Node* r = load_node(root_id)) {
        // uncache it
        state_.root_ = state_.cache_.remove(1);
    } else {
        Branch root(root_id, std::nullopt);

        auto root_bytes = root.to_bytes();

        int rc = state_.db_.put(
            root_id.data(), root_id.size(), 
            root_bytes.data(), root_bytes.size()
        );

        state_.root_ = std::move(std::make_unique<Branch>(std::move(root)));
        state_.db_.end_txn(rc);
        return;
    }
    state_.db_.end_txn(0);
}

Ledger::~Ledger() {
    auto root_bytes = state_.root_->to_bytes();
    auto root_id = state_.root_->get_id();

    state_.db_.start_txn();
    int rc = state_.db_.put(
        root_id->data(), root_id->size(), 
        root_bytes.data(), root_bytes.size()
    );
    state_.db_.end_txn(rc);
}



inline bool Ledger::value_exists(Hash &val_hash) {
    state_.db_.start_txn();
    int res = state_.db_.exists(val_hash.data(), val_hash.size());
    state_.db_.end_txn();
    return res == 0;
}

bool Ledger::key_value_exists(ByteSlice &key, Hash &val_hash) {
    Hash key_value = derive_leaf_hash(key, val_hash);
    return value_exists(key_value);
}

optional<ByteSlice> Ledger::get_value(ByteSlice &key) {
    optional<ByteSlice> res;
    if (state_.root_) {

        state_.db_.start_txn();

        auto search_res = state_.root_->search(*this, key);
        if (search_res.has_value()) {

            Hash val_hash = search_res.value();

            void* ptr;
            size_t size;

            int rc = state_.db_.get(
                val_hash.data(), val_hash.size(), 
                &ptr, &size
            );

            state_.db_.end_txn(rc);

            if (rc != 0) return std::nullopt;

            return ByteSlice(reinterpret_cast<byte*>(ptr), size);
        }

        state_.db_.end_txn();
    }
    return std::nullopt;
}

optional<Hash> Ledger::put(ByteSlice &key, ByteSlice &value) {
    Hash val_hash = derive_value_hash(value.data(), value.size());
    optional<Hash> root_hash = std::nullopt;

    if (!value_exists(val_hash)) {
        if (state_.root_) {

            state_.db_.start_txn();
            int rc;

            ByteSlice nibbles(key);
            auto hash_opt = state_.root_->put(*this, nibbles, key, val_hash);
            if (hash_opt.has_value()) {
                root_hash = hash_opt.value();

                // save new value
                rc = state_.db_.put(
                    val_hash.data(), val_hash.size(), 
                    value.data(), value.size());

                if (rc == 0) {
                    // save new root
                    // TODO -- delete old root?
                    auto root_bytes = state_.root_->to_bytes();
                    rc = state_.db_.put(
                        state_.root_->get_id()->data(), 8, 
                        root_bytes.data(), root_bytes.size());
                }

            }
            state_.db_.end_txn(rc);

            if (rc != 0) return std::nullopt;
        }
    }
    return root_hash;
}

optional<Hash> Ledger::virtual_put(ByteSlice &key, ByteSlice &value) {
    Hash val_hash = derive_value_hash(value.data(), value.size());
    optional<Hash> root_hash = std::nullopt;
    if (!value_exists(val_hash)) {
        if (state_.root_) {

            state_.db_.start_txn();

            ByteSlice nibbles(key);
            auto hash_opt = state_.root_->virtual_put(*this, nibbles, key, val_hash);
            if (hash_opt.has_value()) {
                root_hash = hash_opt.value();
            }
            state_.db_.end_txn();
        }
    }
    return root_hash;
}

optional<Hash> Ledger::remove(ByteSlice &key) {
    optional<Hash> root_hash = std::nullopt;
    if (state_.root_) {
        state_.db_.start_txn();
        auto hash_opt = state_.root_->remove(*this, key);
        if (hash_opt.has_value()) {
            auto [root_hash, path_ext] = hash_opt.value();
            state_.db_.end_txn();
        } else {
            state_.db_.end_txn(1);
        }
    }
    return root_hash;
}

void Ledger::cache_node(std::unique_ptr<Node> node) {
    uint64_t id = u64_from_array(*node->get_id());
    auto put_res = state_.cache_.put(id, std::move(node));
    if (put_res.has_value()) {

        // move the tuple
        auto expired = std::move(put_res.value());
        auto& expired_id = std::get<0>(expired);

        // move out the unique_ptrA
        std::unique_ptr<Node> expired_node = std::move(std::get<1>(expired)); 

        uint64_array expired_key = u64_to_array(expired_id);
        auto node_bytes = expired_node->to_bytes();
        assert(state_.db_.put(
            expired_key.data(), expired_key.size(), 
            node_bytes.data(), node_bytes.size()
        ) == 0);
    }
}

Node* Ledger::load_node(const NodeId &id) {
    uint64_t id_num = u64_from_array(id);
    if (Node* node = state_.cache_.get(id_num)) return node;

    void* ptr;
    size_t size;
    if (state_.db_.get(id.data(), id.size(), &ptr, &size) == 0) {
        ByteSlice raw_node(reinterpret_cast<byte*>(ptr), size);

        std::unique_ptr<Node> node_ptr;

        if (raw_node[0] == BRANCH)
            node_ptr = std::make_unique<Branch>(id, &raw_node);
        else if (raw_node[0] == EXT)
            node_ptr = std::make_unique<Extension>(id, &raw_node);
        else
            node_ptr = std::make_unique<Leaf>(id, &raw_node);

        // Insert into cache (cache now owns the unique_ptr)
        Node* raw = node_ptr.get(); // temporary raw pointer for return
        cache_node(std::move(node_ptr));

        return raw;
    }
    return nullptr;
}

optional<Node_ptr> Ledger::delete_node(const NodeId &id) {
    uint64_t num_id = u64_from_array(id);
    Node_ptr entry = state_.cache_.remove(num_id);
    if (!entry) {
        if (load_node(id)) 
            entry = state_.cache_.remove(num_id);
    }
    if (entry) {
        state_.db_.del(id.data(), 8);
    }
    return entry;
}

Branch* Ledger::new_cached_branch(uint64_t id) {
    auto ptr = std::make_unique<Branch>(u64_to_array(id), std::nullopt);
    Branch* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}

Extension* Ledger::new_cached_extension(uint64_t id) {
    auto ptr = std::make_unique<Extension>(u64_to_array(id), std::nullopt);
    Extension* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}

Leaf* Ledger::new_cached_leaf(uint64_t id) {
    auto ptr = std::make_unique<Leaf>(u64_to_array(id), std::nullopt);
    Leaf* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}
