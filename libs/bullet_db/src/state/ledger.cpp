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

#include "verkle.h"
#include <future>

LedgerState::LedgerState(
    std::string path, 
    size_t cache_size, 
    size_t map_size,
    std::string tag,
    blst_scalar secret_sk
) : 
    db_(path.data(), map_size), 
    cache_(cache_size), 
    srs_(ORDER, secret_sk),
    poly_(ORDER, new_scalar()), 
    tag_(tag)
{ }


Ledger::Ledger(
    std::string path, 
    size_t cache_size, 
    size_t map_size,
    std::string tag,
    blst_scalar secret_sk
) : 
    state_(path, cache_size, map_size, tag, secret_sk) 
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
std::string* Ledger::get_tag() { return &state_.tag_; }

scalar_vec* Ledger::get_poly() {
    for (blst_scalar &c: state_.poly_)
        for (byte &b: c.b) 
            b = 0;
    return &state_.poly_;
}

bool Ledger::key_value_exists(
    const Hash &key_hash,
    const Hash &val_hash
) { 
    return value_exists(derive_kv_hash(key_hash, val_hash)); 
}

bool Ledger::value_exists(const Hash &hash) {
    state_.db_.start_txn();
    int res = state_.db_.exists(hash.data(), hash.size());
    state_.db_.end_txn();
    return res == 0;
}

std::optional<ByteSlice> Ledger::get_value(
    ByteSlice &key, 
    uint8_t idx
) {
    std::optional<ByteSlice> res;
    if (state_.root_) {

        Hash key_hash = derive_hash(key);
        key_hash.back() = idx;

        state_.db_.start_txn();
        std::optional<Hash> search_res = state_.root_->search(*this, key_hash);
        if (search_res.has_value()) {

            Hash hash = derive_kv_hash(key_hash, search_res.value());

            void* ptr; size_t size;
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

std::optional<std::tuple<
    Commitment, Proof, 
    vector<Commitment>, 
    vector<scalar_vec>, 
    scalar_vec
>> Ledger::get_existence_proof(
    ByteSlice &key, 
    uint8_t idx
) {
    const size_t MAX_LEVELS = 4; // over 4 billion accounts == 256^4
    if (!state_.root_) return std::nullopt;

    vector<scalar_vec> Fxs; Fxs.reserve(MAX_LEVELS);
    Bitmap Zs;

    Hash key_hash = derive_hash(key);
    key_hash.back() = idx;

    // Get components for proving Fxs, and Zs
    state_.db_.start_txn();
    int ok = state_.root_->build_commitment_path(*this, key_hash, ByteSlice(key_hash), Fxs, Zs);
    state_.db_.end_txn();
    if (!ok) return std::nullopt;


    // Z bitmap to scalar_vec
    scalar_vec Zs_vec; Zs_vec.reserve(Zs.count());
    for (uint64_t k = 0; k < ORDER; k++)
        if (Zs.is_set(k))
            Zs_vec.push_back(new_scalar(k));


    // for each Fx solve at all Zs
    vector<scalar_vec> Ys_mat(Fxs.size(), scalar_vec(Zs.count(), new_scalar())); 
    for (uint64_t i = 0; i < Fxs.size(); i++)
        for (auto k = 0; k < Zs_vec.size(); k++) 
            Ys_mat[i][k] = eval_poly(Fxs[i], Zs_vec[k]);


    // Build Commits via Fxs in parrallel
    std::vector<Commitment> Cs(Fxs.size(), new_p1());
    std::vector<std::future<void>> futures; futures.reserve(Fxs.size());
    for (size_t i = 0; i < Fxs.size(); i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            commit_g1_projective_in_place(Fxs[i], state_.srs_, &Cs[i]);
        }));
    }
    for (auto& f : futures) f.get();

    // Build aggregate Commitment and Proof
    auto [C, Pi] = multi_func_multi_point_prover(
        Fxs, Cs, Zs_vec, Ys_mat, state_.srs_
    );
    return std::make_tuple(C, Pi, Cs, Ys_mat, Zs_vec);
}


std::optional<Commitment> Ledger::put(
    ByteSlice &key, 
    ByteSlice &value, 
    uint8_t idx
) {
    std::optional<Commitment> root_c = std::nullopt;

    Hash key_hash = derive_hash(key);
    key_hash.back() = idx;

    Hash val_hash = derive_hash(value);

    if (!key_value_exists(key_hash, val_hash)) {
        if (state_.root_) {

            state_.db_.start_txn();
            int rc;

            auto c_opt = state_.root_->put(*this, ByteSlice(key_hash), key_hash, val_hash);
            if (c_opt.has_value()) {
                root_c = *c_opt.value();

                Hash k_v_hash = derive_kv_hash(key_hash, val_hash);

                // save new value
                rc = state_.db_.put(
                    k_v_hash.data(), k_v_hash.size(), 
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
    return root_c;
}

std::optional<Commitment> Ledger::virtual_put(
    ByteSlice &key, 
    ByteSlice &value, 
    uint8_t idx
) {
    std::optional<Commitment> root_c = std::nullopt;

    Hash key_hash = derive_hash(key);
    key_hash.back() = idx;

    Hash val_hash = derive_hash(value);

    if (!key_value_exists(key_hash, val_hash)) {
        if (state_.root_) {
            state_.db_.start_txn();
            root_c = state_.root_->virtual_put(
                *this, 
                ByteSlice(key_hash), 
                key_hash, 
                val_hash
            );
            state_.db_.end_txn();
        }
    }
    return root_c;
}

std::optional<Commitment> Ledger::remove(
    ByteSlice &key, 
    uint8_t idx
) {
    std::optional<Commitment> root_c = std::nullopt;
    if (state_.root_) {

        Hash key_hash = derive_hash(key);
        key_hash.back() = idx;

        state_.db_.start_txn();
        auto res = state_.root_->remove(*this, ByteSlice(key_hash), key_hash);
        if (res.has_value()) {
            auto [root_c, removed] = res.value();
            state_.db_.end_txn(0);

        } else state_.db_.end_txn(1);
    }
    return root_c;
}


////////////////////////////////////////////////////////
//////////////////  NODE RELATED   ////////////////////
//////////////////////////////////////////////////////

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
        else
            node_ptr = std::make_unique<Leaf>(id, &raw_node);

        // Insert into cache (cache now owns the unique_ptr)
        Node* raw = node_ptr.get(); // temporary raw pointer for return
        cache_node(std::move(node_ptr));

        return raw;
    }
    return nullptr;
}

std::optional<Node_ptr> Ledger::delete_node(const NodeId &id) {

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

bool Ledger::delete_value(const Hash &kv) {
    return state_.db_.del(kv.data(), kv.size()) == 0;
}

Branch* Ledger::new_cached_branch(uint64_t id) {
    auto ptr = std::make_unique<Branch>(u64_to_array(id), std::nullopt);
    Branch* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}

Leaf* Ledger::new_cached_leaf(uint64_t id) {
    auto ptr = std::make_unique<Leaf>(u64_to_array(id), std::nullopt);
    Leaf* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}
