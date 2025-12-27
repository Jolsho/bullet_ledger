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
#include <cassert>
#include <future>

void derive_Zs_n_Ys(
    Ledger &ledger,
    Hash& key_hash,
    Hash& val_hash,
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
    for (int k{}; k < n; k++) {

        if (k == 0) {
            // full key proof is idx == 0.
            Zs->at(0) = 0;

            // evals to full key_hash where last byte is ZERO
            Hash key_hash_c = key_hash;
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
            hash_p1_to_scalar(&Cs->at(k - 1), &Ys->at(k), &tag);

        }
    }
}

int generate_proof(
    Ledger &ledger,
    std::vector<Commitment> &Cs, 
    std::vector<Proof> &Pis,
    Hash &key_hash, 
    uint16_t block_id
) {
    std::vector<Scalar_vec> Fxs; 
    Fxs.reserve(6);
    Cs.reserve(6);

    if (!ledger.in_shard(key_hash)) return NOT_IN_SHARD;

    // Get components for proving Fxs, and Zs
    Result<Node_ptr, int> r = ledger.get_root(block_id);
    if (r.is_err()) return r.unwrap_err();
    Node_ptr root = r.unwrap();

    int res = root->generate_proof(&key_hash, Fxs, Cs, 0);
    if (res != OK) return res;

    size_t n = Fxs.size();

    // Build Commits via Fxs in parrallel
    std::vector<std::future<void>> futures; 
    futures.reserve(n);

    // add one for key proof on leaf commitment
    Pis.resize(n + 1);

    auto settings = ledger.get_gadgets()->settings;

    std::atomic<int> res_atomic(OK);
    for (size_t i{}; i < n; i++) {
        futures.push_back(std::async(std::launch::async, [&, i] {
            byte nib;

            if (i == 0) {
                // proof for leaf commitment is linked to this key.
                auto kzg_res = prove_kzg(Fxs[0], 0, settings);
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

            auto kzg_res = prove_kzg(Fxs[i], nib, settings);
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
