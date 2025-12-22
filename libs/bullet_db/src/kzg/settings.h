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
#include <string>
#include <vector>
#include "blst.h"


struct NTTRoots {
    std::vector<blst_scalar> roots;
    std::vector<blst_scalar> inv_roots;
};


NTTRoots build_roots(size_t n = 256);

// =======================================
// ============= SRS =====================
// =======================================


class SRS {
public:
    std::vector<blst_p1> g1_powers_jacob;
    std::vector<blst_p1_affine> g1_powers_aff;

    std::vector<blst_p2> g2_powers_jacob;
    std::vector<blst_p2_affine> g2_powers_aff;

    blst_p1 g;  // generator in G1 (g == g1_powers[0])
    blst_p2 h;  // generator in G2 (h == g2_powers[0])
    
    SRS(size_t degree, const blst_scalar &s);
    size_t max_degree();

    void set_srs(
        std::vector<blst_p1> &g1s,
        std::vector<blst_p2> &g2s
    ) { 
        for (int i = 0; i < g1s.size(); i++) {
            g1_powers_jacob[i] = g1s[i];
            blst_p1_to_affine(
                &g1_powers_aff[i], 
                &g1_powers_jacob[i]
            );

            g2_powers_jacob[i] = g2s[i];
            blst_p2_to_affine(
                &g2_powers_aff[i], 
                &g2_powers_jacob[i]
            );
        }
    }
};

struct KZGSettings {
    NTTRoots roots;
    SRS setup;
    std::string tag;
};
KZGSettings init_settings(size_t degree, const blst_scalar &s, std::string tag);
