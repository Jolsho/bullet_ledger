#include <cstring>
#include <ostream>
#include <array>
#include "blst.h"


// =======================================
// =============== POINTS ================
// =======================================

blst_p1 new_p1();
blst_p2 new_p2();
blst_p1_affine p1_to_affine(const blst_p1 &p1);
blst_p1 p1_from_affine(const blst_p1_affine &aff);
blst_p2 p2_from_affine(const blst_p2_affine &aff);
blst_p2_affine p2_to_affine(const blst_p2 &p2);

std::array<uint8_t, 48> compress_p1(blst_p1* pk);
void print_p1(const blst_p1& pk);
void print_p1_affine(const blst_p1_affine& pk);

std::array<uint8_t, 96> compress_p2(const blst_p2& pk);
void print_p2(const blst_p2& pk);
void print_p1_affine(const blst_p1_affine& pk);


// =======================================
// =============== SCALARS ===============
// =======================================

blst_scalar new_scalar(const uint64_t v = 0);
blst_scalar rand_scalar();
bool scalar_is_zero(const blst_scalar &s);
std::ostream& operator<<(std::ostream& os, const blst_scalar &x);

blst_scalar scalar_mul(const blst_scalar &a, const blst_scalar &b);
blst_scalar scalar_add(const blst_scalar &a, const blst_scalar &b);
blst_scalar scalar_sub(const blst_scalar &a, const blst_scalar &b);
void scalar_add_inplace(blst_scalar &dst, const blst_scalar &src);
void scalar_sub_inplace(blst_scalar &dst, const blst_scalar &src);
void scalar_mul_inplace(blst_scalar &dst, const blst_scalar &mult);
void scalar_pow(blst_scalar &out, const blst_scalar &base, uint64_t exp);

blst_scalar neg_scalar(const blst_scalar &sk);
blst_scalar inv_scalar(const blst_scalar &a);
void inv_scalar_inplace(blst_scalar &a);

