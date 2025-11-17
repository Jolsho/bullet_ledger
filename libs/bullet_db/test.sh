#!/bin/bash
# build_and_run.sh
set -e

BUILD_DIR="./test_build"

if [ ! -d "$BUILD_DIR" ]; then
    cmake -B "$BUILD_DIR" -DTESTING=ON -DCMAKE_BUILD_TYPE=Debug
fi

cmake --build "$BUILD_DIR"
#"$BUILD_DIR/tests/tests_exec" "$1"
