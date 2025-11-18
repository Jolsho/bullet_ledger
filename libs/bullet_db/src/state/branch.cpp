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

#include "verkle.h"

Branch::Branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff) : 
    children_(ORDER), commit_{new_p1()}, count_{0}
{
    if (id.has_value()) id_ = id.value();

    for (auto &c: children_) c = nullptr;
    if (!buff.has_value()) { return; }

    ByteSlice* buffer = buff.value();

    auto current = buffer->subspan(1,48);
    commit_from_bytes(current.data(), &commit_);

    auto cursor = buffer->begin() + 51;
    while (cursor < buffer->end()) {
        byte nib(*cursor);
        cursor += 1;
        children_[nib] = std::make_unique<Commitment>(new_p1());
        commit_from_bytes((cursor).base(), children_[nib].get());
        cursor += 48;
        count_++;
    }
}

void Branch::insert_child(const byte &nib, const Commitment* new_commit) {
    if (!children_[nib].get()) count_++;
    children_[nib] = std::make_unique<Commitment>(*new_commit);
}

void Branch::delete_child(const byte &nib) {
    auto child = std::move(children_[nib]);
    if (child.get()) {
        children_[nib] = nullptr;
        count_--;
    }
}

const std::optional<Commitment*> Branch::get_child(const byte &nib) const {
    if (!children_[nib].get()) return std::nullopt;
    return children_[nib].get();
}


const std::tuple<byte, Commitment, NodeId> Branch::get_last_remaining_child() const {
    for (size_t i = 0; i < ORDER; i++) {
        auto child = get_child(static_cast<byte>(i));
        if (child.has_value()) {
            uint64_t id = u64_from_array(id_) * ORDER + i;
            return std::make_tuple(
                static_cast<byte>(i), 
                *child.value(),
                u64_to_array(id)
            );
        }
    }
    return std::make_tuple(byte(0), Commitment{}, NodeId{});
}

NodeId* Branch::get_next_id(ByteSlice &nibs) {
    if (get_child(nibs.front())) {
        uint64_t id = u64_from_array(id_) * ORDER + nibs.front();
        tmp_id_ = u64_to_array(id);
        return &tmp_id_;
    } else {
        return nullptr;
    }
}

void Branch::change_id(const NodeId &new_id, Ledger &ledger) {
    uint64_t num = u64_from_array(new_id);

    for (uint64_t i = 0; i < ORDER; i++) {
        if (!children_[i]) continue;  // no child here, skip

        // Compute old and new IDs
        uint64_t old_child_id = u64_from_array(id_) * ORDER + i;
        uint64_t new_child_id = num * ORDER + i;

        if (old_child_id == new_child_id) continue;

        // Prevent accidental deletion of the root
        if (old_child_id == 1) continue;

        // Remove the child node from cache/db safely
        std::optional<Node_ptr> node = ledger.delete_node(u64_to_array(old_child_id));

        // Recursively update children safely
        node.value().get()->change_id(u64_to_array(new_child_id), ledger);

        // Re-cache the node
        ledger.cache_node(std::move(node.value()));
    }

    // Update this branch's own ID after all children are safe
    id_ = new_id;
}

const Commitment* Branch::derive_commitment(Ledger &ledger) {
    scalar_vec* Fx = ledger.get_poly();
    for (auto i = 0; i < ORDER; i++) {
        if (Commitment* c = children_[i].get())
            Fx->at(i) = p1_to_scalar(c);
    }
    commit_ = commit_g1_projective(*Fx, *ledger.get_srs());
    return &commit_;
}

std::vector<byte> Branch::to_bytes() const {
    std::vector<byte> buffer;
    buffer.reserve(BRANCH_SIZE);

    buffer.push_back(BRANCH);

    std::array<byte, 48> commit_bytes;
    commit_bytes = compress_p1(&commit_);
    buffer.insert(buffer.end(), commit_bytes.begin(), commit_bytes.end());

    auto count_bytes = std::bit_cast<std::array<byte,2>>(count_);
    buffer.insert(buffer.end(), count_bytes.begin(), count_bytes.end());

    for (auto i = 0; i < ORDER; i++) {
        if (Commitment* c = children_[i].get()) {
            buffer.push_back(i);
            commit_bytes = compress_p1(c);
            buffer.insert(buffer.end(), 
                          commit_bytes.begin(), 
                          commit_bytes.end());
        }
    }

    return buffer;
}

