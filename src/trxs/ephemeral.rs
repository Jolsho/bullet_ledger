use curve25519_dalek::{ristretto::CompressedRistretto, scalar::Scalar};
use merlin::Transcript;
use bulletproofs::{ProofError, RangeProof};

use crate::{crypto::{schnorr::SchnorrProof, TrxGenerators}, trxs::{TrxSecrets, PROOF_LENGTH, TRX_LENGTH, VALUE_SIZE}};

/*
*   TRX FORMAT         |size|
*   ------------------------|
*   | sender_rng_proof  672 <====I--- PROOF_LEN
*   |-----------------------|    I  
*   | sender_commit_I    32 |----I---I 
*   | delta_commit       32 |    I   I  Sig Hash Context
*   | fee value          8  |    I   I
*   | receiver_commit_I  32 |----I---I       
*   ------------------------|    I
*   | sender_schnorr     96 |    I
*   | receiver_schnorr   96 |====I TOTAL_TRX_PROOF
*   -------------------------
*
*/

pub const TOTAL_TRX_PROOF: usize = TRX_LENGTH + (96 *2);

#[derive(Clone)]
pub struct EphemeralTrx {
    pub tag: &'static [u8],
    pub hash: [u8; 32],

    pub sender_proof: Option<RangeProof>,
    pub sender_init: CompressedRistretto,
    pub delta_commit: CompressedRistretto,
    pub fee_value: u64,
    pub receiver_init: CompressedRistretto,

    pub sender_schnorr: SchnorrProof,
    pub receiver_schnorr: SchnorrProof,
}

impl Default for EphemeralTrx {
    fn default() -> Self {
        Self::new(b"bullet_ledger")
    }
}

impl EphemeralTrx {
    pub fn new(tag: &'static [u8]) -> Self {
        Self { 
            tag,
            hash: [0u8; 32],
            delta_commit: CompressedRistretto::default(),
            fee_value: 0u64,

            sender_init: CompressedRistretto::default(),
            sender_proof: None, 
            sender_schnorr: SchnorrProof::default(),

            receiver_init: CompressedRistretto::default(),
            receiver_schnorr: SchnorrProof::default(),
        }
    }

    pub fn set_delta_commit(&mut self, delta: &CompressedRistretto) {
        self.delta_commit.0.copy_from_slice(&delta.0);
    }

    pub fn set_fee(&mut self, fee: &TrxSecrets) {
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
        let mut hasher = blake3::Hasher::new();
        hasher.update(self.tag);
        hasher.update(self.sender_init.as_bytes());
        hasher.update(self.delta_commit.as_bytes());
        hasher.update(&self.fee_value.to_le_bytes());
        hasher.update(self.receiver_init.as_bytes());
        self.hash.copy_from_slice(hasher.finalize().as_bytes());
    }

    pub fn marshal(&mut self, buffer: &mut Vec<u8>) {
        if buffer.len() < TOTAL_TRX_PROOF { 
            buffer.resize(TOTAL_TRX_PROOF, 0); 
        }

        // Sender
        if let Some(proof) = self.sender_proof.take() {
            buffer[..PROOF_LENGTH].copy_from_slice(&proof.to_bytes());
        }
        let c = PROOF_LENGTH;

        // Shared
        buffer[c..c+32].copy_from_slice(self.sender_init.as_bytes());
        buffer[c+32..c+64].copy_from_slice(self.delta_commit.as_bytes());
        buffer[c+64..c+72].copy_from_slice(&self.fee_value.to_le_bytes());
        buffer[c+72..c+104].copy_from_slice(self.receiver_init.as_bytes());
    }

    pub fn unmarshal(&mut self, buffer: &mut[u8]) -> Result<(), ProofError> {
        self.sender_proof = Some(RangeProof::from_bytes(&buffer[..PROOF_LENGTH])?);
        let p = PROOF_LENGTH;

        self.sender_init.0.copy_from_slice(&buffer[p..p+32]);
        self.delta_commit.0.copy_from_slice(&mut buffer[p+32..p+64]);
        let mut fee = [0u8; 8];
        fee.copy_from_slice(&buffer[p+64..p+72]);
        self.fee_value = u64::from_le_bytes(fee);
        self.receiver_init.0.copy_from_slice(&buffer[p+72..p+104]);

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
    pub fn is_valid(&mut self, 
        gens: &TrxGenerators
    ) -> Result<(CompressedRistretto, CompressedRistretto), ()> {

        if !self.verify_schnorrs(gens) { return Err(()) }

        return match (
            self.delta_commit.decompress(), 
            self.sender_init.decompress(), 
            self.receiver_init.decompress(), 
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
