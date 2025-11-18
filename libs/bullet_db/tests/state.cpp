#include "../src/state/verkle.h"
#include <cassert>
#include <filesystem>
#include <random>

Hash pseudo_random_hash(int i) {
    std::mt19937_64 gen(i);            // 64-bit PRNG
    std::uniform_int_distribution<uint64_t> dist;

    Hash hash;

    for (int i = 0; i < 4; ++i) {        // 4 * 8 bytes = 32 bytes
        uint64_t num = dist(gen);
        for (int j = 0; j < 8; ++j) {
            hash[i*8 + j] = static_cast<byte>((num >> (8 * j)) & 0xFF);
        }
    }
    return hash;
}

void main_state_trie() {
    namespace fs = std::filesystem;
    const char* path = "./fake_db";
    if (fs::exists(path)) {
        fs::remove_all(path);
    }
    fs::create_directory(path);

    Ledger l(path, 128, 10 * 1024 * 1024, new_scalar(13));

    vector<Hash> raw_hashes;
    raw_hashes.reserve(100);
    for (int i = 0; i < raw_hashes.capacity(); i++) {
        Hash rnd = pseudo_random_hash(i);
        raw_hashes.push_back(rnd);
    }


    // --- Insert phase ---
    int i = 0;
    for (Hash h: raw_hashes) {
        ByteSlice key(h.data(), h.size());
        ByteSlice value(h.data(), h.size());

        auto virt_root = l.virtual_put(key, value, 1).value();

        auto root = l.put(key, value, 1).value();

        auto virt = compress_p1(&virt_root);
        assert(std::equal(virt.begin(), virt.end(), compress_p1(&root).begin()));

        ByteSlice got = l.get_value(key, 1).value();
        assert(std::equal(got.begin(), got.end(), value.begin()));
        printf("INSERT %d\n", i);
        i++;
    }

    // --- Remove phase ---
    i = 0;
    for (Hash h: raw_hashes) {
        std::span<byte> key(h.data(), h.size());
        l.remove(key, 1);
        auto got = l.get_value(key, 1);
        assert(got.has_value() == false);
        printf("DELTED %d\n", i);
        i++;
    }

    fs::remove_all("./fake_db");

    printf("VERKLE STATE SUCCESSFUL \n");
}
