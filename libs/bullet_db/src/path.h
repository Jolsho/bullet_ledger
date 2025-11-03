#include <tuple>
#include <vector>

#include "blst.h"
#include "../src/kzg.h"

using std::vector;

//================================================
//============= UPDATE COMMITS ===================
//================================================

// (v_new - v_old) * g1 == delta_p1
// C_parent += delta_p1
// return delta_p1
blst_p1 update_leaf_parent(
    blst_p1_affine &C_parent, 
    const blst_scalar &v_old,
    const blst_scalar &v_new
);

// use delta_p1 from update_leaf_parent() as delta_in
// C_parent is updated in place, and delta_in is scaled by child_alpha
void update_internal_parent(
    blst_p1_affine &C_parent,
    blst_p1 &delta_in,
    const blst_scalar child_alpha
);


//================================================
//============= FIAT-SHAMIR CALCULATIONS =========
//================================================

// Compute challenge α = H(prev || value || z)
void compute_challenge_inplace(
    blst_scalar& prev,          // previous challenge (or root)
    const blst_scalar& value,   // leaf or node value at this level
    const blst_scalar& z        // evaluation point
);


//================================================
//============= DERIVE_N_VERIFY KZGs =============
//================================================

std::tuple<blst_p1_affine, blst_p1_affine> verkle_kzg( 
    const blst_scalar seed,             // initial challenge
    const vector<blst_p1> proofs,       // Pi_i
    const vector<blst_p1> commits,      // C_i 
    const vector<blst_scalar> zs,       // evaluation points
    const vector<blst_scalar> values    // v_i (leaf or child commitments)
);

bool verify_verkle_kzg(
    const blst_scalar &seed,            // Fiat-Shamir seed
    const blst_p1_affine &C_agg,        // agg commitment
    const blst_p1_affine &pi_agg,       // aggregated proof
    const vector<blst_scalar> &values,  // leaf values
    const vector<blst_scalar> &zs,      // leaf indices
    const SRS &S                        // SRS
);
