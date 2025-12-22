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
#include "polynomial.h"
#include "helpers.h"
#include "leaf.h"
#include "ledger.h"

class Branch : public Branch_i {
private:
    NodeId id_;
    Commitment commit_;

    std::vector<blst_scalar> children_;
    std::vector<uint16_t> child_block_ids_;

    uint8_t count_;
    NodeId tmp_id_;

public:

    Branch(const NodeId* id, const ByteSlice* buff) :
        id_(id),
        tmp_id_(0, 0),
        children_(BRANCH_ORDER), 
        commit_{blst_p1()}, 
        count_{0}
    {
        for (auto &c: children_) c = ZERO_SK;
        if (buff == nullptr) { return; }

        byte* cursor = buff->data(); cursor++;

        id_.from_bytes(cursor);
        cursor += id_.size();

        commit_ = p1_from_bytes(cursor); cursor += 48;

        std::array<byte, 2> block_id_buff;
        count_ = *cursor; cursor++;
        for (int i = 0; i < count_; i++) {
            byte nib(*cursor); 
            cursor++;

            blst_scalar_from_le_bytes(&children_[nib], cursor, 32);
            cursor += 32;

            std::memcpy(block_id_buff.data(), cursor, 2);
            cursor += 2;
            child_block_ids_[i] = std::bit_cast<uint16_t>(block_id_buff);
        }
    }

    std::vector<byte> to_bytes() const override {
        std::vector<byte> buffer(1 + id_.size() + 48 + 1 + (count_ * (32 + 2)));
        byte* cursor = buffer.data();

        *cursor = BRANCH; cursor++;

        std::memcpy(cursor, id_.get_full(), id_.size());
        cursor += id_.size();

        blst_p1_compress(cursor, &commit_); cursor += 48;

        *cursor = count_; 
        cursor++;

        std::array<byte, 2> block_id_buff;
        for (int i = 0; i < BRANCH_ORDER; i++) {
            if (!scalar_is_zero(children_[i])) {
                *cursor = static_cast<uint8_t>(i); 
                cursor++;

                std::memcpy(cursor, children_[i].b, 32);
                cursor += 32;

                block_id_buff = std::bit_cast<std::array<byte, 2>>(child_block_ids_[i]);
                std::memcpy(cursor, block_id_buff.data(), 2);
                cursor += 2;
            }
        }
        return buffer;
    }

    const NodeId* get_id() override { return &id_; };

    const Commitment* get_commitment() const override { return &commit_; };

    const byte get_type() const override { return BRANCH; };
    const bool should_delete() const override { return count_ == 0; };


    void insert_child(
        const byte &nib, 
        const Commitment* new_commit, 
        const Gadgets *gadgets,
        const uint16_t block_id
    ) override {
        if (scalar_is_zero(children_[nib])) count_++;
        hash_p1_to_sk(children_[nib], *new_commit, &gadgets->settings.tag);
        child_block_ids_[nib] = block_id;
    }

    const NodeId* get_next_id(ByteSlice &nibs) override {
        if (!scalar_is_zero(children_[nibs.front()])) {
            
            uint64_t child_node_id = id_.derive_child_id(nibs.front());

            tmp_id_.set_block_id(child_block_ids_[nibs.front()]);
            tmp_id_.set_node_id(child_node_id);

            return &tmp_id_;
        } else {
            return nullptr;
        }
    }

