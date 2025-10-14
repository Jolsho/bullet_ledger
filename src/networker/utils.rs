use std::ops::{Deref, DerefMut};
use crate::{msging::MsgCons, networker::{connection::Connection, header::{HEADER_LEN, PREFIX_LEN}}};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::time::{Duration, Instant};
use nix::sys::epoll::EpollFlags;

use std::os::fd::{AsFd, AsRawFd, RawFd};
use crate::crypto::random_b2;
use crate::networker::handlers::{Handler, PacketCode};

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
pub struct NetMsg {
    pub id: u16,
    pub from_code: i32,
    pub stream_fd: RawFd,

    pub addr: Option<SocketAddr>,
    pub pub_key: Option<[u8;32]>,

    pub code: NetMsgCode,
    pub body: WriteBuffer,
    pub handler: Option<Handler>,
}
pub const DEFAULT_BUFFER_SIZE:usize = 1024;

impl Default for NetMsg {
    fn default() -> Self { 
        let mut s = Self { 
            id: u16::from_le_bytes(random_b2()),
            code: NetMsgCode::Internal(NetManCode::None),
            stream_fd: -1, 
            from_code: -1,
            addr: None,
            body: WriteBuffer::new(),
            handler: None,
            pub_key: None,
        };
        s.reset();
        s
    }
}

impl NetMsg {
    pub fn fill_fd_and_id(&mut self, conn: &mut Connection) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_fd = conn.as_fd().as_raw_fd();
        self.from_code = 0;
        self.addr = None;
        self.pub_key = None;
    }
    pub fn reset(&mut self) {
        self.id = u16::from_le_bytes(random_b2());
        self.stream_fd = -1;
        self.from_code = 0;
        self.addr = None;
        self.pub_key = None;
        self.body.reset();
    }
}


////////////////////////////////////////////////////////
////////////////////////////////////////////////////////


pub struct Mappings {
    pub conns: HashMap<i32, Connection>,
    pub addrs: HashMap<SocketAddr, i32>,
}

impl Mappings {
    pub fn new(cap: usize) -> Self {
        Self { 
            conns: HashMap::with_capacity(cap), 
            addrs: HashMap::with_capacity(cap)
        }
    }
}

pub fn next_deadline(timeout: u64) -> Instant {
    Instant::now() + Duration::from_secs(timeout)
}
pub fn epoll_flags() -> EpollFlags {
    EpollFlags::EPOLLIN | EpollFlags::EPOLLHUP | EpollFlags::EPOLLERR
}
pub fn epoll_flags_write() -> EpollFlags {
    epoll_flags() | EpollFlags::EPOLLOUT
}

pub type Messengers = HashMap<i32, MsgCons<NetMsg>>;


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
    SocketFailed(String),
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
    pub fn new() -> Self {
        let mut s = Self { 
            buf: Vec::with_capacity(DEFAULT_BUFFER_SIZE), 
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
