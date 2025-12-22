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

#include <cstdlib>
#include <cstring>
#include "key_sig.h"

std::tuple<const byte*, size_t> str_to_bytes(const char* str) {
    const byte* bytes = reinterpret_cast<const byte*>(str);
    return std::make_tuple(bytes, strlen(str));
}

key_pair gen_key_pair(
    const byte* tag, 
    size_t tag_len,
    Hash seed
) {
    key_pair keys;
    blst_keygen(&keys.sk, seed.h, sizeof(seed.h), tag, tag_len);
    blst_sk_to_pk_in_g1(&keys.pk, &keys.sk);
    return keys;
}

bool verify_sig(
    const blst_p1 &PK,
    blst_p2 &signature, 
    const byte* msg,
    size_t msg_len,
    const byte* tag,
    size_t tag_len
) {
    blst_p2_affine sig_affine; 
    blst_p2_to_affine(&sig_affine, &signature);

    blst_p1_affine pk_affine;
    blst_p1_to_affine(&pk_affine, &PK);

    blst_fp12 final;

    auto ctx = reinterpret_cast<blst_pairing*>(malloc(blst_pairing_sizeof()));
    blst_pairing_init(ctx, true, tag, tag_len);
    blst_pairing_aggregate_pk_in_g1(ctx, &pk_affine, &sig_affine, msg, msg_len);
    blst_pairing_commit(ctx);
    bool res = blst_pairing_finalverify(ctx);
    free(ctx);

    return res;

}

bool verify_aggregate_signature(
    std::vector<blst_p1>& pks,
    const blst_p2& agg_sig,
    const byte* msg,
    size_t msg_len,
    const byte* dst,
    size_t dst_len
) {
    if (pks.size() < 2) return false;

    // Convert agg_sig to affine
    blst_p2_affine sig_aff;
    blst_p2_to_affine(&sig_aff, &agg_sig);

    blst_fp12 gtsig;
    blst_aggregated_in_g2(&gtsig, &sig_aff);

    // INIT PAIRING
    auto ctx = reinterpret_cast<blst_pairing*>(malloc(blst_pairing_sizeof()));
    blst_pairing_init(ctx, true, dst, dst_len);

    // Convert agg_pk to affine
    blst_p1_affine aff;
    for (size_t i = 0; i < pks.size(); i++) {
        blst_p1_to_affine(&aff, &pks[i]);
        blst_pairing_aggregate_pk_in_g1(ctx, &aff, NULL, msg, msg_len);
    }

    blst_pairing_commit(ctx);
    bool res = blst_pairing_finalverify(ctx, &gtsig);

    free(ctx);
    return res;
}

