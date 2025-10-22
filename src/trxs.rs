use curve25519_dalek::{ristretto::CompressedRistretto, scalar::Scalar};
use merlin::Transcript;
use bulletproofs::{ProofError, RangeProof};
use sha2::{Digest, Sha256};

use crate::core::{priority::DeriveStuff, utils::Hash};
use crate::crypto::{random_b32, TrxGenerators, schnorr::SchnorrProof};

const VALUE_SIZE: usize = 64;
pub const PROOF_LENGTH: usize = 672; // (2(log2(64)) + 9) * 32
pub const TRX_LENGTH: usize = PROOF_LENGTH + (6 * 32) + 8;
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
*   | sender_commit_F    32 |====I
*   | sender_proof_F    672 <====I=== PROOF_LEN
*   |-----------------------|    I  
*   | sender_commit_I    32 |----I---I 
*   | delta_commit       32 |    I   I 
*   | fee_commit         32 |    I   I Schnorr Hash Context
*   | fee value          8  |    I   I
*   | receiver_commit_I  32 |----I---I       
*   |-----------------------|    I
*   | receiver_commit_F  32 |====I TRX_LENGTH 
*   ------------------------|    I
*   | sender_Schnoor     96 |    I
*   | receiver_Schnoor   96 |====I TOTAL_TRX_PROOF
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

#[derive(Clone)]
pub struct Trx {
    pub tag: &'static [u8],
    pub delta_commit: CompressedRistretto,
    pub fee_commit: CompressedRistretto,
    pub fee_value: u64,

    pub sender_proof: Option<RangeProof>,
    pub sender_init: CompressedRistretto,
    pub sender_final: CompressedRistretto,
    pub sender_schnorr: SchnorrProof,

    pub receiver_init: CompressedRistretto,
    pub receiver_final: CompressedRistretto, // TODO  this is not needed
                                            // its delta_commit + receiver_init
                                           // sender_final isnt needed either
    pub receiver_schnorr: SchnorrProof,

    pub hash: [u8; 32],
}

impl Default for Trx {
    fn default() -> Self {
        Self::new(b"bullet_ledger")
    }
}
impl DeriveStuff<Box<Hash>> for Box<Trx> {
    fn fill_key(&self, key: &mut Box<Hash>) {
        key.copy_from_slice(&self.hash);
    }
    fn get_comperator(&self) -> u64 {
        self.fee_value
    }
}

impl Trx {
    pub fn new(tag: &'static [u8]) -> Self {
        Self { 
            tag,
            hash: [0u8; 32],
            delta_commit: CompressedRistretto::default(),
            fee_commit: CompressedRistretto::default(),
            fee_value: 0u64,

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

    pub fn set_fee(&mut self, fee: &TrxSecrets) {
        self.fee_commit.0.copy_from_slice(&fee.commit.0);
        self.fee_value = fee.val;
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
    pub fn schnorr_sender(&mut self, gens: &TrxGenerators, s: &TrxSecrets, buffer: &mut Vec<u8>) {
        self.marshal(buffer);
        self.compute_hash();
        self.sender_schnorr.generate(gens, s.x, s.r, &self.hash);
        self.sender_schnorr.copy_to_slice(&mut buffer[TRX_LENGTH..TRX_LENGTH+96])
    }

    /// Marshals data into internal buffer and Creates a schnorr proof.
    /// Proof is stored in struct and marshalled to internal.
    /// In prod you won't call schnorr_sender() & schnorr_receiver() sequentially
    pub fn schnorr_receiver(&mut self, gens: &TrxGenerators, s: &TrxSecrets, buffer: &mut Vec<u8>) {
        self.marshal(buffer);
        self.compute_hash();
        self.receiver_schnorr.generate(gens, s.x, s.r, &self.hash);
        self.receiver_schnorr.copy_to_slice(&mut buffer[TRX_LENGTH+96..])
    }

    pub fn verify_schnorrs(&mut self, gens: &TrxGenerators) -> bool {
        self.compute_hash();
        return self.sender_schnorr.verify(gens,
            &self.sender_init, 
            &self.hash
        ) &&
        self.receiver_schnorr.verify(gens,
            &self.receiver_init, 
            &self.hash
        )
    }

    pub fn compute_hash(&mut self) {
        let mut hasher = Sha256::new();
        hasher.update(self.tag);
        hasher.update(&self.sender_init.as_bytes());
        hasher.update(&self.delta_commit.as_bytes());
        hasher.update(&self.fee_commit.as_bytes());
        hasher.update(&self.receiver_init.as_bytes());
        self.hash.copy_from_slice(hasher.finalize().as_slice());
    }

    pub fn marshal(&mut self, buffer: &mut Vec<u8>) {
        if buffer.len() < TOTAL_TRX_PROOF { 
            buffer.resize(TOTAL_TRX_PROOF, 0); 
        }

        let mut c = 32;
        // Sender
        buffer[..c].copy_from_slice(self.sender_final.as_bytes());
        if let Some(proof) = self.sender_proof.take() {
            buffer[c..c+PROOF_LENGTH].copy_from_slice(&proof.to_bytes());
        }
        c += PROOF_LENGTH;

        // Shared
        buffer[c..c+32].copy_from_slice(self.sender_init.as_bytes());
        buffer[c+32..c+64].copy_from_slice(self.delta_commit.as_bytes());
        buffer[c+64..c+96].copy_from_slice(self.fee_commit.as_bytes());
        buffer[c+96..c+104].copy_from_slice(&self.fee_value.to_le_bytes());
        buffer[c+104..c+136].copy_from_slice(self.receiver_init.as_bytes());

        // Recevier
        buffer[c+136..TRX_LENGTH].copy_from_slice(self.receiver_final.as_bytes());
    }

    pub fn unmarshal(&mut self, buffer: &mut[u8]) -> Result<(), ProofError> {
        let mut p = 32;
        self.sender_final.0.copy_from_slice(&mut buffer[..p]);
        self.sender_proof = Some(RangeProof::from_bytes(&buffer[p..p+PROOF_LENGTH])?);
        p += PROOF_LENGTH;

        self.sender_init.0.copy_from_slice(&buffer[p..p+32]);
        self.delta_commit.0.copy_from_slice(&mut buffer[p+32..p+64]);
        self.fee_commit.0.copy_from_slice(&buffer[p+64..p+96]);
        let mut fee = [0u8; 8];
        fee.copy_from_slice(&buffer[p+96..p+104]);
        self.fee_value = u64::from_le_bytes(fee);
        self.receiver_init.0.copy_from_slice(&buffer[p+104..p+136]);

        self.receiver_final.0.copy_from_slice(&mut buffer[p+136..TRX_LENGTH]);

        self.sender_schnorr.from_bytes(&mut buffer[TRX_LENGTH..TRX_LENGTH+96]);
        self.receiver_schnorr.from_bytes(&mut buffer[TRX_LENGTH+96..]);
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
        self.set_init(is_sender, &init.commit);
        self.set_delta_commit(&delta.commit);
        self.set_fee(&fee);

        if is_sender {
            new_r = init.r - delta.r - fee.r;

            y = init.x - delta.x - fee.x;
            val = init.val - delta.val - fee.val;

            let (range_proof, commit) = RangeProof::prove_single(
                &gens.bullet, 
                &gens.pedersen, 
                &mut Transcript::new(gens.tag), 
                val, &new_r, 
                VALUE_SIZE,
            )?;
            self.sender_proof = Some(range_proof);
            self.sender_final = commit;

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
                if (Scalar::from(self.fee_value) * gens.pedersen.B) != fee { return false }

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
