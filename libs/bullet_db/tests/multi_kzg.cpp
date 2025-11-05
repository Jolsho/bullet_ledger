#include <cstdio>
#include <cassert>
#include <iostream>
#include "../src/kzg.h"
#include "blst.h"

int main() {

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

        printf("SUCCESSFUL MULTI_POINT KZG\n");

    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
    }
    return 0;
}
