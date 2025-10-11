pub mod schnorr;

use rand::{rngs::OsRng, TryRngCore};
use bulletproofs::{BulletproofGens, PedersenGens};

pub fn random_b32() -> [u8; 32] {
    let mut buff = [0u8; 32];
    OsRng.try_fill_bytes(&mut buff).unwrap();
    buff
}

pub struct TrxGenerators {
    pub tag: &'static [u8],
    pub pedersen: PedersenGens,
    pub bullet: BulletproofGens,
}

impl TrxGenerators {
    pub fn new(tag: &'static str, bullet_count: usize) -> Self {
        Self { 
            pedersen: PedersenGens::default(), 
            bullet: BulletproofGens::new(64, bullet_count), 
            tag: tag.as_bytes(),
        }
    }
}
