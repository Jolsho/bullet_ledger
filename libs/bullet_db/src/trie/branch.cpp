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

#include "branch.h"
#include "blst.h"
#include "hashing.h"
#include "helpers.h"
#include "leaf.h"
#include "polynomial.h"
#include "state_types.h"
#include <utility>

Branch::Branch(Gadgets_ptr gadgets, const NodeId* id, const ByteSlice* buff) :
    id_(id),
    commit_{new_inf_p1()},
    is_split_{false},
    gadgets_{gadgets}
{
    if (buff == nullptr) { return; }

    byte* cursor = buff->data(); 
    cursor++;

    is_split_ = *cursor++;

    commit_ = p1_from_bytes(cursor); 
    cursor += blst_p1_sizeof();

    uint8_t children_count = *cursor++; 
    children_.assign(children_count, {});

    for (auto &child: children_) {
         child.anchor = *cursor++;
         child.end = *cursor++;

        blst_scalar_from_le_bytes(&child.sk, cursor, sizeof(blst_scalar));
        cursor += sizeof(blst_scalar);

        std::memcpy(&child.blk_id, cursor, sizeof(uint16_t));
        cursor += sizeof(uint16_t);
    }
}

std::vector<byte> Branch::to_bytes() const {

    std::vector<byte> buffer(
        sizeof(BRANCH) + 
        sizeof(is_split_) + 
        blst_p1_sizeof() + 
        sizeof(uint8_t) +
        (children_.size() * (2 + 32 + 2))
    );

    byte* cursor = buffer.data();

    *cursor++ = BRANCH; 

    *cursor++ = is_split_; 

    blst_p1_compress(cursor, &commit_); 
    cursor += blst_p1_sizeof();

    *cursor++ = children_.size(); 

    for (auto &child: children_) {
        *cursor++ = child.anchor;
        *cursor++ = child.end;

        std::memcpy(cursor, &child.sk.b, sizeof(blst_scalar));
        cursor += sizeof(blst_scalar);

        std::memcpy(cursor, &child.blk_id, sizeof(uint16_t));
        cursor += sizeof(uint16_t);
    }

    return buffer;
}

void Branch::insert_child(
    byte nib, 
    uint16_t block_id, 
    std::optional<byte> end
) {

    if (!end.has_value()) end = nib;

    Child* child = get_child(nib);
    if (!child && !is_split_) {

        int i{};
        for (; i < children_.size(); i++)
            if (nib < children_[i].anchor) break;

        children_.resize(children_.size() + 1);

        Child tmp = {nib, end.value(), ZERO_SK, block_id};
        tmp.sk.b[0] = 1;

        for (; i < children_.size(); i++) {
            std::swap(tmp, children_[i]);
        }

        return;
    }

    if (scalar_is_zero(child->sk)) child->sk.b[0] = 1;
    child->blk_id = block_id;
}

void Branch::delete_child(byte nib) {
    int i{};
    for (; i < children_.size(); i++) {
        Child* child = &children_[i];

        if (child->anchor <= nib && nib <= child->end) 
            break;
    }

    i++;

    for (; i < children_.size(); i++) {
        std::swap(children_[i - 1], children_[i]);
    }

    children_.pop_back();
}


Child* Branch::get_child(byte nib) {

    byte i = nib;

    // find child such that i < child.end && child.anchor < i
    for (int k{}; k < children_.size(); k++) {

        auto &child = children_[k];

        if (child.anchor <= i && i <= child.end) 
            return &child;
    }

    return nullptr;
}


const NodeId* Branch::get_next_id(byte nib) {
    Child* child = get_child(nib);
    if (!child) return nullptr;

    if (scalar_is_zero(child->sk)) return nullptr;

    tmp_id_.set_node_id(id_.derive_child_id(child->anchor));
    tmp_id_.set_block_id(child->blk_id);

    return &tmp_id_;
}

int Branch::generate_proof(
    const Hash* key,
    std::vector<Polynomial> &Fxs,
    std::vector<blst_p1> &Cs,
    int i, 
    Bitmap<8>* split_map
) {

    byte nib = key->h[i];

    const NodeId* next_id = get_next_id(nib);
    if (!next_id) return NOT_EXIST;

    Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
    if (res.is_err()) return res.unwrap_err();

    if (is_split_) {
        split_map->set(i);
    } else {
        i++;
    }

    int rc = res.unwrap()->generate_proof(key, Fxs, Cs, i, split_map);
    if (rc != OK) return rc;

    Polynomial Fx(BRANCH_ORDER, ZERO_SK);
    for (auto &child: children_) {
        for (int i = child.anchor; i <= child.end; i++) {
            Fx[i] = child.sk;
        }
    }

    Fxs.push_back(Fx);
    Cs.push_back(commit_);

    return OK;
}

int Branch::replace(
    const Hash* key,
    const Hash* val_hash,
    const Hash* prev_val_hash,
    uint16_t block_id,
    int i
) {
    byte nib = key->h[i];

    const NodeId* next_id = get_next_id(nib);
    if (!next_id) return NOT_EXIST;

    Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
    if (res.is_err()) return res.unwrap_err();

    if (!is_split_) i++;

    int rc = res.unwrap()->replace(key, val_hash, prev_val_hash, block_id, i);
    if (rc != OK) return rc;

    if (id_.get_block_id() != block_id) {
        NodeId new_id(id_.get_node_id(), block_id);
        int cache_res = gadgets_->alloc.recache(&id_, &new_id);
        if (cache_res != OK) return cache_res;
    }

    insert_child(nib, block_id);

    return OK;
}

