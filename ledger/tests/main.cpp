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

#include <string>
#include "tests.h"

int main(int argc, char* argv[]) {
    printf("\noptions == {key_sig, kzg, state}\n\n");
    printf("=====================================\n");

    if (argc > 1) {
        std::string a = argv[1];
        if (a == "key_sig") {
            main_key_sig();

        } else if (a == "kzg") {
            main_kzg();

        } else if (a == "state") {
            main_state_trie();
        }
        return 0;
    }


    // KEY_N_SIG
    main_key_sig();

    // KZG
    main_kzg();

    // STATE_TRIE
    main_state_trie();

    return 0;

}
