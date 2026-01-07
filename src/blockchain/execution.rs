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

use curve25519_dalek::{ristretto::CompressedRistretto, RistrettoPoint};

use crate::{blockchain::{blockchain::Blockchain, schnorr::SchnorrProof}, utils::msg::NetMsg};

pub fn execute_hidden_block(blockchain: &mut Blockchain, m: &mut NetMsg) {
    let mut cursor = 32;
    let mut validator_commit = [0u8; 32];
    validator_commit.copy_from_slice(&m.body[..cursor]);
    let validator = CompressedRistretto::from_slice(&validator_commit).unwrap();

    if blockchain.consensus.is_validator(&validator) {

        let mut v_proof = SchnorrProof::default();
        v_proof.from_bytes(&mut m.body[cursor..cursor+96]);
        cursor += 96;

        let mut hasher = blake3::Hasher::new();
        hasher.update(&m.body[cursor..]);
        let context_hash: [u8;32] = hasher.finalize().into();

        if !v_proof.verify(&blockchain.gens, &validator, &context_hash) { return; }

        let mut fee_commit = RistrettoPoint::default();
        let mut k = blockchain.pool.get_key();

        let end = cursor + blockchain.config.block_size;
        while cursor < end {
            k.copy_from_slice(&m.body[cursor..cursor+32]);
            cursor += 32;

            if let Some((trx, _)) = blockchain.pool.get(&k) {
                trx.execute(
                    &mut blockchain.ledger, 
                    &mut blockchain.gens, 
                    &mut fee_commit,
                );
            }

            blockchain.pool.remove_one(&k);
        }

        // NOT CORRECT
        let _res = blockchain.ledger.db_put(
            validator.as_bytes(), 
            fee_commit.compress().as_bytes()
        );
    }
}

/*
*
*   STRUCTS:: 
*       CANONICAL
*           + INITS
*
*       BLOCKS
*           + [ HASH ] -> { INITS, FINALS, PARENT }
*
*
*   PROPOSED BLOCKS::
*       SIZES::: 2MB/Block  && 1000B/TRX = 2000 TRXs / BLOCK
*
*       Compressed TRX = 128 Bytes
*       Processed Block = 2000 * 128 = 256KB
*
*       SEARCH COMPLEXITY:: 32 Processed Blocks * log2( 2000 Compressed TRXs / Processed Block )
*
*       IF holding up to 3 different checkpoints: 
*           TOTAL MEMORY FOR BLOCKS ~= 25MB = 3 * 32 * 256KB 
*
*       Structs::
*           + ROOT
*           + PARENT
*           + Proposer_Info
*           + Slot#
*           + Trxs
*           - HASH = H(Slot# + Trxs)
*           
*       Procedure::
*           + check if already received ( BLOCKS[HASH] )
*           + validate proposer info
*
*           + validate trxs
*               - block = BLOCKS[HASH]
*
*               - for each trx to be valid
*                   = prev_blocks.INITS[trx_init_states] not exists 
*                   = (and prev_blocks.FINALS[trx_init_states] or CANONICAL[trx_init_states} exists)
*                   = Then validation process
*
*           + if all trxs are valid begin staging
*               - if there are multiple transitions for a single address only keep latest
*               - insert trx.init_states to block.INITS(ENSURE ORDERING)
*               - insert trx.final_states to block.FINALS(ENSURE ORDERING)
*               - accumulate fees
*
*           + compute proposer + fees state change and stage it
*
*           + add block hash to BLOCKMAP
*               - BRANCHES[ROOT].BLOCKS[Hash] = {}
*
*
*/
