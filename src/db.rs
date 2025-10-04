use curve25519_dalek::ristretto::CompressedRistretto;
use rusqlite::{Connection, Result};

pub fn start_db() -> Result<Connection> {
    let conn = Connection::open("ledger.sqlite3")?;
    conn.execute(
        "CREATE TABLE IF NOT EXISTS ledger (
            account BLOB PRIMARY KEY,
            balance BLOB
        )", 
        [],
    )?;
    Ok(conn)
}

pub fn initialize_account(
    conn: &Connection, 
    account: &[u8;32], 
    balance: &CompressedRistretto,
) -> Result<usize> {
    conn.execute(
        "INSERT OR IGNORE INTO ledger (account, balance) VALUES (?1, ?2)",
        [account, balance.as_bytes()],
    )
}

pub fn update_balance(
    conn: &Connection, 
    account: &[u8;32], 
    balance: &CompressedRistretto,
) -> Result<usize> {
    conn.execute(
        "UPDATE ledger SET balance = ?1 WHERE account = ?2", 
        [balance.as_bytes(), account],
    )
}

pub fn get_balance(
    conn: &Connection, 
    account: &[u8;32], 
) -> Result<CompressedRistretto> {
    Ok(CompressedRistretto(
        conn.query_row(
            "SELECT balance FROM ledger WHERE account = ?1", 
            [account], 
            |row| row.get(0)
        )?
    ))
}