int Branch::remove(
    const Hash* key,
    uint16_t block_id, 
    int i
) {
    byte nib = key->h[i];

    const NodeId* next_id = get_next_id(nib);
    if (!next_id) return NOT_EXIST;

    Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
    if (res.is_err()) return res.unwrap_err();

    if (!is_split_) i++;

    int rc = res.unwrap()->remove(key, block_id, i);
    if (rc != DELETED && rc != OK) return rc;

    // ensure new cached node to be modified
    if (id_.get_block_id() != block_id) {
        NodeId new_id (id_.get_node_id(), block_id);
        int cache_res = gadgets_->alloc.recache(&id_, &new_id);
        if (cache_res != OK) return cache_res;
    }

    Child* child = get_child(nib);
    if (!child || scalar_is_zero(child->sk)) 
        return ALREADY_DELETED;

    child->blk_id = block_id;

    if (rc == DELETED) {

        delete_child(nib);

        if (children_.size() == 0) {
            auto res = gadgets_->alloc.delete_node(id_);
            if (res.is_err() && res.unwrap_err() != MDB_NOTFOUND) 
                return res.unwrap_err();

            return DELETED;
        }
    }

    return OK;
}

int Branch::create_account(
    const Hash* key,
    uint16_t block_id, 
    int i
) { 
    byte nib = key->h[i];
    const NodeId* next_id = get_next_id(nib);

    if (next_id) {

        Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
        if (res.is_err()) return res.unwrap_err();

        if (!is_split_) i++;

        int rc = res.unwrap()->create_account(key, block_id, i);
        if (rc != OK) return rc;


    } else {

        // TODO -- actually needs to calculate id of new child

        // NORMALLY YOU WOULD DO:
        //      nib = determine_child_idx(nib);
        // but since ledger prevents this method from 
        // being called outside of shard, and the only
        // time a split occurs is if children exist and
        // are heavy there is no need to do it here.

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

int Branch::finalize(
    std::vector<ShardVote>* hashes,
    const uint16_t block_id,
    Commitment *out,
    const size_t start, 
    size_t end,
    Polynomial* Fx
) {
    if (end == 0) end = BRANCH_ORDER;

    NodeId tmp(0, block_id);

    Commitment child_commit;

    for (auto &child: children_) {
        if (child.anchor < start) continue;
        if (end < child.end) break;

        if (child.blk_id != block_id || 
            scalar_is_zero(child.sk)) 
            continue;

        tmp.set_node_id(id_.derive_child_id(child.anchor));

        auto loaded = gadgets_->alloc.load_node(&tmp, true);
        if (loaded.is_err()) {

            // TODO -- if child is out of shard path.
            // then we don't need to report an error here.
            
            return LOAD_NODE_ERR;
        }

        Node_ptr child_node = loaded.unwrap();

        int rc = child_node->finalize(hashes, block_id, &child_commit);
        if (rc != OK) return rc;

        hash_p1_to_scalar(
            &child_commit, 
            &child.sk, 
            &gadgets_->settings.tag
        );

        if (Fx && !out) {
            // fill in range from in fx if available
            for (int i = child.anchor; i <= child.end; i++) {
                Fx->at(i) = child.sk;
            }
        }
    }

    if (!Fx && out) {
        *out = *derive_commitment();
    }

    if (is_split_) {
        // meaning we need the key path of this node.
        // then return a tuple of path and hash
        Hash commit_hash; 
        blst_scalar sk;
        hash_p1_to_scalar(&commit_, &sk, &gadgets_->settings.tag);
        std::memcpy(commit_hash.h, sk.b, 32);
        hashes->push_back({commit_hash, path_});
    }

    return OK;
}

int Branch::prune(const uint16_t block_id) {
    int rc {};
    tmp_id_.set_block_id(block_id);
    for (auto &child: children_) {

        if (child.blk_id != block_id) continue;

        tmp_id_.set_node_id(id_.derive_child_id(child.anchor));

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
    children_.clear();


    // delete
    auto res = gadgets_->alloc.delete_node(id_);
    if (res.is_err() && res.unwrap_err() != MDB_NOTFOUND) 
        return res.unwrap_err();

    return OK;
}

/// TODO -- thinking about block ids here with split nodes...
/// NOT SURE WHAT TO DO THERE...

int Branch::justify(const uint16_t block_id) {

    // change all child block ids != 0 -> 0
    for (auto &child: children_) {

        if (child.blk_id == 0) continue;

        tmp_id_.set_node_id(id_.derive_child_id(child.anchor));
        tmp_id_.set_block_id(child.blk_id);

        Result<Node_ptr, int> res = gadgets_->alloc.load_node(&tmp_id_);
        if (res.is_err()) return res.unwrap_err();

        int just_res = res.unwrap()->justify(block_id);
        if (just_res != OK && just_res != DELETED) return just_res;

        child.blk_id = 0;
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

bool Branch::commit_is_in_path(
    const Hash* key,
    const Commitment &commitment,
    int i
) {
    if (blst_p1_is_equal(&commit_, &commitment)) return true;

    Child* child = get_child(key->h[i]);
    if (!child) return false;

    const NodeId* next_id = get_next_id(child->anchor);
    if (next_id) {

        Result<Node_ptr, int> res = gadgets_->alloc.load_node(next_id);
        if (res.is_err()) {
            // this happens when this node is the split into other shards
            // so we dont have the rest of the shard we can just check 
            // the specified child hash and thats all
            if (is_split_) {
                blst_scalar sk;
                hash_p1_to_scalar(&commitment, &sk, &gadgets_->settings.tag);
                return equal_scalars(sk, child->sk);
            }

            return res.unwrap_err();
        }

        if (!is_split_) i++;

        return res.unwrap()->commit_is_in_path(key, commitment, i);
    }

    return false;
};


