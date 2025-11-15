#include "blake3.h"
#include "nodes.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <tuple>
#include <vector>

Extension::Extension(std::optional<NodeId> id, std::optional<ByteSlice*> buffer) : path_(32){
    if (!id.has_value()) return;
    id_ = id.value();
    child_id_ = u64_to_array(u64_from_array(id_) * ORDER);

    if (!buffer.has_value()) return;
    ByteSlice* buff = buffer.value();

    auto cursor = buff->begin() + 1;
    std::ranges::copy(cursor, cursor+32, hash_.begin());
    cursor += 32;

    std::ranges::copy(cursor, cursor+32, child_hash_.begin());
    cursor += 32;


    uint64_array path_len;
    std::ranges::copy(cursor, cursor+8, path_len.begin());
    cursor += 8;

    uint64_t len = u64_from_array(path_len);
    std::span<byte> path_span(cursor, len);
    for (byte b : path_span) {
        path_.push_back(b);
    }
}

NodeId* Extension::get_child_id() { return &child_id_; }
Hash* Extension::get_child_hash() { return &child_hash_; }
void Extension::set_child(Hash &hash) {
    child_hash_ = hash;
}

Path* Extension::get_path() { return &path_; }
void Extension::set_path(ByteSlice path) {
    path_.clear();
    for (auto b : path) {
        path_.push_back(b);
    }
}

optional<size_t> Extension::in_path(ByteSlice nibbles) {
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

NodeId* Extension::get_next_id(ByteSlice &nibs) {
    auto res = in_path(nibs);
    if (!res.has_value()) return &child_id_;
    return nullptr;
}

void Extension::change_id(const NodeId &id, Ledger &ledger) {
    uint64_t num = u64_from_array(id);
    id_ = id;

    if (u64_from_array(child_id_) != num * ORDER) {
        // load child_node and delete it from cache/db
        Node_ptr child = ledger.delete_node(child_id_).value();

        // change its children recursively
        child_id_ = u64_to_array(num * ORDER);
        child->change_id(child_id_, ledger);

        // re-cache updated child once its children have been updated
        ledger.cache_node(std::move(child));
    }
}

Hash Extension::derive_hash() {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, 
        child_hash_.data(),
        child_hash_.size());

    Hash hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size());

    hash_ = hash;
    return hash;
}

std::vector<byte> Extension::to_bytes() {
    std::vector<byte> buffer;
    buffer.reserve(EXT_SIZE);

    buffer.push_back(EXT);
    buffer.insert(buffer.end(), hash_.begin(), hash_.end());
    buffer.insert(buffer.end(), child_hash_.begin(), child_hash_.end());

    uint64_array path_len = u64_to_array(path_.size());
    buffer.insert(buffer.end(), path_len.begin(), path_len.end());

    for (auto i = 0; i < path_.size(); i++) {
        byte nib = path_.get(i).value();
        buffer.push_back(nib);
    }

    return buffer;
}

optional<Hash> Extension::search( 
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    if (NodeId* next = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next)) {
            auto remaining_nibbles = nibbles.subspan(path_.size());
            return n->search(ledger, remaining_nibbles);
        }
    }
    return std::nullopt;
}

optional<Hash> Extension::virtual_put(
    Ledger &ledger, 
    ByteSlice &nibbles,
    const ByteSlice &key, 
    const Hash &val_hash
) {
    if (NodeId* next = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next)) {
            auto remaining_nibbles = nibbles.subspan(path_.size());
            auto res = n->virtual_put(ledger, remaining_nibbles, key, val_hash);
            if (res.has_value())
                return derive_value_hash(res.value().data(), 32);
        }
        return std::nullopt;
    }
    // if there is more than one nibble left after removing shared prefix...
    // insert new extension that will point to new leaf from branch
    // else remove/update old self, create new leaf, and point to it directly from branch
    size_t shared_path = in_path(nibbles).value();

    // load branch
    Branch branch(std::nullopt, std::nullopt);

    auto prefix = nibbles.first(shared_path);
    nibbles = nibbles.subspan(shared_path);

    Hash leaf_hash = derive_leaf_hash(key, val_hash);

    branch.insert_child(nibbles[0], leaf_hash);

    // if self.path shares nothing with new key
    // remove old self id
    // connect new_branch to self_copy using popped nibble from self
    // return new_branch to parent
    if (shared_path == 0) {
        byte nib = path_.pop_front().value();

        // handle edge case where pathlen becomes 0.
        if (path_.empty()) {
            branch.insert_child(nib, child_hash_);
        } else {
            branch.insert_child(nib, hash_);
        }

        path_.push_front(nib);
        // parent will point to new_branch instead of self
        return branch.derive_hash();
    }

    // if there is path remaining use it to create new extension
    // new extension will connect new_branch to self.child
    // else point branch to self.child directly.

    byte nib = path_.get(shared_path).value();
    if (path_.size() > shared_path) {
        // new branch points to new extension
        branch.insert_child(nib, hash_);
    } else {
        // point to child
        branch.insert_child(nib, child_hash_);
    }

    // derive final virtual state
    return derive_value_hash(child_hash_.data(), 32);
}

