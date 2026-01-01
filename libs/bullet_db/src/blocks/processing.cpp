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
#include "bitmap.h"
#include "hashing.h"
#include "helpers.h"
#include "kzg.h"
#include "state_types.h"
#include <cstdint>
#include <cstdio>
#include <future>

// descends subtree & generates proofs and commitments.
// returns the new root hash for that block.
int finalize_block(
    Ledger &ledger, 
    const Hash* block_hash, 
    Hash* out
) {

    size_t BATCHES = 4;
    size_t PER_BATCH = BRANCH_ORDER / BATCHES;

    Polynomial Fx(BRANCH_ORDER, ZERO_SK);

    std::vector<std::future<int>> futures; 
    futures.reserve(BATCHES);

    uint16_t block_id = ledger.get_block_id(block_hash, false);
    if (block_id == 0) return BLOCK_NOT_EXIST;


    /*
     *  TODO --
     *      this function needs to be able to get 
     *      a specific shard root.
     *          because you cant finalize a layer before you get all the other shard hashes
     *          but then how can you tell when you do recieve them all
     *              keep track of what you recieve and what you need...
    */

    Result<Node_ptr, int> r = ledger.get_root(block_id);
    if (r.is_err()) return r.unwrap_err();

    Node_ptr root = r.unwrap();


    for (size_t i{}; i < BATCHES; i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {

            int start = i * PER_BATCH;
            int end = start + PER_BATCH;

            // TODO -- return hashes
            std::vector<ShardVote> hashes;

            return root->finalize(
                &hashes,
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

    blst_scalar sk;
    hash_p1_to_scalar(root->derive_commitment(), &sk, &settings->tag);
    std::memcpy(out->h, sk.b, sizeof(out->h));


    // TODO -- delete_block_hash...

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
int justify_block(Ledger &ledger, const Hash* block_hash) {


    uint16_t block_id = ledger.get_block_id(block_hash, false);
    if (block_id == 0) return BLOCK_NOT_EXIST;

    Result<Node_ptr, int> root = ledger.get_root(block_id);
    if (root.is_err()) return root.unwrap_err();

    int res = root.unwrap()->justify(block_id);
    return res == DELETED ? OK : res;
}

// descends subtree and removes all nodes belonging to that block_id.
// including leaf values
int prune_block(Ledger &ledger, const Hash* block_hash) {

    uint16_t block_id = ledger.get_block_id(block_hash, false);
    if (block_id == 0) return BLOCK_NOT_EXIST;
    ledger.remove_block_id(block_hash);

    Result<Node_ptr, int> root = ledger.get_root(block_id);
    if (root.is_err()) {
        int err = root.unwrap_err();
        if (err == MDB_NOTFOUND) return OK;
        return err;
    }
    return root.unwrap()->prune(block_id);
}


int generate_proof(
    Ledger &ledger,
    std::vector<Commitment> &Cs, 
    std::vector<Proof> &Pis,
    Bitmap<8>* split_map,
    const Hash* key_hash, 
    const Hash* block_hash
) {
    std::vector<Scalar_vec> Fxs; 
    Fxs.reserve(6);
    Cs.reserve(6);

    if (!ledger.in_shard(key_hash)) return NOT_IN_SHARD;

    uint16_t block_id = ledger.get_block_id(block_hash, false);

    // Get components for proving Fxs, and Zs
    Result<Node_ptr, int> r = ledger.get_root(block_id);
    if (r.is_err()) return r.unwrap_err();
    Node_ptr root = r.unwrap();

    int rc = root->generate_proof(key_hash, Fxs, Cs, 0, split_map);
    if (rc != OK) return rc;

    size_t n = Fxs.size();

    // Build Commits via Fxs in parrallel
    std::vector<std::future<LedgerCodes>> futures; 
    futures.reserve(n);

    // add one for key proof on leaf commitment
    Pis.resize(n + 1);

    auto settings = ledger.get_gadgets()->settings;

    uint8_t key_offset{};
    for (size_t i{}; i < n; i++) {

        int key_idx = (n - 1) - i;

        futures.push_back(std::async(std::launch::async, [&, i, key_idx, key_offset] {
            byte nib;

            if (i == 0) {
                // proof leaf commitment is linked to this key.
                auto kzg_res = prove_kzg(Fxs[0], 0, settings);
                if (!kzg_res.has_value()) return KZG_PROOF_ERR;

                Pis[i] = kzg_res.value();

                // value index nibble 
                nib = key_hash->h[31];
            } else {

                // nibble propogating upward from leaf
                nib = key_hash->h[key_idx - key_offset];
            }

            auto kzg_res = prove_kzg(Fxs[i], nib, settings);
            if (!kzg_res.has_value()) return KZG_PROOF_ERR;

            Pis[i + 1] = kzg_res.value();

            return OK;
        }));

        // if there is a SPLIT in the proof we have to adjust
        // our key index by one because of the reused nibble
        if (split_map->is_set(key_idx)) key_offset++;
    }


    int res {OK};
    for (auto &f : futures) {
        int interm_res = f.get();
        if (interm_res != OK) {
            res = interm_res;
        }
    }

    return res;
}

bool valid_proof(
    Ledger &ledger,
    std::vector<Commitment>* Cs,
    std::vector<Proof>* Pis,
    Bitmap<8>* split_map,
    const Hash* key_hash,
    const Hash* val_hash,
    const uint8_t val_idx,
    const Hash* block_hash
) {
    std::vector<size_t> Zs;
    std::vector<blst_scalar> Ys;

    derive_Zs_n_Ys(ledger, key_hash, val_hash, split_map, Cs, Pis, &Zs, &Ys);

    auto tag = ledger.get_gadgets()->settings.tag;
    ByteSlice tag_slice((byte*)tag.data(), tag.size());
    Hash base_hash;
    derive_hash(base_hash.h, tag_slice);

    if (!ledger.in_shard(key_hash)) return NOT_IN_SHARD;

    uint16_t block_id = ledger.get_block_id(block_hash, false);

    Result<Node_ptr, int> r = ledger.get_root(block_id);
    if (r.is_err()) return r.unwrap_err();

    // check that the last commit, which is the closest to root exists
    bool is = r.unwrap()->commit_is_in_path(key_hash, Cs->back(), 0);
    if (!is) return false;

    return batch_verify(*Pis, *Cs, Zs, Ys, base_hash, ledger.get_gadgets()->settings);
}

void derive_Zs_n_Ys(
    Ledger &ledger,
    const Hash* key_hash,
    const Hash* val_hash,
    Bitmap<8>* split_map,
    std::vector<Commitment>* Cs,
    std::vector<Proof>* Pis,
    std::vector<size_t>* Zs,
    Scalar_vec* Ys
) {
    size_t n{Pis->size()};
    assert(n == Cs->size());

    Ys->resize(n);
    Zs->resize(n);

    blst_scalar s;
    auto tag = ledger.get_gadgets()->settings.tag;
    uint8_t key_offset{};
    for (int k{}; k < n; k++) {

        if (k == 0) {
            // full key proof is idx == 0.
            Zs->at(0) = 0;

            // evals to full key_hash where last byte is ZERO
            Hash key_hash_c = *key_hash;
            key_hash_c.h[32 - 1] = 0;

            blst_scalar_from_le_bytes(&Ys->at(0), key_hash_c.h, 32);

        } else if (k == 1) {
            // idx of value being proven in leaf.
            Zs->at(1) = key_hash->h[32 - 1];
            // evals to val hash
            blst_scalar_from_le_bytes(&Ys->at(1), val_hash->h, 32);

        } else {
            // if this is a split the next Z must be the same as now
            // so we incrememnt key_offset which will be -1 for next
            // iteration when deriving i again
            int i = (n - 1) - k - key_offset;
            if (split_map->is_set(i)) key_offset++;

            // F(z) == H(Cs[k - 1])
            Zs->at(k) = key_hash->h[i];
            hash_p1_to_scalar(&Cs->at(k - 1), &Ys->at(k), &tag);
        }
    }
}
