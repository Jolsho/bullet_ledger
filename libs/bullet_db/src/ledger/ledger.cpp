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
#include "kzg.h"
#include "leaf.h"
#include "state_types.h"
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
    NodeId root_id(1, 0);

    int rc = 0;
    gadgets_.alloc.db_.start_txn();

    Result<Node*, int> res = gadgets_.alloc.load_node(&root_id);

    if (res.is_ok()) {
        // uncache to prevent unintentional invalidation
        gadgets_.alloc.root_ = gadgets_.alloc.cache_.remove(root_id);

    } else {
        Branch_i* root = create_branch(nullptr, nullptr);

        std::vector<byte> root_bytes = root->to_bytes();

        rc = gadgets_.alloc.db_.put(
            root_id.get_full(), root_id.size(), 
            root_bytes.data(), root_bytes.size()
        );

        gadgets_.alloc.root_ = root;
    }
    gadgets_.alloc.db_.end_txn(rc);
}

Ledger::~Ledger() {}

Node* Ledger::get_root(uint16_t block_id) {
    NodeId id(0, block_id);
    void* ptr; size_t size;
    int rc = gadgets_.alloc.db_.get(id.get_full(), id.size(), &ptr, &size);
    if (rc == 0) {
        ByteSlice raw_node(reinterpret_cast<byte*>(ptr), size);

        Node* node_ptr;

        if (raw_node[0] == BRANCH)
            node_ptr = create_branch(&id, &raw_node);
        else
            node_ptr = create_leaf(&id, &raw_node);

        return node_ptr;
    }
    return nullptr;
}

int Ledger::delete_root(uint16_t block_id) {
    NodeId id(0, block_id);
    return gadgets_.alloc.db_.del(id.get_full(), id.size());
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

int Ledger::generate_proof(
    std::vector<Commitment>* Cs, 
    std::vector<Proof>* Pis,
    ByteSlice &key, 
    uint8_t idx
) {
    const size_t MAX_LEVELS = 5;
    if (!gadgets_.alloc.root_) return ROOT_ERR;

    std::vector<Scalar_vec> Fxs(MAX_LEVELS); 

    Hash hash = new_hash();
    derive_hash(hash.h, key);
    hash.h[32 - 1] = idx;

    if (!in_shard(hash)) return NOT_IN_SHARD;

    // Get components for proving Fxs, and Zs
    gadgets_.alloc.db_.start_txn();
    ByteSlice nibbles(hash.h, 32);
    int res = gadgets_.alloc.root_->generate_proof(&this->gadgets_, hash, nibbles, Fxs);
    gadgets_.alloc.db_.end_txn();
    if (res != EXISTS) return res;

    // Build Commits via Fxs in parrallel
    std::vector<std::future<void>> futures; futures.reserve(Fxs.size());
    Cs->resize(Fxs.size());
    Pis->resize(Fxs.size());
    std::atomic<int> res_atomic(EXISTS);

    for (size_t i = 0; i < Fxs.size(); i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            auto kzg_res = prove_kzg(Fxs[i], hash.h[i], gadgets_.settings);
            if (kzg_res.has_value()) {
                auto [C, Pi] = kzg_res.value();
                (*Cs)[i] = C;
                (*Pis)[i] = Pi;
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
    Hash key_hash = new_hash();
    derive_hash(key_hash.h, key);
    key_hash.h[32-1] = idx;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    Hash val_hash = new_hash();
    derive_hash(val_hash.h, value);

    Node* root = get_root(new_block_id);
    if (root == nullptr) return ROOT_ERR;

    gadgets_.alloc.db_.start_txn();
    ByteSlice nibs(key_hash.h, 32);
    int res = root->put(
        &this->gadgets_, 
        nibs, key_hash, val_hash, 
        new_block_id
    );

    if (res == OK) {
        // determine leaf value node id and then insert value with block_id
        uint64_t node_id = 0;
        for (int i = 0; i < sizeof(key_hash.h); i++) {
            node_id *= BRANCH_ORDER;
            node_id += key_hash.h[i];
        }
        NodeId id(node_id, new_block_id);
        res = gadgets_.alloc.db_.put(id.get_full(), id.size(), &value, value.size());
    }
    gadgets_.alloc.db_.end_txn(res == OK && res != MDB_KEYEXIST);
    
    return res;
}

int Ledger::delete_account(
    ByteSlice &key, 
    uint16_t new_block_id
) {
    Hash key_hash = new_hash();
    key_hash.h[32 - 1] = 0;

    if (!in_shard(key_hash)) return NOT_IN_SHARD;

    Node* root = get_root(new_block_id);
    if (root == nullptr) return ROOT_ERR;

    gadgets_.alloc.db_.start_txn();
    ByteSlice nibs(key_hash.h, 32);
    return root->delete_account(&this->gadgets_, nibs, key_hash, new_block_id);

}
