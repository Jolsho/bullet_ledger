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

#include "../utils/utils.h"

//================================================
//============= UPDATE COMMITS ===================
//================================================

void update_parent(
    blst_p1_affine &C_parent,
    blst_p1 &delta
) {
    // convert parent to projective
    blst_p1 C_parent_pro;
    blst_p1_from_affine(&C_parent_pro, &C_parent);

    // add delta to parent
    blst_p1_add_or_double(&C_parent_pro, &C_parent_pro, &delta);

    // convert back to affine
    blst_p1_to_affine(&C_parent, &C_parent_pro);
}

// (v_new - v_old) * g1 == delta_p1
// C_parent += delta_p1
// return delta_p1
blst_p1 update_leaf_parent(
    blst_p1_affine &C_parent, 
    const blst_scalar &Y_old,
    const blst_scalar &Y_new
) {
    // delta = v_new - v_old
    blst_scalar delta = scalar_sub(Y_new, Y_old);

    // delta * G1
    blst_p1 delta_commit;
    p1_mult(delta_commit, *blst_p1_generator(), delta);

    update_parent(C_parent, delta_commit);

    // convert back to affine
    return delta_commit;
}

// use delta_p1 from update_leaf_parent() as delta_in
// C_parent is updated in place, and delta_in is scaled by child_alpha
void update_internal_parent(
    blst_p1_affine &C_parent,
    blst_p1 &delta_in,
    blst_scalar alpha
) {
    // scale delta
    p1_mult(delta_in, delta_in, alpha );
    update_parent(C_parent,  delta_in);
}
