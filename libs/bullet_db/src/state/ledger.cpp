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

//  =======================================================
//  |             META STRUCTURE OF STATE TRIE            |
//  |=====================================================|
//  |    root_  = 256^0 == 1                BRANCH        |
//  |    l1_    = 256^1 == 256              BRANCHES      |
//  |    l2_    = 256^2 == 65,536           BRANCHES      |
//  |    l3_    = 256^3 == 16,777,216       LEAVES        |
//  |    l4_    = 256^4 == 4,294,967,296    VALUE HASHES  |
//  =======================================================


#include "verkle.h"
#include <future>


//////////////////////////////////
///////// STRUCTORS /////////////
////////////////////////////////

Ledger::Ledger(
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
{
    NodeId root_id = u64_to_id(1);
    int rc = 0;
    db_.start_txn();
    if (Node* r = load_node(root_id)) {
        // uncache to prevent unintentional invalidation
        root_ = cache_.remove(1);
    } else {
        auto root = create_branch(root_id, std::nullopt);

        auto root_bytes = root->to_bytes();

        rc = db_.put(
            root_id.data(), root_id.size(), 
            root_bytes.data(), root_bytes.size()
        );

        root_ = std::move(root);
    }
    db_.end_txn(rc);
}

Ledger::~Ledger() {
    auto root_bytes = root_->to_bytes();
    auto root_id = root_->get_id();

    db_.start_txn();
    int rc = db_.put(
        root_id->data(), root_id->size(), 
        root_bytes.data(), root_bytes.size()
    );
    db_.end_txn(rc);
}

////////////////////////////////
///////// GETTERS /////////////
//////////////////////////////

scalar_vec* Ledger::get_poly() {
    for (blst_scalar &c: poly_) for (byte &b: c.b) b = 0;
    return &poly_;
}
const SRS* Ledger::get_srs() const { return &srs_;}
void Ledger::set_srs(
    std::vector<blst_p1> &g1s,
    std::vector<blst_p2> &g2s
) { 
    for (auto i = 0; i < g1s.size(); i++) {
        srs_.g1_powers_jacob[i] = g1s[i];
        blst_p1_to_affine(&srs_.g1_powers_aff[i], &srs_.g1_powers_jacob[i]);

        srs_.g2_powers_jacob[i] = g2s[i];
        blst_p2_to_affine(&srs_.g2_powers_aff[i], &srs_.g2_powers_jacob[i]);
    }
}
const std::string* Ledger::get_tag() const { return &tag_; }
void Ledger::set_tag(std::string &tag) { tag_ = tag; }


////////////////////////////////
///////// QUERIES /////////////
//////////////////////////////

bool Ledger::key_value_exists(
    const Hash &key_hash,
    const Hash &val_hash
) { 
    return value_exists(derive_kv_hash(key_hash, val_hash)); 
}

bool Ledger::value_exists(const Hash &hash) {
    db_.start_txn();
    int res = db_.exists(hash.data(), hash.size());
    db_.end_txn();
    return res == 0;
}

std::optional<ByteSlice> Ledger::get_value(
    ByteSlice &key, 
    byte idx
) {
    std::optional<ByteSlice> res;
    if (root_) {

        Hash key_hash = derive_hash(key);
        key_hash.back() = idx;

        db_.start_txn();
        std::optional<Hash> search_res = root_->search(*this, key_hash);
        if (search_res.has_value()) {

            Hash hash = derive_kv_hash(key_hash, search_res.value());

            void* ptr; size_t size;
            int rc = db_.get(
                hash.data(), hash.size(), 
                &ptr, &size
            );

            db_.end_txn(rc);

            if (rc != 0) return std::nullopt;

            return ByteSlice(reinterpret_cast<byte*>(ptr), size);
        }

        db_.end_txn();
    }
    return std::nullopt;
}

std::optional<std::tuple<
    Commitment, Proof, 
    std::vector<Commitment>, 
    std::vector<scalar_vec>, 
    scalar_vec
>> Ledger::get_existence_proof(
    ByteSlice &key, 
    uint8_t idx
) {
    const size_t MAX_LEVELS = 4; // over 4 billion accounts == 256^4
    if (!root_) return std::nullopt;

    std::vector<scalar_vec> Fxs; Fxs.reserve(MAX_LEVELS);
    Bitmap Zs;

    Hash key_hash = derive_hash(key);
    key_hash.back() = idx;

    // Get components for proving Fxs, and Zs
    db_.start_txn();
    int ok = root_->build_commitment_path(*this, key_hash, ByteSlice(key_hash), Fxs, Zs);
    db_.end_txn();
    if (!ok) return std::nullopt;


    // Z bitmap to scalar_vec
    scalar_vec Zs_vec; Zs_vec.reserve(Zs.count());
    for (uint64_t k = 0; k < ORDER; k++)
        if (Zs.is_set(k))
            Zs_vec.push_back(new_scalar(k));


    // for each Fx solve at all Zs
    std::vector<scalar_vec> Ys_mat(Fxs.size(), scalar_vec(Zs.count(), new_scalar())); 
    for (uint64_t i = 0; i < Fxs.size(); i++)
        for (auto k = 0; k < Zs_vec.size(); k++) 
            Ys_mat[i][k] = eval_poly(Fxs[i], Zs_vec[k]);


    // Build Commits via Fxs in parrallel
    std::vector<Commitment> Cs(Fxs.size(), new_p1());
    std::vector<std::future<void>> futures; futures.reserve(Fxs.size());
    for (size_t i = 0; i < Fxs.size(); i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            commit_g1_projective_in_place(Fxs[i], srs_, &Cs[i]);
        }));
    }
    for (auto& f : futures) f.get();

    // Build aggregate Commitment and Proof
    auto [C, Pi] = multi_func_multi_point_prover(
        Fxs, Cs, Zs_vec, Ys_mat, srs_
    );
    return std::make_tuple(C, Pi, Cs, Ys_mat, Zs_vec);
}

