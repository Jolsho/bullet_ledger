# SPDX-License-Identifier: GPL-3.0-only

#!/bin/bash
# build_and_run.sh
set -e

BUILD_DIR="./test_build"
EXEC="$BUILD_DIR/tests/tests_exec"

# Default mode and run behavior
MODE=${1:-sanitizer}
RUN_MODE=${2:-gdb}

# Determine flags based on mode
case "$MODE" in
    sanitizer)
        CXX_FLAGS="-g -O0 -fno-inline -gdwarf-4 -fsanitize=address -fno-omit-frame-pointer"
        LINK_FLAGS="-fsanitize=address"
        ;;
    fast)
        CXX_FLAGS="-g -O2"
        LINK_FLAGS=""
        ;;
    gdb)
        CXX_FLAGS="-g -O0 -fno-inline -gdwarf-4 -fno-omit-frame-pointer"
        LINK_FLAGS=""
        ;;
    *)
        echo "Unknown mode: $MODE"
        exit 1
        ;;
esac

# Configure CMake if build dir doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    cmake -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_CXX_FLAGS_DEBUG="$CXX_FLAGS" \
          -DCMAKE_EXE_LINKER_FLAGS_DEBUG="$LINK_FLAGS" \
          -DTESTING=ON
fi

# Build
cmake --build "$BUILD_DIR"

case "$RUN_MODE" in 
    gdb)
        gdb "$EXEC"
        ;;
    reg)
        "$EXEC"
        ;;
    *)
        echo "No run mode selected"
        ;;
esac
