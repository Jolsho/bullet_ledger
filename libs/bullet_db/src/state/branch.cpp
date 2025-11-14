#include "blake3.h"
#include "nodes.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>
#include <vector>

Branch::Branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff) {
    if (!id.has_value()) { return; }
    NodeId node_id = id.value();
    ByteSlice* buffer = buff.value();

    id_ = node_id;
    uint64_t p = u64_from_array(node_id);
    for (size_t i = 0; i < static_cast<size_t>(ORDER + 1); i++) {
        size_t start = 32 * i;
        size_t end = 32 * (i + 1);

        if (i == 0) {
            while (start < end) {
                hash_[start] = (*buffer)[start];
                start++;
            }
        } else {
            auto current = buffer->subspan(start,end);
            if (!is_zero(current)) {
                Hash hash;
                std::ranges::copy(current, hash.begin());

                uint64_t child_id = (p & ORDER) + (i - 1);
                children_[i - 1] = std::make_unique<Hash>(hash);
            } else {
                children_[i-1] = std::make_unique<Hash>();
            }
        }
    }
}

Branch::~Branch() {}


void Branch::insert_child(byte &nib, Hash &hash) {
    auto nib_num = static_cast<int>(nib);
    if (Hash* child_hash = children_[nib_num].get()) {
        std::ranges::copy(hash, child_hash->begin());
    } else {
        uint64_t self_id = u64_from_array(id_);
        Hash hash_copy(hash);
        children_[nib_num] = std::make_unique<Hash>(hash_copy);
        count_++;
    }
}

void Branch::delete_child(byte &nib) {
    auto nib_num = static_cast<int>(nib);
    if (Hash* child_hash = children_[nib_num].get()) {
        children_[nib_num] = std::unique_ptr<Hash>();
        count_--;
    }
}

tuple<byte, Hash, NodeId> Branch::get_last_remaining_child() {
    for (size_t i = 0; i < ORDER; i++) {
        if (Hash* child_hash = children_[i].get()) {
            uint64_t id = u64_from_array(id_) * ORDER + i;
            return std::make_tuple(
                static_cast<byte>(i), 
                *child_hash, 
                u64_to_array(id)
            );
        }
    }
    return std::make_tuple(byte(0), Hash{}, NodeId{});
}

NodeId* Branch::get_next_id(ByteSlice &nibs) {
    int num = static_cast<int>(nibs[0]);
    if (Hash* child_hash = children_[num].get()) {
        uint64_t id = u64_from_array(id_) * ORDER + num;
        std::ranges::copy(tmp_id_, u64_to_array(id).begin());
        return &tmp_id_;
    } else {
        return nullptr;
    }
}

void Branch::change_id(NodeId &id, Ledger &ledger) {
    uint64_t num = u64_from_array(id);
    uint64_array raw_id(id_);

    for (uint64_t i = 0; i < count_; i++) {
        if (Hash* child_hash = children_[i].get()) {
            uint64_t child_id = u64_from_array(id_) * ORDER + i;

            uint64_t should_be = num * ORDER + static_cast<uint64_t>(i);
            if (should_be != child_id) {

                // load/delete child_node from cache/db
                uint64_array raw_child_id = u64_to_array(child_id);
                optional<Node> n = ledger.delete_node(raw_child_id);
                if (n.has_value()) {
                    Node node = n.value();

                    // change its children recursively
                    uint64_array new_child_id = u64_to_array(should_be);
                    n->change_id(new_child_id, ledger);

                    // re-cache updated child once its children have been updated
                    ledger.cache_node(should_be, node);
                }
            }
        }
    }
}

Hash Branch::derive_hash() {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    Hash zero_hash{};
    for (int i = 0; i < ORDER; i++) {
        if (Hash* child_hash = children_[i].get()) {
            blake3_hasher_update(
                &hasher, 
                child_hash->data(), 
                child_hash->size()
            );
        } else {
            blake3_hasher_update(
                &hasher, 
                zero_hash.data(), 
                zero_hash.size()
            );
        }
    }

    Hash hash;
    blake3_hasher_finalize(
        &hasher, 
        reinterpret_cast<uint8_t*>(hash.data()), 
        hash.size());

    std::ranges::copy(hash, hash_.begin());
    return hash;
}

std::vector<byte> Branch::to_bytes() {
    std::vector<byte> buffer;
    buffer.reserve(BRANCH_SIZE);

    buffer[0] = BRANCH;
    std::ranges::copy(hash_, buffer.begin() + 1);

    Hash zero_hash{};

    auto start = buffer.begin() + 1 + 32;

    for (int i = 0; i < ORDER; i++) {
        if (Hash* child_hash = children_[i].get()) {
            std::ranges::copy(*child_hash, start);

        } else {
            std::ranges::copy(zero_hash, start);
        }
        start += 32;
    }
    return buffer;
}

optional<Hash> Branch::search(Ledger &ledger, ByteSlice &nibbles) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            auto remaining_nibbles = nibbles.subspan(1);
            return n->search(ledger, remaining_nibbles);
        }
    }
    return std::nullopt;
}

