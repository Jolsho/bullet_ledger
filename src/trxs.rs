use std::error;
use curve25519_dalek::{ristretto::CompressedRistretto, scalar::Scalar};
use merlin::Transcript;
use bulletproofs::{ProofError, RangeProof};
use crate::{accounts, utils::{random_b32, Params}};

const VALUE_SIZE: usize = 64;
pub const PROOF_LENGTH: usize = 672; // (2(log2(64)) + 9) * 32
pub const TRX_LENGTH: usize = (2 * PROOF_LENGTH) + (5 * 32);

/*
*   TRX FORMAT     |size|
*   ---------------------- ======I
*   | sender_commit   32 |       I
*   | sender_proof   672 |       I== Sender Hashes
*   |--------------------|       I      & signs
*   | sender          32 | ==========I
*   | delta_commit    32 |       I   I
*   | receiver        32 | ======I   I
*   |------------------- |           I== Receiver Hashes 
*   | receiver_commit 32 |           I      & signs
*   | receiver_proof 672 |           I
*   ---------------------- ==========I
*   | sender_sig      64 |
*   | receiver_sig    64 |
*   ----------------------
*/

pub struct Trx {
    pub delta_commit: CompressedRistretto,

    pub sender: [u8; 32],
    pub sender_proof: Option<RangeProof>,
    pub sender_commit: CompressedRistretto,

    pub receiver: [u8; 32],
    pub receiver_proof: Option<RangeProof>,
    pub receiver_commit: CompressedRistretto,

    pub buffer: [u8; TRX_LENGTH + 128],
}

impl Trx {
    pub fn new() -> Self {
        Self { 
            buffer: [0u8; TRX_LENGTH + 128],
            delta_commit: CompressedRistretto::default(),
            sender: [0u8; 32], 
            sender_proof: None, 
            sender_commit: CompressedRistretto::default(),
            receiver: [0u8; 32], 
            receiver_proof: None, 
            receiver_commit: CompressedRistretto::default(),
        }
    }
    pub fn set_delta_commit(&mut self, delta_c: &CompressedRistretto) {
        self.delta_commit.0.copy_from_slice(&delta_c.0);
    }
    pub fn set_sender(&mut self, sender: &[u8;32]) {
        self.sender.copy_from_slice(sender);
    }
    pub fn set_receiver(&mut self, receiver: &[u8;32]) {
        self.receiver.copy_from_slice(receiver);
    }

    pub fn copy_sender_receipt(&mut self, account: &[u8;32], receipt: &mut TrxReceipt) {
        self.copy_receipt(account, receipt, true)
    }

    pub fn copy_receiver_receipt(&mut self, account: &[u8;32], receipt: &mut TrxReceipt) {
        self.copy_receipt(account, receipt, false)
    }

    fn copy_receipt(&mut self,
        account: &[u8;32],
        receipt: &mut TrxReceipt,
        is_sender: bool,
    ) {
        if is_sender {
            self.sender.copy_from_slice(account);
            self.sender_proof = receipt.proof.take();
            self.sender_commit = receipt.commit;
        } else {
            self.receiver.copy_from_slice(account);
            self.receiver_proof = receipt.proof.take();
            self.receiver_commit = receipt.commit;
        }
    }

    pub fn sign_sender(&mut self, priv_key: &Scalar, prefix: &[u8;32]) {
        let data = &self.buffer[..TRX_LENGTH - PROOF_LENGTH - 32];
        let sig = accounts::sign(priv_key, prefix, data);
        self.buffer[TRX_LENGTH..TRX_LENGTH+64].copy_from_slice(&sig);
    }

    pub fn sign_receiver(&mut self, priv_key: &Scalar, prefix: &[u8;32]) {
        let data = &self.buffer[PROOF_LENGTH + 32..TRX_LENGTH];
        let sig = accounts::sign(priv_key, prefix, data);
        self.buffer[TRX_LENGTH+64..].copy_from_slice(&sig);
    }

    pub fn verify_signatures(&self) -> bool {
        let sender_sig = &self.buffer[TRX_LENGTH..TRX_LENGTH+64];
        let sender_data = &self.buffer[..TRX_LENGTH - PROOF_LENGTH - 32];

        let receiver_sig = &self.buffer[TRX_LENGTH+64..];
        let receiver_data = &self.buffer[PROOF_LENGTH + 32..TRX_LENGTH];

        return accounts::verify(&self.sender, sender_data, sender_sig)
        && accounts::verify(&self.receiver, receiver_data, receiver_sig)
    }

