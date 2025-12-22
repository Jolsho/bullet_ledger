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

#include "helpers.h"
#include "proofs.h"

/* 
 *  each node has up to l2 commitment
 *
 *  1/2 trx::
 *      p1 = l2 evals to hash of c2 @ z1
 *      c2 = l3 commitment
 *      p2 = l3 evals to hash of trx_value @ z2
 *      ---------------------------------------
 *      (48 + 2) * 3 = 150 = 300 / TRX
 *
 *      additional layer == +c +p = +(48+2) +(48+2) = +100
 *
 *
 *  ORDERING::
 *      n = 1 byte
 *      proof_i -> commit_i+1
 *      proof_n -> value_hash
 *
 */


// TODO --
//      i think we have to commit to remainder path in leaf...
//          use a leaf slot and commit to first 31 bytes of key + 255(uint8_t) constant
//      because otherwise attacker can just create an account with the same prefix
//          you couldnt tell the difference and you dont want that...
//      issue is this does introduce one more point per query...
//          well no... because you can derive the Y, and the zs...
//          and the witness is the same as the leaf...

size_t calculate_proof_size(
    Commitment &C, Proof &Pi,
    std::vector<Commitment> &Ws,
    std::vector<Scalar_vec> &Ys,
    Bitmap<32> &Zs
) {
    return 48 + 48 + (48 * Ws.size()) 
        + (32 * Ys.size()) 
        + Zs.BYTE_SIZE
        + (32 * Zs.count());
}

void marshal_existence_proof(
    byte* beginning,
    Commitment C, Proof Pi,
    std::vector<Commitment> Ws,
    std::vector<Scalar_vec> Ys,
    Bitmap<32> Zs
) {
    byte* cursor = beginning;

    blst_p1_compress(cursor, &C); cursor += 48;
    blst_p1_compress(cursor, &Pi); cursor += 48;

    *cursor = static_cast<uint8_t>(Ws.size()); cursor++;
    for (auto &w: Ws) {
        blst_p1_compress(cursor, &w); cursor += 48;
    }

    *cursor = static_cast<uint8_t>(Zs.count());
    std::memcpy(cursor, Zs.data_ptr(), Zs.BYTE_SIZE);

    for (auto &r: Ys) {
        for (auto &Y: r) {
            std::memcpy(cursor, Y.b, 32);
        }
    }
}

std::optional<std::tuple<
    Commitment, Proof, 
    std::vector<Commitment>, 
    std::vector<Scalar_vec>, 
    Bitmap<32>
>> unmarshal_existence_proof(
    const byte* beginning, 
    size_t size
) {

    const byte* cursor = beginning;

    if (size < 96) return std::nullopt;
    size -= 96;

    // AGGREGATE COMMITMENT
    Commitment C = p1_from_bytes(cursor); cursor += 48;

    // AGGREGATE PROOF
    Commitment Pi = p1_from_bytes(cursor); cursor += 48;

    // Witnesses LENGTH
    uint8_t ws_len = *cursor; cursor++;
    if (size < ws_len * 48) return std::nullopt;
    size -= (ws_len * 48);

    // Witnesses
    std::vector<Commitment> Ws; 
    Ws.reserve(ws_len);
    for (size_t i = 0; i < ws_len; i++) {
        Ws.push_back(p1_from_bytes(cursor)); cursor += 48;
    }

    // Evaluation indices & Evaluation outputs LENGTH
    uint8_t zs_len = *cursor; cursor++;
    if (size != (4 + (32 * ws_len * zs_len)))
        return std::nullopt;

    // Evaluation indices
    Bitmap<32> Zs(cursor);
    cursor += Zs.BYTE_SIZE;

    // Evaluation outputs
    std::vector<Scalar_vec> Ys(Ws.size(), Scalar_vec(zs_len, blst_scalar()));
    for (auto &Y_row: Ys) {
        for (auto &y: Y_row) {
            blst_scalar_from_le_bytes(&y, cursor, 32); 
            cursor += 32;
        }
    }

    return std::make_tuple(C, Pi, Ws, Ys, Zs);
}
