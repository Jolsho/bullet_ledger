use curve25519_dalek::ristretto::CompressedRistretto;
use rusqlite::{Connection, Result};

pub fn start_db() -> Result<Connection> {
    let conn = Connection::open("ledger.sqlite3")?;
    conn.execute(
        "CREATE TABLE IF NOT EXISTS ledger (
            claim BLOB PRIMARY KEY
        )", 
        [],
    )?;
    Ok(conn)
}

pub fn initialize_account(
    conn: &Connection, 
    commit: &CompressedRistretto,
) -> Result<usize> {
    conn.execute(
        "INSERT OR IGNORE INTO ledger (claim) VALUES (?1)",
        [commit.as_bytes()],
    )
}

pub fn update_balance(
    conn: &Connection, 
    commit: &CompressedRistretto,
    new_commit: &CompressedRistretto,
) -> Result<usize> {
    conn.execute(
        "UPDATE ledger SET claim = ?1 WHERE claim = ?2", 
        [new_commit.as_bytes(), commit.as_bytes()],
    )
}
