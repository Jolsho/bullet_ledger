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
#include "branch.h"
#include "helpers.h"
#include "leaf.h"

class Branch : public Branch_i {
private:
    NodeId id_;
    Commitment commit_;

    std::vector<blst_scalar> children_;
    std::vector<uint16_t> child_block_ids_;

    uint8_t count_;
    NodeId tmp_id_;

    Gadgets_ptr gadgets_;

public:

    Branch(Gadgets_ptr gadgets, const NodeId* id, const ByteSlice* buff) :
        id_(id),
        tmp_id_(0, 0),
        children_(BRANCH_ORDER, ZERO_SK), 
        child_block_ids_(BRANCH_ORDER, {}), 
        commit_{blst_p1()}, 
        count_{},
        gadgets_{gadgets}
    {
        if (buff == nullptr) { return; }

        const size_t NIB_SIZE = sizeof(byte);
        const size_t CHILD_SIZE = sizeof(children_[0]);
        const size_t BLOCK_ID_SIZE = sizeof(child_block_ids_[0]);

        byte* cursor = buff->data(); 
        cursor++;

        id_.from_bytes(cursor); 
        cursor += id_.size();

        commit_ = p1_from_bytes(cursor); 
        cursor += blst_p1_sizeof();

        count_ = *cursor; 
        cursor++;

        uint8_t count2 = *cursor;
        cursor++;

        for (int i{}; i < count2; i++) {

            byte nib = *cursor; 
            cursor++;

            blst_scalar_from_le_bytes(&children_[nib], cursor, CHILD_SIZE);
            cursor += CHILD_SIZE;

            std::memcpy(&child_block_ids_[nib], cursor, BLOCK_ID_SIZE);
            cursor += BLOCK_ID_SIZE;
        }
    }

    ~Branch() override { 
        if (should_delete()) return;
        gadgets_->alloc.persist_node(this); 
    }

    std::vector<byte> to_bytes() const override {

        const size_t NIB_SIZE = sizeof(byte);
        const size_t CHILD_SIZE = sizeof(children_[0]);
        const size_t BLOCK_ID_SIZE = sizeof(child_block_ids_[0]);

        std::vector<byte> buffer(
            sizeof(BRANCH) + 
            id_.size() + 
            blst_p1_sizeof() + 
            sizeof(count_) +
            sizeof(count_) +
            (BRANCH_ORDER * (
                NIB_SIZE + 
                CHILD_SIZE + 
                BLOCK_ID_SIZE
            ))
        );

        byte* cursor = buffer.data();

        *cursor = BRANCH; 
        cursor++;

        std::memcpy(cursor, id_.get_full(), id_.size()); 
        cursor += id_.size();

        blst_p1_compress(cursor, &commit_); 
        cursor += blst_p1_sizeof();

        *cursor = count_; 
        cursor++;

        byte* count2_cursor = cursor;
        cursor++;

        uint8_t count2{};

        for (int i{}; i < BRANCH_ORDER; i++) {
            if (scalar_is_zero(children_[i]) &&
                child_block_ids_[i] == 0) continue;

            count2++;

            *cursor = i; 
            cursor++;

            std::memcpy(cursor, children_[i].b, CHILD_SIZE);
            cursor += CHILD_SIZE;

            std::memcpy(cursor, &child_block_ids_[i], BLOCK_ID_SIZE);
            cursor += BLOCK_ID_SIZE;
        }

        *count2_cursor = count2;

        buffer.resize(cursor - buffer.data());

        return buffer;
    }

    const NodeId* get_id() override { return &id_; };
    void set_id(const NodeId &id) override { id_ = id; };

    const Commitment* get_commitment() const override { return &commit_; };
    void set_commitment(const Commitment &c) override { commit_ = c; };

    const byte get_type() const override { return BRANCH; };
    const bool should_delete() const override { 
        return count_ == 0; 
    };


    void insert_child(
        const byte &nib, 
        const uint16_t block_id
    ) override {
        if (scalar_is_zero(children_[nib])) {
            children_[nib].b[0] = 1;
            count_++;
        }
        child_block_ids_[nib] = block_id;
    }

    const NodeId* get_next_id(byte nib) override {
        if (!scalar_is_zero(children_[nib])) {
            
            tmp_id_.set_node_id(id_.derive_child_id(nib));
            tmp_id_.set_block_id(child_block_ids_[nib]);

            return &tmp_id_;
        } else {
            return nullptr;
        }
    }

