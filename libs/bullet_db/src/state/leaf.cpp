#include "verkle.h"
#include <cstdint>
#include <cstdio>
#include <optional>

Leaf::Leaf(std::optional<NodeId> id, std::optional<ByteSlice*> buffer) : 
    path_(32), children_(ORDER), commit_{new_p1()}, count_{0}
{
    if (id.has_value()) id_ = id.value();

    for (auto &c: children_) c = nullptr;
    if (!buffer.has_value()) { return; }

    std::span<byte>* buff = buffer.value();

    commit_from_bytes(buff->subspan(1,48).data(), &commit_);

    auto cursor = buff->begin() + 49;

    std::array<byte, 2> len{};
    std::ranges::copy(cursor, cursor+2, len.begin());
    cursor += 2;

    uint16_t path_len = std::bit_cast<uint16_t>(len);
    for (auto i = cursor; i < cursor + path_len; i++) {
        path_.push_back(*i);
    }
    cursor += path_len;
    while (cursor < buff->end()) {
        byte nib(*cursor);
        cursor += 1;
        Hash h;
        std::ranges::copy(cursor, cursor + 32, h.begin());
        children_[nib] = std::make_unique<Hash>(h);
        cursor += 32;
        count_++;
    }
}
void Leaf::insert_child(const byte &nib, const Hash &val_hash) {
    if (!children_[nib].get()) count_++;
    children_[nib] = std::make_unique<Hash>(val_hash);
}

Path* Leaf::get_path() { return &path_; }
void Leaf::set_path(ByteSlice path) {
    path_.clear(); for (auto b : path) path_.push_back(b);
}

void Leaf::change_id(const NodeId &id, Ledger &ledger) { id_ = id; }
NodeId* Leaf::get_next_id(ByteSlice &nibs) { return nullptr; }

const Commitment* Leaf::derive_commitment(Ledger &ledger) { 
    scalar_vec* Fx = ledger.get_poly();
    for (auto i = 0; i < ORDER; i++) {
        if (Hash* h = children_[i].get())
            blst_scalar_from_le_bytes(
                &Fx->at(i), h->data(), h->size()
            );
    }
    commit_ = commit_g1_projective(*Fx, *ledger.get_srs());
    return &commit_; 
}

std::optional<size_t> Leaf::in_path(ByteSlice nibbles) {
    std::size_t matched = 0;
    std::size_t path_size = path_.size();
    std::size_t nibbles_size = nibbles.size();

    while (matched < path_size && matched < nibbles_size) {
        if (path_.get(matched) != nibbles[matched]) {
            break;
        }
        ++matched;
    }
    if (matched == path_size) return std::nullopt;
    else return matched;
}


std::vector<byte> Leaf::to_bytes() const {
    std::vector<byte> buffer;
    buffer.reserve(LEAF_SIZE);

    buffer.push_back(LEAF);
    auto commit_bytes = compress_p1(&commit_);
    buffer.insert(buffer.end(), commit_bytes.begin(), commit_bytes.end());

    auto path_len = std::bit_cast<std::array<byte, 2>>(static_cast<uint16_t>(path_.size()));
    buffer.insert(buffer.end(), path_len.begin(), path_len.end());

    for (auto i = 0; i < path_.size(); i++)
        buffer.push_back(path_.get(i).value());

    for (auto i = 0; i < ORDER; i++) {
        if (Hash* h = children_[i].get()) {
            buffer.push_back(i);
            buffer.insert(buffer.end(), h->begin(), h->end());
        }
    }

    return buffer;
}

std::optional<Hash> Leaf::search( 
    Ledger &ledger, 
    ByteSlice nibbles
) {
    std::optional<size_t> res = in_path(nibbles);
    if (res.has_value() || !children_[nibbles.back()].get()) return std::nullopt;
    return Hash(*children_[nibbles.back()].get());
}

int Leaf::build_commitment_path(
    Ledger &ledger, 
    const Hash &key,
    ByteSlice nibbles,
    vector<scalar_vec> &Fxs, 
    Bitmap &Zs
) { return true; }

