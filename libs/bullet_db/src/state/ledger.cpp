#include "verkle.h"

LedgerState::LedgerState(
    std::string path, 
    size_t cache_size, 
    size_t map_size,
    blst_scalar secret_sk
) : 
    db_(path.data(), map_size), 
    cache_(cache_size), 
    srs_(ORDER, secret_sk)
{ }


Ledger::Ledger(
    std::string path, 
    size_t cache_size, 
    size_t map_size,
    blst_scalar secret_sk
) : 
    state_(path, cache_size, map_size, secret_sk) 
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

SRS* Ledger::get_srs() { return &state_.srs_;}

bool Ledger::key_value_exists(ByteSlice &key, Commitment &val_c) {
    Hash h = derive_k_vc_hash(key, val_c);
    return value_exists(h);
}

bool Ledger::value_exists(Hash &hash) {
    state_.db_.start_txn();
    int res = state_.db_.exists(hash.data(), hash.size());
    state_.db_.end_txn();
    return res == 0;
}

optional<ByteSlice> Ledger::get_value(ByteSlice &key) {
    optional<ByteSlice> res;
    if (state_.root_) {

        state_.db_.start_txn();

        auto search_res = state_.root_->search(*this, key);
        if (search_res.has_value()) {

            Hash hash = derive_k_vc_hash(key, search_res.value());

            void* ptr;
            size_t size;

            int rc = state_.db_.get(
                hash.data(), hash.size(), 
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

optional<Commitment> Ledger::put(ByteSlice &key, Commitment &val_commitment) {
    optional<Commitment> root_c = std::nullopt;
    if (!key_value_exists(key, val_commitment)) {
        if (state_.root_) {
            state_.db_.start_txn();
            int rc;

            ByteSlice nibbles(key);
            auto c_opt = state_.root_->put(*this, nibbles, key, val_commitment);
            if (c_opt.has_value()) {
                root_c = *c_opt.value();

                Hash k_v_hash = derive_k_vc_hash(key, val_commitment);
                auto c_bytes = compress_p1(val_commitment);

                // save new value
                rc = state_.db_.put(
                    k_v_hash.data(), k_v_hash.size(), 
                    c_bytes.data(), c_bytes.size());

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
    return root_c;
}

optional<Commitment> Ledger::virtual_put(ByteSlice &key, Commitment &val_commitment) {
    optional<Commitment> root_c = std::nullopt;
    if (!key_value_exists(key, val_commitment)) {
        if (state_.root_) {
            state_.db_.start_txn();
            root_c = state_.root_->virtual_put(*this, ByteSlice(key), key, val_commitment);
            state_.db_.end_txn();
        }
    }
    return root_c;
}

optional<Commitment> Ledger::remove(ByteSlice &key) {
    optional<Commitment> root_c = std::nullopt;
    if (state_.root_) {
        state_.db_.start_txn();
        auto res = state_.root_->remove(*this, key);
        int rc = 1;
        if (res.has_value()) {
            auto [root_c, removed, path_ext] = res.value();
            rc = 0;
        }
        state_.db_.end_txn(rc);
    }
    return root_c;
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
        state_.db_.put(
            expired_key.data(), expired_key.size(), 
            node_bytes.data(), node_bytes.size()
        );
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
