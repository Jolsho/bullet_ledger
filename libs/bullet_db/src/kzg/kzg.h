#pragma once
#include "../utils/utils.h"
#include <vector>

using std::vector;

// =======================================
// ============= SRS =====================
// =======================================


class SRS {
public:
    vector<blst_p1> g1_powers_jacob;
    vector<blst_p1_affine> g1_powers_aff;

    vector<blst_p2> g2_powers_jacob;
    vector<blst_p2_affine> g2_powers_aff;

    blst_p2 h;  // generator in G2 (h == g2_powers[0])
    blst_p1 g;  // generator in G1 (g == g1_powers[0])
    
    SRS(size_t degree, const blst_scalar &s);
    size_t max_degree();
};


// =======================================
// ============== PIP SHIT ===============
// =======================================

class PippMan {
public:
    PippMan(SRS& srs);
    ~PippMan();

    blst_p1_affine commit(const scalar_vec& coeffs);

    void mult_p1s(
        blst_p1 &agg,
        const scalar_vec& scalars,
        const vector<blst_p1_affine>& points);

    void mult_fixed_base(
        blst_p1 &agg,
        const scalar_vec &scalars,
        const blst_p1 &base);

private:
    limb_t *scratch_space;
    vector<blst_p1_affine*> g1_powers_aff_ptrs;
    vector<const byte*> scalar_ptrs;
    vector<const blst_p1_affine*> point_ptrs;
    size_t scratch_size;

    void fill_scalars(const scalar_vec& scalars);
    void fill_points(const vector<blst_p1_affine>& points);
    void clear_scalars();
    void clear_points();
    void new_scratch_space(size_t n);
    size_t current_scratch_size();
    void free_scratch_space();
};

// =======================================
// ============= SINGLE POINT ============
// =======================================


// Y = f(z)
blst_scalar eval_poly(
    const scalar_vec& coeffs,
    const blst_scalar& z);

// C_f(r)
blst_p1 commit_g1_projective(
    const scalar_vec& coeffs, 
    const SRS& srs
);
blst_p1_affine commit_g1(
    const scalar_vec& coeffs, 
    const SRS& srs);

blst_p2_affine commit_g2(
    const scalar_vec& coeffs, 
    const SRS& srs);

// Q(r) = (f(r) - f(z)) / (r - z)
scalar_vec derive_q(
    const scalar_vec& coeffs, 
    const blst_scalar& z);

// e( C-gY, g2 ) == e( Pi, g2(r-z) )
bool verify_proof(
    const blst_p1_affine& C,
    const blst_scalar& Y,
    const blst_scalar& Z,
    const blst_p1_affine& Pi,
    const SRS& S);


// =======================================
// ============= POLYNOMIALS ============
// =======================================

scalar_vec poly_add(const scalar_vec &a, const scalar_vec &b);
scalar_vec poly_sub(const scalar_vec &a, const scalar_vec &b);
scalar_vec poly_scale(const scalar_vec &a, const blst_scalar &s);
scalar_vec poly_mul(const scalar_vec &a, const scalar_vec &b);
void poly_divmod(const scalar_vec &A, const scalar_vec &B, scalar_vec &Q, scalar_vec &R);
void poly_normalize(scalar_vec &p);

// =======================================
// ============= MULTI POINT ============
// =======================================

bool verify_multi_proof(
    blst_p1_affine &C_aff,
    scalar_vec &ys,
    scalar_vec &zs,
    blst_p1_affine &Pi,
    SRS &S
);

scalar_vec derive_q_multi(
    const scalar_vec &Fx, 
    const scalar_vec &zs, const 
    scalar_vec &ys, 
    const SRS &srs
);

scalar_vec derive_I(
    const scalar_vec &zs, 
    const scalar_vec &ys
);

scalar_vec derive_Z(
    const scalar_vec &zs
);

// =======================================
// ============= MULTI FUNC ==============
// =======================================

scalar_vec fiat_shamir(
    const vector<blst_p1> &Cs,
    const vector<blst_scalar> &Ys,
    const scalar_vec &Zs
);

scalar_vec derive_aggregate_polynomial(
    vector<scalar_vec> &Fxs,
    vector<blst_p1> &Cs,
    scalar_vec &Ys,
    blst_scalar &Z
);

bool verify_multi_func(
    vector<blst_p1> &Cs,
    scalar_vec &Ys,
    blst_scalar &Z,
    blst_p1_affine &Pi,
    SRS &S
);

// ===================================================
// ============= MULTI FUNC MULTI POINT ==============
// ===================================================

std::tuple<blst_p1, blst_p1_affine> multi_func_multi_point_prover(
    const vector<scalar_vec> &funcs,
    const vector<blst_p1> &Cs,
    const scalar_vec &Zs,
    const vector<scalar_vec> &Ys_mat,
    const SRS &S
);

bool multi_func_multi_point_verify(
    const vector<blst_p1> &Cs,
    const scalar_vec &Zs,
    const vector<scalar_vec> &Ys_mat,
    const blst_p1_affine &Pi,
    const SRS &S
);
