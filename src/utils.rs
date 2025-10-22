use std::ops::{Deref, DerefMut};
use std::time::{Duration, Instant};
use crate::peer_net::header::{HEADER_LEN, PREFIX_LEN};
use crate::spsc::Msg;
use std::net::SocketAddr;

use mio::Token;

use crate::peer_net::connection::PeerConnection;
use crate::crypto::random_b2;
use crate::peer_net::handlers::{Handler, PacketCode};
use std::sync::atomic::{AtomicBool, Ordering};

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

pub static SHUTDOWN: AtomicBool = AtomicBool::new(false);

pub fn should_shutdown() -> bool {
    SHUTDOWN.load(Ordering::SeqCst)
}

#[allow(unused)]
pub fn request_shutdown() {
    SHUTDOWN.store(true, Ordering::SeqCst);
}

///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum NetManCode {
    None,
    AddPeer,
    RemovePeer,
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum NetMsgCode {
    Internal(NetManCode),
    External(PacketCode),
}
impl NetMsgCode {
    pub fn is_internal(&self) -> bool {
        match self {
            NetMsgCode::Internal(_) => true,
            _ => false,
        }
    }
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct NetMsg(Box<NetMsgInner>);
impl Deref for NetMsg {
    type Target = Box<NetMsgInner>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}
impl DerefMut for NetMsg {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}
impl Msg for NetMsg {
    fn new(default_cap: Option<usize>) -> Self {
        NetMsg(Box::new(NetMsgInner::new(default_cap)))
    }
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub struct NetMsgInner {
    pub id: u16,
    pub from_code: Token,
    pub stream_token: Token,

    pub addr: Option<SocketAddr>,
    pub pub_key: Option<[u8;32]>,

    pub code: NetMsgCode,
    pub body: WriteBuffer,
    pub handler: Option<Handler>,
}

impl NetMsgInner {
    pub fn new(cap: Option<usize>) -> Self { 
        let mut s = Self { 
            id: u16::from_le_bytes(random_b2()),
            code: NetMsgCode::Internal(NetManCode::None),
            stream_token: Token(0), 
            from_code: Token(0),
            addr: None,
            body: WriteBuffer::new(cap.unwrap()),
            handler: None,
            pub_key: None,
        };
        s.reset();
        s
    }
    pub fn fill_fd_and_id(&mut self, conn: &mut PeerConnection) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_token = conn.token;
        self.from_code = Token(0);
        self.addr = None;
        self.pub_key = None;
    }
    pub fn reset(&mut self) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_token =  Token(0);
        self.from_code = Token(0);
        self.addr = None;
        self.pub_key = None;
        self.body.reset();
    }

    pub fn fill_for_internal(&mut self, from_code: Token, code: NetManCode, buff: &[u8]) {
        self.reset();
        self.from_code = from_code;
        self.code = NetMsgCode::Internal(code);
        self.body.extend_from_slice(buff);
    }
}

pub fn next_deadline(timeout: u64) -> Instant {
    Instant::now() + Duration::from_secs(timeout)
}

////////////////////////////////////////////////////////
////////////////////////////////////////////////////////


#[derive(PartialEq, Eq, Debug, Clone)]
pub enum NetError {
    ConnectionAborted,
    MalformedPrefix,
    Unauthorized,
    NegotiationFailed,
    Encryption(String),
    Decryption(String),
    SocketFailed,
    Other(String),

    PeerDbQ(String),
    PeerDbE(String),
}

impl NetError {
    pub fn to_score(&self) -> usize {
        match self {
            NetError::Unauthorized => 30,
            NetError::MalformedPrefix => 20,
            NetError::Decryption(_) => 10,
            _ => 0
        }
    }
}

pub type NetResult<T> = Result<T, NetError>;


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////


#[derive(PartialEq, Eq, Debug, Clone)]
pub struct WriteBuffer {
    buf: Vec<u8>,
}

impl WriteBuffer {
    pub fn new(cap: usize) -> Self {
        let mut s = Self { 
            buf: Vec::with_capacity(cap), 
        };
        s.reset();

        s
    }
    pub fn from_vec(buf: Vec<u8>) -> Self {
        Self { buf }
    }
    pub fn release_buffer(&mut self) -> Vec<u8> {
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.buf);
        read_buf
    }
    // resize back to PREFIX_LEN + HEADER_LEN
    pub fn reset(&mut self) {
        self.buf.resize(PREFIX_LEN + HEADER_LEN, 0);
    }
}

impl Deref for WriteBuffer {
    type Target = Vec<u8>;
    fn deref(&self) -> &Self::Target {
        &self.buf
    }
}
impl DerefMut for WriteBuffer {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.buf
    }
}
