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
#include "bitmap.h"
#include "hashing.h"
#include "db.h"
#include "lru.h"
#include "settings.h"

using Commitment = blst_p1;
using Proof = blst_p1;

const uint64_t ORDER = 256;
const byte BRANCH = static_cast<const byte>(69);
const byte LEAF = static_cast<const byte>(71);

class Ledger; // forward declared and only used via reference

/// ID CONVERSIONS
using NodeId = std::array<byte, 8>;
inline NodeId u64_to_id(uint64_t num) { 
    return std::bit_cast<NodeId>(num); 
}
inline uint64_t u64_from_id(NodeId a) { 
    return std::bit_cast<uint64_t>(a); 
}


/////////////////////////////////////////////////
//////////    NODE VIRTUAL CLASS    ////////////
///////////////////////////////////////////////
class Node {
public:
    virtual const NodeId* get_id() const = 0;
    virtual void change_id(const NodeId &id, Ledger &ledger) = 0;
    virtual const Commitment* get_commitment() const = 0;
    virtual const byte get_type() const = 0;
    virtual NodeId* get_next_id(ByteSlice &nibs) = 0;
    virtual std::vector<byte> to_bytes() const = 0;
    virtual const Commitment* derive_commitment(Ledger &ledger) = 0;
    virtual std::optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) = 0;
    virtual int build_commitment_path(
        Ledger &ledger, 
        const Hash &key,
        ByteSlice nibbles,
        std::vector<Polynomial> &Fxs, 
        Bitmap<32> &Zs
    ) = 0;
    virtual std::optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key,
        const Hash &val_hash
    ) = 0;
    virtual std::optional<const Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const Hash &key,
        const Hash &val_hash
    ) = 0;
    virtual std::optional<std::tuple<Commitment, bool>> remove(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key
    ) = 0;
    virtual ~Node() {};
};


///////////////////////////////////////////
//////    NODE VIRTUAL SUBCLASSES   //////
/////////////////////////////////////////
class Branch_i : public Node {
public:
    virtual void insert_child(
        const byte &nib, 
        const Commitment* new_commit
    ) = 0;
};
class Leaf_i : public Node {
public:
    virtual void insert_child(
        const byte &nib, 
        const Hash &val_hash
    ) = 0;
    virtual void set_path( ByteSlice path) = 0;
};
Branch_i* create_branch(
    std::optional<NodeId> id, 
    std::optional<ByteSlice*> buff
);
Leaf_i* create_leaf(
    std::optional<NodeId> id, 
    std::optional<ByteSlice*> buff
);

///////////////////////////////////////
//////////      LEDGER      //////////
/////////////////////////////////////
class Ledger {
private:
    Node* root_;
    BulletDB db_;
    LRUCache<uint64_t, Node*> cache_;
    SRS srs_;
    Polynomial poly_;
    std::string tag_;

public:
    Ledger(
        std::string path, 
        size_t cache_size, 
        size_t map_size,
        std::string tag,
        blst_scalar secret_sk
    );
    ~Ledger();
    const SRS* get_srs() const;
    const std::string* get_tag() const;
    void set_srs(
        std::vector<blst_p1> &g1s,
        std::vector<blst_p2> &g2s
    );
    void set_tag(std::string &tag);
    Polynomial* get_poly();

    bool key_value_exists(
        const Hash &key_hash,
        const Hash &val_hash
    );
    bool value_exists(const Hash &hash);
    std::optional<ByteSlice> get_value(
        ByteSlice &key, 
        byte idx
    );
    std::optional<std::tuple<
        Commitment, Proof, 
        std::vector<Commitment>, 
        std::vector<Scalar_vec>, 
        Scalar_vec
    >> get_existence_proof(
        ByteSlice &key, 
        uint8_t idx
    );
    std::optional<Commitment> put(
        ByteSlice &key, 
        ByteSlice &value, 
        uint8_t idx
    );
    std::optional<Commitment> virtual_put(
        ByteSlice &key, 
        ByteSlice &value, 
        uint8_t idx
    );
    std::optional<Commitment> remove(
        ByteSlice &key, 
        uint8_t idx
    );

    // STATE RELATED
    Node* load_node(const NodeId &id);
    void cache_node(Node* node);
    Node* delete_node(const NodeId &id);
    bool delete_value(const Hash &kv);
    Branch_i* new_cached_branch(uint64_t id);
    Leaf_i* new_cached_leaf(uint64_t id);
};
