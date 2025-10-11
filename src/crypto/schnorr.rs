use curve25519_dalek::{ristretto::CompressedRistretto, RistrettoPoint, Scalar};
use sha2::{Digest, Sha256};
use super::{random_b32, TrxGenerators};

#[derive(Clone)]
pub struct SchnorrProof {
    pub random: CompressedRistretto,
    pub s1: Scalar,
    pub s2: Scalar,
    buff: [u8; 96],
}

impl SchnorrProof {
    pub fn default() -> Self {
        Self { 
            random: CompressedRistretto::default(), 
            s1: Scalar::ZERO, 
            s2: Scalar::ZERO,
            buff: [0u8; 96],
        }
    }

    pub fn compute_challenge(&self, 
        commit: CompressedRistretto, 
        context_hash: &[u8; 32],
    ) -> Scalar {
        let mut hasher = Sha256::new();
        hasher.update(&commit.as_bytes());
        hasher.update(&self.random.as_bytes());
        hasher.update(context_hash);
        let mut hash = [0u8;32];
        hash.copy_from_slice(hasher.finalize().as_slice());
        Scalar::from_bytes_mod_order(hash)
    }

    pub fn generate(&mut self, 
        gens: &TrxGenerators, 
        x: Scalar, r:Scalar, 
        context_hash: &[u8; 32],
    ) {
        let commit = gens.pedersen.commit(x, r);
        let r1 = Scalar::from_bytes_mod_order(random_b32());
        let r2 = Scalar::from_bytes_mod_order(random_b32());
        self.random = gens.pedersen.commit(r1, r2).compress();

        let c = self.compute_challenge(commit.compress(), context_hash);

        self.s1 = r1 + c * x;
        self.s2 = r2 + c * r;
    }

    pub fn verify( 
        &self,
        gens: &TrxGenerators, 
        commit: &RistrettoPoint,
        context_hash: &[u8; 32],
    ) -> bool {
        let c = self.compute_challenge(commit.compress(), context_hash);
        let random = self.random.decompress().unwrap_or(RistrettoPoint::default());
        gens.pedersen.commit(self.s1, self.s2) == random + c * commit
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8;96] {
        self.buff[..32].copy_from_slice(self.random.as_bytes());
        self.buff[32..64].copy_from_slice(self.s1.as_bytes());
        self.buff[64..].copy_from_slice(self.s2.as_bytes());
        &mut self.buff
    }
    pub fn from_bytes(&mut self, buff: &mut [u8]) {
        self.random.0.copy_from_slice(&mut buff[..32]);

        let mut s1 = [0u8;32];
        s1.copy_from_slice(&mut buff[32..64]);
        self.s1 = Scalar::from_bytes_mod_order(s1);

        let mut s2 = [0u8;32];
        s2.copy_from_slice(&mut buff[64..96]);
        self.s2 = Scalar::from_bytes_mod_order(s2);
    }
}
