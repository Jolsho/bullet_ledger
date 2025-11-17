#include "verkle.h"

Branch::Branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff) : 
    children_(ORDER), commit_{new_p1()}
{

    if (!id.has_value()) { return; }
    id_ = id.value();

    if (!buff.has_value()) { 
        for (auto &c: children_) c = nullptr;
        return; 
    }
    ByteSlice* buffer = buff.value();

    auto current = buffer->subspan(1,49);
    commit_from_bytes(current.data(), commit_);

    current = buffer->subspan(49);
    for (auto i = 0; i < ORDER; i++) {
        auto sub = current.subspan((48 * i), (48 * i) + 48);
        if (!iszero(sub)) {
            children_[i] = std::make_unique<Commitment>(new_p1());
            commit_from_bytes(sub.data(), *children_[i]);

        } else {
            children_[i] = nullptr;
        }
    }
}

void Branch::insert_child(const byte &nib, Commitment new_commit) {
    size_t index = static_cast<size_t>(nib);
    if (!children_[index]) count_++;
    children_[index] = std::make_unique<Commitment>(new_commit);
}

void Branch::delete_child(const byte &nib) {
    size_t index = static_cast<size_t>(nib);
    auto child = std::move(children_[index]);
    if (child) {
        children_[index] = nullptr;
        count_--;
        blst_p1_add(&commit_, &commit_, &*child);
    }
}

optional<Commitment*> Branch::get_child(byte &nib) {
    size_t index = static_cast<size_t>(nib);
    std::unique_ptr<Commitment> child = std::move(children_[index]);
    if (child) return child.get();
    return std::nullopt;
}


tuple<byte, Commitment, NodeId> Branch::get_last_remaining_child() {
    for (size_t i = 0; i < ORDER; i++) {
        byte nib = static_cast<byte>(i);
        optional<Commitment*> child = get_child(nib);
        if (child.has_value()) {
            uint64_t id = u64_from_array(id_) * ORDER + i;
            return std::make_tuple(
                static_cast<byte>(i), 
                *child.value(),
                u64_to_array(id)
            );
        }
    }
    return std::make_tuple(byte(0), Commitment{}, NodeId{});
}

NodeId* Branch::get_next_id(ByteSlice &nibs) {
    if (get_child(nibs[0])) {
        int num = static_cast<int>(nibs[0]);
        uint64_t id = u64_from_array(id_) * ORDER + num;
        tmp_id_ = u64_to_array(id);
        return &tmp_id_;
    } else {
        return nullptr;
    }
}

void Branch::change_id(const NodeId &new_id, Ledger &ledger) {
    uint64_t num = u64_from_array(new_id);

    for (uint64_t i = 0; i < ORDER; i++) {
        if (!children_[i]) continue;  // no child here, skip

        // Compute old and new IDs
        uint64_t old_child_id = u64_from_array(id_) * ORDER + i;
        uint64_t new_child_id = num * ORDER + i;

        if (old_child_id == new_child_id) continue;

        // Prevent accidental deletion of the root
        if (old_child_id == 1) continue;

        // Remove the child node from cache/db safely
        optional<Node_ptr> node = ledger.delete_node(u64_to_array(old_child_id));

        // Recursively update children safely
        node.value().get()->change_id(u64_to_array(new_child_id), ledger);

        // Re-cache the node
        ledger.cache_node(std::move(node.value()));
    }

    // Update this branch's own ID after all children are safe
    id_ = new_id;
}

Commitment* Branch::derive_commitment(Ledger &ledger) {
    scalar_vec fx; fx.reserve(count_);
    for (auto i = 0; i < ORDER+1; i++) {
        if (children_[i]) {
            auto c = children_[i].get();
            auto c_bytes = compress_p1(*c);
            blst_scalar s = new_scalar();
            blst_scalar_from_be_bytes(&s, c_bytes.data(), c_bytes.size());
            fx.push_back(s);
        }
    }
    commit_ = p1_from_affine(commit_g1(fx, *ledger.get_srs()));
    return &commit_;
}

std::vector<byte> Branch::to_bytes() {
    std::vector<byte> buffer(BRANCH_SIZE);
    buffer.reserve(BRANCH_SIZE);

    buffer.push_back(BRANCH);

    Commit_Serial zero_commit{};
    for (auto i = 0; i < ORDER+1; i++) {
        Commit_Serial commit_bytes;
        if (i == 0) {
            commit_bytes = compress_p1(commit_);
        } else {
            if (Commitment* c = children_[i-1].get()) {
                commit_bytes = compress_p1(*c);

            } else commit_bytes = zero_commit;
        }
        buffer.insert(buffer.end(), commit_bytes.begin(), commit_bytes.end());
    }

    return buffer;
}

optional<Commitment> Branch::search(Ledger &ledger, ByteSlice nibbles) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            return n->search(ledger, nibbles.subspan(1));
        }
    }
    return std::nullopt;
}

optional<Commitment> Branch::virtual_put(
    Ledger &ledger, 
    ByteSlice nibbles,
    const ByteSlice &key, 
    const Commitment &val_commitment
) {
    Commitment new_commit{};
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            auto res = n->virtual_put(ledger, nibbles.subspan(1), key, val_commitment);
            if (!res.has_value()) return std::nullopt;

            new_commit = res.value();

        } else return std::nullopt;
    } else {
        new_commit = val_commitment;
    }
    int nib = static_cast<int>(nibbles[0]);

    Commitment original_commit(commit_);
    std::unique_ptr<Commitment> original_child(std::move(children_[nib]));

    // derive virtual commit
    insert_child(nibbles[0], new_commit);
    Commitment virtual_commit(*derive_commitment(ledger));

    // return original state
    commit_ = original_commit;
    children_[nib].swap(original_child);
    derive_commitment(ledger);

    return virtual_commit;
}