optional<Hash> Extension::put(
    Ledger &ledger, 
    ByteSlice &nibbles, 
    const ByteSlice &key, 
    const Hash &val_hash
) {
    if (NodeId* next = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next)) {

            std::span<byte> remaining_nibbles = nibbles.subspan(path_.size());

            optional<Hash> hash = n->put(ledger, remaining_nibbles, key, val_hash);

            if (hash.has_value()) return derive_value_hash(hash.value().data(), 32);
        }
        return std::nullopt;
    }

    uint64_t self_id = u64_from_array(id_);
    uint64_t branch_id = self_id * ORDER;


    // if there is more than one nibble left after removing shared prefix...
    // insert new extension that will point to new leaf from branch
    // else remove/update old self, create new leaf, and point to it directly from branch
    size_t shared_path = in_path(nibbles).value();
    optional<Node_ptr> self = std::nullopt;
    if (shared_path == 0) {
        // delete old id self if exists
        // hold onto reference to handle later though
        self = ledger.delete_node(id_);

        branch_id = self_id;
    }

    // load self.child and invalidate its cache entry
    // have to put this here(odd spot)
    // because it can collide with a new child id
    Node_ptr child = ledger.delete_node(child_id_).value();

    // load branch
    Branch* branch = ledger.new_cached_branch(branch_id);

    auto prefix = nibbles.first(shared_path);
    nibbles = nibbles.subspan(shared_path);

    uint64_t idx = static_cast<uint64_t>(nibbles[0]);
    uint64_t new_branch_child_id = (branch_id * ORDER) + idx;

    Leaf* leaf = ledger.new_cached_leaf(new_branch_child_id);
    leaf->set_value_hash(val_hash);
    leaf->set_path(nibbles.subspan(1));

    Hash leaf_hash = leaf->derive_real_hash(key);
    branch->insert_child(nibbles[0], leaf_hash);

    // if self.path shares nothing with new key
    // remove old self id
    // connect new_branch to self_copy using popped nibble from self
    // return new_branch to parent
    if (shared_path == 0) {
        byte nib = path_.pop_front().value();
        uint64_t nib_num = static_cast<uint64_t>(nib);

        // handle edge case where pathlen becomes 0.
        uint64_t new_child_id;
        if (path_.empty()) {
            new_child_id = (branch_id * ORDER) + nib_num;
            uint64_array new_id_raw = u64_to_array(new_child_id);
            child->change_id(new_id_raw, ledger);
            branch->insert_child(nib, child_hash_);
            
        } else {
            uint64_t new_id = (branch_id * ORDER) + nib_num;

            // give old child an updated id
            new_child_id = new_id * ORDER;
            uint64_array new_id_raw = u64_to_array(new_child_id);
            child->change_id(new_id_raw, ledger);


            // recover self change_id and reinsert into cache
            uint64_array new_raw_id = u64_to_array(new_id);
            self.value()->change_id(new_raw_id, ledger);
            Hash self_hash = self.value()->derive_hash();
            branch->insert_child(nib, self_hash);

            ledger.cache_node(std::move(self.value()));
        }

        ledger.cache_node(std::move(child));

        // parent will point to new_branch instead of self
        return branch->derive_hash();
    }

    // if there is path remaining use it to create new extension
    // new extension will connect new_branch to self.child
    // else point branch to self.child directly.
    std::vector<byte> remaining;
    remaining.reserve(path_.size() - shared_path);
    for (int i = 0; i < shared_path; i++) {
        byte nib = path_.pop_back().value();
        remaining.push_back(nib);
    }

    byte nib = remaining.back();
    remaining.pop_back();

    uint64_t nib_num = static_cast<uint64_t>(nib);

    uint64_t new_child_id;
    if (remaining.size() > 0) {
        uint64_t new_ext_id = (branch_id * ORDER) + nib_num;

        // create new extension to point to self.child
        Extension* new_ext = ledger.new_cached_extension(new_ext_id);
        new_ext->set_path(remaining);

        // derive new child id
        new_child_id = new_ext_id * ORDER;
        uint64_array new_id = u64_to_array(new_child_id);
        child->change_id(new_id, ledger);

        // set new child hash
        new_ext->set_child(*child->get_hash());

        // new branch points to new extension
        Hash ext_hash = new_ext->derive_hash();
        branch->insert_child(nib, ext_hash);

    } else {
        // derive and set new child id
        new_child_id = (branch_id * ORDER) + nib_num;
        uint64_array new_id = u64_to_array(new_child_id);
        child->change_id(new_id, ledger);

        // point branch to self.child
        Hash child_hash = child->derive_hash();
        branch->insert_child(nib, child_hash);
    }

    // have to recache old_child
    ledger.cache_node(std::move(child));

    // set self to point to branch return self
    Hash branch_hash = branch->derive_hash();
    set_child(branch_hash);
    return derive_hash();
}

optional<std::tuple<Hash, ByteSlice>> Extension::remove(
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    if (NodeId* next = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next)) {
            std::span<byte> remaining_nibbles = nibbles.subspan(path_.size());

            auto response = n->remove(ledger, remaining_nibbles);
            if (!response.has_value()) return std::nullopt;

            auto [hash, path_ext] = response.value();

            if (path_ext.size() > 0) 
                for (auto nib: path_ext) 
                    path_.push_back(nib);

            if (is_zero(hash)) {
                ledger.delete_node(id_);
                return std::make_tuple(hash, path_ext);
            }

            set_child(hash);
            return std::make_tuple(derive_hash(), ByteSlice{});
        }
    }
    return std::nullopt;
}
