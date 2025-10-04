use std::{error::Error, io::Read, os::unix::net::UnixStream};

use crate::trxs::Trx;

#[allow(unused)]
pub fn read_next_trx(s: &mut UnixStream, t: &mut Trx) -> Result<(), Box<dyn Error>> {
    s.read_exact(&mut t.buffer)?;


    Ok(())
}
