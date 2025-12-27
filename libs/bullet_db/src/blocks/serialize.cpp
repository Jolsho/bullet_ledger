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


#include "serialize.h"


/*  TODO --
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
 */
