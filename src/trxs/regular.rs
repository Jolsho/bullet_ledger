pub struct RegularTrx {
    pub fee_value: u64,
    pub hash: [u8;32],
}

impl RegularTrx {
    pub fn default() -> Self {
        Self { fee_value: 0, hash: [0u8;32] }
    }
}
