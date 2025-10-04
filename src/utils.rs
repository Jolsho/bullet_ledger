use rand::{rngs::OsRng, TryRngCore};
use bulletproofs::{BulletproofGens, PedersenGens};

pub fn random_b32() -> [u8; 32] {
    let mut seed = [0u8; 32];
    OsRng.try_fill_bytes(&mut seed).unwrap();
    seed
}

pub struct Params {
    pub pedersen: PedersenGens,
    pub bullet: BulletproofGens,
    pub tag: &'static [u8],
}
impl Params {
    pub fn new(tag: &'static str, bullet_count: usize) -> Self {
        Self { 
            pedersen: PedersenGens::default(), 
            bullet: BulletproofGens::new(64, bullet_count), 
            tag: tag.as_bytes(),
        }
    }
}
