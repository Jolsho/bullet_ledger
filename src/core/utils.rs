use crate::{core::priority::PriorityPool, trxs::Trx};

pub type TrxPool = PriorityPool<Box<Hash>, Box<Trx>>;

#[derive(PartialEq, Eq, Hash, Clone, Copy, PartialOrd, Ord)]
pub struct Hash(pub [u8;32]);
impl Hash {
    pub const ZERO: Hash = Hash([0u8; 32]);
    pub fn copy_from_slice(&mut self, buff: &[u8]) {
        self.0.copy_from_slice(buff);
    }
}
impl Default for Hash {
    fn default() -> Self { Hash::ZERO }
}
impl AsRef<[u8]> for Hash {
    fn as_ref(&self) -> &[u8] {
        &self.0[..]
    }
}


