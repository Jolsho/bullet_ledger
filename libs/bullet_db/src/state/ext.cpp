#include "blake3.h"
#include "nodes.h"
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

Extension::Extension(std::optional<NodeId> id, std::optional<ByteSlice*> buffer) {
    if (!id.has_value()) return;
    id_ = id.value();

    if (!buffer.has_value()) return;
    ByteSlice* buff = buffer.value();
    auto cursor = buff->begin();
    std::ranges::copy(cursor, cursor+32, hash_.begin());
    cursor += 32;

    std::ranges::copy(cursor, cursor+32, child_hash_.begin());
    cursor += 32;

    uint64_t self_id = u64_from_array(id_);
    child_id_ = u64_to_array(self_id * ORDER);

    uint64_array path_len;
    std::ranges::copy(cursor, cursor+8, path_len.begin());
    cursor += 8;

    uint64_t path_len_n = u64_from_array(path_len);
    path_.resize(path_len_n);
    std::ranges::copy(cursor, cursor + path_len_n, path_.begin());
}

Extension::~Extension() {}

NodeId* Extension::get_child_id() { return &child_id_; }
Hash* Extension::get_child_hash() { return &child_hash_; }
void Extension::set_child(Hash &hash) {
    std::ranges::copy(hash, child_hash_.begin());
}

Path* Extension::get_path() { return &path_; }
void Extension::set_path(ByteSlice path) {
    while (path_.size() > 0) {
        path_.pop_front();
    }
    for (auto b : path) {
        path_.push_back(b);
    }
}

optional<size_t> Extension::in_path(ByteSlice nibbles) {
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

NodeId* Extension::get_next_id(ByteSlice &nibs) {
    auto res = in_path(nibs);
    if (!res.has_value()) { return &child_id_; }
    return nullptr;
}

void Extension::change_id(NodeId &id, Ledger &ledger) {
    uint64_t num = u64_from_array(id);
    std::ranges::copy(id, id_.begin());

    if (u64_from_array(child_id_) != num * ORDER) {
        // load child_node and delete it from cache/db
        Node child = ledger.delete_node(child_id_).value();

        // change its children recursively
        std::ranges::copy(u64_to_array(num * ORDER), child_id_.begin());

        // re-cache updated child once its children have been updated
        ledger.cache_node(num * ORDER, child);
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

    std::ranges::copy(hash, hash_.begin());
    return hash;
}

std::vector<byte> Extension::to_bytes() {
    std::vector<byte> buffer;
    buffer.reserve(EXT_SIZE);

    buffer[0] = EXT;
    auto cursor = buffer.begin() + 1;
    std::ranges::copy(hash_, cursor);
    cursor += 32;

    std::ranges::copy(child_hash_, cursor);
    cursor += 32;

    uint64_array path_len = u64_to_array(path_.size());
    std::ranges::copy(path_len, cursor);
    cursor += 8;

    std::ranges::copy(path_, cursor);

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
    ByteSlice &key, 
    Hash &val_hash
) {
    if (NodeId* next = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next)) {
            auto remaining_nibbles = nibbles.subspan(path_.size());
            auto res = n->virtual_put(ledger, remaining_nibbles, key, val_hash);
            if (res.has_value()) {
                auto hash = derive_value_hash(res.value().data(), 32);
                return hash;
            }
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
        byte nib = path_.front();

        // handle edge case where pathlen becomes 0.
        if (path_.size() == 1) {
            branch.insert_child(nib, child_hash_);
        } else {
            branch.insert_child(nib, hash_);
        }
        // parent will point to new_branch instead of self
        return branch.derive_hash();
    }

    // if there is path remaining use it to create new extension
    // new extension will connect new_branch to self.child
    // else point branch to self.child directly.

    byte nib = path_[shared_path];
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
    ByteSlice &key, 
    Hash &val_hash
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
    optional<Node> self = std::nullopt;
    if (shared_path == 0) {
        // delete old id self if exists
        // hold onto reference to handle later though
        self = ledger.delete_node(id_);

        branch_id = self_id;
    }

    // load self.child and invalidate its cache entry
    // have to put this here(odd spot)
    // because it can collide with a new child id
    Node child = ledger.delete_node(child_id_).value();

    // load branch
    Branch* branch = ledger.new_cached_branch(branch_id);

    auto prefix = nibbles.first(shared_path);
    nibbles = nibbles.subspan(shared_path);


    uint64_t idx = static_cast<uint64_t>(nibbles[0]);
    uint64_t new_branch_child_id = (branch_id * ORDER) + idx;

    Leaf* leaf = ledger.new_cached_leaf(new_branch_child_id);
    leaf->set_value_hash(val_hash);
    leaf->set_path(nibbles.subspan(1));

    Hash leaf_hash = leaf->derive_hash(key);
    branch->insert_child(nibbles[0], leaf_hash);

    // if self.path shares nothing with new key
    // remove old self id
    // connect new_branch to self_copy using popped nibble from self
    // return new_branch to parent
    if (shared_path == 0) {
        byte nib = path_.front();
        path_.pop_front();
        uint64_t nib_num = static_cast<uint64_t>(nib);

        // handle edge case where pathlen becomes 0.
        uint64_t new_child_id;
        if (path_.size() == 0) {
            new_child_id = (branch_id * ORDER) + nib_num;
            uint64_array new_id_raw = u64_to_array(new_child_id);
            child.change_id(new_id_raw, ledger);
            branch->insert_child(nib, child_hash_);
            
        } else {
            uint64_t new_id = (branch_id * ORDER) + nib_num;

            // give old child an updated id
            new_child_id = new_id * ORDER;
            uint64_array new_id_raw = u64_to_array(new_child_id);
            child.change_id(new_id_raw, ledger);

            // recover self change_id and reinsert into cache
            Node s = self.value();
            uint64_array new_raw_id = u64_to_array(new_id);
            s.change_id(new_raw_id, ledger);
            Hash self_hash = self->derive_hash();
            branch->insert_child(nib, self_hash);
        }

        ledger.cache_node(new_child_id, child);

        // parent will point to new_branch instead of self
        return branch->derive_hash();
    }

    // if there is path remaining use it to create new extension
    // new extension will connect new_branch to self.child
    // else point branch to self.child directly.
    std::vector<byte> remaining(path_.size() - shared_path);
    remaining.assign(path_.begin() + shared_path, path_.end());
    path_.erase(path_.begin() + shared_path, path_.end());

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
        child.change_id(new_id, ledger);

        // set new child hash
        new_ext->set_child(*child.get_hash());

        // new branch points to new extension
        Hash ext_hash = new_ext->derive_hash();
        branch->insert_child(nib, ext_hash);

    } else {
        // derive and set new child id
        new_child_id = (branch_id * ORDER) + nib_num;
        uint64_array new_id = u64_to_array(new_child_id);
        child.change_id(new_id, ledger);

        // point branch to self.child
        Hash child_hash = child.derive_hash();
        branch->insert_child(nib, child_hash);
    }

    // have to recache old_child
    ledger.cache_node(new_child_id, child);

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
