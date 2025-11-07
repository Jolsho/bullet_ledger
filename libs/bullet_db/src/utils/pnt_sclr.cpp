#include <cstring>
#include <iostream>
#include <ostream>
#include <array>
#include <iomanip>

#include "blst.h"
#include "blst_aux.h"
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


// =======================================
// =============== SCALAR ================
// =======================================

blst_scalar new_scalar(const uint64_t v) {
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

blst_scalar scalar_mul(const blst_scalar &a, const blst_scalar &b) {
    blst_scalar res;
    blst_sk_mul_n_check(&res, &a, &b);
    return res;
}

blst_scalar scalar_add(const blst_scalar &a, const blst_scalar &b) {
    blst_scalar res;
    blst_sk_add_n_check(&res, &a, &b);
    return res;
}
blst_scalar scalar_sub(const blst_scalar &a, const blst_scalar &b) {
    blst_scalar res;
    blst_sk_sub_n_check(&res, &a, &b);
    return res;
}

blst_scalar inv_scalar(const blst_scalar &a) {
    blst_scalar res;
    blst_sk_inverse(&res, &a); 
    return res;
}

void inv_scalar_inplace(blst_scalar &a) { 
    blst_sk_inverse(&a, &a); 
}

void scalar_add_inplace(blst_scalar &dst, const blst_scalar &src) {
    blst_sk_add_n_check(&dst, &dst, &src);
}
void scalar_sub_inplace(blst_scalar &dst, const blst_scalar &src) {
    blst_sk_sub_n_check(&dst, &dst, &src);
}
void scalar_mul_inplace(blst_scalar &dst, const blst_scalar &mult) {
    blst_sk_mul_n_check(&dst, &dst, &mult);
}

bool scalar_is_zero(const blst_scalar &s) {
    for (size_t i = 0; i < 32; i++) {
        if (s.b[i] != 0) return false;
    }
    return true;
}

blst_scalar neg_scalar(const blst_scalar &sk) {
    blst_scalar zero = new_scalar();
    scalar_sub_inplace(zero, sk);
    return zero;
}


// For debug printing
std::ostream& operator<<(std::ostream& os, const blst_scalar &x) {
    os << x.b;
    return os;
}

void scalar_pow(blst_scalar &out, const blst_scalar &base, uint64_t exp) {
    blst_scalar tmp;
    blst_scalar result = new_scalar(1);
    tmp = base;

    while (exp > 0) {
        if (exp & 1) {
            blst_sk_mul_n_check(&result, &result, &tmp);
        }
        blst_sk_mul_n_check(&tmp, &tmp, &tmp);
        exp >>= 1;
    }
    out = result;
}

