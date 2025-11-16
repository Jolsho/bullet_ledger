#include <bit>
#include <cmath>
#include <cstdlib>
#include "blake3.h"
#include "blst.h"
#include "nodes.h"

bool iszero(const ByteSlice &slice) {
    byte zero_byte{0};
    for (auto &b: slice) if (b != zero_byte) return false;
    return true;
}

uint64_array u64_to_array(uint64_t num) { return std::bit_cast<std::array<byte, 8>>(num); }
uint64_t u64_from_array(uint64_array a) { return std::bit_cast<uint64_t>(a); }

void commit_from_bytes(const byte* src, Commitment &dst) {
    blst_p1_affine aff;
    blst_p1_uncompress(&aff, src);
    blst_p1_from_affine(&dst, &aff);
}

Hash derive_k_vc_hash(const ByteSlice &key, const Commitment &val_c) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, key.data(), key.size());

    auto commit_bytes = compress_p1(val_c);
    blake3_hasher_update(&hasher, 
                         commit_bytes.data(), 
                         commit_bytes.size());
    Hash hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size());

    return hash;
}
