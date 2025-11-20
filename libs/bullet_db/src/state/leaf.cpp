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

#include "points.h"
#include "verkle.h"
#include "ring_buffer.h"
#include <cstdio>
#include <cstring>

using Path = RingBuffer<byte>;

class Leaf : public Leaf_i {
private:
    NodeId id_;
    Commitment commit_;
    std::vector<std::unique_ptr<Hash>> children_;
    Path path_;
    uint8_t count_;

public:
    const NodeId* get_id() const override { return &id_; };
    const byte get_type() const override { return LEAF; };
    const Commitment* get_commitment() const override { return &commit_; };

    Path* get_path() { return &path_; }
    void set_path(ByteSlice path) override {
        path_.clear(); for (auto b : path) path_.push_back(b);
    }

    void change_id(const NodeId &id, Ledger &ledger) override { id_ = id; }
    NodeId* get_next_id(ByteSlice &nibs) override { return nullptr; }

    void insert_child(const byte &nib, const Hash &val_hash) override {
        if (!children_[nib].get()) count_++;
        children_[nib] = std::make_unique<Hash>(val_hash);
    }


    Leaf(std::optional<NodeId> id, std::optional<ByteSlice*> buffer) : 
        path_(32), children_(ORDER), commit_{new_p1()}, count_{0} 
    {
        if (id.has_value()) id_ = id.value();

        for (auto &c: children_) c = nullptr;
        if (!buffer.has_value()) { return; }

        byte* cursor = buffer.value()->data();
        cursor++;

        p1_from_bytes(cursor, &commit_);
        cursor += 48;

        uint8_t path_len = *cursor; cursor++;
        for (auto i = 0; i < path_len; i++, cursor++) {
            path_.push_back(*cursor);
        }

        uint8_t child_len = *cursor; cursor++;
        for (auto i = 0; i < child_len; i++) {
            byte nib(*cursor); cursor++;
            children_[nib] = std::make_unique<Hash>(Hash());
            std::memcpy(children_[nib].get()->data(), cursor, 32);
            cursor += 32;
            count_++;
        }
    }

    std::vector<byte> to_bytes() const override {
        std::vector<byte> buffer(
            1 + 48 + 1 + path_.size() + 1 + (count_ * 33)
        );
        byte* cursor = buffer.data();

        *cursor = LEAF; cursor++;

        blst_p1_compress(cursor, &commit_); cursor += 48;

        auto path_len = static_cast<uint8_t>(path_.size());
        *cursor = path_len; cursor++;

        for (auto i = 0; i < path_.size(); i++, cursor++)
            *cursor = path_.get(i).value();

        *cursor = count_; cursor++;
        for (auto i = 0; i < ORDER; i++) {
            if (Hash* h = children_[i].get()) {
                *cursor = static_cast<uint8_t>(i); cursor++;
                std::memcpy(cursor, h->data(), h->size());
                cursor += 32;
            }
        }
        return buffer;
    }

    const Commitment* derive_commitment(Ledger &ledger) override { 
        scalar_vec* Fx = ledger.get_poly();
        for (auto i = 0; i < ORDER; i++) {
            if (Hash* h = children_[i].get())
                blst_scalar_from_le_bytes(
                    &Fx->at(i), h->data(), h->size()
                );
        }
        commit_g1_projective_in_place(*Fx, *ledger.get_srs(), &commit_);
        return &commit_; 
    }

    std::optional<size_t> in_path(ByteSlice nibbles) {
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

    std::optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) override {
        std::optional<size_t> res = in_path(nibbles);
        if (res.has_value() || !children_[nibbles.back()].get()) return std::nullopt;
        return Hash(*children_[nibbles.back()].get());
    }

    int build_commitment_path(
        Ledger &ledger, 
        const Hash &key,
        ByteSlice nibbles,
        std::vector<scalar_vec> &Fxs, 
        Bitmap<32> &Zs
    ) override { 
        // build Fx
        scalar_vec fx(ORDER, new_scalar());
        for (auto i = 0; i < ORDER; i++)
            if (Hash* c = children_[i].get())
                blst_scalar_from_le_bytes(
                    &fx[i], c->data(), c->size()
                );

        Fxs.push_back(fx);
        // index of nibble within key
        Zs.set(32 - 1);
        return true; 
    }

