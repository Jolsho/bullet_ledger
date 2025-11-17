#include <cassert>
#include <iostream>
#include "../src/kzg/kzg.h"

void test_commit_and_verify_pip(
    const SRS& S, 
    PippMan& pip, 
    const scalar_vec& Fx,
    const blst_scalar Z
) {
    // Open f(z) at z=2
    blst_scalar Y = eval_poly(Fx, Z);

    // Commit to f(x)
    blst_p1_affine PIP_C = pip.commit(Fx);
    std::cout << "PIP_Commitment to f(x):\n" << "    ";
    print_p1_affine(PIP_C);
    printf("\n");

    // f(x) = q(x) + r(x)
    scalar_vec Qx = derive_q(Fx, Z);

    // Commit to q(x)
    std::cout << "PIP_Commitment to Q(x) AKA Pi:\n" << "    ";
    blst_p1_affine Pi = pip.commit(Qx);
    print_p1_affine(Pi);

    // validate q(x) = (f(x) - f(z))/(x-z)
    assert(verify_proof(PIP_C, Y, Z, Pi, S));
    std::cout << "\nVALID PIP...\n";

    blst_scalar fake_Z = Fx[3];
    assert(!verify_proof(PIP_C, Y, fake_Z, Pi, S));
}

void test_commit_and_verify_reg(
    const SRS& S, 
    const scalar_vec& Fx,
    const blst_scalar Z
) {

    // Open f(z) at z=2
    blst_scalar Y = eval_poly(Fx, Z);

    // Commit to f(x)
    blst_p1_affine C = commit_g1(Fx, S);
    std::cout << "Commitment to f(x):\n" << "    ";
    print_p1_affine(C);
    printf("\n");

    // f(x) = q(x) + r(x)
    scalar_vec Qx = derive_q(Fx, Z);
    std::cout << "Derived Q(x)...\n\n";

    // Commit to q(x)
    std::cout << "Commitment to Q(x) AKA Pi:\n" << "    ";
    blst_p1_affine Pi = commit_g1(Qx, S);
    print_p1_affine(Pi);

    // validate q(x) = (f(x) - f(z))/(x-z)
    assert(verify_proof(C, Y, Z, Pi, S));
    std::cout << "\nVALID KZG...\n";

    blst_scalar fake_Z = Fx[3];
    assert(!verify_proof(C, Y, fake_Z, Pi, S));

    int total_size = 0;
    total_size += 48; // C
    total_size += 32; // Y
    total_size += 32; // z
    total_size += 48; // Pi
    printf("TOTAL BYTE_COUNT: %d\n", total_size);
}

void main_single() {
    scalar_vec Fx;
    for (uint64_t i = 1; i <= 50; i++) {
        blst_scalar s = rand_scalar();
        Fx.push_back(s);
    }

    // GENERATE SRS
    blst_scalar s = rand_scalar();
    SRS S(Fx.size(), s);
    blst_scalar Z = new_scalar(2);

    printf("TESTING KZG DEFAULT \n");
    test_commit_and_verify_reg(S, Fx, Z);

    printf("\n");

    // PIPPEN::
    PippMan pip(S);
    printf("TESTING KZG PIP_OPTIMIZATION \n");
    test_commit_and_verify_pip(S, pip, Fx, Z);

    printf("=====================================\n");
}
