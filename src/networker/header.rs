use std::io::{self, Error};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PacketCode {
    None = 0,
    PingPong = 1,
}
pub fn code_from_u8(dig: u8) -> PacketCode {
    match dig {
        1 => PacketCode::PingPong,
        _ => PacketCode::None,
    }
}

pub const HEADER_LEN: usize = 9;

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct Header {
    pub code: PacketCode,
    pub buff: [u8; HEADER_LEN],
    max_buffer_size: usize,
}

impl Header {
    pub fn new(max_buffer_size: usize) -> Self {
        Self { 
            code: PacketCode::None,
            buff: [0u8; HEADER_LEN],
            max_buffer_size,
        }
    }

    pub fn unmarshal(&mut self, buff: &[u8]) -> io::Result<usize> {
        self.code = code_from_u8(buff[0]);

        let len_bytes: [u8; 8] = buff[1..9].try_into()
            .map_err(|_| Error::other("Invalid length"))?;
        let len = u64::from_le_bytes(len_bytes) as usize;
        self.buff.fill(0);

        if len > self.max_buffer_size {
            return Err(Error::other("Max buff"));
        }
        return Ok(len)

    }

    pub fn marshal(&mut self, len: usize) -> &mut [u8; HEADER_LEN] {
        self.buff[0] = self.code as u8;
        self.buff[1..9].copy_from_slice(&len.to_ne_bytes());
        &mut self.buff
    }
}

