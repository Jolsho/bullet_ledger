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
#include "branch.h"
#include "state_types.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>


class Leaf : public Leaf_i {
private:
    NodeId id_;
    Commitment commit_;
    bool is_deleted_;

    std::vector<Hash> children_;
    std::vector<uint16_t> child_block_ids_;

    Hash path_;

    Gadgets_ptr gadgets_;

public:

    Leaf(Gadgets_ptr gadgets, const NodeId* id, const ByteSlice* buff) : 
        id_(id),
        children_(LEAF_ORDER, ZERO_HASH), 
        child_block_ids_(LEAF_ORDER, {}), 
        commit_{new_inf_p1()},
        is_deleted_{false},
        gadgets_{gadgets}
    {
        if (buff == nullptr) { return; }

        byte* cursor = buff->data(); cursor++;

        id_.from_bytes(cursor); cursor += id_.size();

        commit_ = p1_from_bytes(cursor); cursor += 48;

        std::memcpy(path_.h, cursor, 32); cursor += 32;

        for (int i{}; i < LEAF_ORDER; i++) {
            std::memcpy(children_[i].h, cursor, 32);
            cursor += 32;
        }
    }
    ~Leaf() override { 
        if (should_delete()) return;
        gadgets_->alloc.persist_node(this); 
    }

    std::vector<byte> to_bytes() const override {

        std::vector<byte> buffer(
            1 + 
            id_.size() + 
            48 + 
            32 + 
            (LEAF_ORDER * 32)
        );

        byte* cursor = buffer.data();

        *cursor = LEAF; cursor++;

        std::memcpy(cursor, id_.get_full(), id_.size());
        cursor += id_.size();

        blst_p1_compress(cursor, &commit_); cursor += 48;

        std::memcpy(cursor, path_.h, 32); cursor += 32;

        for (int i{}; i < LEAF_ORDER; i++) {
            std::memcpy(cursor, children_[i].h, 32);
            cursor += 32;
        }
        return buffer;
    }
    
    const NodeId* get_id() override { return &id_; };
    void set_id(const NodeId &id) override { id_ = id; };

    const byte get_type() const override { return LEAF; };
    const bool should_delete() const override { return is_deleted_; };

    const Commitment* get_commitment() const override { return &commit_; };
    void set_commitment(const Commitment &c) override { commit_ = c; };

    Hash* get_path() { return &path_; }

    void set_path(const Hash* key, uint16_t block_id) override {
        std::memcpy(path_.h, key->h, 32);
        // since last byte can be different just set to zero
        path_.h[31] = 0;
        insert_child(0, &path_, block_id);
    }

    NodeId* get_next_id(byte nib) override { return nullptr; }

    int change_id(
        uint64_t node_id, 
        uint16_t block_id
    ) override {
        if (id_.get_block_id() != block_id || 
            id_.get_node_id() != node_id) {

            NodeId new_id (node_id, block_id);
            int cache_res = gadgets_->alloc.recache(&id_, &new_id);
            if (cache_res != OK) return cache_res;
        }
        return OK;
    }

    void insert_child(
        const byte &nib, 
        const Hash* val_hash, 
        const uint16_t block_id
    ) override {
        children_[nib] = *val_hash;
        child_block_ids_[nib] = block_id;
    }

    std::optional<size_t> matching_path(const Hash* key, int i) {
        // Last byte of key is a value index, so ignore it
        const size_t KEY_SIZE = 32 - 1;

        size_t matched = i;
        for (; matched < KEY_SIZE; ++matched) {
            if (path_.h[matched] != key->h[matched])
                break;  
        }

        if (matched == KEY_SIZE) return std::nullopt; 

        return matched - i;
    }


