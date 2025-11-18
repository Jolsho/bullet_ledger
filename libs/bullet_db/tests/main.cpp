// SPDX-License-Identifier: GPL-2.0-only

#include <string>
#include "tests.h"

int main(int argc, char* argv[]) {
    printf("\noptions == {multi, single, key_sig, kzg, state}\n\n");
    printf("=====================================\n");

    if (argc > 1) {
        std::string a = argv[1];
        if (a == "multi") {
            main_multi();

        } else if (a == "single") {
            main_single();

        } else if (a == "key_sig") {
            main_key_sig();

        } else if (a == "kzg") {
            main_multi();
            main_single();

        } else if (a == "state") {
            main_state_trie();
        }
        return 0;
    }


    // KEY_N_SIG
    main_key_sig();

    // SINGLE_POINT
    main_single();

    // MULTI_POINT
    main_multi();

    // STATE_TRIE
    main_state_trie();

    return 0;

}