    Result<void*, int> change_id(uint64_t node_id, uint16_t block_id, Gadgets *gadgets) override {
        for (uint64_t i = 0; i < BRANCH_ORDER; i++) {
            if (scalar_is_zero(children_[i])) continue;  // no child here, skip

            // Compute old and new IDs
            uint64_t old_child_id = id_.derive_child_id(i);
            uint64_t new_child_id = node_id * BRANCH_ORDER + i;

            if (old_child_id == new_child_id) continue;

            // Prevent accidental deletion of the root
            if (old_child_id == 1) continue;

            // Remove the child node from cache/db safely
            NodeId old_child(old_child_id, block_id);
            Result<Node*, int> res = gadgets->alloc.load_node( &old_child);
            if (res.is_err()) return res.unwrap_err();

            Node* node = res.unwrap();

            if (node->get_type() == BRANCH) {
                // Recursively update children safely
                auto change_res = reinterpret_cast<Branch*>(node)->change_id(new_child_id, block_id, gadgets);
                if (change_res.is_err()) return change_res.unwrap_err();
            }
        }

        // change id and recache...
        if (id_.get_block_id() != block_id) {
            NodeId old_id(id_);
            id_.set_node_id(node_id);
            id_.set_block_id(block_id);
            gadgets->alloc.recache(old_id, this);
        }

        return nullptr;
    }


    const Commitment* derive_commitment(Gadgets *gadgets) override {
        Polynomial Fx(BRANCH_ORDER, ZERO_SK);
        for (int i = 0; i < children_.size(); i++) 
            Fx[i] = children_[i];

        inverse_fft_in_place(Fx, gadgets->settings.roots.inv_roots);
        commit_g1(&commit_, Fx, gadgets->settings.setup);
        return &commit_;
    }

    Result<Hash, int> search(Gadgets *gadgets, ByteSlice nibbles) override {
        if (const NodeId* next_id = get_next_id(nibbles)) {
            Result<Node*, int> res = gadgets->alloc.load_node(next_id);
            if (res.is_ok()) {
                Node* n = res.unwrap();
                return n->search(gadgets, nibbles.subspan(1));
            } else {
                return res.unwrap_err();
            }
        }
        return NOT_EXIST;
    }

    int generate_proof(
        Gadgets *gadgets, 
        const Hash &key,
        ByteSlice nibbles,
        std::vector<Polynomial> &Fxs
    ) override {
        if (const NodeId* next_id = get_next_id(nibbles)) {
            Result<Node*, int> res = gadgets->alloc.load_node(next_id);
            if (res.is_ok()) {
                Node* n = res.unwrap();

                int res = n->generate_proof(
                    gadgets, key, 
                    nibbles.subspan(1), 
                    Fxs
                );
                if (res != EXISTS) return res;

            } else return res.unwrap_err();
        } else return NOT_EXIST;

        int fx_idx = 32 - nibbles.size();

        // build Fx
        for (int i = 0; i < BRANCH_ORDER; i++)
            Fxs[fx_idx][i] = children_[i];

        return EXISTS;
    }

    int put(
        Gadgets *gadgets, 
        ByteSlice nibbles, 
        const Hash &key, 
        const Hash &val_hash,
        uint16_t new_block_id
    ) override {

        if (const NodeId* next_id = get_next_id(nibbles)) {
            Result<Node*, int> res = gadgets->alloc.load_node(next_id);
            if (res.is_ok()) {
                Node* n = res.unwrap();

                Result<const Commitment*, int> res = n->put(
                    gadgets, nibbles.subspan(1), 
                    key, val_hash, new_block_id
                );
                if (res.is_ok()) {

                    insert_child(nibbles.front(), res.unwrap(), gadgets, new_block_id);

                    // change id if not already changed
                    if (id_.get_block_id() != new_block_id) {
                        NodeId old_id = id_;
                        id_.set_block_id(new_block_id);
                        gadgets->alloc.recache(old_id, this);
                    }

                } else return res.unwrap_err();
            } else return res.unwrap_err();

        } else {

            NodeId leaf_id(id_.derive_child_id(nibbles.front()), id_.get_block_id());
            // create and fill leaf
            Leaf_i* leaf = create_leaf(&leaf_id, nullptr);
            leaf->set_path(nibbles.subspan(1, nibbles.size() - 2));
            leaf->insert_child(nibbles.back(), val_hash, new_block_id);
            gadgets->alloc.cache_node(leaf);

            // set fake commitment will be derived when being finalized
            insert_child(nibbles.front(), {}, gadgets, new_block_id);

            if (id_.get_block_id() != new_block_id) {
                NodeId old_id = id_;
                id_.set_block_id(new_block_id);
                gadgets->alloc.recache(old_id, this);
            }
        }

        return OK;
    }

