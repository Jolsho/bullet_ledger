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

use curve25519_dalek::{ristretto::CompressedRistretto, RistrettoPoint, Scalar};

use crate::blockchain::Hash;
use crate::blockchain::ledger::Ledger;
use crate::blockchain::{priority::DeriveStuff, schnorr::TrxGenerators}; 
use crate::utils::random::random_b32; 
use {ephemeral::EphemeralTrx, hidden::HiddenTrx, regular::RegularTrx};


pub mod ephemeral;
pub mod regular;
pub mod hidden;

pub const EPHEMERAL: u8 = 99;
pub const HIDDEN: u8 = 101;
pub const REGULAR: u8 = 103;

pub enum Trx {
    Ephemeral(Box<EphemeralTrx>),
    Hidden(Box<HiddenTrx>),
    Regular(Box<RegularTrx>),
}

impl DeriveStuff<Box<Hash>> for Trx {
    fn new(id: u8) -> Self {
        match id {
            EPHEMERAL => Trx::Ephemeral(Box::new(EphemeralTrx::default())),
            HIDDEN => Trx::Hidden(Box::new(HiddenTrx::default())),
            _ => Trx::Regular(Box::new(RegularTrx::default())),
        }
    }

    fn fill_key(&self, key: &mut Box<Hash>) {
        match self {
            Trx::Ephemeral(trx) => key.copy_from_slice(&trx.hash),
            Trx::Hidden(trx) => key.copy_from_slice(&trx.hash),
            Trx::Regular(trx) => key.copy_from_slice(&trx.hash),
        }
    }

    fn get_comperator(&self) -> u64 {
        match &self {
            Trx::Ephemeral(trx) => trx.fee_value,
            Trx::Hidden(trx) => trx.fee_value,
            Trx::Regular(trx) => trx.fee_value,
        }
    }

    fn get_value_id(&self) -> &u8 {
        match &self {
            Trx::Ephemeral(_) => &EPHEMERAL,
            Trx::Hidden(_) => &HIDDEN,
            Trx::Regular(_) => &REGULAR,
        }
    }
}

impl Trx {
    pub fn execute(&self, 
        ledger: &mut Ledger, 
        gens: &mut TrxGenerators, 
        fee_commit: &mut RistrettoPoint
    ) {
        match self {
            Trx::Ephemeral(trx) => {
                let tmp_fee_commit = Scalar::from(trx.fee_value) * gens.pedersen.B;
                let delta = trx.delta_commit.decompress().unwrap();

                let sender_final = (trx.sender_init.decompress().unwrap() - delta - tmp_fee_commit).compress();
                let receiver_final = (trx.receiver_init.decompress().unwrap() + delta).compress();

                let _ = ledger.db_put(
                    trx.sender_init.as_bytes(), 
                    sender_final.as_bytes()
                );
                let _ = ledger.db_put(
                    trx.receiver_init.as_bytes(), 
                    receiver_final.as_bytes()
                );

                *fee_commit += tmp_fee_commit;
            }
            _ => {}
        }

    }
}


pub const VALUE_SIZE: usize = 64;
pub const PROOF_LENGTH: usize = 672; // (2(log2(64)) + 9) * 32
pub const TRX_LENGTH: usize = PROOF_LENGTH + (3 * 32) + 8;

pub const SENDER: bool = true;
pub const RECEIVER: bool = false;


#[derive(Clone)]
pub struct TrxSecrets {
    pub commit: CompressedRistretto,
    pub val: u64,
    pub x: Scalar,
    pub r: Scalar,
}

/// a random blinding factor is used in a pedersen commitment to val
/// it is returned as r in TrxSecrets
pub fn hidden_value_commit(p: &TrxGenerators, val: u64) -> TrxSecrets {
    let r = Scalar::from_bytes_mod_order(random_b32());
    let x = Scalar::from(val);
    let commit = p.pedersen.commit(x, r).compress();
    TrxSecrets{ x, val, r, commit }
}

/// and zero scalar is the blinding factor in a pedersen commitment to val
/// The purpose of this is so you can choose to transact openly.
/// If for some reason that is needed.
#[allow(unused)]
pub fn visible_value_commit(p: &TrxGenerators, val: u64) -> TrxSecrets {
    let r = Scalar::ZERO;
    let x = Scalar::from(val);
    let commit = p.pedersen.commit(x, r).compress();
    TrxSecrets{ x, val, r, commit }
}
