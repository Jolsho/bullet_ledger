# SPDX-License-Identifier: GPL-3.0-only

#!/usr/bin/env bash
set -e

STARTING_DIR=$(pwd);

# Usage: ./fetch_blst.sh /path/to/project
BLST_VERSION="v0.3.15"
BLST_SOURCE="./deps/blst"

# Create deps directory if needed
mkdir -p "./deps"

# Clone or update blst
if [ ! -d "$BLST_SOURCE" ]; then
    echo "Cloning blst..."
    git clone https://github.com/supranational/blst.git "$BLST_SOURCE"
else
    echo "blst already exists..."
    git -C "$BLST_SOURCE" fetch --all
fi

(
    # Checkout the desired version
    cd "$BLST_SOURCE"
    git checkout "$BLST_VERSION"
    git submodule update --init --recursive
    # Only build if libblst.a does not exist
    if [ ! -f "libblst.a" ]; then
        echo "Building blst..."
        ./build.sh
    else
        echo "libblst.a already exists, skipping build."
    fi
)

BLAKE3_SOURCE="./deps/blake3"
# Clone or update blst
if [ ! -d "$BLAKE3_SOURCE" ]; then
    echo "Cloning blst..."
    git clone https://github.com/BLAKE3-team/BLAKE3.git "$BLAKE3_SOURCE"
else
    echo "blake already exists..."
    git -C "$BLAKE3_SOURCE" fetch --all
fi
(
    # Checkout the desired version
    cd "$BLAKE3_SOURCE/c"
    # Only build if libblake3.a does not exist
    if [ ! -f "libblake3.a" ]; then
        echo "Building blake3..."
        cmake -B build
        cmake --build build
        mv build/libblake3.a ../
    else
        echo "libblake3.a already exists, skipping build."
    fi
)

cmake -B build
cmake --build build
