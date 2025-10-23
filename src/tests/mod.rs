use std::fs;

mod ephemeral_trx;
mod schnorr;
mod pool;
mod msging;
mod net_outbound;
mod montgomery;
mod peers;
mod ledger;

pub struct TestFile {
    pub path: String,
}
impl TestFile {
    pub fn new(path: &'static str) -> Self {
        Self { path: path.to_string() }
    }
}

impl Drop for TestFile {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.path);
    }
}
