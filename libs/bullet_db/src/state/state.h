#pragma once
#include <cstdint>
#include <cstdio>
#include <lmdb.h>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <array>

class BulletDB {
public:
    MDB_env* env_;
    MDB_txn* txn_;
    MDB_dbi dbi_;
    std::vector<uint64_t> keys_;

    void* root_;                                // 256^0 == 1           | NODE
    std::array<void*, 256> l1_;                 // 256^1 == 256         | NODES
    std::unordered_map<uint64_t, void*> l2_;    // 256^2 == 65,536      | NODES
    std::unordered_map<uint64_t, void*> l3_;    // 256^3 == 16,777,216  | LEAVES

    /*
     *  0 one proof for a single node 256 degree
     *  1 one proof aggregated 256 other proofs
     *  
     *  senders can compute their own proofs for l2, and account info for l3
     *  then the block provider can just recompute collisions in l2.
     *      and can batch them into a single one.
     *  and that means l2 nodes are the only commitments needed since l3 are in trx
     *      that is 2k * 2 * 48bytes = 200kb
     *        
     *  but if everyone has atleast l2 saved...
     *      then you dont need to transmit those...
     *      verifer/executor can just retrieve using keys...
     *      but then that means no such thing as statelessness...
     *
     *  at this scale anyone can store full state.
     *      thats not that big of an issue...
     *      So maybe we just do it that way than...
     *      no need to do statelessness...
     *      maybe statlessness will be added later...
     *      a different client that has its own codec, with a small union.
     *          like a light client literally...
     *          just consensus and then own validation module.
     *
     *  Again the answer to these issues is scale...
     *  All of these stateless features and what not is cool
     *  but at the end of the day these systems arent made for that scale.
     *      its like having to share a single bus for a whole school.
     *      That is only doable if the school is small enough.
     *      There is no such things as like a world currency...
     *          The world is just too big...
     *              Instead you need many chains...
     *              And all of them operate indepedently...
     *  Then the question is what about something like a ethereum base layer?
     *  And that seems reasonable right...
     *  But you cant block people from making those fees incredibly large...
     *  with l2s as soon as you try to prevent censorship on it you are just creating an l1
     *
     *  What I am doing is most definetely the more reasonable approach.
     *  To just drop scale and increase count..
     *  The revolution here is everyone having the capacity to create currencies out of thin air
     *      that are more accessible, effecient, and trustworthy than any other currency in the world.
     *
     *  So the next company is bringing that power to the individual.
     *  however that can be done.
     *
     *  We have all inherited the capacity to father nations.
     *      That is a right given by God.
     *      The ability to worship God freely.
     *
     *
    */

    BulletDB(const char* path, size_t map_size);
    ~BulletDB();
    int put(const void* key_data, size_t key_size, 
            const void* value_data, size_t value_size);
    int get(const void* key_data, size_t key_size, 
            void** value_data, size_t* value_size);
    void* mut_get(const void* key, size_t key_size, 
                  size_t value_size);
    int del(const void* key_data, size_t key_size);
    int exists(const void* key_data, size_t key_size);
    std::vector<uint64_t> flatten_sort_l2();

};
