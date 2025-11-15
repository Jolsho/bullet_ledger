#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

#include "state.h"
#include "lru.h"
#include "../utils/structs.h"

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

Hash derive_leaf_hash(const ByteSlice &key, const Hash &hash);
Hash derive_value_hash(byte* value, size_t size);

using uint64_array = std::array<byte, 8>;
uint64_array u64_to_array(uint64_t num);
uint64_t u64_from_array(uint64_array a);
void print_hash(const byte* a, const size_t size, const char* tag);

/////////////////////////////////////////////////
//////////    NODE VIRTUAL CLASS    ////////////
///////////////////////////////////////////////
class Node {
public:
    virtual NodeId* get_id() = 0;
    virtual Hash* get_hash() = 0;
    virtual byte get_type() = 0;
    virtual NodeId* get_next_id(ByteSlice &nibs) = 0;
    virtual std::vector<byte> to_bytes() = 0;
    virtual void change_id(const NodeId &id, Ledger &ledger) = 0;
    virtual Hash derive_hash() = 0;
    virtual optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    ) = 0;

    virtual optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        const ByteSlice &key, 
        const Hash &val_hash
    ) = 0;
    virtual optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        const ByteSlice &key, 
        const Hash &val_hash
    ) = 0;
    virtual optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
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
    Hash hash_;
    std::array<std::unique_ptr<Hash>, ORDER> children_;
    uint16_t count_;
    NodeId tmp_id_;

public:
    Branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff);

    void insert_child(byte &nib, Hash &hash);
    void delete_child(byte &nib);
    tuple<byte, Hash, NodeId> get_last_remaining_child();

    // NODE IMPLEMENTATION
    NodeId* get_id() override { return &id_; };
    Hash* get_hash() override { return &hash_; };
    byte get_type() override { return BRANCH; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    void change_id(const NodeId &id, Ledger &ledger) override;
    Hash derive_hash() override;
    std::vector<byte> to_bytes() override;
    optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    ) override;
    optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        const ByteSlice &key, 
        const Hash &val_hash
    ) override;
    optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        const ByteSlice &key, 
        const Hash &val_hash
    ) override;
    optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
    ) override;

};

//////////////////////////////////////////////
///////////////    EXT    ///////////////////
////////////////////////////////////////////
using Path = RingBuffer<byte>;

class Extension : public Node {
private:
    NodeId id_;
    Hash hash_;
    Hash child_hash_;
    NodeId child_id_;
    Path path_;

public:
    Extension(std::optional<NodeId> id, std::optional<ByteSlice*> buffer);

    NodeId* get_child_id();
    Hash* get_child_hash();
    void set_child(Hash &hash);

    Path* get_path();
    void set_path(ByteSlice path);
    optional<size_t> in_path(ByteSlice nibbles);

    // NODE IMPLEMENTATION
    NodeId* get_id() override { return &id_; };
    Hash* get_hash() override { return &hash_; };
    byte get_type() override { return EXT; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    void change_id(const NodeId &id, Ledger &ledger) override;
    Hash derive_hash() override;
    std::vector<byte> to_bytes() override;
    optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    ) override;
    optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        const ByteSlice &key, 
        const Hash &val_hash
    ) override;
    optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        const ByteSlice &key, 
        const Hash &val_hash
    ) override;
    optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
    ) override;
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

    Path* get_path();
    void set_path(ByteSlice path);

    Hash* get_value_hash();
    void set_value_hash(const Hash &hash);

    optional<size_t> in_path(const ByteSlice nibbles);
    Hash derive_real_hash(const ByteSlice &key);

    // NODE IMPLEMENTATION
    NodeId* get_id() override { return &id_; };
    Hash* get_hash() override { return &hash_; };
    byte get_type() override { return LEAF; };
    NodeId* get_next_id(ByteSlice &nibs) override;
    void change_id(const NodeId &id, Ledger &ledger) override;
    Hash derive_hash() override;
    std::vector<byte> to_bytes() override;
    optional<Hash> search( 
        Ledger &ledger, 
        ByteSlice &nibbles
    ) override;
    optional<Hash> virtual_put(
        Ledger &ledger, 
        ByteSlice &nibbles,
        const ByteSlice &key, 
        const Hash &val_hash
    ) override;
    optional<Hash> put(
        Ledger &ledger, 
        ByteSlice &nibbles, 
        const ByteSlice &key, 
        const Hash &val_hash
    ) override;
    optional<std::tuple<Hash, ByteSlice>> remove(
        Ledger &ledger, 
        ByteSlice &nibbles
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

    LedgerState(std::string path, size_t cache_size, size_t map_size);
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
    Ledger(std::string path, size_t cache_size, size_t map_size);
    ~Ledger();

    bool value_exists(Hash &val_hash);
    bool key_value_exists(ByteSlice &key, Hash &val_hash);
    optional<ByteSlice> get_value(ByteSlice &key);
    optional<Hash> put(ByteSlice &key, ByteSlice &value);
    optional<Hash> virtual_put(ByteSlice &key, ByteSlice &value);
    optional<Hash> remove(ByteSlice &key);
};
