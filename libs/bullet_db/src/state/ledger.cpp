#include "nodes.h"
#include <cstdint>
#include <memory>
#include <optional>

// TODO need start_trx and end_trx on all state_.db_ actions...

LedgerState::LedgerState(std::vector<char> path, size_t cache_size) : 
    db_(path.data(), path.size()), 
    cache_(cache_size) {}

Ledger::Ledger(std::vector<char> path, size_t cache_size) : 
    state_(path, cache_size) 
{
    uint64_array root_id = u64_to_array(1);
    if (Node* r = load_node(root_id)) {
        // uncache it
        Node root = state_.cache_.remove(root_id).value();
        state_.root_ = std::make_unique<Node>(root);
    } else {
        Branch root(root_id, std::nullopt);

        auto root_bytes = root.to_bytes();
        state_.db_.put(
            root_id.data(), root_id.size(), 
            root_bytes.data(), root_bytes.size()
        );

        state_.root_ = std::make_unique<Node>(root);
    }
}

Ledger::~Ledger() {
    auto root_bytes = state_.root_->to_bytes();
    auto root_id = state_.root_->get_id();
    state_.db_.put(
        root_id->data(), root_id->size(), 
        root_bytes.data(), root_bytes.size()
    );
}



inline bool Ledger::value_exists(Hash &val_hash) {
    return state_.db_.exists(val_hash.data(), val_hash.size());
}

bool Ledger::key_value_exists(ByteSlice &key, Hash &val_hash) {
    Hash key_value = derive_leaf_hash(key, val_hash);
    return value_exists(key_value);
}

optional<ByteSlice> Ledger::get_value(ByteSlice &key) {
    optional<ByteSlice> res;
    if (state_.root_) {
        auto search_res = state_.root_->search(*this, key);
        if (search_res.has_value()) {

            Hash val_hash = search_res.value();

            void* ptr;
            size_t size;

            state_.db_.get(
                val_hash.data(), val_hash.size(), 
                &ptr, &size
            );

            return ByteSlice(reinterpret_cast<byte*>(ptr), size);
        }
    }
    return std::nullopt;
}

optional<Hash> Ledger::put(ByteSlice &key, ByteSlice &value) {
    Hash val_hash = derive_value_hash(value.data(), value.size());
    optional<Hash> root_hash = std::nullopt;

    if (!value_exists(val_hash)) {
        if (state_.root_) {
            ByteSlice nibbles(key);
            auto hash_opt = state_.root_->put(*this, nibbles, key, val_hash);
            if (hash_opt.has_value()) {
                root_hash = hash_opt.value();

                // save new value
                state_.db_.put(
                    val_hash.data(), val_hash.size(), 
                    value.data(), value.size());

                // save new root
                // TODO -- delete old root?
                auto root_bytes = state_.root_->to_bytes();
                state_.db_.put(
                    root_hash.value().data(), root_hash.value().size(), 
                    root_bytes.data(), root_bytes.size());
            }
        }
    }
    return root_hash;
}

optional<Hash> Ledger::virtual_put(ByteSlice &key, ByteSlice &value) {
    Hash val_hash = derive_value_hash(value.data(), value.size());
    optional<Hash> root_hash = std::nullopt;
    if (!value_exists(val_hash)) {
        if (state_.root_) {
            ByteSlice nibbles(key);
            auto hash_opt = state_.root_->virtual_put(*this, nibbles, key, val_hash);
            if (hash_opt.has_value()) {
                root_hash = hash_opt.value();
            }
        }
    }
    return root_hash;
}

optional<Hash> Ledger::remove(ByteSlice &key) {
    optional<Hash> root_hash = std::nullopt;
    if (state_.root_) {
        auto hash_opt = state_.root_->remove(*this, key);
        if (hash_opt.has_value()) {
            auto [root_hash, path_ext] = hash_opt.value();
        }
    }
    return root_hash;
}

void Ledger::cache_node(uint64_t id, Node node) {
    auto put_res = state_.cache_.put(u64_to_array(id), node);
    if (put_res.has_value()) {
        auto [expired_key, expired_node] = put_res.value();
        auto node_bytes = expired_node.to_bytes();
        state_.db_.put(
            expired_key.data(), expired_key.size(), 
            node_bytes.data(), node_bytes.size()
        );
    }
}

Node* Ledger::load_node(NodeId &id) {
    if (Node* node = state_.cache_.get(id)) return node;

    void* ptr;
    size_t size;
    if (state_.db_.get(id.data(), id.size(), &ptr, &size) == 0) {
        ByteSlice raw_node(reinterpret_cast<byte*>(ptr), size);

        Node node;
        if (raw_node[0] == BRANCH) {
            node = Branch(id, &raw_node);
        } else if (raw_node[0] == EXT) {
            node = Extension(id, &raw_node);
        } else {
            node = Leaf(id, &raw_node);
        }
        Node* node_ptr = &node;
        cache_node(u64_from_array(id), node);

        return node_ptr;
    }
    return nullptr;
}

optional<Node> Ledger::delete_node(NodeId &id) {
    optional<Node> entry = state_.cache_.remove(id);
    state_.db_.del(id.data(), id.size());
    return entry;
}

Branch* Ledger::new_cached_branch(uint64_t id) {
    Branch branch(u64_to_array(id), std::nullopt);
    Branch* branch_ptr = &branch;
    cache_node(id, branch);
    return branch_ptr;
}

Extension* Ledger::new_cached_extension(uint64_t id) {
    Extension ext(u64_to_array(id), std::nullopt);
    Extension* ext_ptr = &ext;
    cache_node(id, ext);
    return ext_ptr;
}

Leaf* Ledger::new_cached_leaf(uint64_t id) {
    Leaf leaf(u64_to_array(id), std::nullopt);
    Leaf* leaf_ptr = &leaf;
    cache_node(id, leaf);
    return leaf_ptr;
}
