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
#include "kzg.h"
#include "ledger.h"

int finalize_block(
    Ledger &ledger, 
    uint16_t block_id, 
    Hash* out
);

int prune_block(
    Ledger &ledger, 
    uint16_t block_id
);

int justify_block(
    Ledger &ledger, 
    uint16_t block_id
);

int generate_proof(
    Ledger &ledger, 
    std::vector<Commitment> &Cs,
    std::vector<Proof> &Pis,
    Hash& key_hash,
    uint16_t block_id
);

void derive_Zs_n_Ys(
    Ledger &ledger, 
    Hash& key_hash,
    Hash& val_hash,
    std::vector<Commitment>* Cs,
    std::vector<Proof>* Pis,
    std::vector<size_t>* Zs,
    Scalar_vec* Ys
);
