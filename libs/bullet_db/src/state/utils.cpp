#include <bit>
#include <cstdlib>
#include "blake3.h"
#include "nodes.h"

uint64_array u64_to_array(uint64_t num) {
    return std::bit_cast<std::array<byte, 8>>(num);
}

uint64_t u64_from_array(uint64_array a) {
    return std::bit_cast<uint64_t>(a);
}

bool is_zero(std::span<std::byte> s) {
    std::byte zero {0};
    for (auto b : s) {
        if (b != zero) return false;
    }
    return true;
}

Hash derive_leaf_hash(ByteSlice &key, Hash &hash) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, 
        key.data(), 
        key.size()
    );
    blake3_hasher_update(
        &hasher, 
        hash.data(), 
        hash.size()
    );

    Hash derived_hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(derived_hash.data()), 
        derived_hash.size());
    return derived_hash;
}

Hash derive_value_hash(byte* value, size_t size) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, value, size);

    Hash hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size());

    return hash;
}