//////////////////////////////////
///////// EXECUTORS /////////////
////////////////////////////////

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
        if (root_) {

            db_.start_txn();
            int rc;

            auto c_opt = root_->put(*this, ByteSlice(key_hash), key_hash, val_hash);
            if (c_opt.has_value()) {
                root_c = *c_opt.value();

                Hash k_v_hash = derive_kv_hash(key_hash, val_hash);

                // save new value
                rc = db_.put(
                    k_v_hash.data(), k_v_hash.size(), 
                    value.data(), value.size());

                if (rc == 0) {
                    // save new root
                    // TODO -- delete old root?
                    auto root_bytes = root_->to_bytes();
                    rc = db_.put(
                        root_->get_id()->data(), 8, 
                        root_bytes.data(), root_bytes.size());
                }
            }
            db_.end_txn(rc);
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
        if (root_) {
            db_.start_txn();
            root_c = root_->virtual_put(
                *this, ByteSlice(key_hash), 
                key_hash, val_hash
            );
            db_.end_txn();
        }
    }
    return root_c;
}

std::optional<Commitment> Ledger::remove(
    ByteSlice &key, 
    uint8_t idx
) {
    std::optional<Commitment> root_c = std::nullopt;
    if (root_) {
        Hash key_hash = derive_hash(key);
        key_hash.back() = idx;

        db_.start_txn();
        auto res = root_->remove(*this, ByteSlice(key_hash), key_hash);
        if (res.has_value()) {
            auto [root_c, removed] = res.value();
            db_.end_txn(0);
        } else db_.end_txn(1);
    }
    return root_c;
}


////////////////////////////////////////////////////////
///////    STATE / NODE RELATED FUNCTIONS    //////////
//////////////////////////////////////////////////////

void Ledger::cache_node(std::unique_ptr<Node> node) {

    uint64_t id = u64_from_id(*node->get_id());
    auto put_res = cache_.put(id, std::move(node));
    if (put_res.has_value()) {

        // move the tuple
        auto expired = std::move(put_res.value());
        auto& expired_id = std::get<0>(expired);

        // move out the unique_ptrA
        Node_ptr expired_node = std::move(std::get<1>(expired)); 

        NodeId expired_key = u64_to_id(expired_id);
        auto node_bytes = expired_node->to_bytes();
        db_.put(
            expired_key.data(), expired_key.size(), 
            node_bytes.data(), node_bytes.size()
        );
    }
}

Node* Ledger::load_node(const NodeId &id) {

    uint64_t id_num = u64_from_id(id);
    if (Node* node = cache_.get(id_num)) return node;

    void* ptr; size_t size;
    int rc = db_.get(id.data(), id.size(), &ptr, &size);
    if (rc == 0) {
        ByteSlice raw_node(reinterpret_cast<byte*>(ptr), size);

        std::unique_ptr<Node> node_ptr;

        if (raw_node[0] == BRANCH)
            node_ptr = create_branch(id, &raw_node);
        else
            node_ptr = create_leaf(id, &raw_node);

        // Insert into cache (cache now owns the unique_ptr)
        Node* raw = node_ptr.get(); // temporary raw pointer for return
        cache_node(std::move(node_ptr));

        return raw;
    }
    return nullptr;
}

std::optional<Node_ptr> Ledger::delete_node(const NodeId &id) {

    uint64_t num_id = u64_from_id(id);
    Node_ptr entry = cache_.remove(num_id);
    if (!entry) {
        if (load_node(id)) 
            entry = cache_.remove(num_id);
    }
    if (entry) {
        db_.del(id.data(), 8);
    }
    return entry;
}

bool Ledger::delete_value(const Hash &kv) {
    return db_.del(kv.data(), kv.size()) == 0;
}

Branch_i* Ledger::new_cached_branch(uint64_t id) {
    auto ptr = create_branch(u64_to_id(id), std::nullopt);
    Branch_i* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}

Leaf_i* Ledger::new_cached_leaf(uint64_t id) {
    auto ptr = create_leaf(u64_to_id(id), std::nullopt);
    Leaf_i* raw = ptr.get();
    cache_node(std::move(ptr));
    return raw;
}
