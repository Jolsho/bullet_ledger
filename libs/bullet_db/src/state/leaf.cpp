#include "verkle.h"

Leaf::Leaf(optional<NodeId> id, optional<ByteSlice*> buffer) : path_(32){
    commit_ = new_p1();
    if (!id.has_value()) return;
    id_ = id.value();

    if (!buffer.has_value()) return;

    std::span<byte>* buff = buffer.value();

    commit_from_bytes(buff->subspan(1,49).data(), commit_);

    auto cursor = buff->begin() + 49;

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
    for (auto b : path) path_.push_back(b);
}

void Leaf::set_commitment(const Commitment &c) { commit_ = c; }
void Leaf::change_id(const NodeId &id, Ledger &ledger) { id_ = id; }
Commitment* Leaf::derive_commitment(Ledger &ledger) { return &commit_; }
NodeId* Leaf::get_next_id(ByteSlice &nibs) { return nullptr; }

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


std::vector<byte> Leaf::to_bytes() {
    std::vector<byte> buffer;
    buffer.reserve(LEAF_SIZE);

    buffer.push_back(LEAF);
    auto commit_bytes = compress_p1(commit_);
    buffer.insert(buffer.end(), commit_bytes.begin(), commit_bytes.end());

    uint64_array path_len = u64_to_array(path_.size());
    buffer.insert(buffer.end(), path_len.begin(), path_len.end());

    for (auto i = 0; i < path_.size(); i++) {
        byte nib = path_.get(i).value();
        buffer.push_back(nib);
    }
    return buffer;
}

optional<Commitment> Leaf::search( 
    Ledger &ledger, 
    ByteSlice nibbles
) {
    optional<size_t> res = in_path(nibbles);
    if (res.has_value()) return std::nullopt;
    return commit_;
}

optional<Commitment> Leaf::virtual_put(
    Ledger &ledger, 
    ByteSlice nibbles,
    const ByteSlice &key, 
    const Commitment &val_commitment
) {
    optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) return Commitment(val_commitment);

    size_t shared_path = is.value();

    if (shared_path > 0) nibbles = nibbles.subspan(shared_path);

    Branch branch(std::nullopt, std::nullopt);
    byte self_nib = path_.get(shared_path).value();
    branch.insert_child(self_nib, commit_);

    branch.insert_child(nibbles[0], val_commitment);

    return *branch.derive_commitment(ledger);
}

optional<Commitment*> Leaf::put(
    Ledger &ledger, 
    ByteSlice nibbles,
    const ByteSlice &key, 
    const Commitment &val_commitment
) {
    optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) {
        set_commitment(val_commitment);
        return &commit_;
    }
    size_t shared_path = is.value();

    uint64_t self_id = u64_from_array(id_);

    Node_ptr self = ledger.delete_node(id_).value();

    uint64_t branch_id = self_id;

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
    }

    Branch* branch = ledger.new_cached_branch(branch_id);

    // pop off first nibble to act as key for branch
    byte self_nib = path_.pop_front().value();
    uint64_t nib_num = static_cast<uint64_t>(self_nib);

    // update self and insert into branch using nib and recache
    uint64_t new_self_id = (branch_id * ORDER) + nib_num;
    change_id(u64_to_array(new_self_id), ledger);
    branch->insert_child(self_nib, commit_);
    ledger.cache_node(std::move(self));

    // derive a new leaf for new value and insert into branch
    uint64_t nib_num1 = static_cast<uint64_t>(nibbles[0]);
    uint64_t new_leaf_id = (branch_id * ORDER) + nib_num1;
    Leaf* leaf = ledger.new_cached_leaf(new_leaf_id);
    leaf->set_path(nibbles.subspan(1));
    leaf->set_commitment(val_commitment);
    branch->insert_child(nibbles[0], val_commitment);


    // return either branch hash or extension to branch hash
    return branch->derive_commitment(ledger);
}

optional<std::tuple<Commitment, bool, ByteSlice>> Leaf::remove(
    Ledger &ledger, 
    ByteSlice nibbles
) {
    Node_ptr self = ledger.delete_node(id_).value();
    return std::make_tuple(Commitment{}, true, ByteSlice{});
}
