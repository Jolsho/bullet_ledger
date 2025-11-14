#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "state.h"
#include "lru.h"

using std::byte;
using std::optional;
using std::tuple;
using std::vector;

const uint64_t ORDER = 256;
const size_t BRANCH_SIZE = 1 + ( ORDER + 1) * 32;
const size_t EXT_SIZE = 1 + 32 + 32 + 8 + 64;
const size_t LEAF_SIZE = 1 + 32 + 32 + 8 + 64;
const byte BRANCH = static_cast<const byte>(69);
const byte EXT = static_cast<const byte>(70);
const byte LEAF = static_cast<const byte>(71);

using ByteSlice = std::span<byte>;
using Hash = std::array<byte, 32>;
using NodeId = std::array<byte, 8>;
using Hash_Id_Pair = std::tuple<Hash, NodeId>;

class Ledger; // forward declared and only used via reference

//////////////////////////////////////////////
//////////////    UTILS   ///////////////////
////////////////////////////////////////////

bool is_zero(std::span<std::byte> s);

Hash derive_leaf_hash(ByteSlice &key, Hash &hash);
Hash derive_value_hash(byte* value, size_t size);

using uint64_array = std::array<byte, 8>;
uint64_array u64_to_array(uint64_t num);
uint64_t u64_from_array(uint64_array a);


/////////////////////////////////////////////////
//////////    NODE VIRTUAL CLASS    ////////////
///////////////////////////////////////////////
class Node {
public:
    virtual NodeId* get_id();
    virtual Hash* get_hash();
    virtual byte get_type();
    virtual NodeId* get_next_id(ByteSlice &nibs);
    virtual std::vector<byte> to_bytes();
    virtual void change_id(NodeId &id, Ledger &ledger);
    virtual Hash derive_hash();
    virtual optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    );

    virtual optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        ByteSlice &key, 
        Hash &val_hash
    );
    virtual optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        ByteSlice &key, 
        Hash &val_hash
    );
    virtual optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
    );

};

/////////////////////////////////////////////////
///////////////    BRANCH    ///////////////////
///////////////////////////////////////////////
class Branch : public Node {
private:
    NodeId id_;
    Hash hash_;
    std::array<std::unique_ptr<Hash>, ORDER> children_;
    uint16_t count_;
    NodeId tmp_id_;

public:
    Branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff);
    ~Branch();

    void insert_child(byte &nib, Hash &hash);
    void delete_child(byte &nib);
    tuple<byte, Hash, NodeId> get_last_remaining_child();

    // NODE IMPLEMENTATION
    NodeId* get_id() { return &id_; };
    Hash* get_hash() { return &hash_; };
    byte get_type() { return BRANCH; };
    NodeId* get_next_id(ByteSlice &nibs);
    void change_id(NodeId &id, Ledger &ledger);
    Hash derive_hash();
    std::vector<byte> to_bytes();
    optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    );
    optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        ByteSlice &key, 
        Hash &val_hash
    );
    optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        ByteSlice &key, 
        Hash &val_hash
    );
    optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
    );

};

//////////////////////////////////////////////
///////////////    EXT    ///////////////////
////////////////////////////////////////////
using Path = std::deque<byte>;
class Extension : public Node {
private:
    NodeId id_;
    Hash hash_;
    Hash child_hash_;
    NodeId child_id_;
    Path path_;

public:
    Extension(std::optional<NodeId> id, std::optional<ByteSlice*> buffer);
    ~Extension();

    NodeId* get_child_id();
    Hash* get_child_hash();
    void set_child(Hash &hash);

    Path* get_path();
    void set_path(ByteSlice path);
    optional<size_t> in_path(ByteSlice nibbles);

    // NODE IMPLEMENTATION
    NodeId* get_id() { return &id_; };
    Hash* get_hash() { return &hash_; };
    byte get_type() { return EXT; };
    NodeId* get_next_id(ByteSlice &nibs);
    void change_id(NodeId &id, Ledger &ledger);
    Hash derive_hash();
    std::vector<byte> to_bytes();
    optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    );
    optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        ByteSlice &key, 
        Hash &val_hash
    );
    optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        ByteSlice &key, 
        Hash &val_hash
    );
    optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
    );
};

//////////////////////////////////////////////
///////////////    LEAF    //////////////////
////////////////////////////////////////////
class Leaf : public Node {
private:
    NodeId id_;
    Hash hash_;
    Path path_;
    Hash value_hash_;

public:
    Leaf(optional<NodeId> id, optional<ByteSlice*> buffer);
    ~Leaf();

    Path* get_path();
    void set_path(ByteSlice path);

    Hash* get_value_hash();
    void set_value_hash(Hash &hash);

    optional<size_t> in_path(ByteSlice nibbles);

    // NODE IMPLEMENTATION
    NodeId* get_id() { return &id_; };
    Hash* get_hash() { return &hash_; };
    byte get_type() { return LEAF; };
    NodeId* get_next_id(ByteSlice &nibs);
    void change_id(NodeId &id, Ledger &ledger);
    Hash derive_hash(ByteSlice &key);
    std::vector<byte> to_bytes();
    optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    );
    optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        ByteSlice &key, 
        Hash &val_hash
    );
    optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        ByteSlice &key, 
        Hash &val_hash
    );
    optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
    );
};



/////////////////////////////////////////////////
//////////    MAIN LEDGER CLASS    /////////////
///////////////////////////////////////////////

class LedgerState {
private:
    std::unique_ptr<Node> root_;
    BulletDB db_;
    LRUCache<NodeId, Node> cache_;

    LedgerState(std::vector<char> path, size_t cache_size);
    ~LedgerState();

    // only Ledger can access
    friend class Ledger;   
};

class Ledger {
private:
    LedgerState state_;

    Node* load_node(NodeId &id);
    void cache_node(uint64_t id, Node node);
    optional<Node> delete_node(NodeId &id);
    Branch* new_cached_branch(uint64_t id);
    Extension* new_cached_extension(uint64_t id);
    Leaf* new_cached_leaf(uint64_t id);
    friend class Branch;
    friend class Extension;
    friend class Leaf;

public:
    Ledger(std::vector<char> path, size_t cache_size);
    ~Ledger();

    bool value_exists(Hash &val_hash);
    bool key_value_exists(ByteSlice &key, Hash &val_hash);
    optional<ByteSlice> get_value(ByteSlice &key);
    optional<Hash> put(ByteSlice &key, ByteSlice &value);
    optional<Hash> virtual_put(ByteSlice &key, ByteSlice &value);
    optional<Hash> remove(ByteSlice &key);
};
