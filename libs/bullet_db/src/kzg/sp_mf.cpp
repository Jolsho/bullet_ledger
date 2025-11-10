#include <array>
#include <cstdio>
#include <cassert>
#include <vector>
#include "blst.h"
#include "kzg.h"
#include "blake3.h"

// ======================================================
// ========== SINGLE POINT MULTI_FUNCTION ===============
// ======================================================

using std::array;

scalar_vec fiat_shamir(
    const vector<blst_p1> &Cs,
    const vector<blst_scalar> &Ys,
    const scalar_vec &Zs
) {
    blake3_hasher h;
    blake3_hasher_init(&h);

    // Domain separation
    blake3_hasher_update(&h, "KZG-multi-open", 15);

    // Hash commitments
    array<uint8_t,48> buf;
    for (auto &C : Cs) {
        blst_p1_compress(buf.data(), &C);
        blake3_hasher_update(&h, buf.data(), buf.size());
    }

    // Hash evaluation scalars
    for (auto &y : Ys)
        blake3_hasher_update(&h, y.b, 32);

    // Hash zs
    for (auto &z: Zs) 
        blake3_hasher_update(&h, z.b, 32);

    // Produce alpha
    array<uint8_t,32> digest;
    blake3_hasher_finalize(&h, digest.data(), digest.size());

    blst_scalar alpha;
    blst_scalar_from_le_bytes(&alpha, digest.data(), 32);

    // derive powers of alpha
    auto m = Ys.size();
    scalar_vec powers(m);
    powers[0] = new_scalar(1);
    for (size_t i = 1; i < m; ++i) 
        // field multiply
        blst_sk_mul_n_check(&powers[i], &powers[i-1], &alpha);
    
    return powers;
}


scalar_vec derive_aggregate_polynomial(
    vector<scalar_vec> &Fxs,
    vector<blst_p1> &Cs,
    scalar_vec &Ys,
    blst_scalar &Z
) {
    scalar_vec z {Z};
    auto alphas = fiat_shamir(Cs, Ys, z);

    scalar_vec g(Fxs[0].size());
    for (size_t i = 0; i < Fxs.size(); i++) {
        g = poly_add(g, poly_scale(Fxs[i], alphas[i]));
    }
    return g;
}

bool verify_multi_func(
    vector<blst_p1> &Cs,
    scalar_vec &Ys,
    blst_scalar &Z,
    blst_p1_affine &Pi,
    SRS &S
) {
    scalar_vec zs = { Z };
    auto alphas = fiat_shamir(Cs, Ys, zs);

    blst_p1 C = new_p1();
    for (size_t i = 0; i < Cs.size(); i++) {
        blst_p1 scaled;
        p1_mult(scaled, Cs[i], alphas[i]);
        blst_p1_add_or_double(&C, &C, &scaled);
    }

    blst_scalar Y = new_scalar();
    for (size_t i = 0; i < Ys.size(); i++) {
        scalar_add_inplace(Y, scalar_mul(Ys[i], alphas[i]));
    }

    blst_p1 C_Z = new_p1();
    p1_mult(C_Z, S.g, Y);
    blst_p1_cneg(&C_Z, true);

    blst_p1_add_or_double(&C, &C, &C_Z);

    blst_p1_affine C_Z_aff = p1_to_affine(C);

    blst_p2 X_minus_z = S.g2_powers_jacob[1];
    blst_p2 tmp;
    p2_mult(tmp, S.h, Z);
    blst_p2_cneg(&tmp, true);                     // -z exponent
    blst_p2_add_or_double(&X_minus_z, &X_minus_z, &tmp); // G2^{tau - z}
    
    blst_p2_affine X_Z_aff = p2_to_affine(X_minus_z);


    blst_fp12 lhs, rhs;
    blst_miller_loop(&lhs,&S.g2_powers_aff[0], &C_Z_aff);
    blst_miller_loop(&rhs, &X_Z_aff, &Pi);
    // Final exponentiation
    blst_final_exp(&lhs, &lhs);
    blst_final_exp(&rhs, &rhs);
    return blst_fp12_is_equal(&lhs, &rhs);
}