    int change_id(
        uint64_t node_id, 
        uint16_t block_id
    ) override {
        for (uint64_t i = 0; i < BRANCH_ORDER; i++) {
            if (scalar_is_zero(children_[i])) continue;  // no child here, skip

            uint64_t old_child_id = id_.derive_child_id(i);
            uint64_t new_child_id = node_id * BRANCH_ORDER + i;

            if (old_child_id == new_child_id) continue;

            // Remove the child node from cache/db safely
            NodeId old_child(old_child_id, block_id);
            Result<Node_ptr, int> res = gadgets_->alloc.load_node(&old_child);
            if (res.is_err()) return res.unwrap_err();

            Node_ptr node = res.unwrap();

            if (node->get_type() == BRANCH) {
                // Recursively update children safely
                int change_res = node->change_id(new_child_id, block_id);
                if (change_res != OK) return change_res;
            }
        }

        if (id_.get_block_id() != block_id || 
            id_.get_node_id() != node_id) {

            NodeId new_id(node_id, block_id);
            int cache_res = gadgets_->alloc.recache(&id_, &new_id);
            if (cache_res != OK) return cache_res;
        }

        return OK;
    }


    int generate_proof(
        const Hash* key,
        std::vector<Polynomial> &Fxs,
        std::vector<blst_p1> &Cs,
        int i
    ) override {

        if (const NodeId* next_id = get_next_id(key->h[i])) {
            Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
            if (res.is_ok()) {

                int rc = res.unwrap()->generate_proof(key, Fxs, Cs, ++i);
                if (rc != OK) return rc;

            } else return res.unwrap_err();
        } else return NOT_EXIST;

        Polynomial Fx(children_);
        Fxs.push_back(Fx);
        Cs.push_back(commit_);

        return OK;
    }

    int put(
        const Hash* key, 
        const Hash* val_hash,
        uint16_t block_id,
        int i
    ) override {

        byte nib = key->h[i];
        if (const NodeId* next_id = get_next_id(nib)) {
            Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
            if (res.is_ok()) {

                int rc = res.unwrap()->put(key, val_hash, block_id, ++i);
                if (rc != OK) return rc;

                if (id_.get_block_id() != block_id) {
                    NodeId new_id(id_.get_node_id(), block_id);
                    int cache_res = gadgets_->alloc.recache(&id_, &new_id);
                    if (cache_res != OK) return cache_res;
                }

                insert_child(nib, block_id);

                return OK;

            } else return res.unwrap_err();
        }
        return NOT_EXIST;
    }

    int remove(
        const Hash* key,
        uint16_t block_id, 
        int i
    ) override {
        byte nib = key->h[i];
        if (const NodeId* next_id = get_next_id(nib)) {
            Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
            if (res.is_ok()) {
                Node_ptr n = res.unwrap();

                int res = n->remove(key, block_id, ++i);
                if (res == DELETED) {

                    if (!scalar_is_zero(children_[nib])) {

                        child_block_ids_[nib] = block_id;
                        children_[nib] = ZERO_SK;
                        count_--;

                    } else return ALREADY_DELETED;

                } else if (res == OK) {

                    child_block_ids_[nib] = block_id;

                } else return res;

                if (id_.get_block_id() != block_id) {
                    NodeId new_id (id_.get_node_id(), block_id);
                    int cache_res = gadgets_->alloc.recache(&id_, &new_id);
                    if (cache_res != 0) return cache_res;
                }

                return OK;
            }
        }
        return NOT_EXIST;
    }

    inline int create_account(
        const Hash* key,
        uint16_t block_id, 
        int i
    ) override { 
        byte nib = key->h[i];
        if (const NodeId* next_id = get_next_id(nib)) {
            Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
            if (res.is_ok()) {

                int rc = res.unwrap()->create_account(key, block_id, ++i);
                if (rc != OK) return rc;

            } else return res.unwrap_err();

        } else {

            // create and fill leaf
            NodeId leaf_id(id_.derive_child_id(nib), block_id);
            auto leaf = create_leaf(gadgets_, &leaf_id, nullptr);
            leaf->set_path(key, block_id);
            gadgets_->alloc.cache_node(leaf);
        }

        if (id_.get_block_id() != block_id) {
            NodeId new_id (id_.get_node_id(), block_id);
            int cache_res = gadgets_->alloc.recache(&id_, &new_id);
            if (cache_res != OK) return cache_res;
        }
        insert_child(nib, block_id);

        return OK;
    }

