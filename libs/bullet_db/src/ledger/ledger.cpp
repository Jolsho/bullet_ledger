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

#include "ledger.h"
#include "branch.h"
#include "hashing.h"
#include "kzg.h"
#include "state_types.h"
#include <cassert>
#include <cstdio>
#include <future>
#include <lmdb.h>

Ledger::Ledger(
    std::string path, 
    size_t cache_size, 
    size_t map_size,
    std::string tag,
    blst_scalar secret_sk
) : 
    gadgets_(init_gadgets(
        BRANCH_ORDER, 
        secret_sk, tag, 
        path, cache_size, map_size
    ))
{
    shard_prefix_.reserve(32);
    NodeId root_id(ROOT_NODE_ID, 0);

    int rc = 0;
    gadgets_->alloc.db_.start_txn();

    Result<Node_ptr, int> res = gadgets_->alloc.load_node(&root_id);

    if (res.is_err() && res.unwrap_err() == MDB_NOTFOUND) {
        auto root = create_branch(gadgets_, &root_id, nullptr);

        std::vector<byte> root_bytes = root->to_bytes();

        rc = gadgets_->alloc.db_.put(
            root_id.get_full(), root_id.size(), 
            root_bytes.data(), root_bytes.size()
        );
    }
    assert(rc == 0);
    gadgets_->alloc.db_.end_txn(rc);
}

Ledger::~Ledger() {}

const KZGSettings* Ledger::get_settings() const {
    return &gadgets_->settings;
}

// descends subtree & generates proofs and commitments.
// returns the new root hash for that block.
int Ledger::finalize_block(uint16_t block_id, Hash* out) {

    gadgets_->alloc.db_.start_txn();

    Result<Node_ptr, int> root = get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    Result<const Commitment*, int> res = 0; 
    res = root.unwrap()->finalize(block_id);
    if (res.is_err()) {
        int err = res.unwrap_err();
        gadgets_->alloc.db_.end_txn(err);
        return err;
    }
    Commitment root_commit = *res.unwrap();

    // incase root is only referenced here
    // need to trigger destructor before end_txn
    // because destructor persists to disk
    root.unwrap().reset();

    gadgets_->alloc.db_.end_txn();

    blst_scalar sk;
    hash_p1_to_scalar(&root_commit, &sk, &gadgets_->settings.tag);
    std::memcpy(out->h, sk.b, sizeof(out->h));
    return OK;
}

// descends subtree and removes all nodes belonging to that block_id.
// including leaf values
int Ledger::prune_block(uint16_t block_id) {
    gadgets_->alloc.db_.start_txn();

    Result<Node_ptr, int> root = get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    int res = root.unwrap()->prune(block_id);
    if (res == OK) 
        res = delete_root(block_id);

    // incase root is only referenced here
    // need to trigger destructor before end_txn
    // because destructor persists to disk
    root.unwrap().reset();

    gadgets_->alloc.db_.end_txn(res);

    return res;

}
// descends subtree and changes all block_ids to ZERO.
// if child block_id != 0 load it... recurse...
    // delete self (block_id) 
    // if have no children return DELETED
    // else overwrite block_id == 0 node with self
        // and if leaf, save values under new block_id??
    // return OK
// ALL DESCENDANTS AND COMPETITORS MUST BE PRUNED
int Ledger::justify_block(uint16_t block_id) {

    gadgets_->alloc.db_.start_txn();

    Result<Node_ptr, int> root = get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    int res = root.unwrap()->justify();
    if (res == DELETED) delete_root(block_id);

    // incase root is only referenced here
    // need to trigger destructor before end_txn
    // because destructor persists to disk
    root.unwrap().reset();

    gadgets_->alloc.db_.end_txn(res);

    return res;
}

Result<Node_ptr, int> Ledger::get_root(uint16_t block_id) {

    NodeId id(ROOT_NODE_ID, block_id);
    Result<Node_ptr, int> res = gadgets_->alloc.load_node(&id);
    if (res.is_err() && res.unwrap_err() == MDB_NOTFOUND) {
        Node_ptr node = create_branch(gadgets_, &id, nullptr);
        assert(gadgets_->alloc.cache_node(node) == OK);
        return node;
    } else if (res.is_err()) {
        return res.unwrap_err();
    }
    return res.unwrap();
}

int Ledger::delete_root(uint16_t block_id) {
    NodeId id(ROOT_NODE_ID, block_id);
    return gadgets_->alloc.db_.del(id.get_full(), id.size());
}

bool Ledger::in_shard(const Hash h) {
    size_t matched = 0;
    size_t path_size = shard_prefix_.size();

    while (matched < path_size && matched < 32) {
        if (shard_prefix_[matched] != h.h[matched]) {
            break;
        }
        ++matched;
    }
    return matched == path_size;
}

