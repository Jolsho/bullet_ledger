# SPDX-License-Identifier: GPL-3.0-only

#!/usr/bin/env bash

set -e

# ledger || trie || all
mode=ledger

# fast || sanitizer || gdb
t_comp=fast

# reg || gdb
t_debug=reg

# specific ledger test
l_test=

while getopts ":m:c:d:t:" opt; do
  case "$opt" in

    m) mode="$OPTARG" ;;

    c) t_comp="$OPTARG" ;;

    d) t_debug="$OPTARG" ;;

    t) l_test="$OPTARG" ;;

    *)
        echo "Usage: $0 -m <mode> -c <t_comp> -d <t_debug> -t <l_test>"
        ;;
  esac
done


test_blockchain_state() {
    # first test inner C++ ledger lib
    cd ledger
    ./test.sh "$t_comp" "$t_debug"
    cd ..
}

test_ledger() {
    # test full rust project
    export LD_LIBRARY_PATH="$PWD/ledger/build:${LD_LIBRARY_PATH}"

    RUSTFLAGS="-Awarnings" cargo test "$l_test"
}


case "$mode" in
    "trie")
        test_blockchain_state
        ;;

    "ledger")
        test_ledger
        ;;

    "all")
        test_blockchain_state
        test_ledger
        ;;
    *)
        echo "Specify -mode [state_trie | ledger | all]"
        exit 1
        ;;
esac
