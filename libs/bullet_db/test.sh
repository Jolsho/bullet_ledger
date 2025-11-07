#!/bin/bash
# build_and_run.sh
set -e

cmake --build build 
./build/tests/tests_exec $1