    std::optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key,
        const Hash &val_hash
    ) override {
        std::optional<size_t> is = in_path(nibbles);
        if (!is.has_value()) {

            blst_scalar tmp_sk = new_scalar();
            blst_p1 old_c = ledger.get_srs()->g1_powers_jacob[nibbles.back()];
            blst_p1 new_c = old_c;

            blst_scalar_from_le_bytes(&tmp_sk, val_hash.data(), val_hash.size());
            p1_mult(new_c, new_c, tmp_sk);

            if (Hash* prev = children_[nibbles.back()].get()) {
                blst_scalar_from_le_bytes(&tmp_sk, prev->data(), prev->size());
                p1_mult(old_c, old_c, tmp_sk);
            }

            insert_child(nibbles.back(), val_hash);

            // new_c = c + (new_c - old_c)
            blst_p1_cneg(&old_c, true);
            blst_p1_add_or_double(&new_c, &new_c, &old_c);
            blst_p1_add_or_double(&new_c, &commit_, &new_c);

            return new_c;
        }

        size_t shared_path = is.value();

        std::vector<byte> shared_nibs; 
        shared_nibs.reserve(shared_path);

        for (auto i = 0; i < shared_path; i++)
            shared_nibs.push_back(path_.get(i).value());

        if (shared_path > 0) nibbles = nibbles.subspan(shared_path);
        
        // create lowest branch
        auto branch = create_branch(std::nullopt, std::nullopt);

        // insert current
        branch->insert_child(path_.get(shared_path).value(), &commit_);

        // insert new
        Leaf leaf(std::nullopt, std::nullopt);
        leaf.insert_child(nibbles.back(), val_hash);
        const Commitment* new_commit = leaf.derive_commitment(ledger);
        branch->insert_child(nibbles.front(), new_commit);

        // work up the tree inserting children and passing commitment upward
        Commitment c = *branch->derive_commitment(ledger);
        for (size_t i = shared_nibs.size(); i-- > 0;) {
            scalar_vec Fx(ORDER, new_scalar());
            hash_p1_to_scalar(&c, &Fx[shared_nibs[i]], ledger.get_tag());
            commit_g1_projective_in_place(Fx, *ledger.get_srs(), &c);
        }
        return c;
    }

    std::optional<const Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key,
        const Hash &val_hash
    ) override {

        std::optional<size_t> is = in_path(nibbles);
        if (!is.has_value()) {

            blst_scalar tmp_sk = new_scalar();
            blst_p1 old_c = ledger.get_srs()->g1_powers_jacob[nibbles.back()];
            blst_p1 new_c = old_c;

            blst_scalar_from_le_bytes(&tmp_sk, val_hash.data(), val_hash.size());
            p1_mult(new_c, new_c, tmp_sk);

            if (Hash* prev = children_[nibbles.back()].get()) {
                blst_scalar_from_le_bytes(&tmp_sk, prev->data(), prev->size());
                p1_mult(old_c, old_c, tmp_sk);
            }

            insert_child(nibbles.back(), val_hash);

            // c += (new_c - old_c)
            blst_p1_cneg(&old_c, true);
            blst_p1_add_or_double(&new_c, &new_c, &old_c);
            blst_p1_add_or_double(&commit_, &commit_, &new_c);

            return &commit_;
        }

        size_t shared_path = is.value();

        Node_ptr self = ledger.delete_node(id_).value();

        uint64_t new_id = u64_from_id(id_);

        std::vector<std::tuple<Branch_i*, byte>> branches; 
        branches.reserve(shared_path);

        for (auto i = 0; i < shared_path; i++) {
            Branch_i* branch = ledger.new_cached_branch(new_id);
            branches.push_back(std::make_tuple(branch, path_.pop_front().value()));
            new_id = (new_id * ORDER) + nibbles.front();
        }
        if (shared_path > 0) nibbles = nibbles.subspan(shared_path);

        Branch_i* branch = ledger.new_cached_branch(new_id);
        new_id *= ORDER;

        // pop off nibble to act as key for parent
        // insert existing leaf into branch
        byte nib = path_.pop_front().value();
        change_id(u64_to_id(new_id + nib), ledger);
        branch->insert_child(nib, &commit_);
        ledger.cache_node(std::move(self));

        // derive a new leaf for new value and insert into branch
        Leaf_i* leaf = ledger.new_cached_leaf(new_id + nibbles.front());
        leaf->set_path(nibbles.subspan(1, nibbles.size() - 2));
        leaf->insert_child(nibbles.back(), val_hash);
        branch->insert_child(nibbles.front(), leaf->derive_commitment(ledger));

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

    std::optional<std::tuple<Commitment, bool>> remove(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key
    ) override {
        std::optional<size_t> is = in_path(nibbles);
        if (is.has_value()) return std::nullopt;

        Commitment c = ledger.get_srs()->g1_powers_jacob[nibbles.back()];
        if (Hash* v_hash = children_[nibbles.back()].get()) {

            // delete value
            ledger.delete_value(derive_kv_hash(key, *v_hash));

            // derive negative child term
            blst_scalar child_neg = new_scalar();
            blst_scalar zero_sk = new_scalar();
            blst_scalar_from_le_bytes(&child_neg, v_hash->data(), v_hash->size());
            blst_sk_sub_n_check(&child_neg, &zero_sk, &child_neg);

            // commit to neg child
            p1_mult(c, c, child_neg);

            // subtract child from commitment
            blst_p1_add_or_double(&commit_, &commit_, &c);

            // remove child
            children_[nibbles.back()] = nullptr;
            count_--;
        }

        bool destroyed = count_ == 0;
        if (destroyed) ledger.delete_node(id_);

        return std::make_tuple(c, destroyed);
    }
};

std::unique_ptr<Leaf_i> create_leaf(std::optional<NodeId> id, std::optional<ByteSlice*> buff) {
    return std::make_unique<Leaf>(id, buff);
}
