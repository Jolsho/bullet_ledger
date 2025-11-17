#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "../utils/ring_buff.cpp"
#include "../kzg/kzg.h"
#include "lru.cpp"
#include "db.h"

using std::optional;
using std::tuple;
using std::vector;

const uint64_t ORDER = 256;
const size_t BRANCH_SIZE = 1 + (ORDER + 1) * 48;
const size_t EXT_SIZE = 1 + 48 + 8 + 32;
const size_t LEAF_SIZE = 1 + 48 + 8 + 32;
const byte BRANCH = static_cast<const byte>(69);
const byte EXT = static_cast<const byte>(70);
const byte LEAF = static_cast<const byte>(71);

using Commitment = blst_p1;
using Commit_Serial = std::array<byte, 48>;
using Path = RingBuffer<byte>;
using ByteSlice = std::span<byte>;
using Hash = std::array<byte, 32>;
using NodeId = std::array<byte, 8>;
using Hash_Id_Pair = std::tuple<Hash, NodeId>;

class Ledger; // forward declared and only used via reference

//////////////////////////////////////////////
//////////////    UTILS   ///////////////////
////////////////////////////////////////////

bool iszero(const ByteSlice &slice);

using uint64_array = std::array<byte, 8>;
uint64_array u64_to_array(uint64_t num);
uint64_t u64_from_array(uint64_array a);

void commit_from_bytes(const byte* src, Commitment &dst);
Hash derive_k_vc_hash(const ByteSlice &key, const Commitment &val_c);

/////////////////////////////////////////////////
//////////    NODE VIRTUAL CLASS    ////////////
///////////////////////////////////////////////
class Node {
public:
    virtual NodeId* get_id() = 0;
    virtual void change_id(const NodeId &id, Ledger &ledger) = 0;

    virtual Commitment* get_commitment() = 0;
    virtual byte get_type() = 0;
    virtual NodeId* get_next_id(ByteSlice &nibs) = 0;

    virtual std::vector<byte> to_bytes() = 0;

    virtual Commitment* derive_commitment(Ledger &ledger) = 0;

    virtual optional<Commitment> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) = 0;

    virtual optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) = 0;
    virtual optional<Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) = 0;
    virtual optional<std::tuple<Commitment, bool, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice nibbles
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

    optional<Commitment*> get_child(byte &nib);
    void insert_child(const byte &nib, Commitment new_commit);
    void delete_child(const byte &nib);
    tuple<byte, Commitment, NodeId> get_last_remaining_child();

    // NODE IMPLEMENTATION
    NodeId* get_id() override { return &id_; };
    Commitment* get_commitment() override { return &commit_; };
    byte get_type() override { return BRANCH; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    void change_id(const NodeId &id, Ledger &ledger) override;

    Commitment* derive_commitment(Ledger &ledger) override;

    std::vector<byte> to_bytes() override;
    optional<Commitment> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;
    optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) override;
    optional<Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) override;
    optional<std::tuple<Commitment, bool, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;

};

//////////////////////////////////////////////
///////////////    EXT    ///////////////////
////////////////////////////////////////////

class Extension : public Node {
private:
    NodeId id_;
    Commitment commit_;
    NodeId child_id_;
    Path path_;

public:
    Extension(std::optional<NodeId> id, std::optional<ByteSlice*> buffer);

    NodeId* get_child_id();
    void set_child(Commitment &c);

    Path* get_path();
    void set_path(ByteSlice path);
    optional<size_t> in_path(ByteSlice nibbles);

    // NODE IMPLEMENTATION
    NodeId* get_id() override { return &id_; };
    Commitment* get_commitment() override { return &commit_; };
    byte get_type() override { return EXT; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    void change_id(const NodeId &id, Ledger &ledger) override;
    std::vector<byte> to_bytes() override;

    Commitment* derive_commitment(Ledger &ledger) override;

    optional<Commitment> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;
    optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) override;
    optional<Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) override;
    optional<std::tuple<Commitment, bool, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;
};

//////////////////////////////////////////////
///////////////    LEAF    //////////////////
////////////////////////////////////////////
class Leaf : public Node {
private:
    NodeId id_;
    Commitment commit_;
    Path path_;

public:
    Leaf(optional<NodeId> id, optional<ByteSlice*> buffer);

    Path* get_path();
    void set_path(ByteSlice path);
    void set_commitment(const Commitment &c);
    optional<size_t> in_path(const ByteSlice nibbles);

    // NODE IMPLEMENTATION
    NodeId* get_id() override { return &id_; };
    void change_id(const NodeId &id, Ledger &ledger) override;

    byte get_type() override { return LEAF; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    std::vector<byte> to_bytes() override;

    Commitment* get_commitment() override { return &commit_; };
    Commitment* derive_commitment(Ledger &ledger) override;

    optional<Commitment> search( 
        Ledger &ledger, 
        ByteSlice nibbles
    ) override;
    optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) override;
    optional<Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const ByteSlice &key, 
        const Commitment &val_commitment
    ) override;
    optional<std::tuple<Commitment, bool, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice nibbles
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
    optional<Node_ptr> delete_node(const NodeId &id);
    Branch* new_cached_branch(uint64_t id);
    Extension* new_cached_extension(uint64_t id);
    Leaf* new_cached_leaf(uint64_t id);
    friend class Branch;
    friend class Extension;
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
    bool value_exists(Hash &hash);
    bool key_value_exists(ByteSlice &key, Commitment &val_c);
    optional<ByteSlice> get_value(ByteSlice &key);
    optional<Commitment> put(ByteSlice &key, Commitment &val_commitment);
    optional<Commitment> virtual_put(ByteSlice &key, Commitment &val_commitment);
    optional<Commitment> remove(ByteSlice &key);
};