    pub fn to_bytes(&mut self) {

        let mut c = 32;
        // Sender
        self.buffer[..c].copy_from_slice(&self.sender_commit.to_bytes());
        if let Some(proof) = &self.sender_proof.take() {
            self.buffer[c..c+PROOF_LENGTH].copy_from_slice(&proof.to_bytes());
            c += PROOF_LENGTH;
        }

        // Shared
        self.buffer[c..c+32].copy_from_slice(&self.sender);
        self.buffer[c+32..c+64].copy_from_slice(&self.delta_commit.to_bytes());
        self.buffer[c+64..c+96].copy_from_slice(&self.receiver);

        // Recevier
        self.buffer[c+96..c+128].copy_from_slice(&self.receiver_commit.to_bytes());
        if let Some(proof) = &self.receiver_proof.take() {
            self.buffer[c+128..TRX_LENGTH].copy_from_slice(&proof.to_bytes());
        }
    }

    pub fn from_bytes(&mut self) -> Result<(), Box<dyn error::Error>> {
        self.sender_commit.0.copy_from_slice(&mut self.buffer[..32]);
        self.sender_proof = Some(RangeProof::from_bytes(&self.buffer[32..32+PROOF_LENGTH])?);

        let p1 = 32 + PROOF_LENGTH;
        self.sender.copy_from_slice(&self.buffer[p1..p1+32]);
        self.delta_commit.0.copy_from_slice(&mut self.buffer[p1+32..p1+64]);
        self.receiver.copy_from_slice(&self.buffer[p1+64..p1+96]);

        self.receiver_commit.0.copy_from_slice(&mut self.buffer[p1+96..p1+128]);
        self.receiver_proof = Some(RangeProof::from_bytes(&self.buffer[p1+128..TRX_LENGTH])?);
        Ok(())
    }
}

// =============================================
//      THIS IS THE REAL THICK OF IT HERE
// =============================================
pub struct TrxSecrets {
    pub x: u64,
    pub r: Scalar,
}
impl TrxSecrets {
    pub fn new(x: u64, r: Scalar) -> Self {
        Self { x, r }
    }
}

pub fn value_commit(p: &Params, x: u64) -> (TrxSecrets, CompressedRistretto) {
    let s = Scalar::from_bytes_mod_order(random_b32());
    let commit = p.pedersen.commit(Scalar::from(x), s).compress();
    (TrxSecrets::new(x, s), commit)
}

pub struct TrxReceipt {
    pub secrets: TrxSecrets,
    pub proof: Option<RangeProof>,
    pub commit: CompressedRistretto,
}

/// Consumes init and delta secrets to derive a final state
/// Final state is new secrets, balance and balance range proof
pub fn state_transition_sender(
    p: &Params, init: TrxSecrets, delta: &TrxSecrets,
) -> Result<TrxReceipt, ProofError> {
    state_transition(p, init, delta, true)
}
pub fn state_transition_receiver(
    p: &Params, init: TrxSecrets, delta: &TrxSecrets,
) -> Result<TrxReceipt, ProofError> {
    state_transition(p, init, delta, false)
}
pub fn state_transition(
    p: &Params, init: TrxSecrets, delta: &TrxSecrets, is_sender: bool,
) -> Result<TrxReceipt, ProofError> {
    let y: u64;
    let new_r: Scalar;
    if is_sender {
        y = init.x - delta.x;
        new_r = init.r - delta.r;
    } else {
        y = init.x + delta.x;
        new_r = init.r + delta.r;
    }
    let mut t = Transcript::new(p.tag);
    let (proof, commit) = RangeProof::prove_single(
        &p.bullet, &p.pedersen, &mut t, y, &new_r, VALUE_SIZE,
    )?;

    let mut new_secrets = init;
    new_secrets.r = new_r;
    new_secrets.x = y;
    Ok(TrxReceipt{
        commit, secrets: new_secrets, proof: Some(proof), 
    })
}

/// Ensures initial_commit - delta_commit == final_commit
/// And that final_proof.verify() == true
pub fn validate_trx(
    p: &Params, t: &mut Trx, 
    sender_init_commit: &CompressedRistretto, 
    receiver_init_commit: &CompressedRistretto
) -> bool {
    return match (
        t.delta_commit.decompress(), 
        sender_init_commit.decompress(), 
        t.sender_commit.decompress(),
        receiver_init_commit.decompress(), 
        t.receiver_commit.decompress(),
        t.sender_proof.take(),
        t.receiver_proof.take(),
    ) {
        (
            Some(delta_c),
            Some(s_init_c), Some(s_final_c),
            Some(r_init_c), Some(r_final_c),
            Some(s_proof), Some(r_proof),
        ) => {
            if s_init_c - delta_c != s_final_c { return false }
            if r_init_c + delta_c != r_final_c { return false }

            let mut st = Transcript::new(p.tag);
            let mut rt = Transcript::new(p.tag);
            return s_proof.verify_single(
                    &p.bullet, &p.pedersen, &mut st, 
                    &t.sender_commit, VALUE_SIZE,
                ).is_ok() 
                && 
                r_proof.verify_single(
                    &p.bullet, &p.pedersen, &mut rt, 
                    &t.receiver_commit,VALUE_SIZE
                ).is_ok()
        },

        _ => false,
    }
}
