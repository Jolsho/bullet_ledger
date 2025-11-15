#include "nodes.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <tuple>

Leaf::Leaf(optional<NodeId> id, optional<ByteSlice*> buffer) : path_(32){
    if (!id.has_value()) return;
    id_ = id.value();

    if (!buffer.has_value()) return;

    std::span<byte>* buff = buffer.value();

    auto cursor = buff->begin() + 1;
    std::ranges::copy(cursor, cursor + 32, hash_.begin());
    cursor += 32;

    std::ranges::copy(cursor, cursor + 32, value_hash_.begin());
    cursor += 32;

    uint64_array path_len;
    std::ranges::copy(cursor, cursor+8, path_len.begin());
    cursor += 8;

    for (auto i = cursor; i < cursor + u64_from_array(path_len); i++) {
        path_.push_back(*i);
    }
}

Path* Leaf::get_path() { return &path_; }
void Leaf::set_path(ByteSlice path) {
    path_.clear();
    assert(path.size() <= path_.capacity());
    for (auto b : path) path_.push_back(b);
}

Hash* Leaf::get_value_hash() { return &value_hash_; }
void Leaf::set_value_hash(const Hash &hash) { value_hash_ = hash; }

optional<size_t> Leaf::in_path(ByteSlice nibbles) {
    std::size_t matched = 0;
    std::size_t path_size = path_.size();
    std::size_t nibbles_size = nibbles.size();

    while (matched < path_size && matched < nibbles_size) {
        if (path_.get(matched) != nibbles[matched]) {
            break;
        }
        ++matched;
    }

    if (matched == path_size) {
        return std::nullopt;
    } else {
        return matched;
    }
}

NodeId* Leaf::get_next_id(ByteSlice &nibs) { return nullptr; }
void Leaf::change_id(const NodeId &id, Ledger &ledger) { id_ = id; }
Hash Leaf::derive_hash() { return Hash(hash_); }
Hash Leaf::derive_real_hash(const ByteSlice &key) {
    return derive_leaf_hash(key, value_hash_);
}

std::vector<byte> Leaf::to_bytes() {
    std::vector<byte> buffer;
    buffer.reserve(LEAF_SIZE);

    buffer.push_back(LEAF);
    buffer.insert(buffer.end(), hash_.begin(), hash_.end());
    buffer.insert(buffer.end(), value_hash_.begin(), value_hash_.end());
    uint64_array path_len = u64_to_array(path_.size());
    buffer.insert(buffer.end(), path_len.begin(), path_len.end());

    for (auto i = 0; i < path_.size(); i++) {
        byte nib = path_.get(i).value();
        buffer.push_back(nib);
    }
    return buffer;
}

optional<Hash> Leaf::search( 
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    optional<size_t> res = in_path(nibbles);
    if (res.has_value()) return std::nullopt;
    return Hash(value_hash_);
}

optional<Hash> Leaf::virtual_put(
    Ledger &ledger, 
    ByteSlice &nibbles,
    const ByteSlice &key, 
    const Hash &val_hash
) {
    optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) return derive_leaf_hash(key, val_hash);

    size_t shared_path = is.value();

    if (shared_path > 0) nibbles = nibbles.subspan(shared_path);

    Branch branch(std::nullopt, std::nullopt);
    byte self_nib = path_.get(shared_path).value();
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
    const ByteSlice &key, 
    const Hash &val_hash
) {
    optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) {
        set_value_hash(val_hash);
        return derive_real_hash(key);
    }
    size_t shared_path = is.value();

    uint64_t self_id = u64_from_array(id_);

    Node_ptr self = ledger.delete_node(id_).value();

    uint64_t branch_id = self_id;

    optional<Extension*> ext_o = std::nullopt;
    if (shared_path > 0) {

        branch_id = self_id * ORDER;
        Extension* ext = ledger.new_cached_extension(self_id);

        // transfer shared path to new extension
        std::vector<byte> ext_path;
        ext_path.reserve(shared_path);
        while (ext_path.size() < shared_path) {
            byte nib = path_.pop_front().value();
            ext_path.push_back(nib);
        }
        ext->set_path(ext_path);

        nibbles = nibbles.subspan(shared_path);
        ext_o = ext;
    }

    Branch* branch = ledger.new_cached_branch(branch_id);

    // pop off first nibble to act as key for branch
    byte self_nib = path_.pop_front().value();
    uint64_t nib_num = static_cast<uint64_t>(self_nib);

    // update self and insert into branch using nib and recache
    uint64_t new_self_id = (branch_id * ORDER) + nib_num;
    change_id(u64_to_array(new_self_id), ledger);
    branch->insert_child(self_nib, hash_);
    ledger.cache_node(std::move(self));

    // derive a new leaf for new value and insert into branch
    uint64_t nib_num1 = static_cast<uint64_t>(nibbles[0]);
    uint64_t new_leaf_id = (branch_id * ORDER) + nib_num1;
    Leaf* leaf = ledger.new_cached_leaf(new_leaf_id);
    leaf->set_path(nibbles.subspan(1));
    leaf->set_value_hash(val_hash);
    Hash leaf_hash = leaf->derive_real_hash(key);
    branch->insert_child(nibbles[0], leaf_hash);


    // return either branch hash or extension to branch hash
    Hash res = branch->derive_hash();
    if (ext_o.has_value()) {
        ext_o.value()->set_child(res);
        res = ext_o.value()->derive_hash();
    }
    return res;
}

optional<std::tuple<Hash, ByteSlice>> Leaf::remove(
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    Node_ptr self = ledger.delete_node(id_).value();
    return std::make_tuple(Hash{}, ByteSlice{});
}
