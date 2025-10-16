use std::net::{Ipv4Addr, SocketAddr};
use rusqlite::{Connection, Error, Result};

use crate::utils::{NetError, NetResult};

pub struct PeerMan {
    db: Connection,
    threshold: usize,
}

impl PeerMan {
    pub fn new(db_path: String, threshold: usize, initial_ips: Vec<[u8;4]>) -> Result<Self> {
        let db = Connection::open(db_path)?;
        db.execute(
            "CREATE TABLE IF NOT EXISTS peers (
                ip TEXT UNIQUE PRIMARY KEY,
                score INT
            );", 
            [],
        )?;
        let s = Self { db , threshold };
        for ip in initial_ips {
            s.add_peer(&Ipv4Addr::new(ip[0],ip[1],ip[2],ip[3])).unwrap();
        }
        Ok(s)
    }

    pub fn add_peer(&self, ip: &Ipv4Addr) -> NetResult<()> {
        let mut q = self.db.prepare_cached(
            "INSERT INTO peers (ip, score) VALUES(?1, ?2);"
        ).map_err(|e| NetError::PeerDbQ(e.to_string()))?;

        if q.execute((ip.to_string(), 0))
            .map_err(|e| NetError::PeerDbE(e.to_string())
        )? == 0 {
            return Err(NetError::PeerDbE("peer not added".into()));
        }

        Ok(())
    }

    pub fn remove_peer(&self, ip: &Ipv4Addr) -> NetResult<()> {
        let mut q = self.db.prepare_cached(
            "DELETE FROM peers WHERE ip = ?1;"
        ).map_err(|e| NetError::PeerDbQ(e.to_string()))?;

        if q.execute((ip.to_string(),))
            .map_err(|e| NetError::PeerDbE(e.to_string())
        )? == 0 {
            return Err(NetError::PeerDbE("peer not found".into()));
        }

        Ok(())
    }

    pub fn is_banned(&self, addr: &SocketAddr) -> NetResult<()> {
        let mut q = self.db.prepare_cached(
            "SELECT score FROM peers WHERE ip = ?1;"
        ).map_err(|e| NetError::PeerDbQ(e.to_string()))?;
        if !addr.is_ipv4() {
            return Err(NetError::Unauthorized);
        }
        let ip = addr.ip().to_canonical().to_string();

        let score: usize = q.query_row(
            (ip,), |row| row.get(0)
        ).map_err(|e| {
                if e == Error::QueryReturnedNoRows {
                    return NetError::Unauthorized;
                }
                return NetError::PeerDbE(e.to_string())
            })?;

        if score < self.threshold {
            return Ok(());
        }
        Err(NetError::Unauthorized)
    }

    pub fn record_behaviour(&self, addr: &SocketAddr, error: NetError) -> NetResult<()> {
        let mut q = self.db.prepare_cached(
            "UPDATE peers SET score = score + ?1 WHERE ip = ?2;"
        ).map_err(|e| NetError::PeerDbQ(e.to_string()))?;

        let ip = addr.ip().to_canonical().to_string();

        if q.execute((error.to_score(), ip))
            .map_err(|e| NetError::PeerDbE(e.to_string())
        )? == 0 {
            return Err(NetError::Unauthorized);
        }

        Ok(())
    }
}
