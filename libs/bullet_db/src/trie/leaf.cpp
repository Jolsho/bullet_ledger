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

#include "fft.h"
#include "helpers.h"
#include "leaf.h"
#include "polynomial.h"
#include "ring_buffer.h"
#include "branch.h"
#include "ledger.h"
#include "state_types.h"
#include <cstring>

using Path = RingBuffer<byte>;

class Leaf : public Leaf_i {
private:
    NodeId id_;
    Commitment commit_;
    bool is_deleted_;

    std::vector<Hash> children_;
    std::vector<uint16_t> child_block_ids_;

    Path path_;

public:

    Leaf(const NodeId* id, const ByteSlice* buff) : 
        path_(32), 
        children_(LEAF_ORDER), 
        commit_{new_p1()},
        is_deleted_{false}
    {

        for (auto &c: children_) c = ZERO_HASH;
        if (buff == nullptr) { return; }

        byte* cursor = buff->data();
        cursor++;

        id_.from_bytes(cursor);
        cursor += id_.size();

        commit_ = p1_from_bytes(cursor); cursor += 48;

        uint8_t path_len = *cursor; cursor++;
        for (int i = 0; i < path_len; i++, cursor++) {
            path_.push_back(*cursor);
        }

        for (int i = 0; i < LEAF_ORDER; i++) {
            std::memcpy(children_[i].h, cursor, 32);
            cursor += 32;
        }
    }

    std::vector<byte> to_bytes() const override {

        std::vector<byte> buffer(
            1 + 
            id_.size() + 
            48 + 
            1 + path_.size() + 
            (LEAF_ORDER * 32)
        );

        byte* cursor = buffer.data();

        *cursor = LEAF; cursor++;

        std::memcpy(cursor, id_.get_full(), id_.size());
        cursor += id_.size();

        blst_p1_compress(cursor, &commit_); cursor += 48;

        int path_len = static_cast<uint8_t>(path_.size());
        *cursor = path_len; cursor++;

        for (int i = 0; i < path_.size(); i++, cursor++)
            *cursor = path_.get(i).value();

        for (int i = 0; i < LEAF_ORDER; i++) {
            std::memcpy(cursor, children_[i].h, 32);
            cursor += 32;
        }
        return buffer;
    }
    
    const NodeId* get_id() override { return &id_; };

    const byte get_type() const override { return LEAF; };
    const bool should_delete() const override { return is_deleted_; };

    const Commitment* get_commitment() const override { return &commit_; };

    Path* get_path() { return &path_; }

    void set_path(ByteSlice path) override {
        path_.clear(); 
        for (byte b : path) 
            path_.push_back(b);
    }

    NodeId* get_next_id(ByteSlice &nibs) override { return nullptr; }

    void insert_child(const byte &nib, const Hash &val_hash, const uint16_t block_id) override {
        children_[nib] = val_hash;
        child_block_ids_[nib] = block_id;
    }

