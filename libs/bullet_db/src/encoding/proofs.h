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
#include "verkle.h"

size_t calculate_proof_size(
    Commitment &C, Proof &Pi,
    std::vector<Commitment> &Ws,
    std::vector<Scalar_vec> &Ys,
    Scalar_vec &Zs
);

void marshal_existence_proof(
    byte* beginning,
    Commitment C, Proof Pi,
    std::vector<Commitment> Ws,
    std::vector<Scalar_vec> Ys,
    Scalar_vec Zs
);

std::optional<std::tuple<
    Commitment, Proof, 
    std::vector<Commitment>, 
    std::vector<Scalar_vec>, 
    Bitmap<32>
>> unmarshal_existence_proof(
    const byte* beginning, 
    size_t size
);
