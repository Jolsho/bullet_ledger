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


/*
 *  
 *  What is an SPLIT node?
 *      
 *           [root]
 *          / / \ \
 *       [1][2][3][4]
 *             / \
 *      [3->3.5] [3.5->4]
 *
 *  say we want to shard node #3
 *  and imagine we want to shard it in half...
 *  well you cant have each shard report back 256/2 hashes on each block.
 *  so instead we will turn node #3 into a SPLIT node which means the following.
 *      it will be a commitment to 2 children(in this case) and those 2 child nodes
 *      will be like regular branches except half of the children_ vector will be zero scalars.
 *
 *  the SPLIT node would then be in charge of keeping track of which node holds what part of the range.
 *      and directing a query toward the appropriate one given a key.
 *  then when a shard wants to report a hash they report the child of the SPLIT node.
 *
 *  we can then call methods like insert_SPLIT_hash() or something to fill the shards that dont belong to us.
 *      once we do that then we can start deriving global root states...
 *
 *
 *  WHEN TO SHARD AND HOW TO DO IT...
 *      you calculate the weight of the current tree...
 *          QUESTION -- how to keep track of weight.
 *
 *      if it reaches a threshold you SPLIT the root of that shard
 *      if it is already a split then you just further split the child and add the new nib to ranges
 *
 *      then a SPLIT call will be a path to the split, then the set of ranges...
 *          then you will compare your key against that.
 *          then you will delete the other branches of that split that are not yours.
 *          And you can do this for nodes anywhere
 *
 *      also you need to account for when a split node starts to become a branch...
 *      like if it has a range that is a single byte... that is a child then of a branch node...
 *      so you would need to consume its child nodes until you reach a node that is either a leaf or a branch with more than one child.
 *
 *      in this sense I think a split and a branch are not that different...
 *      i mean a split is just a more advanced branch.
 *
*/

#pragma once
#include "fft.h"
#include "gadgets.h"
#include "helpers.h"
#include "node.h"
#include "nodeid.h"
#include "state_types.h"

class Branch : public Node {
private:
    NodeId id_;

    Commitment commit_;

    std::vector<Child> children_;
    bool is_split_;

    Gadgets_ptr gadgets_;

    NodeId tmp_id_;

public:
    Branch(
        Gadgets_ptr gadgets, 
        const NodeId* id, 
        const ByteSlice* buff
    );
    ~Branch() {
        if (should_delete()) return;
        gadgets_->alloc.persist_node(this); 
    };


    const NodeId* get_id() const override { return &id_; }
    void set_id(const NodeId* h) override { id_ = *h; }

    int recache(uint16_t block_id) {
        tmp_id_ = id_;
        tmp_id_.set_block_id(block_id);

        int cache_res = gadgets_->alloc.recache(&id_, &tmp_id_);
        if (cache_res != OK) return cache_res;
        return OK;
    }

    const Commitment* derive_commitment() override {
        Polynomial poly(BRANCH_ORDER, ZERO_SK);
        for (auto &child: children_) {
            for (int i = child.anchor; i <= child.end; i++) {
                poly[i] = child.sk;
            }
        }
        inverse_fft_in_place(poly, gadgets_->settings.roots.inv_roots);
        commit_g1(&commit_, poly, gadgets_->settings.setup);
        return &commit_;
    }

    const Commitment* get_commitment() const override { return &commit_; }
    void set_commitment(const Commitment &c) override { commit_ = c; }

    bool should_delete() const override { return children_.size() == 0; }

    Child* get_child(byte nib);
    void insert_child(
        byte nib, 
        uint16_t block_id, 
        std::optional<byte> end = std::nullopt
    );
    void delete_child(byte nib);

    const NodeId* get_next_id(byte nib) override;

    std::vector<byte> to_bytes() const override;

    int put(
        const Hash* key,
        const Hash* val_hash,
        uint16_t block_id
    )override {
        return replace(key, val_hash, nullptr, block_id);
    }

    int replace(
        const Hash* key,
        const Hash* val_hash,
        const Hash* prev_val_hash,
        uint16_t block_id
    )override;

    int remove(
        const Hash* key,
        uint16_t block_id
    )override;

    int create_account(
        const Hash* key,
        uint16_t block_id
    ) override;

    int delete_account(
        const Hash* key,
        uint16_t block_id
    ) override {
        return remove(key, block_id);
    }

    int generate_proof(
        const Hash* key,
        std::vector<Polynomial> &Fxs,
        std::vector<blst_p1> &Cs,
        Bitmap<8>* split_map
    ) override;


    int finalize(
        const Hash* shard_path,
        uint16_t block_id,
        Commitment *out,
        size_t start = 0, 
        size_t end = 0,
        Polynomial* Fx = nullptr
    ) override;

    int prune(uint16_t block_id) override;

    int justify(uint16_t block_id) override;

    bool commit_is_in_path(
        const Hash* key,
        const Commitment &commitment
    ) override;
};

inline std::shared_ptr<Branch> create_branch(
    Gadgets_ptr gadgets, 
    const NodeId* id, 
    const ByteSlice* buff
) {
    return std::make_shared<Branch>(gadgets, id, buff);
}
