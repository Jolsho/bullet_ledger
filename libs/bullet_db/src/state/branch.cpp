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

#include "verkle.h"
#include "polynomial.h"
#include "helpers.h"
#include <memory>


class Branch : public Branch_i {
private:
    NodeId id_;
    Commitment commit_;
    std::vector<std::unique_ptr<Commitment>> children_;
    uint8_t count_;
    NodeId tmp_id_;
public:

    const NodeId* get_id() const override { return &id_; };
    const Commitment* get_commitment() const override { return &commit_; };
    const byte get_type() const override { return BRANCH; };

    const std::optional<Commitment*> get_child(const byte &nib) const {
        if (!children_[nib].get()) return std::nullopt;
        return children_[nib].get();
    }
    void insert_child(const byte &nib, const Commitment* new_commit) override {
        if (!children_[nib].get()) count_++;
        children_[nib] = std::make_unique<Commitment>(*new_commit);
    }
    void delete_child(const byte &nib) {
        auto child = std::move(children_[nib]);
        if (child.get()) {
            children_[nib] = nullptr;
            count_--;
        }
    }

    NodeId* get_next_id(ByteSlice &nibs) override {
        if (get_child(nibs.front())) {
            uint64_t id = u64_from_id(id_) * ORDER + nibs.front();
            tmp_id_ = u64_to_id(id);
            return &tmp_id_;
        } else {
            return nullptr;
        }
    }

    Branch(
        std::optional<NodeId> id, 
        std::optional<ByteSlice*> buff
    ) :
        children_(ORDER), 
        commit_{blst_p1()}, 
        count_{0}
    {
        if (id.has_value()) id_ = id.value();

        for (auto &c: children_) c = nullptr;
        if (!buff.has_value()) { return; }

        byte* cursor = buff.value()->data(); cursor++;

        commit_ = p1_from_bytes(cursor); cursor += 48;

        count_ = *cursor; cursor++;
        for (auto i = 0; i < count_; i++) {
            byte nib(*cursor); cursor++;
            children_[nib] = std::make_unique<blst_p1>(p1_from_bytes(cursor));
            cursor += 48;
        }
    }

    std::vector<byte> to_bytes() const override {
        std::vector<byte> buffer(1 + 48 + 1 + (count_ * 49));
        byte* cursor = buffer.data();

        *cursor = BRANCH; cursor++;

        blst_p1_compress(cursor, &commit_); cursor += 48;

        *cursor = count_; cursor++;
        for (auto i = 0; i < ORDER; i++) {
            if (Commitment* c = children_[i].get()) {
                *cursor = static_cast<uint8_t>(i); cursor++;
                blst_p1_compress(cursor, c); cursor += 48;
            }
        }
        return buffer;
    }

    void change_id(const NodeId &new_id, Ledger &ledger) override {
        uint64_t num = u64_from_id(new_id);

        for (uint64_t i = 0; i < ORDER; i++) {
            if (!children_[i]) continue;  // no child here, skip

            // Compute old and new IDs
            uint64_t old_child_id = u64_from_id(id_) * ORDER + i;
            uint64_t new_child_id = num * ORDER + i;

            if (old_child_id == new_child_id) continue;

            // Prevent accidental deletion of the root
            if (old_child_id == 1) continue;

            // Remove the child node from cache/db safely
            Node* node = ledger.delete_node(u64_to_id(old_child_id));

            // Recursively update children safely
            node->change_id(u64_to_id(new_child_id), ledger);

            // Re-cache the node
            ledger.cache_node(node);
        }

        // Update this branch's own ID after all children are safe
        id_ = new_id;
    }

    const Commitment* derive_commitment(Ledger &ledger) override {
        Polynomial* Fx = ledger.get_poly();
        std::array<byte, 48> buffer;
        auto tag = ledger.get_tag();
        for (auto i = 0; i < ORDER; i++) {
            if (Commitment* c = children_[i].get()) {
                blst_p1_compress(buffer.data(), c);
                BlakeHasher h;
                h.update(buffer.data(), buffer.size());
                h.update(reinterpret_cast<const byte*>(tag->data()), tag->size());
                blst_scalar_from_le_bytes(
                    &Fx->at(i), 
                    h.finalize().data(),
                    32);
            }
        }
        commit_g1(&commit_, *Fx, *ledger.get_srs());
        return &commit_;
    }

    std::optional<Hash> search(Ledger &ledger, ByteSlice nibbles) override {
        if (NodeId* next_id = get_next_id(nibbles)) {
            if (Node* n = ledger.load_node(*next_id)) {
                return n->search(ledger, nibbles.subspan(1));
            }
        }
        return std::nullopt;
    }

    int build_commitment_path(
        Ledger &ledger, 
        const Hash &key,
        ByteSlice nibbles,
        std::vector<Scalar_vec> &Fxs, 
        Bitmap<32> &Zs
    ) override {

        if (NodeId* next_id = get_next_id(nibbles)) {
            if (Node* n = ledger.load_node(*next_id)) {
                if (!n->build_commitment_path(
                    ledger, key, nibbles.subspan(1), Fxs, Zs
                )) return false;
            } else return false;
        } else return false;

        // build Fx
        Scalar_vec fx(ORDER);
        std::vector<byte> buffer; buffer.reserve(32);
        auto tag = ledger.get_tag();
        for (auto i = 0; i < ORDER; i++)
            if (Commitment* c = children_[i].get()) {
                blst_hash_to_g1(c, buffer.data(), buffer.size(), 
                            reinterpret_cast<const byte*>(tag->data()), 
                            tag->size()
                );
                blst_scalar_from_le_bytes(&fx[i], buffer.data(), buffer.size());
            }

        Fxs.push_back(fx);
        // index of nibble within key
        Zs.set(key.size() - nibbles.size());
        return true;
    }

