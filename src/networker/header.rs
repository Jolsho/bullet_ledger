use chacha20poly1305::{aead::{AeadMutInPlace, OsRng}, AeadCore, ChaCha20Poly1305, Key, KeyInit};

use crate::{crypto::random_b2, networker::{connection::{NetError, NetResult}, handlers::Handler}};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PacketCode {
    None = 0,
    PingPong = 1,

    NegotiationSyn = 2,
    NegotiationAck = 3,
}

pub fn code_from_u8(dig: u8) -> PacketCode {
    match dig {
        1 => PacketCode::PingPong,
        2 => PacketCode::NegotiationSyn,
        3 => PacketCode::NegotiationAck,
        _ => PacketCode::None,
    }
}



// Length + Nonce + Tag
pub const PREFIX_LEN: usize = 8 + 12 + 16; 

// code + msg_id
pub const HEADER_LEN: usize = 1 + 2;

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct Header {
    pub code: PacketCode,
    pub response_handler: Option<Handler>,
    pub msg_id: u16,
    pub nonce: [u8; 12],
    pub tag: [u8; 16],
    pub is_marshalled: bool,
}

impl Header {
    pub fn new() -> Self {
        Self { 
            response_handler: None,
            msg_id: u16::from_le_bytes(random_b2()),
            code: PacketCode::None,
            nonce: [0u8; 12],
            tag: [0u8; 16],
            is_marshalled: false,
        }
    }

    pub fn raw_unmarshal(&mut self, buff: &[u8]) 
        -> NetResult<()> 
    {
        let mut cursor = PREFIX_LEN;
        self.code = code_from_u8(buff[cursor]);
        cursor += 1;

        let msg_id_bytes: [u8; 2] = buff[cursor..cursor + 2].try_into()
            .map_err(|_| NetError::Other("Invalid msg id".to_string()))?;
        self.msg_id = u16::from_le_bytes(msg_id_bytes);
        Ok(())
    }

    pub fn encrypt_unmarshal(&mut self, buff: &mut[u8], key: &Key) 
        -> NetResult<()> 
    {
        let mut cipher = ChaCha20Poly1305::new(key);
        cipher.decrypt_in_place_detached(
            &self.nonce.into(), 
            b"bullet_ledger", 
            &mut buff[HEADER_LEN..], 
            &self.tag.into(),
        ).map_err(|e| NetError::Decryption(e.to_string()))?;

        self.raw_unmarshal(&buff)
    }

    pub fn raw_marshal(&mut self, buff: &mut [u8]) {
        let len = buff.len() - HEADER_LEN - PREFIX_LEN;

        buff[0..9].copy_from_slice(&len.to_ne_bytes());
        buff[PREFIX_LEN] = self.code as u8;
        buff[PREFIX_LEN + 1..PREFIX_LEN + 3].copy_from_slice(&self.msg_id.to_le_bytes());
        self.is_marshalled = true;
    }

    pub fn encrypt_marshal(&mut self, buff: &mut [u8], key: &Key) 
        -> NetResult<()> 
    {
        let mut header_cipher = ChaCha20Poly1305::new(key);
        let nonce = ChaCha20Poly1305::generate_nonce(&mut OsRng);

        self.raw_marshal(buff);

        let tag = header_cipher.encrypt_in_place_detached(
            &nonce, b"bullet_ledger", &mut buff[PREFIX_LEN..]
        ).map_err(|e| NetError::Encryption(e.to_string()))?;

        buff[8..20].copy_from_slice(&nonce[..12]);
        buff[20..36].copy_from_slice(&tag[..16]);

        Ok(())
    }
}

