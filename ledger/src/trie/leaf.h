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

#pragma once
#include "fft.h"
#include "gadgets.h"
#include "helpers.h"

// TODO -- make children vector cleaner...like branch

class Leaf : public Node {
private:
    NodeId id_;
    Hash path_;

    Commitment commit_;

    uint8_t count_;
    bool is_deleted_;

    std::vector<Hash> children_;
    std::vector<uint16_t> child_block_ids_;

    Gadgets_ptr gadgets_;


public:

    Leaf(
        Gadgets_ptr gadgets, 
        const NodeId* id, 
        const ByteSlice* buff
    );
    ~Leaf() override;


    void insert_child(
        byte nib, 
        const Hash* val_hash, 
        uint16_t block_id
    );

    int recache(uint16_t block_id) {
        NodeId tmp_id_ = id_;
        tmp_id_.set_block_id(block_id);

        int cache_res = gadgets_->alloc.recache(&id_, &tmp_id_);
        if (cache_res != OK) return cache_res;
        return OK;
    }

    bool should_delete() const override { return is_deleted_; };

    const Commitment* get_commitment() const override { return &commit_; };
    void set_commitment(const Commitment &c) override { commit_ = c; };
    const Commitment* derive_commitment() override {
        Polynomial poly(BRANCH_ORDER, ZERO_SK);
        for (int i{}; i < LEAF_ORDER; i++) {
            if (!hash_is_zero(children_[i]))
                blst_scalar_from_le_bytes(&poly[i], children_[i].h ,32);
        }

        inverse_fft_in_place(poly, gadgets_->settings.roots.inv_roots);
        commit_g1(&commit_, poly, gadgets_->settings.setup);
        return &commit_;
    }

    const NodeId* get_id() const override { return &id_; }
    void set_id(const NodeId* h) override { id_ = *h; }
    void set_path(const Hash* path);

    std::optional<size_t> matching_path(const Hash* key);

    const NodeId* get_next_id(byte nib) override { return nullptr; }

    std::vector<byte> to_bytes() const override;

    inline int put(
        const Hash* key,
        const Hash* val_hash,
        uint16_t block_id
    ) override {
        return replace(key, val_hash, nullptr, block_id);
    }

    int replace(
        const Hash* key,
        const Hash* val_hash,
        const Hash* prev_val_hash,
        uint16_t block_id
    ) override;

    int remove(
        const Hash* key,
        uint16_t block_id
    ) override;

    int create_account(
        const Hash* key,
        uint16_t block_id
    ) override;

    int delete_account(
        const Hash* key,
        uint16_t block_id
    ) override;

    int generate_proof(
        const Hash* key,
        std::vector<Polynomial> &Fxs,
        std::vector<blst_p1> &Cs,
        Bitmap<8>* split_map
    ) override;

    inline int finalize(
        const Hash* shard_path,
        uint16_t block_id,
        Commitment *out,
        size_t start, 
        size_t end,
        Polynomial* Fx
    ) override {
        *out = *derive_commitment();
        return OK;
    }

    int prune(uint16_t block_id) override;

    int justify(uint16_t block_id) override;

    inline bool commit_is_in_path(
        const Hash* key,
        const Commitment &commitment
    ) override {
        return blst_p1_is_equal(&commit_, &commitment);
    }
};

inline std::shared_ptr<Leaf> create_leaf(
    Gadgets_ptr gadgets, 
    const NodeId* id, 
    const ByteSlice* buff
) {
    return std::make_shared<Leaf>(gadgets, id, buff);
}
