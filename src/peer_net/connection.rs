use std::collections::{HashMap, VecDeque};
use std::net::SocketAddr;
use std::{io::{self, Read, Write},time::Instant};
use std::os::fd::AsRawFd;
use chacha20poly1305::aead::{AeadMutInPlace, OsRng};
use chacha20poly1305::{AeadCore, ChaCha20Poly1305, Key, KeyInit};
use mio::net::TcpStream;
use mio::{Interest, Poll, Token};
use zeroize::Zeroize;

use crate::crypto::{ random_b32, montgomery::{ecdh_shared_secret, hkdf_derive_key}};
use crate::peer_net::handlers::{code_switcher, Handler, HandlerRes, PacketCode};
use crate::peer_net::header::{Header, HEADER_LEN,PREFIX_LEN};
use crate::utils::{next_deadline, NetError, NetMsg, NetMsgCode, NetResult, WriteBuffer};
use crate::server::{NetServer, TcpConnection};

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum ReadState {
    ReadingPrefix,
    Reading,
    Processing,
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum WriteState {
    Idle,
    Writing,
}

pub struct PeerConnection {
    stream: Option<TcpStream>,
    pub token: Token,
    pub last_deadline: Instant,

    pub addr: Option<SocketAddr>,
    pub remote_pub_key: [u8;32],
    key: Key,
    is_negotiated: bool,
    is_acking: bool,

    outbound: VecDeque<NetMsg>,
    inbound_handlers: HashMap<u16, Handler>,

    pub read_header: Header,
    read_state: Option<ReadState>,
    pub read_buf: Vec<u8>,
    pub read_pos: usize, 
    read_target: usize,

    pub write_header: Header,
    write_state: Option<WriteState>,
    write_buf: WriteBuffer,
    write_pos: usize,
}

impl Drop for PeerConnection {
    fn drop(&mut self) {
        self.key.zeroize();
    }
}

impl TcpConnection for PeerConnection {
    fn new(server: &mut NetServer<Self>) -> Box<Self> {
        let conn = Self {
            // META
            token: Token(0), 
            stream: None,
            addr: None,
            last_deadline: next_deadline(server.idle_timeout),

            // ENCRYPTION
            remote_pub_key: [0u8; 32], 
            key: *Key::from_slice(&[0u8; 32]),
            is_negotiated: false,
            is_acking: false,

            // MSG QUEUES
            outbound: VecDeque::with_capacity(server.conn_q_cap),
            inbound_handlers: HashMap::with_capacity(server.conn_q_cap),

            // READER
            read_header: Header::new(),
            read_state: Some(ReadState::ReadingPrefix),
            read_buf: server.get_buff(),
            read_pos: 0,
            read_target: HEADER_LEN,

            // WRITER
            write_header: Header::new(),
            write_state: Some(WriteState::Idle),
            write_buf: WriteBuffer::from_vec(server.get_buff()),
            write_pos: 0,
        };
        Box::new(conn)
    }
    fn get_stream(&mut self) -> &mut TcpStream { self.stream.as_mut().unwrap() }
    fn get_addr(&self) -> SocketAddr { self.addr.unwrap().clone() }
    fn get_last_deadline(&self) -> Instant { self.last_deadline }
    fn set_last_deadline(&mut self, deadline: Instant) { self.last_deadline = deadline; }

    fn initialize(&mut self, 
        stream: TcpStream, 
        addr: SocketAddr, 
        mut pub_key: Option<[u8;32]>,  
        server: &mut NetServer<Self>,
    ) {
        if let Some(key) = pub_key.take() {
            self.remote_pub_key = key;
        }
        self.token = Token(stream.as_raw_fd() as usize);
        self.addr = Some(addr);
        self.stream = Some(stream);
        self.write_buf = WriteBuffer::from_vec(server.get_buff());
        self.read_buf = server.get_buff();
        self.last_deadline = Instant::now();
        self.read_state = Some(ReadState::ReadingPrefix);
        self.write_state= Some(WriteState::Idle);
    }

    /// Return conns resources back to net_man
    /// aka mem::swap buffers with zero_cap Vecs
    /// and push them back into net_man.buffers
    fn reset(&mut self, server: &mut NetServer<Self>) {
        if let Some(mut s) = self.stream.take() {
            let _ = server.poll.registry().deregister(&mut s);
        }
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.read_buf);
        server.put_buff(read_buf);

        let write_buf = self.write_buf.release_buffer();
        server.put_buff(write_buf);

        while let Some(msg) = self.outbound.pop_front() {
            server.put_msg(msg);
        }

        self.stream = None;
        self.token = Token(0);
        self.addr = None;
        self.remote_pub_key.fill(0);
        self.key.zeroize();
        self.is_negotiated = false;
        self.is_acking = false;
        self.inbound_handlers.clear();

        self.read_header.reset();
        self.read_state = None;
        self.read_pos = 0;
        self.read_target = 0;

        self.write_header.reset();
        self.write_state = None;
        self.write_pos = 0;
    }

    ///////////////////////////////////////////////////////////////////////////////
    // -- Reading ---------------------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////
    fn on_readable(&mut self, server: &mut NetServer<Self>) -> NetResult<bool> {
        let mut did_work = false;
        loop {
            match self.read_state.take() {

                Some(ReadState::ReadingPrefix) => {
                    if self.read_buf.len() < PREFIX_LEN {
                        self.read_buf.resize(PREFIX_LEN, 0);
                    }

                    match self.stream.as_mut().unwrap().read(&mut self.read_buf[..PREFIX_LEN]) {
                        Ok(0) => return Err(NetError::ConnectionAborted),
                        Ok(n) => {
                            did_work = true;
                            self.read_pos += n;

                            if self.read_pos < PREFIX_LEN { 
                                self.read_state = Some(ReadState::ReadingPrefix);
                                continue; 
                            }

                            // Read LENGTH of encrypted load
                            let len = usize::from_le_bytes(
                                self.read_buf[..8].try_into()
                                .map_err(|_| NetError::MalformedPrefix)?
                            );


                            // Read NONCE of encrypted load
                            self.read_header.nonce.copy_from_slice(
                                &self.read_buf[8..20]
                            );

                            // Read AUTH_TAG of encrypted load
                            self.read_header.tag.copy_from_slice(
                                &self.read_buf[20..36]
                            );

                            // Prepare to read load
                            self.read_buf.clear();
                            self.read_buf.resize(len, 0);
                            self.read_pos = 0;
                            self.read_target = len;
                            self.read_state = Some(ReadState::Reading);
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            self.read_state = Some(ReadState::ReadingPrefix);
                            break;
                        },
                        Err(e) => return Err(NetError::Other(e.to_string())),
                    }
                }

                //==================================================================
                //==================================================================

                Some(ReadState::Reading) => {
                    match self.stream.as_mut().unwrap().read(&mut self.read_buf[self.read_pos..self.read_target]) {
                        Ok(0) => { return Err(NetError::ConnectionAborted); }
                        Ok(n) => {
                            did_work = true;
                            self.read_pos += n;
                            if self.read_pos < self.read_target { 
                                self.read_state = Some(ReadState::Reading);
                                continue; 
                            }

                            if self.is_negotiated {
                                self.read_header.encrypt_unmarshal(
                                    &mut self.read_buf, &self.key
                                )?;
                            } else {
                                self.read_header.raw_unmarshal(
                                    &mut self.read_buf
                                )?;
                            }

                            
                            // full message received
                            self.read_state = Some(ReadState::Processing);
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            self.read_state = Some(ReadState::Reading);
                            break
                        },
                        Err(e) => return Err(NetError::Other(e.to_string())),
                    }
                }

                //==================================================================
                //==================================================================

                Some(ReadState::Processing) => {

                    self.read_pos = HEADER_LEN;

                    // if not negotiated and attempting to do anything that is not negotiating
                    if !self.is_negotiated && 
                    self.read_header.code != PacketCode::NegotiationAck &&
                    self.read_header.code != PacketCode::NegotiationSyn {
                        return Err(NetError::Unauthorized);
                    }


                    // Handle the packet
                    let res = match self.inbound_handlers.remove(&self.read_header.msg_id) {
                        Some(handler) => handler(self, server),
                        None => code_switcher(self, server),
                    };

                    did_work = true;

                    self.read_buf.clear();
                    self.read_buf.resize(PREFIX_LEN, 0);
                    self.read_pos = 0;
                    self.read_target = PREFIX_LEN;
                    match res {
                        Ok(HandlerRes::Write(msg)) => {
                            if let Err(msg) = self.enqueue_msg(msg, server) {
                                server.put_msg(msg);
                            } else {

                                // if writer is idle enable it
                                if self.write_state == Some(WriteState::Idle) {
                                    let _ = self.enable_writable(&mut server.poll);
                                }
                            }
                        }
                        Ok(HandlerRes::Read((msg_id, handler))) => {
                            self.inbound_handlers.insert(msg_id, handler);
                        },
                        Ok(HandlerRes::None) => {}
                        Err(e) => return Err(e),
                    }
                    self.read_state = Some(ReadState::ReadingPrefix);
                    break;
                },

                None => return Err(NetError::Other("ReadState == None".to_string())),
            }
        }

        Ok(did_work)
    }


    ///////////////////////////////////////////////////////////////////////////////
    // -- Writing ---------------------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////

    /// Adds EPOLLOUT flag to streams epoll event
    fn enable_writable(&mut self, poll: &mut Poll) -> io::Result<()> {
        poll.registry().reregister(self.stream.as_mut().unwrap(), self.token, 
            Interest::READABLE | Interest::WRITABLE
        )
    }

    /// Removes EPOLLOUT flag to streams epoll event
    fn disable_writable(&mut self, poll: &mut Poll) -> io::Result<()> {
        poll.registry().reregister(self.stream.as_mut().unwrap(), self.token, 
            Interest::READABLE
        )
    }

    /// Enqueues an outgoing message to be delivered when available
    fn enqueue_msg(&mut self, msg: NetMsg, server: &mut NetServer<Self>) -> Result<(),NetMsg> {
        if self.outbound.len() == 0 {
            if self.enable_writable(&mut server.poll).is_err() {
                return Err(msg);
            }
        }
        Ok(self.outbound.push_back(msg))
    }

    fn on_writable(&mut self, server: &mut NetServer<Self>) -> NetResult<bool> {
        let mut did_work = false;
        loop {
            match self.write_state.take() {

                Some(WriteState::Idle) => {
                    // If there appears to be nothing to write
                    // check outbound buffer for queued msgs...
                    if let Some(mut msg) = self.outbound.pop_front() {

                        if self.outbound.len() < self.outbound.capacity() / 2 {
                            self.outbound.shrink_to_fit();
                        }

                        if let NetMsgCode::External(c) = msg.code {
                            self.write_header.code = c;

                        } else {
                            server.put_msg(msg);
                            self.write_state = Some(WriteState::Idle);
                            continue;
                        }

                        self.write_header.response_handler = msg.handler.take();
                        self.write_header.msg_id = msg.id;

                        // switch buffers
                        std::mem::swap(&mut self.write_buf, &mut msg.body);
                       
                        server.put_msg(msg);
                        self.write_state = Some(WriteState::Writing);

                    } else {

                        let _ = self.disable_writable(&mut server.poll);
                        self.write_state = Some(WriteState::Idle);
                        return Ok(false)
                    }
                }

                //==================================================================
                //==================================================================

                Some(WriteState::Writing) => {
                    if self.is_negotiated && !self.write_header.is_marshalled {
                        self.write_header.encrypt_marshal(&mut *self.write_buf, &self.key)?;
                    } else if !self.write_header.is_marshalled {
                        self.write_header.raw_marshal(&mut *self.write_buf);
                    }

                    while self.write_pos < self.write_buf.len() {
                        match self.stream.as_mut().unwrap().write(&self.write_buf[self.write_pos..]) {
                            Ok(0) => {return Err(NetError::ConnectionAborted)},
                            Ok(n) => {
                                did_work = true;
                                self.write_pos += n;
                            },
                            Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => break,
                            Err(e) => return Err(NetError::Other(e.to_string())),
                        }
                    }
                    if self.write_pos < self.write_buf.len() {
                        self.write_state = Some(WriteState::Writing);
                        break;
                    }

                    self.write_buf.reset();
                    self.write_pos = 0;
                    self.write_header.is_marshalled = false;

                    if let Some(res_handle) = self.write_header.response_handler.take() {
                        self.inbound_handlers.insert(self.write_header.msg_id, res_handle);
                    }

                    if self.is_acking {
                        self.is_acking = false;
                        self.is_negotiated = true;
                    }

                    self.write_state = Some(WriteState::Idle);
                    // if no more messages queued Or wait until negotiation has finsihed
                    if self.outbound.len() == 0 || !self.is_negotiated {
                        let _ = self.disable_writable(&mut server.poll);
                    }
                    break;
                }


                None => return Err(NetError::Other("Writestate == None".to_string())),
            };
        }
        Ok(did_work)
    }

    fn initiate_negotiation(&mut self, server: &mut NetServer<Self>) -> NetResult<()> {
        let mut msg = server.get_new_msg();
        msg.fill_fd_and_id(self);
        let salt = random_b32();

        // Write pub_key, salt, and nonce
        msg.body.extend_from_slice(&server.pub_key);
        msg.body.extend_from_slice(&salt);

        // Set msg id, code, is_negotiated, and write_state
        msg.code = NetMsgCode::External(PacketCode::NegotiationSyn);

        self.is_negotiated = false;

        if let Err(mut m) = self.enqueue_msg(msg, server) {
            m.reset();
            server.put_msg(m);
            return Err(NetError::NegotiationFailed);
        }

        // copy this because its needed on the SYNACK
        self.remote_pub_key.copy_from_slice(&salt);

        // Will loop back into WRITING
        // since is_negotiated == false it wont encrypt
        // and it will disable_writable
        // then we wait on the PacketCode::NegotiationAck response
        // handling that will re-enable writable
        // which will then process the queued message(s)
        
        Ok(())
    }
}

