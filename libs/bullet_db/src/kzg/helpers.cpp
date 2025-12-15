/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <cstring>
#include <iomanip>
#include <iostream>
#include "helpers.h"


blst_scalar num_scalar(const uint64_t v) {
    blst_scalar s;

    uint64_t limbs[4] = {0};
    limbs[0] = v;          // low 64 bits
    limbs[1] = 0;
    limbs[2] = 0;
    limbs[3] = 0;

    blst_scalar_from_uint64(&s, limbs);
    return s;
}

bool scalar_is_zero(const blst_scalar &s) {
    blst_p1 tmp;
    blst_p1_mult(&tmp, blst_p1_generator(), s.b, 256);
    return blst_p1_is_inf(&tmp);
}

bool equal_scalars(const blst_scalar &a, const blst_scalar &b) {
    return std::memcmp(a.b, b.b, 32) == 0;
}

void print_scalar(blst_scalar* s) {
    for (byte &b : s->b)
        std::cout
            << std::hex 
            << std::setw(2) 
            << std::setfill('0') 
            << (int)b;
    std::cout << std::endl;
}

void print_p1(blst_p1* p) {
    std::array<byte, 48> buff;
    blst_p1_compress(buff.data(), p);
    for (byte &b : buff)
        std::cout
            << std::hex 
            << std::setw(2) 
            << std::setfill('0') 
            << (int)b;
    std::cout << std::endl;
}

blst_p1 p1_from_bytes(const byte* buff) {
    blst_p1_affine commit_aff;
    blst_p1_uncompress(&commit_aff, buff);

    blst_p1 commit;
    blst_p1_from_affine(&commit, &commit_aff);
    return commit;
}
inline bool fr_is_zero(const blst_fr &x) {
    return (x.l[0] | x.l[1] | x.l[2] | x.l[3]) == 0;
}
inline bool fr_is_odd(const blst_fr &x) {
    return (x.l[0] & 1) != 0;
}

// Modular exponentiation: r = base^exp mod p
blst_scalar modular_pow(const blst_scalar &base, const BigInt &exp) {
    blst_scalar result(num_scalar(1)); // multiplicative identity
    blst_scalar base_acc = base;
    BigInt e = exp;          // copy of exponent

    while (!e.is_zero()) {
        if (e.is_odd()) {
            // multiply by base if LSB is 1
            blst_sk_mul_n_check(&result, &result, &base_acc);
        }
        // square the base
        blst_sk_mul_n_check(&base_acc, &base_acc, &base_acc);

        // divide exponent by 2
        e.div_u64(2);
    }

    return result;
}

void hash_to_sk(blst_scalar* dst, const Hash hash) {
    blst_scalar_from_le_bytes(dst, hash.data(), 32);
    assert(blst_sk_check(dst));
}

void hash_p1_to_sk(
    blst_scalar &out,
    const blst_p1 &p, 
    const std::vector<byte> &buffer, 
    const std::string* tag
) {
    byte buff[48];
    blst_p1_compress(buff, &p);

    BlakeHasher h;
    h.update(buff, 48);
    hash_to_sk(&out, h.finalize());
}


blst_p1 new_p1() {
    blst_p1 p;
    memset(&p, 0, sizeof(p));
    return p;
}

blst_p1 new_inf_p1() {
    blst_scalar zero = num_scalar(0);
    blst_p1 p1;
    blst_p1_mult(&p1, blst_p1_generator(), zero.b, 256);
    return p1;
}

blst_p2 new_p2() {
    blst_p2 p;
    memset(&p, 0, sizeof(p));
    return p;
}

blst_p2 new_inf_p2() {
    blst_scalar zero = num_scalar(0);
    blst_p2 p2;
    blst_p2_mult(&p2, blst_p2_generator(), zero.b, 256);
    return p2;
}
