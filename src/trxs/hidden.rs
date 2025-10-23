use std::error;

use bulletproofs::{ProofError, RangeProof};
use curve25519_dalek::{ristretto::CompressedRistretto, Scalar};
use ed25519_dalek::{ed25519::signature::SignerMut, Signature, SigningKey, VerifyingKey};
use merlin::Transcript;


use crate::{crypto::TrxGenerators, trxs::{TrxSecrets, PROOF_LENGTH, TRX_LENGTH, VALUE_SIZE}};

/*
*   TRX FORMAT         |size|
*   ------------------------|
*   | sender_rng_proof  672 <====I--- PROOF_LEN
*   |-----------------------|    I  
*   | sender_addr        32 |----I---I 
*   | delta_commit       32 |    I   I  Sig Hash Context
*   | fee value          8  |    I   I  && TRX_LENGTH
*   | receiver_addr      32 |----I---I       
*   ------------------------|    I
*   | sender_sig         64 |    I
*   | receiver_sig       64 |====I TOTAL_TRX_PROOF
*   -------------------------
*
*/

pub const TOTAL_TRX_PROOF: usize = TRX_LENGTH + (64 * 2);

#[derive(Clone)]
pub struct HiddenTrx {
    pub tag: &'static [u8],
    pub hash: [u8; 32],

    pub sender_proof: Option<RangeProof>,
    pub sender_addr: VerifyingKey,

    pub delta_commit: CompressedRistretto,
    pub fee_value: u64,

    pub receiver_addr: VerifyingKey,

    pub sender_sig: Signature,
    pub receiver_sig: Signature,
}

impl Default for HiddenTrx {
    fn default() -> Self {
        Self::new(b"bullet_ledger")
    }
}

impl HiddenTrx {
    pub fn new(tag: &'static [u8]) -> Self {
        Self { 
            tag,
            hash: [0u8; 32],
            delta_commit: CompressedRistretto::default(),
            fee_value: 0u64,

            sender_addr: VerifyingKey::default(),
            sender_proof: None, 
            sender_sig: Signature::from_bytes(&[0u8;64]),

            receiver_addr: VerifyingKey::default(),
            receiver_sig: Signature::from_bytes(&[0u8;64]),
        }
    }

    pub fn set_delta_commit(&mut self, delta: &CompressedRistretto) {
        self.delta_commit.0.copy_from_slice(&delta.0);
    }

    pub fn set_fee(&mut self, fee: &TrxSecrets) {
        self.fee_value = fee.val;
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
        hasher.update(self.delta_commit.as_bytes());
        hasher.update(&self.fee_value.to_le_bytes());
        hasher.update(self.receiver_addr.as_bytes());
        self.hash.copy_from_slice(hasher.finalize().as_bytes());
    }

    pub fn marshal(&mut self, buffer: &mut Vec<u8>) {
        if buffer.len() < TOTAL_TRX_PROOF { 
            buffer.resize(TOTAL_TRX_PROOF, 0); 
        }

        let mut c = 32;
        // Sender
        if let Some(proof) = self.sender_proof.take() {
            buffer[c..c+PROOF_LENGTH].copy_from_slice(&proof.to_bytes());
        }
        c += PROOF_LENGTH;

        // Shared
        buffer[c..c+32].copy_from_slice(self.sender_addr.as_bytes());
        buffer[c+32..c+64].copy_from_slice(self.delta_commit.as_bytes());
        buffer[c+64..c+92].copy_from_slice(&self.fee_value.to_le_bytes());
        buffer[c+92..c+124].copy_from_slice(self.receiver_addr.as_bytes());
    }

    pub fn unmarshal(&mut self, buffer: &mut[u8]) -> Result<(), Box<dyn error::Error>> {
        let mut p = 32;
        self.sender_proof = Some(RangeProof::from_bytes(&buffer[p..p+PROOF_LENGTH])?);
        p += PROOF_LENGTH;

        self.sender_addr = VerifyingKey::from_bytes(&buffer[p..p+32].try_into()?)?;
        self.delta_commit.0.copy_from_slice(&mut buffer[p+32..p+64]);
        let mut fee = [0u8; 8];
        fee.copy_from_slice(&buffer[p+64..p+92]);
        self.fee_value = u64::from_le_bytes(fee);
        self.receiver_addr = VerifyingKey::from_bytes(&buffer[p+104..p+136].try_into()?)?;

        self.sender_sig = Signature::from_bytes(&buffer[TRX_LENGTH..TRX_LENGTH+64].try_into()?);
        self.receiver_sig = Signature::from_bytes(&buffer[TRX_LENGTH+64..].try_into()?);
        self.compute_hash();
        Ok(())
    }

    /// Consumes init and delta secrets to derive a final state
    /// Final state is new secrets, balance and balance range proof
    pub fn state_transition(&mut self,
        is_sender: bool,
        gens: &TrxGenerators, 
        init: &TrxSecrets, 
        delta: &TrxSecrets, 
        fee: TrxSecrets, 
    ) -> Result<TrxSecrets, ProofError> {
        let y: Scalar;
        let new_r: Scalar;
        let val: u64;
        self.set_delta_commit(&delta.commit);
        self.set_fee(&fee);

        if is_sender {
            new_r = init.r - delta.r - fee.r;

            y = init.x - delta.x - fee.x;
            val = init.val - delta.val - fee.val;

            let (range_proof, _commit) = RangeProof::prove_single(
                &gens.bullet, 
                &gens.pedersen, 
                &mut Transcript::new(gens.tag), 
                val, &new_r, 
                VALUE_SIZE,
            )?;
            self.sender_proof = Some(range_proof);

        } else {
            y = init.x + delta.x;
            new_r = init.r + delta.r;
            val = init.val + delta.val;
        }

        let mut new_secrets = fee;
        new_secrets.x = y;
        new_secrets.val = val;
        new_secrets.r = new_r;

        Ok(new_secrets)
    }

    /// Ensures initial_commit - delta_commit == final_commit
    /// And that final_proof.verify() == true
    pub fn is_valid(
        &mut self,
        gens: &TrxGenerators,
        sender_init: CompressedRistretto,
        receiver_init: CompressedRistretto,
    ) -> Result<(CompressedRistretto, CompressedRistretto), ()> {

        if !self.verify_sigs() { return Err(()) }

        return match (
            self.delta_commit.decompress(), 
            sender_init.decompress(), 
            receiver_init.decompress(), 
            self.sender_proof.take(),
        ) {
            (
                Some(delta),
                Some(s_init),
                Some(r_init),
                Some(s_proof)
            ) => {
                let fee = Scalar::from(self.fee_value) * gens.pedersen.B;

                let s_final = (s_init - delta - fee).compress();
                let r_final = (r_init + delta).compress();

                let mut st = Transcript::new(gens.tag);
                if s_proof.verify_single(
                        &gens.bullet, &gens.pedersen, &mut st, 
                        &s_final, VALUE_SIZE,
                    ).is_ok()  {
                    return Ok((s_final, r_final));

                } else {

                    return Err(());
                }
            },
            _ => Err(()),
        }
    }
}