std::optional<Commitment> Leaf::virtual_put(
    Ledger &ledger, 
    ByteSlice nibbles,
    const Hash &key,
    const Hash &val_hash
) {
    std::optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) {
        auto original_child(std::move(children_[nibbles.front()]));

        insert_child(nibbles.back(), val_hash);
        auto new_commit(*derive_commitment(ledger));

        children_[nibbles.front()] = std::move(original_child);
        count_--;

        return new_commit;
    }

    size_t shared_path = is.value();

    vector<byte> shared_nibs; 
    shared_nibs.reserve(shared_path);

    for (auto i = 0; i < shared_path; i++)
        shared_nibs.push_back(path_.get(i).value());

    if (shared_path > 0) nibbles = nibbles.subspan(shared_path);
    
    // create lowest branch
    Branch branch(std::nullopt, std::nullopt);

    // insert current
    branch.insert_child(path_.get(shared_path).value(), &commit_);

    // insert new
    Leaf leaf(std::nullopt, std::nullopt);
    leaf.insert_child(nibbles.back(), val_hash);
    const Commitment* new_commit = leaf.derive_commitment(ledger);
    branch.insert_child(nibbles.front(), new_commit);

    // work up the tree inserting children and passing commitment upward
    Commitment child_commit = *branch.derive_commitment(ledger);
    for (size_t i = shared_nibs.size(); i-- > 0;)
        child_commit = derive_init_commit(shared_nibs[i], child_commit, ledger);

    return child_commit;
}

std::optional<const Commitment*> Leaf::put(
    Ledger &ledger, 
    ByteSlice nibbles,
    const Hash &key,
    const Hash &val_hash
) {

    std::optional<size_t> is = in_path(nibbles);
    if (!is.has_value()) {
        insert_child(nibbles.back(), val_hash);
        return derive_commitment(ledger);
    }

    size_t shared_path = is.value();

    Node_ptr self = ledger.delete_node(id_).value();

    uint64_t new_id = u64_from_array(id_);

    vector<std::tuple<Branch*, byte>> branches; 
    branches.reserve(shared_path);

    for (auto i = 0; i < shared_path; i++) {
        Branch* branch = ledger.new_cached_branch(new_id);
        branches.push_back(std::make_tuple(branch, path_.pop_front().value()));
        new_id = (new_id * ORDER) + nibbles.front();
    }
    if (shared_path > 0) nibbles = nibbles.subspan(shared_path);

    Branch* branch = ledger.new_cached_branch(new_id);
    new_id *= ORDER;

    // pop off nibble to act as key for parent
    // insert existing leaf into branch
    byte nib = path_.pop_front().value();
    change_id(u64_to_array(new_id + nib), ledger);
    branch->insert_child(nib, &commit_);
    ledger.cache_node(std::move(self));

    // derive a new leaf for new value and insert into branch
    Leaf* leaf = ledger.new_cached_leaf(new_id + nibbles.front());
    leaf->set_path(nibbles.subspan(1, nibbles.size() - 2));
    leaf->insert_child(nibbles.back(), val_hash);
    const Commitment* new_commit = leaf->derive_commitment(ledger);
    branch->insert_child(nibbles.front(), new_commit);

    // work up the tree inserting children and passing commitment upward
    const Commitment* child_commit = branch->derive_commitment(ledger);
    for (size_t i = branches.size(); i-- > 0; ) {
        auto [branch, child_key] = branches[i];

        // insert previous commitment into current branch using key
        branch->insert_child(child_key, child_commit);

        // update child commitment
        child_commit = branch->derive_commitment(ledger);
    }
    return child_commit;
}

std::optional<std::tuple<Commitment, bool>> Leaf::remove(
    Ledger &ledger, 
    ByteSlice nibbles,
    const Hash &key
) {
    std::optional<size_t> is = in_path(nibbles);
    if (is.has_value()) return std::nullopt;

    Commitment c = new_p1();
    if (children_[nibbles.back()]) {
        Hash v_hash(*children_[nibbles.back()].get());
        Hash kv = derive_kv_hash(key, v_hash);
        ledger.delete_value(kv);

        children_[nibbles.back()] = nullptr;
        count_--;
        c = *derive_commitment(ledger);
    }
    bool destroyed = count_ == 0;
    if (destroyed) ledger.delete_node(id_);

    return std::make_tuple(c, destroyed);
}