    int remove(
        Gadgets *gadgets, 
        ByteSlice nibbles,
        const Hash &kv,
        uint16_t new_block_id
    ) override {
        if (const NodeId* next_id = get_next_id(nibbles)) {
            Result<Node*, int> res = gadgets->alloc.load_node(next_id);
            if (res.is_ok()) {
                Node* n = res.unwrap();

                auto nib = nibbles.front();

                int res = n->remove(gadgets, nibbles.subspan(1), kv, new_block_id);

                if (res == DELETED) {

                    if (!scalar_is_zero(children_[nib])) {

                        child_block_ids_[nib] = new_block_id;
                        children_[nib] = ZERO_SK;
                        count_--;

                    } else return ALREADY_DELETED;

                } else if (res == OK) {

                    child_block_ids_[nib] = new_block_id;

                } else return res;

                if (id_.get_block_id() != new_block_id) {
                    NodeId old_id(id_);
                    id_.set_block_id(new_block_id);
                    int cache_res = gadgets->alloc.recache(old_id, this);
                    if (cache_res != 0) return cache_res;
                }

                return OK;
            }
        }
        return NOT_EXIST;
    }

    inline int delete_account(
        Gadgets *gadgets, 
        ByteSlice nibbles,
        const Hash &kv,
        uint16_t new_block_id
    ) override { 
        return remove(gadgets, nibbles, kv, new_block_id); 
    }


    Result<const Commitment*, int> finalize(
        Gadgets* gadgets,
        const uint16_t block_id
    ) override {

        for (int i = 0; i < BRANCH_ORDER; i++) {
            if (child_block_ids_[i] != block_id ||
                scalar_is_zero(children_[i])) 
                continue;

            tmp_id_.set_block_id(child_block_ids_[i]);
            tmp_id_.set_node_id(id_.derive_child_id(i));

            Result<Node*, int> res = gadgets->alloc.load_node(&tmp_id_);
            if (res.is_err()) return res.unwrap_err();

            auto final_res = res.unwrap()->finalize(gadgets, block_id);
            if (final_res.is_err()) return final_res.unwrap_err();

            hash_p1_to_sk(children_[i], *final_res.unwrap(), &gadgets->settings.tag);
        }

        derive_commitment(gadgets);
        return &commit_;
    }

    int prune(
        Gadgets* gadgets,
        const uint16_t block_id
    ) override {
        for (int i = 0; i < BRANCH_ORDER; i++) {
            if (child_block_ids_[i] != block_id) continue;

            tmp_id_.set_block_id(child_block_ids_[i]);
            tmp_id_.set_node_id(id_.derive_child_id(i));

            Result<Node*, int> res = gadgets->alloc.load_node(&tmp_id_);
            if (res.is_err()) continue;

            int prune_res = res.unwrap()->prune(gadgets, block_id);
            if (prune_res != OK) return prune_res;

            delete res.unwrap();

        }

        auto res = gadgets->alloc.delete_node(id_);
        if (res.is_err()) return res.unwrap_err();

        return OK;
    }

    int justify(Gadgets* gadgets) override {

        for (int i = 0; i < BRANCH_ORDER; i++) {
            if (child_block_ids_[i] == 0) continue;

            uint64_t node_id = id_.derive_child_id(i);
            tmp_id_.set_node_id(node_id);
            tmp_id_.set_block_id(child_block_ids_[i]);

            Result<Node*, int> res = gadgets->alloc.load_node(&tmp_id_);
            if (res.is_err()) return res.unwrap_err();

            int just_res = res.unwrap()->justify(gadgets);
            if (just_res == DELETED) {

                delete res.unwrap();

            } else if (just_res != OK) 
                return just_res;

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

Branch_i* create_branch(const NodeId* id, const ByteSlice* buff) {
    return new Branch(id, buff);
}
