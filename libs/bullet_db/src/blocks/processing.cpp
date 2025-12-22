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

#include "blst.h"
#include "helpers.h"
#include "ledger.h"
#include "state_types.h"
#include <cstdlib>
#include <cstring>

/// inserts paths marked by the new_block_id
/// does not derive new commitments or hashes.
int insert_block(
    Ledger& ledger, uint16_t block_id, 
    byte* buffer, size_t size
) {

    return OK;
}

// descends subtree & generates proofs and commitments.
// returns the new root hash for that block.
int finalize_block(
    Ledger& ledger, uint16_t block_id, 
    void* out, size_t* out_size
) {
    Node* root = ledger.get_root(block_id);

    ledger.gadgets_.alloc.db_.start_txn();

    Result<const Commitment*, int> res = 0; 
    res = root->finalize(&ledger.gadgets_, block_id);

    ledger.gadgets_.alloc.db_.end_txn(!res.is_ok());

    const size_t HASH_SIZE = 32;

    out = malloc(HASH_SIZE);
    *out_size = HASH_SIZE;

    blst_scalar sk;
    hash_p1_to_sk(sk, *res.unwrap(), &ledger.gadgets_.settings.tag);
    std::memcpy(out, sk.b, HASH_SIZE);

    return OK;
}

// descends subtree and removes all nodes belonging to that block_id.
// including leaf values
int prune_block(Ledger& ledger, uint16_t block_id) {

    Node* root = ledger.get_root(block_id);

    ledger.gadgets_.alloc.db_.start_txn();

    int res = root->prune(&ledger.gadgets_, block_id);
    if (res == OK) 
        res = ledger.delete_root(block_id);

    ledger.gadgets_.alloc.db_.end_txn(res != OK);

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
int justify_block(Ledger& ledger, uint16_t block_id) {

    Node* root = ledger.get_root(block_id);

    ledger.gadgets_.alloc.db_.start_txn();

    int res = root->justify(&ledger.gadgets_);
    if (res == DELETED) ledger.delete_root(block_id);

    ledger.gadgets_.alloc.db_.end_txn(res != OK);

    return res;
}
