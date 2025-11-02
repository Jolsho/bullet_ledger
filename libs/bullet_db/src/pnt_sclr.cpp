#include <cstring>
#include <iostream>
#include <ostream>
#include <array>
#include <iomanip>
#include <blst.h>
#include "blst_aux.h"
#include "key_sig.h"


// -------------------- points --------------------
blst_p1 new_p1() {
    blst_p1 p;
    memset(&p, 0, blst_p1_sizeof());
    return p;
}

blst_p2 new_p2() {
    blst_p2 p;
    memset(&p, 0, blst_p2_sizeof());
    return p;
}


std::array<uint8_t, 48> compress_p1(const blst_p1& pk) {
    std::array<uint8_t, 48> pk_comp;
    blst_p1_compress(pk_comp.data(), &pk);
    return pk_comp;
}

void print_p1(const blst_p1& pk) {
    auto pk_comp = compress_p1(pk);
    for (auto b : pk_comp)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    std::cout << std::endl;
}

std::array<uint8_t, 96> compress_p2(const blst_p2& pk) {
    std::array<uint8_t, 96> pk_comp;
    blst_p2_compress(pk_comp.data(), &pk);
    return pk_comp;
}

void print_p2(const blst_p2& pk) {
    auto pk_comp = compress_p2(pk);
    for (auto b : pk_comp)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    std::cout << std::endl;
}

// -------------------- scalar --------------------
blst_scalar new_scalar(const uint64_t v = 0) {
    blst_scalar s;
    uint64_t a[4] = {v, 0, 0, 0};
    blst_scalar_from_uint64(&s, a);
    return s;
}

blst_scalar rand_scalar() {
    blst_scalar s;
    blst_scalar_from_le_bytes(&s, gen_rand_32().begin(), 32);
    return s;
}


void scalar_add_inplace(blst_scalar &dst, const blst_scalar &src) {
    blst_sk_add_n_check(&dst, &dst, &src);
}
void scalar_mul_inplace(blst_scalar &dst, const blst_scalar &mult) {
    blst_sk_mul_n_check(&dst, &dst, &mult);
}

void scalar_pow(blst_scalar &out, const blst_scalar &base, uint64_t exp) {
    blst_scalar tmp;
    blst_scalar result = new_scalar(1);
    tmp = base;                             // tmp = base

    while (exp > 0) {
        if (exp & 1) {
            blst_sk_mul_n_check(&result, &result, &tmp);
        }
        blst_sk_mul_n_check(&tmp, &tmp, &tmp);
        exp >>= 1;
    }
    out = result;
}

