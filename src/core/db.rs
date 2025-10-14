use curve25519_dalek::ristretto::CompressedRistretto;
use rusqlite::{Connection, Result};

pub struct Ledger {
    db: Connection,
}

impl Ledger {
    pub fn new(path:String) -> Result<Self> {
        let db = Connection::open(path)?;
        db.execute(
            "CREATE TABLE IF NOT EXISTS ledger (
                claim BLOB PRIMARY KEY
            );", 
            [],
        )?;
        Ok(Ledger { db })
    }

    pub fn initialize_balance(&self, 
        commit: &CompressedRistretto
    ) -> Result<usize> {
        let mut q = self.db.prepare_cached(
            "INSERT OR IGNORE INTO ledger (claim) VALUES (?1);"
        )?;
        q.execute((commit.as_bytes(),))
    }

    pub fn update_balance(&self,
        commit: &CompressedRistretto,
        new_commit: &CompressedRistretto,
    ) -> Result<usize> {
        let mut q = self.db.prepare_cached(
            "UPDATE ledger SET claim = ?1 WHERE claim = ?2;"
        )?;
        q.execute((new_commit.as_bytes(), commit.as_bytes()))
    }

    pub fn remove_balance(&self,
        commit: &CompressedRistretto,
    ) -> Result<usize> {
        let mut q = self.db.prepare_cached(
            "DELETE FROM ledger WHERE claim = ?1;"
        )?;
        q.execute((commit.as_bytes(),))
    }
}
