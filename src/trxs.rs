use curve25519_dalek::{ristretto::CompressedRistretto, scalar::Scalar};
use merlin::Transcript;
use bulletproofs::{ProofError, RangeProof};
use sha2::{Digest, Sha256};
use crate::generators::{random_b32, TrxGenerators};
use crate::schnorr::SchnorrProof;

const VALUE_SIZE: usize = 64;
pub const PROOF_LENGTH: usize = 672; // (2(log2(64)) + 9) * 32
pub const TRX_LENGTH: usize = PROOF_LENGTH + (6 * 32);
pub const TOTAL_TRX_PROOF: usize = TRX_LENGTH + (96 *2);
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


/*
*   TRX FORMAT         |size|
*   ------------------------|
*   | sender_commit_F    32 | 
*   | sender_proof_F    672 |   
*   |-----------------------|      
*   | sender_commit_I    32 | ======I 
*   | delta_commit       32 |       I Schnorr Hash Context
*   | fee_commit         32 |       I
*   | receiver_commit_I  32 |=======I       
*   |-----------------------|
*   | receiver_commit_F  32 |      
*   ------------------------|
*   | sender_Schnoor     96 |
*   | receiver_Schnoor   96 |
*   -------------------------
*
*   NOTE::
*       there is no need for a receiver proof.
*       If sender proof is valid odds are that
*       receiver proof would be valid as well.
*       Unlikely for that many coins to end up 
*       in a single account. If so you're likely
*       dealing with a 51% attack, which negates 
*       the need for a range proof.
*/

pub struct Trx {
    pub delta_commit: CompressedRistretto,
    pub fee_commit: CompressedRistretto,

    pub sender_proof: Option<RangeProof>,
    pub sender_init: CompressedRistretto,
    pub sender_final: CompressedRistretto,
    pub sender_schnorr: SchnorrProof,

    pub receiver_init: CompressedRistretto,
    pub receiver_final: CompressedRistretto,
    pub receiver_schnorr: SchnorrProof,

    pub buffer: [u8; TOTAL_TRX_PROOF],
}

impl Trx {
    pub fn new() -> Self {
        Self { 
            buffer: [0u8; TOTAL_TRX_PROOF],
            delta_commit: CompressedRistretto::default(),
            fee_commit: CompressedRistretto::default(),

            sender_init: CompressedRistretto::default(),
            sender_proof: None, 
            sender_final: CompressedRistretto::default(),
            sender_schnorr: SchnorrProof::default(),

            receiver_init: CompressedRistretto::default(),
            receiver_final: CompressedRistretto::default(),
            receiver_schnorr: SchnorrProof::default(),
        }
    }

    pub fn set_delta_commit(&mut self, delta: &CompressedRistretto) {
        self.delta_commit.0.copy_from_slice(&delta.0);
    }

    pub fn set_fee_commit(&mut self, fee: &CompressedRistretto) {
        self.fee_commit.0.copy_from_slice(&fee.0);
    }

    pub fn set_init(&mut self, is_sender: bool, init: &CompressedRistretto) {
        if is_sender {
            self.sender_init.0.copy_from_slice(&init.0[..]);
        } else {
            self.receiver_init.0.copy_from_slice(&init.0[..]);
        }
    }

    /// Marshals data into internal buffer and Creates a schnorr proof.
    /// Proof is stored in struct and marshalled to internal.
    /// In prod you won't call schnorr_sender() & schnorr_receiver() sequentially
    pub fn schnorr_sender(&mut self, gens: &TrxGenerators, s: &TrxSecrets) {
        self.marshal_internal();
        let hash = self.compute_hash(gens.tag);
        self.sender_schnorr.generate(gens, s.x, s.r, &hash);
        self.buffer[TRX_LENGTH..TRX_LENGTH+96].copy_from_slice(
            self.sender_schnorr.as_mut_slice()
        );
    }

    /// Marshals data into internal buffer and Creates a schnorr proof.
    /// Proof is stored in struct and marshalled to internal.
    /// In prod you won't call schnorr_sender() & schnorr_receiver() sequentially
    pub fn schnorr_receiver(&mut self, gens: &TrxGenerators, s: &TrxSecrets) {
        self.marshal_internal();
        let hash = self.compute_hash(gens.tag);
        self.receiver_schnorr.generate(gens, s.x, s.r, &hash);
        self.buffer[TRX_LENGTH+96..].copy_from_slice(
            self.receiver_schnorr.as_mut_slice()
        );
    }

    pub fn verify_schnorrs(&self, gens: &TrxGenerators) -> bool {
        let hash = self.compute_hash(gens.tag);
        return self.sender_schnorr.verify(gens,
            &self.sender_init.decompress().unwrap(), 
            &hash
        ) &&
        self.receiver_schnorr.verify(gens,
            &self.receiver_init.decompress().unwrap(), 
            &hash
        )
    }

