#include <cstddef>
#include <cstdio>
#include <cassert>
#include "blst.h"
#include "kzg.h"
#include "utils.h"

scalar_vec random_poly_mm(size_t degree) {
    // GENERATE POLYNOMIAL
    scalar_vec Fx;
    for (uint64_t i = 1; i <= degree; i++) {
        blst_scalar s = rand_scalar();
        Fx.push_back(s);
    }
    return Fx;
}

void test_full_multi_square() {

    size_t degree = 32;
    size_t funcs_num = 10;

    // GENERATE SRS
    blst_scalar s = rand_scalar();
    SRS S(degree, s);
    scalar_vec Zs = { new_scalar(2), new_scalar(4), new_scalar(6) };
    vector<scalar_vec> Fxs;
    Fxs.reserve(funcs_num);
    vector<blst_p1> Cs;
    Cs.reserve(funcs_num);
    vector<scalar_vec> Ys_mat(funcs_num);

    for (int i = 0; i < funcs_num; i++) {

        scalar_vec Fx = random_poly_mm(degree);

        for (auto &z: Zs) Ys_mat[i].push_back(eval_poly(Fx, z));

        Fxs.push_back(Fx);
        Cs.push_back(commit_g1_projective(Fx, S));
    }

    auto [Fx_agg, Pi] = multi_func_multi_point_prover(Fxs, Cs, Zs, Ys_mat, S);
    assert(multi_func_multi_point_verify(Cs, Zs, Ys_mat, Pi, S));


    scalar_vec fake_zs = { new_scalar(0), new_scalar(4), new_scalar(6) };
    assert(!multi_func_multi_point_verify(Cs, fake_zs, Ys_mat, Pi, S));
}

void main_full() {
    printf("TESTING FULL MULTI SQUARE\n");
    test_full_multi_square();

    printf("SUCCESS\n");
    printf("\n");
}
