#!/bin/bash
# build_and_run.sh
set -e

BUILD_DIR="./test_build"
EXEC="$BUILD_DIR/tests/tests_exec"

# Default mode
MODE=${1:-gdb}
NO_RUN=${2:-run}

# Determine flags based on mode
case "$MODE" in
    sanitizer)
        CXX_FLAGS="-g -O0 -fno-inline -gdwarf-4 -fsanitize=address -fno-omit-frame-pointer"
        LINK_FLAGS="-fsanitize=address"
        ;;
    fast)
        CXX_FLAGS="-g -O1 -fno-omit-frame-pointer"
        LINK_FLAGS=""
        ;;
    gdb)
        # Same as sanitizer but launch GDB at the end
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
          -DCMAKE_LINKER_FLAGS_DEBUG="$LINK_FLAGS" \
          -DTESTING=ON
fi

# Build
cmake --build "$BUILD_DIR"

if [ "$NO_RUN" = "run" ]; then
    gdb "$EXEC"
fi

