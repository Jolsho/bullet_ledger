#include "blake3.h"
#include "nodes.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>

Leaf::Leaf(optional<NodeId> id, optional<ByteSlice*> buffer) {

}
Leaf::~Leaf() {}

Path* Leaf::get_path() { return &path_; }
void Leaf::set_path(ByteSlice path) {
    while (path_.size() > 0) {
        path_.pop_front();
    }
    for (auto b : path) {
        path_.push_back(b);
    }
}

Hash* Leaf::get_value_hash() { return &value_hash_; }
void Leaf::set_value_hash(Hash &hash) {
    std::ranges::copy(hash, value_hash_.begin());
}

optional<size_t> Leaf::in_path(ByteSlice nibbles) {
    std::size_t matched = 0;

    auto it1 = path_.begin();
    auto it2 = nibbles.begin();

    while (it1 != path_.end() && it2 != nibbles.end() && *it1 == *it2) {
        ++matched;
        ++it1;
        ++it2;
    }

    if (matched == path_.size()) {
        return std::nullopt;
    } else {
        return matched;
    }
}

NodeId* Leaf::get_next_id(ByteSlice &nibs) { return nullptr; }

void Leaf::change_id(NodeId &id, Ledger &ledger) {
    std::ranges::copy(id, id_.begin());
}

Hash Leaf::derive_hash(ByteSlice &key) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, 
        key.data(), 
        key.size()
    );
    blake3_hasher_update(&hasher, 
        value_hash_.data(), 
        value_hash_.size()
    );

    Hash hash;
    blake3_hasher_finalize(&hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size());

    std::ranges::copy(hash, hash_.begin());
    return hash;
}

std::vector<byte> Leaf::to_bytes() {
    std::vector<byte> buffer;
    buffer.reserve(LEAF_SIZE);

    buffer[0] = LEAF;
    auto cursor = buffer.begin() + 1;
    std::ranges::copy(hash_, cursor);
    cursor += 32;
    std::ranges::copy(value_hash_, cursor);
    cursor += 32;

    uint64_array path_len = u64_to_array(path_.size());
    std::ranges::copy(path_len, cursor);
    cursor += 8;

    std::ranges::copy(path_, cursor);

    return buffer;
}

optional<Hash> Leaf::search( 
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    optional<size_t> res = in_path(nibbles);
    if (res.has_value()) return std::nullopt;
    Hash hash(value_hash_);
    return hash;
}

optional<Hash> Leaf::virtual_put(
    Ledger &ledger, 
    ByteSlice &nibbles,
    ByteSlice &key, 
    Hash &val_hash
) {
    optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) return derive_leaf_hash(key, val_hash);

    size_t shared_path = is.value();

    if (shared_path > 0) nibbles = nibbles.subspan(shared_path);

    Branch branch(std::nullopt, std::nullopt);
    byte self_nib = path_[shared_path];
    branch.insert_child(self_nib, hash_);

    Hash leaf_hash = derive_leaf_hash(key, val_hash);
    branch.insert_child(nibbles[0], leaf_hash);

    Hash res = branch.derive_hash();
    if (shared_path > 0) res = derive_value_hash(res.data(), res.size());

    return res;
}

optional<Hash> Leaf::put(
    Ledger &ledger, 
    ByteSlice &nibbles, 
    ByteSlice &key, 
    Hash &val_hash
) {
    optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) {
        set_value_hash(val_hash);
        return derive_hash(key);
    }
    size_t shared_path = is.value();

    uint64_t self_id = u64_from_array(id_);

    Node self = ledger.delete_node(id_).value();

    uint64_t branch_id = self_id;

    optional<Extension*> ext_o = std::nullopt;
    if (shared_path > 0) {
        branch_id = self_id * ORDER;
        Extension* ext = ledger.new_cached_extension(self_id);

        // transfer shared path to new extension
        std::vector<byte> ext_path;
        ext_path.reserve(shared_path);
        while (ext_path.size() < shared_path) {
            byte nib = path_.front();
            ext_path.push_back(nib);
            path_.pop_front();
        }
        ext->set_path(ext_path);

        nibbles = nibbles.subspan(shared_path);
        ext_o = ext;
    }

    Branch* branch = ledger.new_cached_branch(branch_id);

    // pop off first nibble to act as key for branch
    byte self_nib = path_.front();
    path_.pop_front();
    uint64_t nib_num = static_cast<uint64_t>(self_nib);

    // update self and insert into branch using nib
    uint64_t new_self_id = (branch_id * ORDER) + nib_num;
    uint64_array raw_self_id = u64_to_array(new_self_id);
    change_id(raw_self_id, ledger);
    branch->insert_child(self_nib, hash_);

    // derive a new leaf for new value and insert into branch
    nib_num = static_cast<uint64_t>(nibbles[0]);
    uint64_t new_leaf_id = (branch_id * ORDER) + nib_num;
    Leaf* leaf = ledger.new_cached_leaf(new_leaf_id);
    leaf->set_path(nibbles.subspan(1));
    leaf->set_value_hash(val_hash);
    Hash leaf_hash = leaf->derive_hash(key);
    branch->insert_child(nibbles[0], leaf_hash);


    // return either branch hash or extension to branch hash
    Hash res = branch->derive_hash();
    if (ext_o.has_value()) {
        Extension* ext = ext_o.value();
        ext->set_child(res);
        res = ext->derive_hash();
    }
    return res;
}

optional<std::tuple<Hash, ByteSlice>> Leaf::remove(
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    Node self = ledger.delete_node(id_).value();
    return std::make_tuple(Hash{}, ByteSlice{});
}
