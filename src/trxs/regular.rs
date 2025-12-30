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

use std::{error, u64};

use ed25519_dalek::{ed25519::signature::SignerMut, Signature, SigningKey, VerifyingKey};

/*
*   TRX FORMAT         |size|
*   |-----------------------|      
*   | sender_addr        32 |====I---I 
*   | delta value        8  |    I   I  Sig Hash Context
*   | fee value          8  |    I   I  && TRX_LENGTH
*   | receiver_addr      32 |----I---I       
*   ------------------------|    I
*   | sender_sig         64 |    I
*   | receiver_sig       64 |====I TOTAL_TRX_PROOF
*   -------------------------
*
*/

pub const TRX_LENGTH: usize = 32 + 32 + 8 + 8;
pub const TOTAL_TRX_LENGTH: usize = TRX_LENGTH + (64 * 2);

#[derive(Clone)]
pub struct RegularTrx {
    pub tag: &'static [u8],
    pub hash: [u8; 32],

    pub sender_addr: VerifyingKey,
    pub delta_value: u64,
    pub fee_value: u64,
    pub receiver_addr: VerifyingKey,

    pub sender_sig: Signature,
    pub receiver_sig: Signature,
}

impl Default for RegularTrx {
    fn default() -> Self {
        Self::new(b"bullet_ledger")
    }
}

impl RegularTrx {
    pub fn new(tag: &'static [u8]) -> Self {
        Self { 
            tag,
            hash: [0u8; 32],
            fee_value: 0u64,
            delta_value: 0u64,

            sender_addr: VerifyingKey::default(),
            sender_sig: Signature::from_bytes(&[0u8;64]),

            receiver_addr: VerifyingKey::default(),
            receiver_sig: Signature::from_bytes(&[0u8;64]),
        }
    }

    pub fn set_fee_and_delta(&mut self, fee: u64, delta: u64) {
        self.fee_value = fee;
        self.delta_value = delta;
    }

    pub fn set_addr(&mut self, sender: VerifyingKey, receiver: VerifyingKey) {
        self.sender_addr = sender;
        self.receiver_addr = receiver;
    }

    /// Marshals data into internal buffer and sign hash of trx.
    /// Sig is stored in struct and marshalled to internal.
    /// In prod you won't call sender_sign() & receiver_sign() sequentially
    pub fn sender_sign(&mut self, signing_key: &mut SigningKey, buffer: &mut Vec<u8>) {
        self.marshal(buffer);
        self.compute_hash();
        self.sender_sig = signing_key.sign(&self.hash);
        buffer[TRX_LENGTH..TRX_LENGTH+64].copy_from_slice(&self.sender_sig.to_bytes());
    }

    /// Marshals data into internal buffer and sign hash of trx.
    /// Sig is stored in struct and marshalled to internal.
    /// In prod you won't call sender_sign() & receiver_sign() sequentially
    pub fn receiver_sign(&mut self, signing_key: &mut SigningKey, buffer: &mut Vec<u8>) {
        self.marshal(buffer);
        self.compute_hash();
        self.receiver_sig = signing_key.sign(&self.hash);
        buffer[TRX_LENGTH+64..].copy_from_slice(&self.receiver_sig.to_bytes());
    }

    pub fn verify_sigs(&mut self) -> bool {
        self.compute_hash();
        self.sender_addr.verify_strict(&self.hash, &self.sender_sig).is_ok() &&
        self.receiver_addr.verify_strict(&self.hash, &self.receiver_sig).is_ok()
    }

    pub fn compute_hash(&mut self) {
        let mut hasher = blake3::Hasher::new();
        hasher.update(self.tag);
        hasher.update(self.sender_addr.as_bytes());
        hasher.update(&self.delta_value.to_le_bytes());
        hasher.update(&self.fee_value.to_le_bytes());
        hasher.update(self.receiver_addr.as_bytes());
        self.hash.copy_from_slice(hasher.finalize().as_bytes());
    }

    pub fn marshal(&mut self, buffer: &mut Vec<u8>) {
        if buffer.len() < TOTAL_TRX_LENGTH { 
            buffer.resize(TOTAL_TRX_LENGTH, 0); 
        }
        buffer[..32].copy_from_slice(self.sender_addr.as_bytes());
        buffer[32..40].copy_from_slice(&self.delta_value.to_le_bytes());
        buffer[40..48].copy_from_slice(&self.fee_value.to_le_bytes());
        buffer[48..80].copy_from_slice(self.receiver_addr.as_bytes());
    }

    pub fn unmarshal(&mut self, buffer: &mut[u8]) -> Result<(), Box<dyn error::Error>> {
        self.sender_addr = VerifyingKey::from_bytes(&buffer[..32].try_into()?)?;
        let mut delta = [0u8; 8];
        delta.copy_from_slice(&mut buffer[32..40]);
        self.delta_value = u64::from_le_bytes(delta);

        let mut fee = [0u8; 8];
        fee.copy_from_slice(&buffer[40..48]);
        self.fee_value = u64::from_le_bytes(fee);

        self.receiver_addr = VerifyingKey::from_bytes(&buffer[48..80].try_into()?)?;

        self.sender_sig = Signature::from_bytes(&buffer[TRX_LENGTH..TRX_LENGTH+64].try_into()?);
        self.receiver_sig = Signature::from_bytes(&buffer[TRX_LENGTH+64..].try_into()?);
        self.compute_hash();
        Ok(())
    }

    pub fn is_valid(&mut self,
        mut sender_balance: u64,
        mut reciever_balance: u64,
    ) -> Result<(u64, u64), ()> {

        if !self.verify_sigs() { return Err(()); }

        let sub = self.delta_value + self.fee_value;

        if sub > sender_balance { return Err(()); }

        sender_balance -= sub;

        reciever_balance += self.delta_value;

        return Ok((sender_balance, reciever_balance))

    }
}