void Ledger::derive_Zs_n_Ys(
    Hash& key_hash,
    ByteSlice& value,
    std::vector<Commitment>* Cs,
    std::vector<Proof>* Pis,
    std::vector<size_t>* Zs,
    Scalar_vec* Ys,
    const KZGSettings* settings
) {
    size_t n{Pis->size()};
    assert(n == Cs->size());


    Hash val_hash;
    derive_hash(val_hash.h, value);

    Ys->resize(n);
    Zs->resize(n);

    blst_scalar s;
    for (int k{}; k < n; k++) {

        if (k == 0) {
            // full key proof is idx == 0.
            Zs->at(0) = 0;

            // evals to full key_hash where last byte is ZERO
            Hash key_hash_c(key_hash);
            key_hash_c.h[32 - 1] = 0;

            blst_scalar_from_le_bytes(&Ys->at(0), key_hash_c.h, 32);

        } else if (k == 1) {
            // idx of value being proven in leaf.
            Zs->at(1) = key_hash.h[32 - 1];
            // evals to val hash
            blst_scalar_from_le_bytes(&Ys->at(1), val_hash.h, 32);

        } else {
            // F(z) == H(Cs[k - 1])
            Zs->at(k) = key_hash.h[(n - 1) - k];
            hash_p1_to_scalar(&Cs->at(k - 1), &Ys->at(k), &settings->tag);

        }
    }
}

int Ledger::generate_proof(
    std::vector<Commitment> &Cs, 
    std::vector<Proof> &Pis,
    ByteSlice &key, 
    uint16_t block_id,
    uint8_t idx
) {

    std::vector<Scalar_vec> Fxs; 
    Fxs.reserve(6);
    Cs.reserve(6);

    Hash key_hash = new_hash();
    derive_hash(key_hash.h, key);
    key_hash.h[32 - 1] = idx;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;


    // Get components for proving Fxs, and Zs
    gadgets_->alloc.db_.start_txn();

    Result<Node_ptr, int> root = get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    int res = root.unwrap()->generate_proof(&key_hash, Fxs, Cs, 0);

    // incase root is only referenced here
    // need to trigger destructor before end_txn
    // because destructor persists to disk
    root.unwrap().reset();

    gadgets_->alloc.db_.end_txn(res);
    if (res != OK) return res;

    size_t n = Fxs.size();

    // Build Commits via Fxs in parrallel
    std::vector<std::future<void>> futures; 
    futures.reserve(n);

    // add one for key proof on leaf commitment
    Pis.resize(n + 1);

    std::atomic<int> res_atomic(OK);
    for (size_t i{}; i < n; i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            byte nib;

            if (i == 0) {
                // proof for leaf commitment is linked to this key.
                auto kzg_res = prove_kzg(Fxs[0], 0, gadgets_->settings);
                if (kzg_res.has_value()) {
                    Pis[i] = kzg_res.value();
                } else {
                    res_atomic.store(KZG_PROOF_ERR);
                }

                // value index nibble 
                nib = key_hash.h[31];
            } else {

                // nibble propogating upward from leaf
                nib = key_hash.h[(n - 1) - i];
            }

            auto kzg_res = prove_kzg(Fxs[i], nib, gadgets_->settings);
            if (kzg_res.has_value()) {
                Pis[i + 1] = kzg_res.value();
            } else {
                res_atomic.store(KZG_PROOF_ERR);
            }
        }));
    }


    for (auto &f : futures) f.get();
    return res_atomic.load();
}

int Ledger::put(
    ByteSlice &key, 
    ByteSlice &value, 
    uint8_t idx,
    uint16_t new_block_id
) {
    Hash key_hash;
    derive_hash(key_hash.h, key);
    key_hash.h[32-1] = idx;

    Hash val_hash;
    derive_hash(val_hash.h, value);

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    gadgets_->alloc.db_.start_txn();

    Result<Node_ptr, int> root = get_root(new_block_id);
    if (root.is_err()) return root.unwrap_err();

    print_hash(key_hash);

    int res = root.unwrap()->put(
        &key_hash, &val_hash, 
        new_block_id, 0
    );

    if (res == OK) {
        // determine leaf value node id and then insert value with block_id
        uint64_t node_id = 1;
        for (int i{}; i < 6; i++) {
            node_id *= BRANCH_ORDER;
            node_id += key_hash.h[i];
        }

        NodeId id(node_id, new_block_id);
        res = gadgets_->alloc.db_.put(
            id.get_full(), id.size(), 
            value.data(), value.size()
        );
    }

    // incase root is only referenced here
    // need to trigger destructor before end_txn
    // because destructor persists to disk
    root.unwrap().reset();

    gadgets_->alloc.db_.end_txn(res);
    
    return res;
}

int Ledger::delete_account(
    ByteSlice &key, 
    uint16_t new_block_id
) {
    Hash key_hash = new_hash();
    key_hash.h[32 - 1] = 0;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    gadgets_->alloc.db_.start_txn();
    Result<Node_ptr, int> root = get_root(new_block_id);
    if (root.is_err()) return root.unwrap_err();

    ByteSlice nibs(key_hash.h, 32);
    return root.unwrap()->delete_account(&key_hash, new_block_id, 0);

}
