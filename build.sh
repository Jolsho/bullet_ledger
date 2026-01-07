# SPDX-License-Identifier: GPL-3.0-only

#!/usr/bin/env bash

set -e

build_state_trie() {
    # Fetch and build C++ ledger
    cd ledger
    ./build.sh
    cd ..
}


build_ledger() {
    # Build Main Rust project
    cargo build
}


case "$1" in
    ledger)
        build_ledger
        ;;
    trie)
        build_state_trie
        ;;
    *)
        build_ledger
        build_state_trie
        ;;
esac
