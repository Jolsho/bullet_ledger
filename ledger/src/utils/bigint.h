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

#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <cassert>
#include <algorithm>

class BigInt {
public:
    // Little-endian 64-bit limbs
    std::vector<uint64_t> limbs;

    BigInt() = default;
    explicit BigInt(uint64_t v) {
        if (v != 0)
            limbs.push_back(v);
    }

    // ---------- Queries ----------

    bool is_zero() const {
        return limbs.empty();
    }

    bool is_odd() const {
        return !limbs.empty() && (limbs[0] & 1);
    }

    size_t bit_length() const {
        if (limbs.empty()) return 0;
        size_t last = limbs.size() - 1;
        uint64_t v = limbs[last];
        size_t bits = 64;
        while (v >> (bits - 1) == 0) bits--;
        return last * 64 + bits;
    }

    // ---------- Construction ----------

    static BigInt from_hex(const std::string& hex) {
        BigInt x;
        x.limbs.push_back(0);

        for (char c : hex) {
            uint64_t v =
                (c >= '0' && c <= '9') ? c - '0' :
                (c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                0;

            // x *= 16
            uint64_t carry = 0;
            for (auto& limb : x.limbs) {
                __uint128_t t = (__uint128_t)limb * 16 + carry;
                limb = (uint64_t)t;
                carry = (uint64_t)(t >> 64);
            }
            if (carry) x.limbs.push_back(carry);

            // x += v
            carry = v;
            for (auto& limb : x.limbs) {
                __uint128_t t = (__uint128_t)limb + carry;
                limb = (uint64_t)t;
                carry = (uint64_t)(t >> 64);
                if (!carry) break;
            }
            if (carry) x.limbs.push_back(carry);
        }

        x.trim();
        return x;
    }

    static BigInt from_bytes(const uint8_t* bytes, size_t len = 32) {
        BigInt x;
        x.limbs.resize((len + 7) / 8, 0);
        size_t limb_idx = 0;
        for (size_t i = 0; i < len; i++) {
            size_t byte_pos = len - 1 - i;
            x.limbs[limb_idx] |= uint64_t(bytes[byte_pos]) << ((i % 8) * 8);
            if ((i + 1) % 8 == 0) limb_idx++;
        }
        x.trim();
        return x;
    }

    // ---------- Arithmetic ----------

    void sub_u64(uint64_t v) {
        size_t i = 0;
        while (v && i < limbs.size()) {
            if (limbs[i] >= v) {
                limbs[i] -= v;
                v = 0;
            } else {
                v -= limbs[i];
                limbs[i] = 0;
                v += 1; // borrow
            }
            i++;
        }
        trim();
    }

    uint64_t div_u64(uint64_t d) {
        assert(d != 0);
        __uint128_t rem = 0;

        for (size_t i = limbs.size(); i-- > 0;) {
            __uint128_t cur = (rem << 64) | limbs[i];
            limbs[i] = (uint64_t)(cur / d);
            rem = cur % d;
        }

        trim();
        return (uint64_t)rem;
    }

    void shr1() {
        uint64_t carry = 0;
        for (size_t i = limbs.size(); i-- > 0;) {
            uint64_t new_carry = limbs[i] & 1;
            limbs[i] = (limbs[i] >> 1) | (carry << 63);
            carry = new_carry;
        }
        trim();
    }

    // ---------- Modular arithmetic ----------

    // naive modulus operation
    BigInt mod(const BigInt& m) const {
        BigInt res = *this;
        while (res.compare(m) >= 0) {
            sub(res, res, m);
        }
        return res;
    }

    // ---------- Conversion ----------

    // little-endian 32-byte output
    void to_bytes(uint8_t* out, size_t len = 32) const {
        std::fill(out, out + len, 0);
        for (size_t i = 0; i < limbs.size(); i++) {
            for (size_t j = 0; j < 8; j++) {
                size_t idx = i * 8 + j;
                if (idx < len) out[idx] = (limbs[i] >> (8 * j)) & 0xFF;
            }
        }
    }

    // ---------- Comparison ----------
    int compare(const BigInt& other) const {
        if (limbs.size() > other.limbs.size()) return 1;
        if (limbs.size() < other.limbs.size()) return -1;
        for (size_t i = limbs.size(); i-- > 0;) {
            if (limbs[i] > other.limbs[i]) return 1;
            if (limbs[i] < other.limbs[i]) return -1;
        }
        return 0;
    }

private:
    void trim() {
        while (!limbs.empty() && limbs.back() == 0)
            limbs.pop_back();
    }

    static void sub(BigInt& out, const BigInt& a, const BigInt& b) {
        out = a;
        uint64_t borrow = 0;
        size_t n = std::max(a.limbs.size(), b.limbs.size());
        out.limbs.resize(n, 0);
        for (size_t i = 0; i < n; i++) {
            uint64_t ai = i < a.limbs.size() ? a.limbs[i] : 0;
            uint64_t bi = i < b.limbs.size() ? b.limbs[i] : 0;
            __uint128_t diff = (__uint128_t)ai - bi - borrow;
            out.limbs[i] = (uint64_t)diff;
            borrow = (diff >> 127) & 1; // 1 if diff < 0
        }
        out.trim();
    }
};

