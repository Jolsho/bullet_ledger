#include <cstdlib>
#include <cstring>
#include <blst.h>
#include "blst_aux.h"
#include "pnt_sclr.h"


// Compute challenge α = H(prev || value || z)
blst_scalar compute_challenge(
    const blst_scalar& prev,       // previous challenge (or root)
    const blst_scalar& value,      // leaf or node value at this level
    const blst_scalar& z           // evaluation point
) {
    
    byte* buf = (byte*)malloc(96); // 32 bytes each for prev, value, z
    memcpy(buf, prev.b, 32);
    buf += 32;
    memcpy(buf, value.b, 32);
    buf += 32;
    memcpy(buf, z.b, 32);
    buf -= 64;

    uint8_t hash[32];
    blst_sha256(hash, buf, 96);
    free(buf);

    blst_scalar alpha;
    blst_scalar_from_lendian(&alpha, hash);

    return alpha;
}

blst_p1 commit_scalar(const blst_scalar& s) {
    blst_p1 out;
    blst_sk_to_pk_in_g1(&out, &s);
    return out;
}

const byte* sk_to_bytes(const blst_scalar* s) {
    return reinterpret_cast<const byte*>(s);
}


void aggregate_node_proofs() {
    blst_scalar root;
    std::vector<blst_p1> proofs;       // π_j per level
    std::vector<blst_p1> node_commits; // C_j per level
    std::vector<blst_scalar> zs;       // evaluation points per level
    std::vector<blst_scalar> values;   // v_j per level (leaf or child commitments)
    
    blst_p1 pi_agg = proofs[0];
    blst_p1 C_agg = node_commits[0];

    blst_scalar alpha = compute_challenge(root, values[0], zs[0]);

    // start from j = 1
    for (size_t j = 1; j < proofs.size(); ++j) {
        alpha = compute_challenge(alpha, values[j], zs[j]); // Fiat–Shamir
        blst_p1 tmp;

        // pi_agg += alpha * π_j
        blst_p1_mult(&tmp, &proofs[j], sk_to_bytes(&alpha), 255);
        blst_p1_add_or_double(&pi_agg, &pi_agg, &tmp);

        // C_agg += alpha * (C_j - v_j g)
        blst_p1 child_minus_val = node_commits[j];
        blst_p1 v_j_g = commit_scalar(values[j]);
        blst_p1_cneg(&v_j_g, true);
        blst_p1_add_or_double(&child_minus_val, 
                              &child_minus_val, 
                              &v_j_g);

        blst_p1_mult(&tmp, &child_minus_val, sk_to_bytes(&alpha), 255);
        blst_p1_add_or_double(&C_agg, &C_agg, &tmp);
    }
}