    pub fn compute_hash(&self, tag: &[u8]) -> [u8;32] {
        let mut hasher = Sha256::new();
        hasher.update(tag);
        hasher.update(&self.sender_init.as_bytes());
        hasher.update(&self.delta_commit.as_bytes());
        hasher.update(&self.fee_commit.as_bytes());
        hasher.update(&self.receiver_init.as_bytes());
        let mut hash = [0u8;32];
        hash.copy_from_slice(hasher.finalize().as_slice());
        hash
    }

    pub fn marshal_internal(&mut self) {
        let mut c = 32;
        // Sender
        self.buffer[..c].copy_from_slice(self.sender_final.as_bytes());
        if let Some(proof) = &self.sender_proof.take() {
            self.buffer[c..c+PROOF_LENGTH].copy_from_slice(&proof.to_bytes());
        }
        c += PROOF_LENGTH;

        // Shared
        self.buffer[c..c+32].copy_from_slice(self.sender_init.as_bytes());
        self.buffer[c+32..c+64].copy_from_slice(self.delta_commit.as_bytes());
        self.buffer[c+64..c+96].copy_from_slice(self.fee_commit.as_bytes());
        self.buffer[c+96..c+128].copy_from_slice(self.receiver_init.as_bytes());

        // Recevier
        self.buffer[c+128..TRX_LENGTH].copy_from_slice(self.receiver_final.as_bytes());
    }

    pub fn unmarshal_internal(&mut self) -> Result<(), ProofError> {
        let mut p = 32;
        self.sender_final.0.copy_from_slice(&mut self.buffer[..p]);
        self.sender_proof = Some(RangeProof::from_bytes(&self.buffer[p..p+PROOF_LENGTH])?);
        p += PROOF_LENGTH;

        self.sender_init.0.copy_from_slice(&self.buffer[p..p+32]);
        self.delta_commit.0.copy_from_slice(&mut self.buffer[p+32..p+64]);
        self.fee_commit.0.copy_from_slice(&self.buffer[p+64..p+96]);
        self.receiver_init.0.copy_from_slice(&self.buffer[p+96..p+128]);

        self.receiver_final.0.copy_from_slice(&mut self.buffer[p+128..TRX_LENGTH]);

        self.sender_schnorr.from_bytes(&mut self.buffer[TRX_LENGTH..TRX_LENGTH+96]);
        self.receiver_schnorr.from_bytes(&mut self.buffer[TRX_LENGTH+96..]);
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
        self.set_init(is_sender, &init.commit);
        self.set_delta_commit(&delta.commit);
        self.set_fee_commit(&fee.commit);

        if is_sender {
            new_r = init.r - delta.r - fee.r;

            y = init.x - delta.x - fee.x;
            val = init.val - delta.val - fee.val;

            let (rp, c) = RangeProof::prove_single(
                &gens.bullet, 
                &gens.pedersen, 
                &mut Transcript::new(gens.tag), 
                val, &new_r, 
                VALUE_SIZE,
            )?;
            self.sender_proof = Some(rp);
            self.sender_final = c;

        } else {
            y = init.x + delta.x;
            new_r = init.r + delta.r;
            val = init.val + delta.val;
            self.receiver_final = gens.pedersen.commit(y, new_r).compress();
        }

        let mut new_secrets = fee;
        new_secrets.x = y;
        new_secrets.val = val;
        new_secrets.r = new_r;

        Ok(new_secrets)
    }

    /// Ensures initial_commit - delta_commit == final_commit
    /// And that final_proof.verify() == true
    pub fn is_valid(&mut self, gens: &TrxGenerators) -> bool {

        if !self.verify_schnorrs(gens) { return false }

        return match (
            self.delta_commit.decompress(), 
            self.fee_commit.decompress(),
            self.sender_init.decompress(), 
            self.sender_final.decompress(),
            self.receiver_init.decompress(), 
            self.receiver_final.decompress(),
            self.sender_proof.take(),
        ) {
            (
                Some(delta), Some(fee),
                Some(s_init), Some(s_final),
                Some(r_init), Some(r_final),
                Some(s_proof)
            ) => {
                if s_init - delta - fee != s_final { return false }
                if r_init + delta != r_final { return false }

                let mut st = Transcript::new(gens.tag);
                return s_proof.verify_single(
                        &gens.bullet, &gens.pedersen, &mut st, 
                        &self.sender_final, VALUE_SIZE,
                    ).is_ok() 
            },

            _ => false,
        }
    }
}
