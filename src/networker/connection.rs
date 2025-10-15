use std::collections::{HashMap, VecDeque};
use std::net::SocketAddr;
use std::{io::{self, Read, Write},time::Instant};
use std::os::fd::AsRawFd;
use chacha20poly1305::aead::{AeadMutInPlace, OsRng};
use chacha20poly1305::{AeadCore, ChaCha20Poly1305, Key, KeyInit};
use mio::net::TcpStream;
use mio::{Interest, Poll, Token};
use sha2::{Digest, Sha256};
use zeroize::Zeroize;

use crate::crypto::{ random_b32, montgomery::{ecdh_shared_secret, hkdf_derive_key}};
use crate::networker::handlers::{code_switcher, Handler, HandlerRes, PacketCode};
use crate::networker::header::{Header, HEADER_LEN,PREFIX_LEN};
use crate::networker::utils::{next_deadline, Messengers, NetError, NetMsg, NetMsgCode, NetResult, WriteBuffer};

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum ConnDirection {
    Outbound,
    Inbound,
}

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

pub struct Connection {
    stream: TcpStream,
    pub token: Token,
    pub last_deadline: Instant,

    pub addr: SocketAddr,
    pub remote_pub_key: [u8;32],
    key: Key,
    is_negotiated: bool,
    is_acking: bool,

    outbound: VecDeque<Box<NetMsg>>,
    pub local_net_msgs: Vec<Box<NetMsg>>,
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

impl Drop for Connection {
    fn drop(&mut self) {
        self.key.zeroize();
    }
}

impl Connection {
    pub fn new( 
        stream: TcpStream, 
        addr: SocketAddr,
        net_man: &mut super::NetMan, 
        remote_pub_key: [u8; 32],
        dir: ConnDirection, 
    ) -> NetResult<Self> {
        let mut conn = Self {
            // META
            token: Token(stream.as_raw_fd() as usize), 
            stream, addr,
            last_deadline: next_deadline(net_man.config.idle_timeout),

            // ENCRYPTION
            remote_pub_key, 
            key: *Key::from_slice(&[0u8; 32]),
            is_negotiated: false,
            is_acking: false,

            // MSG QUEUES
            outbound: VecDeque::with_capacity(net_man.config.in_out_q_size),
            local_net_msgs: Vec::with_capacity(net_man.config.in_out_q_size),
            inbound_handlers: HashMap::with_capacity(net_man.config.in_out_q_size),

            // READER
            read_header: Header::new(),
            read_state: Some(ReadState::ReadingPrefix),
            read_buf: net_man.get_buff(),
            read_pos: 0,
            read_target: HEADER_LEN,

            // WRITER
            write_header: Header::new(),
            write_state: Some(WriteState::Idle),
            write_buf: WriteBuffer::from_vec(net_man.get_buff()),
            write_pos: 0,
        };

        if dir == ConnDirection::Outbound {
            if let Err(e) = conn.initiate_negotiation(net_man) {
                net_man.poll.registry().deregister(&mut conn.stream).ok();
                return Err(e);
            }
        }
        Ok(conn)
    }


    /// Return conns resources back to net_man
    /// aka mem::swap buffers with zero_cap Vecs
    /// and push them back into net_man.buffers
    pub fn strip_and_delete(&mut self, 
        net_man: &mut super::NetMan,
        messengers: &mut Messengers
    ) {
        let _ = net_man.poll.registry().deregister(&mut self.stream);
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.read_buf);
        net_man.put_buff(read_buf);

        let write_buf = self.write_buf.release_buffer();
        net_man.put_buff(write_buf);

        while let Some(mut msg) = self.outbound.pop_front() {
            if let Some(consumer) = messengers.get_mut(&msg.from_code) {
                consumer.recycle(msg);
            } else {
                net_man.buffs.push(msg.body.release_buffer());
            }
        }
    }

    /// gets a NetMsg from local_net_msgs
    pub fn get_new_msg(&mut self) -> Box<NetMsg> {
        let mut msg = self.local_net_msgs.pop(); 
        if msg.is_none() {
            msg = Some(Box::new(NetMsg::default()));
        }
        let mut msg = msg.unwrap();
        msg.fill_fd_and_id(self);
        msg.body.reset();
        msg
    }


    ///////////////////////////////////////////////////////////////////////////////
    // -- Reading ---------------------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////
    
    /// Read leng of read_buf advancing read_pos that far.
    pub fn read(&mut self, len: usize) -> &[u8] {
        let res = &self.read_buf[self.read_pos..self.read_pos+len];
        self.read_pos += len;
        res
    }