std::optional<Hash> Branch::search(Ledger &ledger, ByteSlice nibbles) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            return n->search(ledger, nibbles.subspan(1));
        }
    }
    return std::nullopt;
}

int Branch::build_commitment_path(
    Ledger &ledger, 
    const Hash &key,
    ByteSlice nibbles,
    vector<scalar_vec> &Fxs, 
    Bitmap &Zs
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            ByteSlice remaining = nibbles.subspan(1);
            int ok = n->build_commitment_path(ledger, key, remaining, Fxs, Zs);
            if (!ok) return false;
        } else return false;
    } else return false;

    // build Fx
    scalar_vec fx(ORDER, new_scalar());
    for (auto i = 0; i < ORDER; i++) {
        if (children_[i]) {
            auto c = children_[i].get();
            fx[i] = p1_to_scalar(c);
        }
    }

    Fxs.push_back(fx);
    Zs.set(key[0]);
    return true;
}

std::optional<Commitment> Branch::virtual_put(
    Ledger &ledger, 
    ByteSlice nibbles,
    const Hash &key, 
    const Hash &val_hash
) {
    Commitment new_commit = new_p1();
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            auto res = n->virtual_put(ledger, nibbles.subspan(1), key, val_hash);
            if (!res.has_value()) return std::nullopt;

            new_commit = res.value();

        } else return std::nullopt;
    } else {

        // commit to hash via last bit of key to emulate leaf commitment
        scalar_vec Fx(ORDER, new_scalar());
        blst_scalar_from_lendian(&Fx[nibbles.back()], val_hash.data());
        new_commit = commit_g1_projective(Fx, *ledger.get_srs());
    }

    Commitment original_commit(commit_);
    std::unique_ptr<Commitment> original_child(std::move(children_[nibbles.front()]));

    // derive virtual commit
    insert_child(nibbles.front(), &new_commit);
    Commitment virtual_commit(*derive_commitment(ledger));

    // return original state
    commit_ = original_commit;
    children_[nibbles.front()].swap(original_child);
    count_--;

    return virtual_commit;
}

std::optional<const Commitment*> Branch::put(
    Ledger &ledger, 
    ByteSlice nibbles, 
    const Hash &key, 
    const Hash &val_hash
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {

            auto res = n->put(ledger, nibbles.subspan(1), key, val_hash);
            if (res.has_value()) {
                insert_child(nibbles.front(), res.value());
                return derive_commitment(ledger);
            }
        }
    } else {
        // leaf is child
        uint64_t child_id = (u64_from_array(id_) * ORDER) + nibbles.front();

        // create and fill leaf
        Leaf* leaf = ledger.new_cached_leaf(child_id);
        leaf->set_path(nibbles.subspan(1, nibbles.size() - 2));
        leaf->insert_child(nibbles.back(), val_hash);
        const Commitment* leaf_commit = leaf->derive_commitment(ledger);
        insert_child(nibbles.front(), leaf_commit);
        return derive_commitment(ledger);
    }
    return std::nullopt;
}

std::optional<std::tuple<Commitment, bool>> Branch::remove(
    Ledger &ledger, 
    ByteSlice nibbles,
    const Hash &kv
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            auto res = n->remove(ledger, nibbles.subspan(1), kv);
            if (res.has_value()) {
                auto [child_commit, removed] = res.value();

                if (removed) delete_child(nibbles.front());
                else insert_child(nibbles.front(), &child_commit);
                
                bool destroyed = count_ == 0;
                Commitment new_commit(*derive_commitment(ledger));
                if (destroyed) ledger.delete_node(id_);

                return std::make_tuple(new_commit, destroyed);

            }
        }
    }
    return std::nullopt;
}
