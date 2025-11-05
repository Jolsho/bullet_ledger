#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "blst.h"
#include "key_sig.h"
#include "pnt_sclr.h"

std::tuple<const byte*, size_t> str_to_bytes(const char* str) {
    const byte* bytes = reinterpret_cast<const byte*>(str);
    return std::make_tuple(bytes, strlen(str));
}

bytes32 gen_rand_32() {
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (!urandom) throw std::runtime_error("Failed to open /dev/urandom");

    bytes32 buffer;
    urandom.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    return buffer;
}

key_pair gen_key_pair(const char* tag, bytes32 &seed) {
    key_pair keys;
    blst_keygen(&keys.sk,
                seed.data(),
                seed.size(),
                reinterpret_cast<const byte*>(tag),
                strlen(tag));
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
    blst_p2_affine sig_affine = p2_to_affine(signature);
    blst_p1_affine pk_affine = p1_to_affine(PK);

    blst_fp12 final;
    blst_aggregated_in_g2(&final, &sig_affine);

    blst_pairing* ctx = (blst_pairing*)malloc(blst_pairing_sizeof());
    blst_pairing_init(ctx, true, tag, tag_len);

    blst_pairing_aggregate_pk_in_g1(
        ctx, &pk_affine, &sig_affine, msg, msg_len);

    blst_pairing_commit(ctx);
    bool is_valid = blst_pairing_finalverify(ctx, &final);

    free(ctx);

    return is_valid;
}

bool verify_aggregate_signature(
    std::vector<blst_p1>& pks,
    const blst_p2& agg_sig,
    const byte* msg,
    size_t msg_len,
    const uint8_t* tag,
    size_t tag_len
) {
    if (pks.size() < 2) return false;

    // Convert agg_sig to affine
    blst_p2_affine sig_aff = p2_to_affine(agg_sig);

    // then to p12
    blst_fp12 gtsig;
    blst_aggregated_in_g2(&gtsig, &sig_aff);

    // INIT PAIRING
    blst_pairing* ctx = (blst_pairing*)malloc(blst_pairing_sizeof());
    blst_pairing_init(ctx, true, tag, tag_len);

    // Convert agg_pk to affine
    for (size_t i = 0; i < pks.size(); i++) {
        blst_p1_affine pk_aff = p1_to_affine(pks[i]);
        blst_pairing_aggregate_pk_in_g1(ctx, &pk_aff, NULL, msg, msg_len);
    }

    blst_pairing_commit(ctx);
    bool is_valid = blst_pairing_finalverify(ctx, &gtsig);
    free(ctx);
    return is_valid;
}

