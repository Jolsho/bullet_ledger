/*
 * Bullet Ledger
 * Copyright (C) 2025 Joshua Olson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <cassert>

void main_state_trie() {
    // namespace fs = std::filesystem;
    // const char* path = "./fake_db";
    // if (fs::exists(path)) fs::remove_all(path);
    // fs::create_directory(path);
    //
    // Ledger l(path, 6, 
    //     10 * 1024 * 1024, 
    //     "bullet", 
    //     num_scalar(13)
    // );
    //
    // std::vector<Hash> raw_hashes;
    // raw_hashes.reserve(256);
    // for (int i = 0; i < raw_hashes.capacity(); i++) {
    //     Hash rnd = seeded_hash(i);
    //     raw_hashes.push_back(rnd);
    // }
    //
    //
    // // --- Insert phase ---
    // int i = 0;
    // for (Hash h: raw_hashes) {
    //     ByteSlice key(h.data(), h.size());
    //     ByteSlice value(h.data(), h.size());
    //
    //     auto virt_root = l.virtual_put(key, value, 1).value();
    //
    //     auto root = l.put(key, value, 1).value();
    //
    //     byte virt[48];
    //     virt_root.compress(virt);
    //
    //     byte r[48];
    //     root.compress(r);
    //
    //     assert(std::equal(virt, virt+48, r));
    //
    //     ByteSlice got = l.get_value(key, 1).value();
    //     assert(std::equal(got.begin(), got.end(), value.begin()));
    //     printf("INSERT %d\n", i);
    //     i++;
    // }
    //
    // // --- Proving phase ---
    // i = 0;
    // for (Hash h: raw_hashes) {
    //     ByteSlice key(h.data(), h.size());
    //     auto [C, Pi, Ws, Ys, Zs] = l.get_existence_proof(key, 1).value();
    //     assert(multi_func_multi_point_verify(Ws, Zs, Ys, Pi, *l.get_srs()));
    //
    //     byte raw[32]; Zs[0].to_lendian(raw);
    //     if (raw[0] != 0) raw[0] = 0;    // TODO what is this??
    //     else raw[0] = 1;
    //     assert(!multi_func_multi_point_verify(Ws, Zs, Ys, Pi, *l.get_srs()));
    //     printf("PROVED %d\n", i);
    //     i++;
    // }
    //
    // // --- Remove phase ---
    // i = 0;
    // for (Hash h: raw_hashes) {
    //     std::span<byte> key(h.data(), h.size());
    //     l.remove(key, 1);
    //     auto got = l.get_value(key, 1);
    //     assert(got.has_value() == false);
    //     printf("DELETED %d\n", i);
    //     i++;
    // }
    //
    //
    // fs::remove_all("./fake_db");
    //
    // printf("VERKLE STATE SUCCESSFUL \n");
}
