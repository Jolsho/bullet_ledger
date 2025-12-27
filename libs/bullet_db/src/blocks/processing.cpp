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


#include "processing.h"
#include "fft.h"
#include "helpers.h"
#include "state_types.h"
#include <cstdio>
#include <future>

// descends subtree & generates proofs and commitments.
// returns the new root hash for that block.
int finalize_block(Ledger &ledger, uint16_t block_id, Hash* out) {


    size_t BATCHES = 4;
    size_t PER_BATCH = BRANCH_ORDER / BATCHES;

    Polynomial Fx(BRANCH_ORDER, ZERO_SK);

    std::vector<std::future<int>> futures; 
    futures.reserve(BATCHES);


    Result<Node_ptr, int> r = ledger.get_root(block_id);
    if (r.is_err()) return r.unwrap_err();

    Node_ptr root = r.unwrap();


    for (size_t i{}; i < BATCHES; i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {

            int start = i * PER_BATCH;
            int end = start + PER_BATCH;

            return root->finalize(
                block_id, nullptr,
                start, end, &Fx
            );
        }));
    }

    int res {OK};
    for (auto &f : futures) {
        int interm_res = f.get();
        if (interm_res != OK) {
            res = interm_res;
        }
    }
    if (res != OK) return res;

    const KZGSettings* settings = &ledger.get_gadgets()->settings;

    inverse_fft_in_place(Fx, settings->roots.inv_roots);
    Commitment root_commit;
    commit_g1(&root_commit, Fx, settings->setup);

    root->set_commitment(root_commit);

    blst_scalar sk;
    hash_p1_to_scalar(&root_commit, &sk, &settings->tag);
    std::memcpy(out->h, sk.b, sizeof(out->h));

    return OK;
}

// descends subtree and changes all block_ids to ZERO.
// if child block_id != 0 load it... recurse...
    // delete self (block_id) 
    // if have no children return DELETED
    // else overwrite block_id == 0 node with self
        // and if leaf, save values under new block_id??
    // return OK
// ALL DESCENDANTS AND COMPETITORS MUST BE PRUNED
int justify_block(Ledger &ledger, uint16_t block_id) {

    Result<Node_ptr, int> root = ledger.get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    int res = root.unwrap()->justify();
    if (res == DELETED) ledger.delete_root(block_id);

    return res;
}

// descends subtree and removes all nodes belonging to that block_id.
// including leaf values
int prune_block(Ledger &ledger, uint16_t block_id) {

    Result<Node_ptr, int> root = ledger.get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    int res = root.unwrap()->prune(block_id);
    if (res == OK) 
        res = ledger.delete_root(block_id);

    return res;
}
