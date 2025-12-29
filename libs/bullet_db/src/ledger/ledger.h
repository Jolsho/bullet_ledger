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

/*  =======================================================
 *  |             META STRUCTURE OF STATE TRIE            |
 *  |=====================================================|
 *  |    root_  = 256^0 == 1                BRANCH        |
 *  |    l1_    = 256^1 == 256              BRANCHES      |
 *  |    l2_    = 256^2 == 65,536           BRANCHES      |
 *  |    l3_    = 256^3 == 16,777,216       LEAVES/ACCTS  |
 *  |    l4_    = 256^4 == 4,294,967,296    VALUES        |
 *  =======================================================
 *
 *  root + l1 = (1 + 256) * (256 * 32 + 48 + 2) = 2.1GB
 *      - full child hash array
 *      - commit + meta
 *
 *  l2 = 65,536 * (48 + 2) = 3.3GB
 *      - commit + meta
 *
 *  1 shard @ 1k accounts & 128 values per account
 *      1,000 * 128 * 32 = 4.1GB
 *
 *  TOTAL min = 9.5GB
*/

/*  
 *  
 *  TODO -- 
 *      
 *  LMD GHOST -- 
 *      Latest Message Drive - Greediest Heaviest Observed SubTree
 *          Choosing the heaviest Fork
 *
 *  Casper FFG --
 *      Casper Friendly Finality Gadget
 *          Finalizing a checkpoint
 *
 *          
 *  STRUCTURES::
 *      pending_blocks_per_epoch = 
 *              - buckets per epoch
 *              - each bucket is a set of block_ids
 *                  - with vote counts and what not
 *
 */

#pragma once
#include "gadgets.h"
#include "node.h"


class Ledger {
private:
    Gadgets_ptr gadgets_;
    std::vector<byte> shard_prefix_;

public:
    Ledger(
        std::string path,
        size_t cache_size,
        size_t map_size,
        std::string tag,
        blst_scalar secret_sk
    );

    const Gadgets_ptr get_gadgets() const;

    bool in_shard(const Hash hash);

    Result<Node_ptr, int> get_root(
        uint16_t block_id,
        uint16_t prev_block_id = 0
    );

    int put(
        ByteSlice& key,
        ByteSlice& value,
        uint8_t idx,
        uint16_t block_id,
        uint16_t prev_block_id = 0
    );
    int create_account(
        ByteSlice& key,
        uint16_t block_id
    );

    int delete_account(
        ByteSlice &key, 
        uint16_t block_id
    );

    ~Ledger();
};

