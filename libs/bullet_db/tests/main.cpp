#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "tests.h"

int main(int argc, char* argv[]) {
    printf("\noptions == {multi, single, key_sig, kzg}\n\n");

    bool tests[] = {true, true, true, true};

    if (argc > 1) {
        std::string a = argv[1];

        if (a == "multi") {
            tests[0] = false;
            tests[1] = false;

        } else if (a == "single") {
            tests[0] = false;
            tests[2] = false;

        } else if (a == "key_sig") {
            tests[1] = false;
            tests[2] = false;

        } else if (a == "kzg") {
            tests[0] = false;
        }
    }

    printf("=====================================\n");

    // KEY_N_SIG
    if (tests[0]) main_key_sig();

    // SINGLE_POINT
    if (tests[1]) main_single();

    // MULTI_POINT
    if (tests[2]) main_multi();


}
