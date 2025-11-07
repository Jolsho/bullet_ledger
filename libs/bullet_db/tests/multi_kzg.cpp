#include <cstddef>
#include <cstdio>
#include <cassert>
#include <iostream>
#include "blst.h"
#include "kzg.h"
#include "utils.h"

/*
 *      
 *  Single, multi_point, and multi_function proofs all have their own utility...
 *
 *  Single can be usied for individual transactions. like basic ones.
 *  Then we can do even more complex account proofs with multipoint.
 *
 *
 *  And then with multi function you can do complex accounts in aggregate...
 *      Multi point per function or not...
 *  But yea I think there are utilities for all of these proofs.
 *  
 *  its just a matter of actually doing the thing.
 *
 *
 *  NEXT IS MULTI-FUNCTION single point...
 *      this is like checking the same attribute of a dataset
 *      like a single column
 *
 */

void test_multi_point() {
    // Example (over placeholder Fr): f(X) = 3 + 2X + X^2  (coeffs [3,2,1])
    scalar_vec Fx = { new_scalar(3), new_scalar(2), new_scalar(1) };

    // GENERATE SRS
    blst_scalar s = rand_scalar();
    SRS S(Fx.size(), s);

    blst_p1_affine C = commit_g1(Fx, S);

    // Points zs = [1, 2]
    scalar_vec zs = { new_scalar(1), new_scalar(2) };
    // evaluations ys = f(1) = 6, f(2) = 11
    scalar_vec ys = { new_scalar(6), new_scalar(11) };

    try {
        scalar_vec Q = derive_q_multi(Fx, zs, ys, S);

        blst_p1_affine Pi = commit_g1(Q, S);

        assert(verify_multi_proof(C, ys, zs, Pi, S));

        scalar_vec zs_fake = { new_scalar(3), new_scalar(5) };
        assert(!verify_multi_proof(C, ys, zs_fake, Pi, S));

        scalar_vec ys_fake = { new_scalar(3), new_scalar(5) };
        assert(!verify_multi_proof(C, ys_fake, zs, Pi, S));

    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
    }
}

scalar_vec random_poly(size_t degree) {
    // GENERATE POLYNOMIAL
    scalar_vec Fx;
    for (uint64_t i = 1; i <= degree; i++) {
        blst_scalar s = rand_scalar();
        Fx.push_back(s);
    }
    return Fx;
}

void test_multi_function() {

    size_t degree = 32;
    // GENERATE SRS
    blst_scalar s = rand_scalar();
    SRS S(degree, s);
    blst_scalar Z = new_scalar(2);

    vector<scalar_vec> Fxs;
    Fxs.reserve(10);

    vector<blst_p1> Cs;
    Cs.reserve(10);

    scalar_vec Ys;
    Ys.reserve(10);

    for (int i = 0; i < 10; i++) {
        scalar_vec Fx = random_poly(degree);
        blst_scalar Y = eval_poly(Fx, Z);
        blst_p1 C = commit_g1_projective(Fx, S);
        Cs.push_back(C);
        Ys.push_back(Y);
        Fxs.push_back(Fx);
    }

    scalar_vec Fx = derive_aggregate_polynomial(Fxs, Cs, Ys, Z);
    blst_p1_affine C = commit_g1(Fx, S);


    scalar_vec Qx = derive_q(Fx, Z);

    // Commit to q(x)
    blst_p1_affine Pi = commit_g1(Qx, S);

    // validate q(x) = (f(x) - f(z))/(x-z)
    assert(verify_multi_func(Cs, Ys, Z, Pi, S));

}

void main_multi() {
    printf("TESTING MULTI_POINT PROOFS\n");
    test_multi_point();

    printf("SUCCESS\n");
    printf("\n");

    printf("TESTING MULTI_FUNCTION PROOFS\n");
    test_multi_function();

    printf("SUCCESS\n");
    printf("\n");
}