optional<Commitment*> Branch::put(
    Ledger &ledger, 
    ByteSlice nibbles, 
    const ByteSlice &key, 
    const Commitment &val_commitment
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {

            auto res = n->put(ledger, nibbles.subspan(1), key, val_commitment);
            if (res.has_value()) insert_child(nibbles[0], *res.value());

        } else return std::nullopt;

    } else {
        // leaf is child
        uint64_t nib_num = static_cast<uint64_t>(nibbles[0]);
        uint64_t child_id = (u64_from_array(id_) * ORDER) + nib_num;

        // create and fill leaf
        Leaf* leaf = ledger.new_cached_leaf(child_id);
        leaf->set_path(nibbles.subspan(1));
        leaf->set_commitment(val_commitment);
        insert_child(nibbles[0], val_commitment);
    }
    return derive_commitment(ledger);
}

optional<std::tuple<Commitment, bool, ByteSlice>> Branch::remove(
    Ledger &ledger, 
    ByteSlice nibbles
) {
    if (NodeId* next_id = get_next_id(nibbles)) {
        if (Node* n = ledger.load_node(*next_id)) {
            auto res = n->remove(ledger, nibbles.subspan(1));
            if (res.has_value()) {
                auto [child_commit, removed, new_path] = res.value();

                if (removed) {
                    delete_child(nibbles[0]);
                } else {
                    insert_child(nibbles[0], child_commit);
                }

                Commitment commit_to_parent{};

                if (count_ == 1) {
                    uint64_array parent_id = u64_to_array(u64_from_array(id_) / ORDER);
                    Node* parent_node = ledger.load_node(parent_id);
                    if (parent_node == nullptr) {
                        return std::make_tuple(Commitment(commit_), false, ByteSlice{});
                    }

                    // get last remaining child nibble, hash, and id
                    auto [nib, last_child_commit, child_id] = get_last_remaining_child();
                    // load last child as node
                    Node* child_node = ledger.load_node(child_id);


                    // initialize path to pass to parent on return
                    std::vector<byte> path_to_parent;


                    if (parent_node->get_type() == EXT) {
                        // delete self
                        optional<Node_ptr> self = ledger.delete_node(id_);

                        // extend parent path by last child nibble
                        path_to_parent.push_back(nib);


                        // remove child by moving path to parent 
                        // and attach parent to g-child
                        if (child_node->get_type() == EXT) {

                            // delete child to obtain ownership
                            Node_ptr child = ledger.delete_node(child_id).value();

                            // cast to extension type
                            Extension* child_node = static_cast<Extension*>(child.get());

                            // move child path to path to parent
                            auto path = *child_node->get_path();
                            while (path.size() > 0) {
                                path_to_parent.push_back(path.pop_front().value());
                            }

                            // get grand child info
                            Commitment* g_child_c = child_node->get_commitment();
                            NodeId* g_child_id = child_node->get_child_id();

                            // get ownership of grand child via deleting from cache
                            Node_ptr g_child = ledger.delete_node(*g_child_id).value();

                            // reassign new id information to grandchild
                            g_child->change_id(id_, ledger);
                            commit_to_parent = *g_child_c;

                            // recache grandchild
                            ledger.cache_node(std::move(g_child));

                        } else {
                            // delete to obtain ownership and expire cache entry
                            Node_ptr child_node = ledger.delete_node(child_id).value();

                            // update fields
                            child_node->change_id(id_, ledger);
                            commit_to_parent = *child_node->get_commitment();

                            // recache child
                            ledger.cache_node(std::move(child_node));
                        }

                        // pass hash and path to parent
                        return std::make_tuple(commit_to_parent, false, ByteSlice(path_to_parent));

                    } else if (child_node->get_type() != BRANCH) {
                        // delete self
                        auto self = ledger.delete_node(id_);

                        // delete to obtain ownership and expire cache entry
                        // of last remaining child
                        Node_ptr child_node = ledger.delete_node(child_id).value();
                        child_node->change_id(id_, ledger);


                        if (child_node->get_type() == EXT) {
                            if (Extension* child_ext = static_cast<Extension*>(child_node.get()))
                                (*child_ext->get_path()).push_front(nib);
                        } else {
                            if (Leaf* child_leaf = static_cast<Leaf*>(child_node.get()))
                                (*child_leaf->get_path()).push_front(nib);
                        }

                        commit_to_parent = *child_node->get_commitment();

                        ledger.cache_node(std::move(child_node));

                        return std::make_tuple(commit_to_parent, false, ByteSlice{});
                    }
                }

                bool deleted = false;
                if (count_ > 0) {
                    commit_to_parent = *derive_commitment(ledger);
                } else {
                    deleted = true;
                    ledger.delete_node(id_);
                }

                return std::make_tuple(commit_to_parent, deleted, ByteSlice{});

            } else {
                //printf("ERROR::BRANCH::REMOVE::RECV_NONE\n");
            }
        } else {
            //printf("ERROR::BRANCH::REMOVE::FAIL_LOAD  ID::%zu\n", u64_from_array(*next_id));
        }
    } else {
            //printf("ERROR::BRANCH::REMOVE::NO_ID\n");
    }
    return std::nullopt;
}
