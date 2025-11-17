#include <iostream>
#include <iomanip>
#include "utils.h"


// =======================================
// =============== POINTS ================
// =======================================


// -------------------- P1 ---------------------------

blst_p1 new_p1() {
    blst_p1 p;
    memset(&p, 0, blst_p1_sizeof());
    return p;
}

std::array<uint8_t, 48> compress_p1(const blst_p1& pk) {
    std::array<uint8_t, 48> pk_comp;
    blst_p1_compress(pk_comp.data(), &pk);
    return pk_comp;
}

blst_p1_affine p1_to_affine(const blst_p1 &p1) {
    blst_p1_affine aff;
    blst_p1_to_affine(&aff, &p1);
    return aff;
}

blst_p1 p1_from_affine(const blst_p1_affine &aff) {
    blst_p1 p1;
    blst_p1_from_affine(&p1, &aff);
    return p1;
}

void print_p1(const blst_p1& pk) {
    auto pk_comp = compress_p1(pk);
    for (auto b : pk_comp)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    std::cout << std::endl;
}
void print_p1_affine(const blst_p1_affine& pk) {
    blst_p1 pk1;
    blst_p1_from_affine(&pk1, &pk);
    print_p1(pk1);
}

const size_t SCALAR_BITS = 256;
void p1_mult(blst_p1& dst, const blst_p1 &a, const blst_scalar &b) {
    blst_p1_mult(&dst, &a, b.b, SCALAR_BITS);
}
void p2_mult(blst_p2& dst, const blst_p2 &a, const blst_scalar &b) {
    blst_p2_mult(&dst, &a, b.b, SCALAR_BITS);
}

// -------------------- P2 ---------------------------

blst_p2 new_p2() {
    blst_p2 p;
    memset(&p, 0, blst_p2_sizeof());
    return p;
}
std::array<uint8_t, 96> compress_p2(const blst_p2& pk) {
    std::array<uint8_t, 96> pk_comp;
    blst_p2_compress(pk_comp.data(), &pk);
    return pk_comp;
}
blst_p2_affine p2_to_affine(const blst_p2 &p2) {
    blst_p2_affine aff;
    blst_p2_to_affine(&aff, &p2);
    return aff;
}
blst_p2 p2_from_affine(const blst_p2_affine &aff) {
    blst_p2 p2;
    blst_p2_from_affine(&p2, &aff);
    return p2;
}

void print_p2(const blst_p2& pk) {
    auto pk_comp = compress_p2(pk);
    for (auto b : pk_comp)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    std::cout << std::endl;
}
void print_p2_affine(const blst_p2_affine& pk) {
    blst_p2 pk2;
    blst_p2_from_affine(&pk2, &pk);
    print_p2(pk2);
}