    const Commitment* derive_commitment(Gadgets *gadgets) override { 
        Polynomial Fx(BRANCH_ORDER, ZERO_SK);

        for (int i = 0; i < LEAF_ORDER; i++) {
            if (!hash_is_zero(children_[i]))
                blst_scalar_from_le_bytes(&Fx[i], children_[i].h, 32);
        }

        // difference in leaf and branch order filled with zeros
        for (int i = LEAF_ORDER; i < BRANCH_ORDER; i++) {
            std::memset(Fx[i].b, 0 , 32);
        }

        inverse_fft_in_place(Fx, gadgets->settings.roots.inv_roots);
        commit_g1(&commit_, Fx, gadgets->settings.setup);
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

    Result<Hash, int> search( 
        Gadgets *gadgets, 
        ByteSlice nibbles
    ) override {
        std::optional<size_t> res = in_path(nibbles);

        if (res.has_value() || hash_is_zero(children_[nibbles.back()])) 
            return NOT_EXIST;

        return children_[nibbles.back()];
    }

    int generate_proof(
        Gadgets *gadgets, 
        const Hash &key,
        ByteSlice nibbles,
        std::vector<Polynomial> &Fxs
    ) override { 
        int fx_idx = 32 - nibbles.size();
        for (int i = 0; i < LEAF_ORDER; i++)
            if (!hash_is_zero(children_[i]))
             blst_scalar_from_le_bytes(&Fxs[fx_idx][i], children_[i].h, 32);
        return EXISTS; 
    }

    int put(
        Gadgets *gadgets, 
        ByteSlice nibbles,
        const Hash &key,
        const Hash &val_hash,
        uint16_t new_block_id
    ) override {
        std::optional<size_t> is = in_path(nibbles);
        if (!is.has_value()) {

            insert_child(nibbles.back(), val_hash, new_block_id);

            if (id_.get_block_id() != new_block_id) {
                NodeId old_id = id_;
                id_.set_block_id(new_block_id);
                int cache_res = gadgets->alloc.recache(old_id, this);
                if (cache_res != 0) return cache_res;
            }

            return OK;
        }

        size_t shared_path = is.value();

        std::vector<std::tuple<Branch_i*, byte>> branches; 
        branches.reserve(shared_path);

        // derive a new id, node regardless + block id if it differs
        NodeId new_id(id_);
        new_id.set_node_id(
            id_.derive_child_id(
                path_.front().value()
            )
        );
        if (new_id.get_block_id() != new_block_id) {
            new_id.set_block_id(new_block_id);
        }


        // For each shared nibble create a new branch
        for (int i = 0; i < shared_path; i++) {

            Branch_i* branch = create_branch(&new_id, nullptr);
            branches.push_back({branch, path_.pop_front().value()});

            byte nib = path_.front().value();
            new_id.set_node_id(new_id.derive_child_id(nib));
        }
        if (shared_path > 0) nibbles = nibbles.subspan(shared_path);

        Branch_i* branch = create_branch(&new_id, nullptr);

        // pop off nibble to act as key for parent
        // insert existing leaf into branch
        byte nib = path_.pop_front().value();

        // increment id_ for this
        NodeId old_id = id_;
        new_id.set_node_id(new_id.derive_child_id(nib));
        id_ = new_id;

        // if id_ before change doesnt match new_block_id 
        // invalidate it in cache and recache it under new id
        if (old_id.get_block_id() != new_block_id) {
            id_.set_block_id(new_block_id);
            int cache_res = gadgets->alloc.recache(old_id, this);
            if (cache_res != 0) return cache_res;
        }

        // insert fake commitment into current branch using key
        // will be derived when being finalized
        branch->insert_child(nib, {}, gadgets, new_block_id);

        // increment id again for new leaf
        new_id.set_node_id(new_id.derive_child_id(nibbles.front()));

        // derive a new leaf for new value and insert into branch
        Leaf_i* leaf = create_leaf(&new_id, nullptr);
        leaf->set_path(nibbles.subspan(1, nibbles.size() - 2));
        leaf->insert_child(nibbles.back(), val_hash, new_block_id);

        int cache_res = gadgets->alloc.cache_node(leaf);
        if (cache_res != 0) return cache_res;


        // insert fake commitment into current branch using key
        // will be derived when being finalized
        branch->insert_child(nibbles.front(), {}, gadgets, new_block_id);

        cache_res = gadgets->alloc.cache_node(branch);
        if (cache_res != 0) return cache_res;

        // work up the tree inserting children and passing commitment upward
        for (size_t i = branches.size(); i-- > 0; ) {

            auto [branch, child_key] = branches[i];

            // insert fake commitment into current branch using key
            // will be derived when being finalized
            branch->insert_child(child_key, {}, gadgets, new_block_id);

            cache_res = gadgets->alloc.cache_node(branch);
            if (cache_res != 0) return cache_res;
        }

        return OK;
    }

    int remove(
        Gadgets *gadgets, 
        ByteSlice nibbles,
        const Hash &key,
        uint16_t new_block_id
    ) override {

        std::optional<size_t> is = in_path(nibbles);
        if (is.has_value()) return NOT_EXIST;

        if (!hash_is_zero(children_[nibbles.back()])) {

            // remove child
            children_[nibbles.back()] = ZERO_HASH;

            if (id_.get_block_id() != new_block_id) {
                NodeId old_id(id_);
                id_.set_block_id(new_block_id);
                int cache_res = gadgets->alloc.recache(old_id, this);
                if (cache_res != 0) return cache_res;
            }

            return OK;
        }

        return NOT_EXIST;
    }

    inline int delete_account(
        Gadgets *gadgets, 
        ByteSlice nibbles,
        const Hash &kv,
        uint16_t new_block_id
    ) override {

        is_deleted_ = true;

        if (id_.get_block_id() != new_block_id) {
            NodeId old_id(id_);
            id_.set_block_id(new_block_id);
            int cache_res = gadgets->alloc.recache(old_id, this);
            if (cache_res != 0) return cache_res;
        }

        return DELETED;
    }

    Result<const Commitment*, int> finalize(
        Gadgets* gadgets,
        const uint16_t block_id
    ) override {
        derive_commitment(gadgets);
        return &commit_;
    }

    int prune(
        Gadgets* gadgets,
        const uint16_t block_id
    ) override {

        NodeId tmp_id_;
        for (int i = 0; i < LEAF_ORDER; i++) {
            if (child_block_ids_[i] != block_id ||
                std::memcmp(children_[i].h, ZERO_HASH.h, 32) == 0) 
                continue;

            tmp_id_.set_block_id(child_block_ids_[i]);
            tmp_id_.set_node_id(id_.derive_child_id(i));

            int res = gadgets->alloc.db_.del(tmp_id_.get_full(), tmp_id_.size());
            if (res != 0) return DELETE_VALUE_ERR;
        }

        auto res = gadgets->alloc.delete_node(id_);
        if (res.is_err()) return res.unwrap_err();

        return OK;
    }

    int justify(Gadgets* gadgets) override {
        NodeId old_id_;

        NodeId new_id_; 
        new_id_.set_block_id(0);

        for (int i = 0; i < LEAF_ORDER; i++) {
            if (child_block_ids_[i] == 0) continue;

            old_id_.set_block_id(child_block_ids_[i]);

            uint64_t node_id = id_.derive_child_id(i);
            old_id_.set_node_id(node_id);
            new_id_.set_node_id(node_id);


            // if self is being deleted
            // delete every value else rename value
            // if not found just continue (could be trying to delete zero hash)
            if (should_delete() || hash_is_zero(children_[i])) {

                int res = gadgets->alloc.db_.del(old_id_.get_full(), old_id_.size());
                if (res != 0 && res != MDB_NOTFOUND) return DELETE_VALUE_ERR;

            } else {

                int res = gadgets->alloc.rename_value(old_id_, new_id_);
                if (res != 0 && res != MDB_NOTFOUND) return REPLACE_VALUE_ERR;
            }

            child_block_ids_[i] = 0;
        }

        // delete self
        auto del_res = gadgets->alloc.delete_node(id_);
        if (del_res.is_err()) return del_res.unwrap_err();

        if (should_delete()) return DELETED;

        // update block_id
        id_.set_block_id(0);

        // recache self to be saved later
        return gadgets->alloc.cache_node(this);
    }
};

Leaf_i* create_leaf(const NodeId* id, const ByteSlice* buff) {
    return new Leaf(id, buff);
}