    int generate_proof(
        const Hash* key,
        std::vector<Polynomial> &Fxs,
        std::vector<blst_p1> &Cs,
        int i
    ) override { 

        std::optional<size_t> matching = matching_path(key, i);
        if (matching.has_value()) return NOT_EXIST;

        Polynomial Fx(BRANCH_ORDER, ZERO_SK);
        for (int i{}; i < LEAF_ORDER; i++)
            blst_scalar_from_le_bytes(&Fx[i], children_[i].h, 32);

        Fxs.push_back(Fx);

        // need to push back two because two proofs are given for this
        Cs.push_back(commit_);
        Cs.push_back(commit_);

        return OK; 
    }

    int put(
        const Hash* key,
        const Hash* val_hash,
        uint16_t new_block_id,
        int i
    ) override {
        if (key->h[31] == 0) return LEAF_IDX_ZERO;

        std::optional<size_t> matching = matching_path(key, i);
        if (!matching.has_value()) {
            if (id_.get_block_id() != new_block_id) {
                NodeId new_id (id_.get_node_id(), new_block_id);
                int cache_res = gadgets_->alloc.recache(&id_, &new_id);
                if (cache_res != OK) return cache_res;
            }

            insert_child(key->h[31], val_hash, new_block_id);

            return OK;
        }

        // Each character of a shared key path allocates a branch.
        // each branch refrences the next through the associated path nibble
        size_t shared_path = matching.value();
        std::vector<std::shared_ptr<Branch_i>> branches(shared_path + 1);

        NodeId new_id(id_.get_node_id(), new_block_id);

        for (int k = 0; k <= shared_path; k++) {

            branches[k] = create_branch(gadgets_, &new_id, nullptr);

            if (k < shared_path) {
                branches[k]->insert_child(key->h[i], new_block_id);

                new_id.set_node_id(new_id.derive_child_id(key->h[i++]));
            }
        }

        assert(key->h[i] != path_.h[i]);

        // INSERT NEW LEAF INTO LAST BRANCH
        byte new_nib = key->h[i];
        new_id.set_node_id(new_id.derive_child_id(new_nib));

        auto leaf = create_leaf(gadgets_, &new_id, nullptr);
        leaf->set_path(key, new_block_id);
        leaf->insert_child(key->h[31], val_hash, new_block_id);

        gadgets_->alloc.cache_node(leaf);

        branches.back()->insert_child(new_nib, new_block_id);



        // set new_id_node_id from sibling
        uint64_t node_id = new_id.get_node_id();
        node_id -= node_id % BRANCH_ORDER;
        node_id += path_.h[i];
        new_id.set_node_id(node_id);




        // INSERT OLD LEAF INTO LAST BRANCH
        int cache_res = gadgets_->alloc.recache(&id_, &new_id);
        if (cache_res != OK) return cache_res;

        branches.back()->insert_child(path_.h[i], new_block_id);



        // due to the fact that one of these branches has this* id_
        // we have to wait to cache until after this* has been recached under a new id 
        for (auto &branch: branches) gadgets_->alloc.cache_node(branch);

        return OK;
    }

    int remove(
        const Hash* key,
        uint16_t new_block_id,
        int i
    ) override {

        std::optional<size_t> matching = matching_path(key, i);
        if (matching.has_value()) return NOT_EXIST;

        byte nib = key->h[32];
        if (!hash_is_zero(children_[nib])) {


            if (id_.get_block_id() != new_block_id) {
                NodeId new_id (id_.get_node_id(), new_block_id);
                int cache_res = gadgets_->alloc.recache(&id_, &new_id);
                if (cache_res != OK) return cache_res;
            }

            // remove child
            children_[nib] = ZERO_HASH;

            return OK;
        }

        return NOT_EXIST;
    }

    inline int delete_account(
        const Hash* key,
        uint16_t new_block_id,
        int i
    ) override {

        std::optional<size_t> matching = matching_path(key, i);
        if (matching.has_value()) return NOT_EXIST;

        if (id_.get_block_id() != new_block_id) {
            NodeId new_id (id_.get_node_id(), new_block_id);
            int cache_res = gadgets_->alloc.recache(&id_, &new_id);
            if (cache_res != OK) return cache_res;
        }

        is_deleted_ = true;

        return DELETED;
    }