optional<Hash> Branch::virtual_put(
    Ledger &ledger, 
    ByteSlice &nibbles,
    ByteSlice &key, 
    Hash &val_hash
) {
    Hash new_hash{};
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            ByteSlice remaining_nibbles = nibbles.subspan(1);
            optional<Hash> res = n->put(ledger, remaining_nibbles, key, val_hash);
            if (res.has_value()) {
                new_hash = res.value();
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    } else {
        new_hash = derive_leaf_hash(key, val_hash);
    }
    int nib = static_cast<int>(nibbles[0]);

    // Save original state
    auto original_child = std::move(children_[nib]);
    Hash original_hash = hash_;

    // Temporarily replace the child to compute virtual hash
    children_[nib] = std::make_unique<Hash>(new_hash);
    Hash virtual_hash = derive_hash();

    // Restore original state
    children_[nib] = std::move(original_child);
    hash_ = original_hash;

    return virtual_hash;
}

optional<Hash> Branch::put(
    Ledger &ledger, 
    ByteSlice &nibbles, 
    ByteSlice &key, 
    Hash &val_hash
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            ByteSlice remaining_nibbles = nibbles.subspan(1);

            optional<Hash> res = n->put(ledger, remaining_nibbles, key, val_hash);
            if (res.has_value()) insert_child(nibbles[0], res.value());
        }
    } else {
        // leaf is child
        uint64_t nib_num = static_cast<uint64_t>(nibbles[0]);
        uint64_t child_id = (u64_from_array(id_) * ORDER) + nib_num;

        // create and fill leaf
        Leaf* leaf = ledger.new_cached_leaf(child_id);
        leaf->set_path(nibbles.subspan(1));
        leaf->set_value_hash(val_hash);

        // derive hash and insert into branch
        Hash new_hash = leaf->derive_hash(key);
        insert_child(nibbles[0], new_hash);
    }
    return derive_hash();
}

optional<std::tuple<Hash, ByteSlice>> Branch::remove(
    Ledger &ledger, 
    ByteSlice &nibbles
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            ByteSlice remaining_nibbles = nibbles.subspan(1);
            auto res = n->remove(ledger, remaining_nibbles);
            if (res.has_value()) {
                auto [hash, value] = res.value();

                if (is_zero(hash)) {
                    delete_child(nibbles[0]);
                } else {
                    insert_child(nibbles[0], hash);
                }

                Hash hash_to_parent{};

                if (count_ == 1) {
                    uint64_array parent_id = u64_to_array(u64_from_array(id_) / ORDER);
                    Node* parent_node = ledger.load_node(parent_id);
                    if (parent_node == nullptr) {
                        return std::make_tuple(derive_hash(), ByteSlice());
                    }

                    // get last remaining child nibble, hash, and id
                    auto [nib, child_hash, child_id] = get_last_remaining_child();
                    // load last child as node
                    Node* child_node = ledger.load_node(child_id);

                    // initialize path to pass to parent on return
                    std::vector<byte> path_to_parent;


                    if (parent_node->get_type() == EXT) {
                        // delete self
                        optional<Node> self = ledger.delete_node(id_);

                        // extend parent path by last child nibble
                        path_to_parent.push_back(nib);


                        if (child_node->get_type() == EXT) {
                            // delete child to obtain ownership
                            Node child = ledger.delete_node(child_id).value();
                            // cast to extension type
                            Extension* child_node = (Extension*)&child;

                            // move child path to path to parent
                            for (auto b: *child_node->get_path()) {
                                path_to_parent.push_back(b);
                            }

                            // get grand child info
                            Hash* g_child_hash = child_node->get_child_hash();
                            NodeId* g_child_id = child_node->get_child_id();

                            // get ownership of grand child via deleting from cache
                            Node g_child = ledger.delete_node(*g_child_id).value();

                            // reassign new id information to grandchild
                            g_child.change_id(id_, ledger);
                            hash_to_parent = g_child.derive_hash();

                            // recache grandchild
                            ledger.cache_node(u64_from_array(id_), g_child);

                        } else {
                            // delete to obtain ownership and expire cache entry
                            Node child_node = ledger.delete_node(child_id).value();

                            // update fields
                            child_node.change_id(id_, ledger);
                            hash_to_parent = child_node.derive_hash();

                            // recache child
                            ledger.cache_node(u64_from_array(id_), child_node);
                        }

                        // pass hash and path to parent
                        return std::tuple(hash_to_parent, ByteSlice(path_to_parent));

                    } else if (child_node->get_type() != BRANCH) {
                        // delete self
                        auto self = ledger.delete_node(id_);

                        // delete to obtain ownership and expire cache entry
                        Node child_node = ledger.delete_node(child_id).value();
                        child_node.change_id(id_, ledger);

                        if (Extension* child_ext = (Extension*)&child_node) {
                            (*child_ext->get_path()).push_front(nib);
                        } else if (Leaf* child_leaf = (Leaf*)&child_node) {
                            (*child_leaf->get_path()).push_front(nib);
                        }

                        hash_to_parent = child_node.derive_hash();

                        ledger.cache_node(u64_from_array(id_), child_node);

                        return std::tuple(hash_to_parent, ByteSlice{});
                    }
                }

                if (count_ > 0) {
                    hash_to_parent = derive_hash();
                } else {
                    ledger.delete_node(id_);
                }

                return std::tuple(hash_to_parent, ByteSlice{});

            } else {
                printf("ERROR::BRANCH::REMOVE::RECV_NONE");
            }
        } else {
            printf("ERROR::BRANCH::REMOVE::FAIL_LOAD");
        }
    } else {
            printf("ERROR::BRANCH::REMOVE::NO_ID");
    }
    return std::nullopt;
}