    std::optional<Commitment> virtual_put(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &key, 
        const Hash &val_hash
    ) override {

        blst_scalar tmp_sk;
        blst_p1 old_c = ledger.get_srs()->g1_powers_jacob[nibbles.front()];
        blst_p1 new_c = old_c;
        std::vector<byte> buffer; buffer.reserve(32);

        if (NodeId* next_id = get_next_id(nibbles)) {
            if (Node* n = ledger.load_node(*next_id)) {
                auto res = n->virtual_put(ledger, nibbles.subspan(1), key, val_hash);
                if (!res.has_value()) return std::nullopt;
                    
                auto tag = ledger.get_tag();

                // hash new child commitment and commit
                hash_p1_to_sk(tmp_sk, res.value(), buffer, tag);
                blst_p1_mult(&new_c, &new_c, tmp_sk.b, 256);


                // hash old child commitment and commit
                hash_p1_to_sk(tmp_sk, *n->get_commitment(), buffer, tag);
                blst_p1_mult(&old_c, &old_c, tmp_sk.b, 256);

            } else return std::nullopt;
        } else {

            // commit to hash via last bit of key to emulate leaf commitment
            Scalar_vec Fx(ORDER);
            blst_scalar_from_le_bytes(&Fx[nibbles.back()], val_hash.data(), val_hash.size());
            Commitment new_commit;
            commit_g1(&new_commit, Fx, *ledger.get_srs());

            hash_p1_to_sk(tmp_sk, new_commit, buffer, ledger.get_tag());
            blst_p1_mult(&new_c, &new_c, tmp_sk.b, 256);
        }

        // new_c = c + (new_c - old_c)
        blst_p1_cneg(&old_c, true);
        blst_p1_add(&new_c, &new_c, &old_c);
        blst_p1_add(&new_c, &new_c, &commit_);
        return new_c;
    }

    std::optional<const Commitment*> put(
        Ledger &ledger, 
        ByteSlice nibbles, 
        const Hash &key, 
        const Hash &val_hash
    ) override {

        blst_scalar tmp_sk;
        blst_p1 old_c = ledger.get_srs()->g1_powers_jacob[nibbles.front()];
        blst_p1 new_c = old_c;
        std::vector<byte> buffer; buffer.reserve(32);

        if (NodeId* next_id = get_next_id(nibbles)) {
            if (Node* n = ledger.load_node(*next_id)) {

                auto res = n->put(ledger, nibbles.subspan(1), key, val_hash);
                if (res.has_value()) {

                    // hash new child commitment and commit
                    hash_p1_to_sk(tmp_sk, *res.value(), buffer, ledger.get_tag());
                    blst_p1_mult(&new_c, &new_c, tmp_sk.b, 256);

                    // hash old child commitment and commit
                    Commitment* old_child = children_[nibbles.front()].get();

                    hash_p1_to_sk(tmp_sk, *old_child, buffer, ledger.get_tag());
                    blst_p1_mult(&old_c, &old_c, tmp_sk.b, 256);

                    insert_child(nibbles.front(), res.value());

                } else return std::nullopt;
            } else return std::nullopt;
        } else {
            // leaf is child
            uint64_t child_id = (u64_from_id(id_) * ORDER) + nibbles.front();

            // create and fill leaf
            Leaf_i* leaf = ledger.new_cached_leaf(child_id);
            leaf->set_path(nibbles.subspan(1, nibbles.size() - 2));
            leaf->insert_child(nibbles.back(), val_hash);
            const Commitment* leaf_commit = leaf->derive_commitment(ledger);
            insert_child(nibbles.front(), leaf_commit);


            hash_p1_to_sk(tmp_sk, *leaf_commit, buffer, ledger.get_tag());
            blst_p1_mult(&new_c, &new_c, tmp_sk.b, 256);
        }

        // new_c = c + (new_c - old_c)
        blst_p1_cneg(&old_c, true);
        blst_p1_add(&new_c, &new_c, &old_c);
        blst_p1_add(&new_c, &new_c, &commit_);

        // c = new_c
        commit_ = new_c;
        return &commit_;
    }

    const std::tuple<byte, Commitment, NodeId> get_last_remaining_child() const {
        for (size_t i = 0; i < ORDER; i++) {
            auto child = get_child(static_cast<byte>(i));
            if (child.has_value()) {
                uint64_t id = u64_from_id(id_) * ORDER + i;
                return std::make_tuple(
                    static_cast<byte>(i), 
                    *child.value(),
                    u64_to_id(id)
                );
            }
        }
        return std::make_tuple(byte(0), Commitment{}, NodeId{});
    }

    std::optional<std::tuple<Commitment, bool>> remove(
        Ledger &ledger, 
        ByteSlice nibbles,
        const Hash &kv
    ) override {
        if (NodeId* next_id = get_next_id(nibbles)) {
            if (Node* n = ledger.load_node(*next_id)) {
                auto res = n->remove(ledger, nibbles.subspan(1), kv);
                if (res.has_value()) {
                    auto [child_commit, removed] = res.value();

                    if (removed) delete_child(nibbles.front());
                    else insert_child(nibbles.front(), &child_commit);
                    
                    bool destroyed = count_ == 0;
                    Commitment new_commit(*derive_commitment(ledger));
                    if (destroyed) delete ledger.delete_node(id_);

                    return std::make_tuple(new_commit, destroyed);

                }
            }
        }
        return std::nullopt;
    }
};

Branch_i* create_branch(std::optional<NodeId> id, std::optional<ByteSlice*> buff) {
    return new Branch(id, buff);
}