    int finalize(
        const uint16_t block_id,
        Commitment *out,
        const size_t start, 
        size_t end,
        Polynomial* Fx
    ) override {

        Polynomial poly(BRANCH_ORDER, ZERO_SK);
        for (int i{}; i < LEAF_ORDER; i++) {
            if (!hash_is_zero(children_[i]))
                blst_scalar_from_le_bytes(&poly[i], children_[i].h ,32);
        }

        inverse_fft_in_place(poly, gadgets_->settings.roots.inv_roots);
        commit_g1(&commit_, poly, gadgets_->settings.setup);
        *out = commit_;
        return OK;
    }

    int prune(const uint16_t block_id) override {

        NodeId tmp_id_(0, block_id);
        for (int i{}; i < LEAF_ORDER; i++) {
            if (child_block_ids_[i] != block_id) continue;

            tmp_id_.set_node_id(id_.derive_child_id(i));

            void* trx = gadgets_->alloc.db_.start_txn();
            int rc = gadgets_->alloc.db_.del(
                tmp_id_.get_full(), tmp_id_.size(), 
                trx
            );
            gadgets_->alloc.db_.end_txn(trx, rc);
            if (rc != OK && rc != MDB_NOTFOUND) 
                return DELETE_VALUE_ERR;
        }

        // should_delete() evals to true now
        is_deleted_ = true;

        // delete
        auto res = gadgets_->alloc.delete_node(id_);
        if (res.is_err() && res.unwrap_err() != MDB_NOTFOUND) 
            return res.unwrap_err();

        return OK;
    }

    int justify() override {
        NodeId child_id_; 
        child_id_.set_block_id(0);

        // change all child block ids != 0 -> 0
        for (int i{}; i < LEAF_ORDER; i++) {
            if (child_block_ids_[i] == 0) continue;

            child_id_.set_block_id(child_block_ids_[i]);

            child_id_.set_node_id(id_.derive_child_id(i));


            /*  
             *  if self is being deleted delete every value 
             *  else rename value 
             *  if not found just continue 
             *      (could be trying to delete ZERO_HASH)
            */
            if (should_delete() || hash_is_zero(children_[i])) {

                void* trx = gadgets_->alloc.db_.start_txn();
                int rc = gadgets_->alloc.db_.del(
                    child_id_.get_full(), child_id_.size(), 
                    trx
                );
                gadgets_->alloc.db_.end_txn(trx, rc);
                if (rc != OK && rc != MDB_NOTFOUND) 
                    return DELETE_VALUE_ERR;

            } else {

                void* out_data = nullptr;
                size_t size = 0;

                // GET
                void* trx = gadgets_->alloc.db_.start_rd_txn();
                int rc = gadgets_->alloc.db_.get_raw(
                    child_id_.get_full(), child_id_.size(), 
                    &out_data, &size, trx
                );
                gadgets_->alloc.db_.end_txn(trx, rc);
                if (rc != OK && rc != MDB_NOTFOUND) {
                    free(out_data);
                    return rc;
                }

                child_id_.set_block_id(0);

                // PUT
                trx = gadgets_->alloc.db_.start_txn();
                rc = gadgets_->alloc.db_.put(
                    child_id_.get_full(), child_id_.size(), 
                    out_data, size, 
                    trx
                );
                gadgets_->alloc.db_.end_txn(trx, rc);
                free(out_data);
                if (rc != OK) return rc;
            }

            child_block_ids_[i] = 0;
        }

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
        return OK;
    }
};

std::shared_ptr<Leaf_i> create_leaf(Gadgets_ptr gadgets, const NodeId* id, const ByteSlice* buff) {
    return std::make_shared<Leaf>(gadgets, id, buff);
}