    inline int delete_account(
        const Hash* key,
        uint16_t block_id, 
        int i
    ) override { 
        return remove(key, block_id, i); 
    }


    int finalize(
        const uint16_t block_id,
        Commitment *out,
        const size_t start, 
        size_t end,
        Polynomial* Fx
    ) override {

        if (end == 0) end = BRANCH_ORDER;
        NodeId tmp(0, block_id);

        Commitment child_commit;

        for (size_t i{start}; i < end; i++) {
            if (child_block_ids_[i] != block_id || 
                scalar_is_zero(children_[i])
            ) continue;

            tmp.set_node_id(id_.derive_child_id(i));

            auto child = gadgets_->alloc.load_node(&tmp, true);
            if (child.is_err()) return LOAD_NODE_ERR;

            int rc = child.unwrap()->finalize(block_id, &child_commit);
            if (rc != OK) return rc;

            hash_p1_to_scalar(
                &child_commit, 
                &children_[i], 
                &gadgets_->settings.tag
            );

            if (Fx && !out) Fx->at(i) = children_[i];
        }

        if (!Fx && out) {
            Polynomial poly(children_);
            inverse_fft_in_place(poly, gadgets_->settings.roots.inv_roots);
            commit_g1(&commit_, poly, gadgets_->settings.setup);
            *out = commit_;
        }
        return OK;
    }

    int prune(const uint16_t block_id) override {
        int rc {};
        tmp_id_.set_block_id(block_id);
        for (int i{}; i < BRANCH_ORDER; i++) {
            if (child_block_ids_[i] != block_id) continue;

            tmp_id_.set_node_id(id_.derive_child_id(i));

            auto res = gadgets_->alloc.load_node(&tmp_id_);
            if (res.is_err()) {
                rc = res.unwrap_err();
                if (rc == MDB_NOTFOUND) continue;
                return rc;
            }

            rc = res.unwrap()->prune(block_id);
            if (rc != OK) return rc;
        }

        // should_delete() evals to true now
        count_ = 0;

        // delete
        auto res = gadgets_->alloc.delete_node(id_);
        if (res.is_err() && res.unwrap_err() != MDB_NOTFOUND) 
            return res.unwrap_err();

        return OK;
    }

    int justify(const uint16_t block_id) override {

        // change all child block ids != 0 -> 0
        for (int i{}; i < BRANCH_ORDER; i++) {
            if (child_block_ids_[i] == 0) continue;

            uint64_t node_id = id_.derive_child_id(i);
            tmp_id_.set_node_id(node_id);
            tmp_id_.set_block_id(child_block_ids_[i]);

            Result<Node_ptr, int> res = gadgets_->alloc.load_node(&tmp_id_);
            if (res.is_err()) return res.unwrap_err();

            int just_res = res.unwrap()->justify(block_id);
            if (just_res != OK && just_res != DELETED) return just_res;

            child_block_ids_[i] = 0;
        }

        if (id_.get_block_id() == block_id) {

            // delete self under this id
            auto del_res = gadgets_->alloc.delete_node(id_);
            if (del_res.is_err()) return del_res.unwrap_err();

            // if have no children
            if (should_delete()) return DELETED;

            id_.set_block_id(0);

            Node_ptr new_self = del_res.unwrap();
            new_self->set_id(id_);

            // this* and new_self should point to same addr
            assert(std::addressof(*this) == std::addressof(*new_self.get()));

            gadgets_->alloc.cache_node(new_self);

        } else {
            // if have no children
            if (should_delete()) return DELETED;

            uint16_t prev_block_id = id_.get_block_id();
            id_.set_block_id(0);
            
            gadgets_->alloc.persist_node(this);

            id_.set_block_id(prev_block_id);
        }

        return OK;
    }
};

std::shared_ptr<Branch_i> create_branch(Gadgets_ptr gadgets, const NodeId* id, const ByteSlice* buff) {
    return std::make_shared<Branch>(gadgets, id, buff);
}
