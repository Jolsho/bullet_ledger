#include <blst.h>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "../src/key_sig.h"
#include "../src/kzg.h"
#include "../src/pippen.h"

/* 
 *  ( C, Y, Z, Pi )
 *      challenge based hiding 
 *
 *  in a verkle...
 *      each put derives a path of commitments...
 *      but we need to wait until all of the put finish before commiting..
 *      because there might be shared commitments...
 *      so you would build some map of commitments to node id.
 *          and we can derive each node id via a key
 *
 *
 *  ::::
 *      transactors create proofs for l2 nodes
 *          odds of collision for l2 are 3% for 2k trxs
 *      block builder creates commits for collided_l2 + l1 + l0(root)
 *          so that would be just more than 300 proofs...
 *          seems manageable...
 *
 *
*/

void test_commit_and_verify_pip(
    const SRS& S, PippMan& pip, const scalar_vec& Fx
) {
    // Open f(z) at z=2
    blst_scalar Z = new_scalar(2);
    blst_scalar PIP_Y = pip.evaluate_polynomial(Fx, Z);

    // Commit to f(x)
    blst_p1 PIP_C = pip.commit(Fx);
    std::cout << "PIP_Commitment to f(x):\n" << "    ";
    print_p1(PIP_C);
    printf("\n");

    // f(x) = q(x) + r(x)
    scalar_vec Qx = derive_q(Fx, Z);

    // Commit to q(x)
    std::cout << "PIP_Commitment to Q(x) AKA Pi:\n" << "    ";
    blst_p1 Pi = pip.commit(Qx);
    print_p1(Pi);

    // validate q(x) = (f(x) - f(z))/(x-z)
    assert(verify_proof(PIP_C, PIP_Y, Z, Pi, S));
    std::cout << "\nVALID PIP...\n";

    blst_scalar fake_Z = Fx[3];
    assert(!verify_proof(PIP_C, PIP_Y, fake_Z, Pi, S));
}

void test_commit_and_verify_reg(
    const SRS& S, const scalar_vec& Fx
) {

    // Open f(z) at z=2
    blst_scalar Z = new_scalar(2);
    blst_scalar Y = eval_poly(Fx, Z);

    // Commit to f(x)
    blst_p1 C = commit(Fx, S);
    std::cout << "Commitment to f(x):\n" << "    ";
    print_p1(C);
    printf("\n");

    // f(x) = q(x) + r(x)
    scalar_vec Qx = derive_q(Fx, Z);
    std::cout << "Derived Q(x)...\n\n";

    // Commit to q(x)
    std::cout << "Commitment to Q(x) AKA Pi:\n" << "    ";
    blst_p1 Pi = commit(Qx, S);
    print_p1(Pi);

    // validate q(x) = (f(x) - f(z))/(x-z)
    assert(verify_proof(C, Y, Z, Pi, S));
    std::cout << "\nVALID KZG...\n";

    blst_scalar fake_Z = Fx[3];
    assert(!verify_proof(C, Y, fake_Z, Pi, S));

    int total_size = 0;
    total_size += 48; // C
    total_size += 32; // f(z)
    total_size += 32; // z
    total_size += 48; // Pi
    printf("TOTAL BYTE_COUNT: %d\n", total_size);
}


int main() {
    // GENERATE POLYNOMIAL
    size_t degree = 50;
    scalar_vec Fx;
    for (uint64_t i = 1; i <= degree; i++) {
        blst_scalar s = rand_scalar();
        Fx.push_back(s);
    }

    // GENERATE SRS
    blst_scalar s = rand_scalar();
    SRS S(Fx.size(), s);

    printf("TESTING KZG COMMIT & VERIFY DEFAULT \n");
    printf("- - - - - - - - - - - - - - - - - \n");
    test_commit_and_verify_reg(S, Fx);
    printf("==================================\n");

    // PIPPEN::
    PippMan pip(S);
    printf("TESTING KZG COMMIT & VERIFY PIP_OPTIMIZATION \n");
    printf("- - - - - - - - - - - - - - - - - \n");
    test_commit_and_verify_pip(S, pip, Fx);
    printf("==================================\n");
}