impl PeerConnection {
    /// Read leng of read_buf advancing read_pos that far.
    pub fn read(&mut self, len: usize) -> &[u8] {
        let res = &self.read_buf[self.read_pos..self.read_pos+len];
        self.read_pos += len;
        res
    }

    ///////////////////////////////////////////////////////////////////////////////
    // -- CRYPTO NEGOTIATION -----------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////

    pub fn handle_negotiation(&mut self,
        server: &mut NetServer<Self>
    ) -> NetResult<HandlerRes> {
        if self.read_header.code == PacketCode::NegotiationSyn {
        // RECEIVING SYN
        // SENDING SYNACK
            
            self.read_pos = HEADER_LEN;

            // recover remote public key
            self.remote_pub_key.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos + 32]
            );
            self.read_pos += 32;

            // recover initiator salt
            let mut remote_salt = [0u8;32];
            remote_salt.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos+32]
            );
            self.read_pos += 32;

            // derive final salt
            let mut local_salt = random_b32();
            let mut hasher = blake3::Hasher::new();
            hasher.update(&remote_salt);
            hasher.update(&local_salt);
            let mut final_salt = *hasher.finalize().as_bytes();
            remote_salt.zeroize();

            // derive shared secret
            let mut secret = ecdh_shared_secret(server.priv_key.clone(), self.remote_pub_key.clone());

            // derive the key and assign to conn.key
            self.key = Key::from_slice(&hkdf_derive_key(
                &secret, 
                b"bullet_ledger", 
                final_salt.into()
            )).to_owned();
            secret.zeroize();

            // get a message to fill out
            let mut msg = server.get_new_msg();
            msg.fill_fd_and_id(self);

            // write the pubkey final salt
            msg.body.extend_from_slice(&server.pub_key);
            msg.body.extend_from_slice(&local_salt);
            local_salt.zeroize();

            let nonce = ChaCha20Poly1305::generate_nonce(&mut OsRng);
            let tag = ChaCha20Poly1305::new(&self.key)
                .encrypt_in_place_detached(
                    &nonce, 
                    b"bullet_ledger", 
                    &mut final_salt,
                ).map_err(|e| NetError::Encryption(e.to_string()))?;
            msg.body.extend_from_slice(&nonce);
            msg.body.extend_from_slice(&tag);
            msg.body.extend_from_slice(&final_salt);

            self.is_acking = true;

            // Set msg id, code, is_negotiated, and write_state
            msg.code = NetMsgCode::External(PacketCode::NegotiationAck);

            return Ok(HandlerRes::Write(msg));

        } else {
        // RECEIVING ACK
        
            self.read_pos = HEADER_LEN;

            // grab local slot from remote_pub_key slot
            let mut local_salt = self.remote_pub_key.clone();

            // recover remote public key
            self.remote_pub_key.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos + 32]
            );
            self.read_pos += 32;

            let mut remote_salt = [0u8; 32];
            remote_salt.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos+32]
            );
            self.read_pos += 32;

            // derive hashed_salt
            let mut hasher = blake3::Hasher::new();
            hasher.update(&local_salt);
            hasher.update(&remote_salt);
            let final_salt = hasher.finalize();
            local_salt.zeroize();
            remote_salt.zeroize();

            // derive shared secret
            let mut secret = ecdh_shared_secret(server.priv_key.clone(), self.remote_pub_key.clone());

            // derive the key and assign to conn.key
            self.key = Key::from_slice(&hkdf_derive_key(
                &secret, 
                b"bullet_ledger", 
                final_salt.into()
            )).to_owned();
            secret.zeroize();

            self.read_header.nonce.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos+12]
            );
            self.read_pos += 12;

            self.read_header.tag.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos+16]
            );
            self.read_pos += 16;

            local_salt.copy_from_slice(
                &self.read_buf[self.read_pos..self.read_pos+32]
            );
            self.read_pos += 32;

            ChaCha20Poly1305::new(&self.key)
                .decrypt_in_place_detached(
                    &self.read_header.nonce.into(), 
                    b"bullet_ledger", 
                    &mut local_salt, 
                    &self.read_header.tag.into(),
                ).map_err( |e| NetError::Decryption(e.to_string()))?;

            if local_salt != *final_salt.as_bytes() {
                return Err(NetError::Unauthorized);
            }

            // upgrade
            self.is_negotiated = true;
            
            // re-enable so we can process our queued outgoing message(s)
            let _ = self.enable_writable(&mut server.poll);

            self.read_state = Some(ReadState::ReadingPrefix);
            self.write_state = Some(WriteState::Idle);

            return Ok(HandlerRes::None);
        }
    }
}