    pub fn on_readable(&mut self, net_man: &mut super::NetMan) -> NetResult<bool> {
        let mut did_work = false;
        loop {
            match self.read_state.take() {

                Some(ReadState::ReadingPrefix) => {
                    if self.read_buf.len() < PREFIX_LEN {
                        self.read_buf.resize(PREFIX_LEN, 0);
                    }

                    match self.stream.read(&mut self.read_buf[..PREFIX_LEN]) {
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
                    match self.stream.read(&mut self.read_buf[self.read_pos..self.read_target]) {
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
                        Some(handler) => handler(self, net_man),
                        None => code_switcher(self, net_man),
                    };

                    did_work = true;

                    self.read_buf.clear();
                    self.read_buf.resize(PREFIX_LEN, 0);
                    self.read_pos = 0;
                    self.read_target = PREFIX_LEN;
                    match res {
                        Ok(HandlerRes::Write(msg)) => {
                            if let Err(msg) = self.enqueue_msg(msg, net_man) {
                                self.local_net_msgs.push(msg);
                            } else {

                                // if writer is idle enable it
                                if self.write_state == Some(WriteState::Idle) {
                                    let _ = self.enable_writable(&mut net_man.poll);
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
        poll.registry().reregister(&mut self.stream, self.token, 
            Interest::READABLE | Interest::WRITABLE
        )
    }

    /// Removes EPOLLOUT flag to streams epoll event
    fn disable_writable(&mut self, poll: &mut Poll) -> io::Result<()> {
        poll.registry().reregister(&mut self.stream, self.token, 
            Interest::READABLE
        )
    }

    /// Enqueues an outgoing message to be delivered when available
    pub fn enqueue_msg(&mut self, msg: Box<NetMsg>, net_man: &mut super::NetMan) -> Result<(),Box<NetMsg>> {
        if self.outbound.len() == 0 {
            if self.enable_writable(&mut net_man.poll).is_err() {
                return Err(msg);
            }
        }
        Ok(self.outbound.push_back(msg))
    }

    pub fn on_writable(&mut self, 
        net_man: &mut super::NetMan,
        messengers: &mut Messengers
    ) -> NetResult<bool> {
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
                            if let Some(consumer) = messengers.get_mut(&msg.from_code) {
                                // Recycle msg
                                consumer.recycle(msg);
                            } else {
                                self.local_net_msgs.push(msg);
                            }
                            self.write_state = Some(WriteState::Idle);
                            continue;
                        }
                        self.write_header.response_handler = msg.handler.take();
                        self.write_header.msg_id = msg.id;

                        // switch buffers
                        std::mem::swap(&mut self.write_buf, &mut msg.body);
                       
                        // if our local supply isn't full fill it first.
                        // there is just less locally... so better use it...
                        if msg.from_code.0 > 0 {
                            if let Some(consumer) = messengers.get_mut(&msg.from_code) {
                                // Recycle msg
                                consumer.recycle(msg);
                            } else {
                                self.local_net_msgs.push(msg);
                            }
                        } else {
                            self.local_net_msgs.push(msg);
                        }
                        self.write_state = Some(WriteState::Writing);

                    } else {

                        let _ = self.disable_writable(&mut net_man.poll);
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
                        match self.stream.write(&self.write_buf[self.write_pos..]) {
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
                        let _ = self.disable_writable(&mut net_man.poll);
                    }
                    break;
                }


                None => return Err(NetError::Other("Writestate == None".to_string())),
            };
        }
        Ok(did_work)
    }


    ///////////////////////////////////////////////////////////////////////////////
    // -- CRYPTO NEGOTIATION -----------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////

    pub fn handle_negotiation(&mut self,
        net_man: &mut super::NetMan
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
            let mut hasher = Sha256::default();
            hasher.update(&remote_salt);
            hasher.update(&local_salt);
            let mut final_salt = hasher.finalize();
            remote_salt.zeroize();

            // derive shared secret
            let mut secret = ecdh_shared_secret(net_man.priv_key.clone(), self.remote_pub_key.clone());

            // derive the key and assign to conn.key
            self.key = Key::from_slice(&hkdf_derive_key(
                &secret, 
                b"bullet_ledger", 
                final_salt.into()
            )).to_owned();
            secret.zeroize();

            // get a message to fill out
            let mut msg = self.get_new_msg();

            // write the pubkey final salt
            msg.body.extend_from_slice(&net_man.pub_key);
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
            let mut hasher = Sha256::default();
            hasher.update(&local_salt);
            hasher.update(&remote_salt);
            let final_salt = hasher.finalize();
            local_salt.zeroize();
            remote_salt.zeroize();

            // derive shared secret
            let mut secret = ecdh_shared_secret(net_man.priv_key.clone(), self.remote_pub_key.clone());

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

            if local_salt != *final_salt {
                return Err(NetError::Unauthorized);
            }

            // upgrade
            self.is_negotiated = true;
            
            // re-enable so we can process our queued outgoing message(s)
            let _ = self.enable_writable(&mut net_man.poll);

            self.read_state = Some(ReadState::ReadingPrefix);
            self.write_state = Some(WriteState::Idle);

            return Ok(HandlerRes::None);
        }
    }

    pub fn initiate_negotiation(&mut self, net_man: &mut super::NetMan,) -> NetResult<()>{

        let mut msg = self.get_new_msg();
        let salt = random_b32();

        // Write pub_key, salt, and nonce
        msg.body.extend_from_slice(&net_man.pub_key);
        msg.body.extend_from_slice(&salt);

        // Set msg id, code, is_negotiated, and write_state
        msg.code = NetMsgCode::External(PacketCode::NegotiationSyn);

        self.is_negotiated = false;

        if let Err(mut m) = self.enqueue_msg(msg, net_man) {
            m.reset();
            self.local_net_msgs.push(m);
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

    ///////////////////////////////////////////////////////////////////////////////
    // -- ERROR ---------------------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////

    #[allow(unused)]
    pub fn on_error(&mut self, _net_man: &mut super::NetMan) -> io::Result<bool> {
        println!("ERROR");
        Err(io::Error::other("Socket error"))
    }

    ///////////////////////////////////////////////////////////////////////////////
    // -- HANGUP ---------------------------------------------------------------
    ///////////////////////////////////////////////////////////////////////////////
    
    #[allow(unused)]
    pub fn on_hangup(&mut self, _net_man: &mut super::NetMan) -> io::Result<bool> {
        println!("HUNG");
        Err(io::Error::other("Socket hangup"))
    }
}

