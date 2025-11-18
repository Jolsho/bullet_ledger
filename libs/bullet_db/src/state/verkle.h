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
#include <span>

#include "../utils/ring_buffer.h"
#include "../kzg/kzg.h"
#include "../utils/bitmap.h"
#include "lru.cpp"
#include "db.h"

const uint64_t ORDER = 256;
const size_t BRANCH_SIZE = (ORDER + 1) * 49;
const size_t LEAF_SIZE = 1 + 48 + 2 + 32 + (ORDER * 33);
const byte BRANCH = static_cast<const byte>(69);
const byte LEAF = static_cast<const byte>(71);

using Commitment = blst_p1;
using Proof = blst_p1_affine;
using Path = RingBuffer<byte>;
using ByteSlice = std::span<byte>;
using Hash = std::array<byte, 32>;
using NodeId = std::array<byte, 8>;

class Ledger; // forward declared and only used via reference

//////////////////////////////////////////////
//////////////    UTILS   ///////////////////
////////////////////////////////////////////

using uint64_array = std::array<byte, 8>;
uint64_array u64_to_array(uint64_t num);
uint64_t u64_from_array(uint64_array a);
void commit_from_bytes(const byte* src, Commitment* dst);
Hash derive_kv_hash(const Hash &key_hash, const Hash &val_hash);
Hash derive_hash(const ByteSlice &value);
bool iszero(const ByteSlice &slice);
Commitment derive_init_commit(byte nib, const Commitment &c, Ledger &ledger);
void print_hash(const Hash& hash);


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
        vector<scalar_vec> &Fxs, 
        Bitmap &Zs
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

using Node_ptr = std::unique_ptr<Node>;

/////////////////////////////////////////////////
///////////////    BRANCH    ///////////////////
///////////////////////////////////////////////
class Branch : public Node {
private:
    NodeId id_;
    Commitment commit_;
    std::vector<std::unique_ptr<Commitment>> children_;
    uint16_t count_;
    NodeId tmp_id_;

public:
    Branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff);
    const std::optional<Commitment*> get_child(const byte &nib) const;
    void insert_child(const byte &nib, const Commitment* new_commit);
    void delete_child(const byte &nib);
    const std::tuple<byte, Commitment, NodeId> get_last_remaining_child() const;


    // NODE IMPLEMENTATION
    const NodeId* get_id() const override { return &id_; };
    const Commitment* get_commitment() const override { return &commit_; };
    const byte get_type() const override { return BRANCH; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    void change_id(const NodeId &id, Ledger &ledger) override;
    const Commitment* derive_commitment(Ledger &ledger) override;
    std::vector<byte> to_bytes() const override;
    std::optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;

    int build_commitment_path(
        Ledger &ledger, 
        const Hash &key,
        ByteSlice nibbles,
        vector<scalar_vec> &Fxs, 
        Bitmap &Zs
    ) override;

    std::optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key,
        const Hash &val_hash
    ) override;

    std::optional<const Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const Hash &key,
        const Hash &val_hash
    ) override;

    std::optional<std::tuple<Commitment, bool>> remove(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key
    ) override;
};

//////////////////////////////////////////////
///////////////    LEAF    //////////////////
////////////////////////////////////////////
class Leaf : public Node {
private:
    NodeId id_;
    Commitment commit_;
    vector<std::unique_ptr<Hash>> children_;
    Path path_;
    uint8_t count_;

public:
    Leaf(std::optional<NodeId> id, std::optional<ByteSlice*> buffer);

    Path* get_path();
    void set_path(ByteSlice path);
    void insert_child(const byte &nib, const Hash &val_hash);
    std::optional<size_t> in_path(const ByteSlice nibbles);

    // NODE IMPLEMENTATION
    const NodeId* get_id() const override { return &id_; };
    void change_id(const NodeId &id, Ledger &ledger) override;
    const byte get_type() const override { return LEAF; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    std::vector<byte> to_bytes() const override;
    const Commitment* get_commitment() const override { return &commit_; };
    const Commitment* derive_commitment(Ledger &ledger) override;
    std::optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;

    int build_commitment_path(
        Ledger &ledger, 
        const Hash &key,
        ByteSlice nibbles,
        vector<scalar_vec> &Fxs, 
        Bitmap &Zs
    ) override;

    std::optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key, 
        const Hash &val_hash
    ) override;

    std::optional<const Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const Hash &key, 
        const Hash &val_hash
    ) override;

    std::optional<std::tuple<Commitment, bool>> remove(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key
    ) override;
};


/////////////////////////////////////////////////
//////////    MAIN LEDGER CLASS    /////////////
///////////////////////////////////////////////

class LedgerState {
private:
    std::unique_ptr<Node> root_;
    BulletDB db_;
    LRUCache<uint64_t, Node> cache_;
    SRS srs_;
    scalar_vec poly_;

    LedgerState(
        std::string path, 
        size_t cache_size, 
        size_t map_size,
        blst_scalar secret_sk
    );
    ~LedgerState() = default;

    // only Ledger can access
    friend class Ledger;   
};

class Ledger {
private:
    LedgerState state_;

    Node* load_node(const NodeId &id);
    void cache_node(std::unique_ptr<Node> node);
    std::optional<Node_ptr> delete_node(const NodeId &id);
    bool delete_value(const Hash &kv);
    Branch* new_cached_branch(uint64_t id);
    Leaf* new_cached_leaf(uint64_t id);
    scalar_vec* get_poly();
    friend class Branch;
    friend class Leaf;

public:
    Ledger(
        std::string path, 
        size_t cache_size, 
        size_t map_size,
        blst_scalar secret_sk
    );
    ~Ledger();

    SRS* get_srs();
    bool value_exists(const Hash &hash);
    bool key_value_exists(const ByteSlice &key, const Hash &val_hash, uint8_t idx);
    std::optional<ByteSlice> get_value(ByteSlice &key, uint8_t idx);
    std::optional<std::tuple<
        Commitment, Proof, 
        vector<Commitment>, 
        vector<scalar_vec>, 
        scalar_vec
    >> get_existence_proof(ByteSlice &key, uint8_t idx);
    std::optional<Commitment> put(ByteSlice &key, ByteSlice &value, uint8_t idx);
    std::optional<Commitment> virtual_put(ByteSlice &key, ByteSlice &value, uint8_t idx);
    std::optional<Commitment> remove(ByteSlice &key, uint8_t idx);
};
