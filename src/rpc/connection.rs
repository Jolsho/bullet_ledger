use std::{collections::VecDeque, io::{self, Read, Write}, os::fd::AsRawFd, time::Instant, usize};
use mio::{net::TcpStream, Interest, Poll, Token};
use crate::{msging::MsgProd, networker::utils::{NetError, NetResult, WriteBuffer}}; 
use crate::networker::utils::{NetManCode, NetMsg};
use super::server::Server; 
use crate::RPC;

pub enum RpcCodes {
    None,
    AddPeer,
    RemovePeer,
}

impl RpcCodes {
    pub fn from_byte(b: u8) -> Self {
        match b {
            1 => Self::AddPeer,
            2 => Self::RemovePeer,
            _ => Self::None,
        }
    }
}

const HEADER_LEN: usize = 1 + 8;

pub struct RpcConn {
    pub stream: Option<TcpStream>,
    token: Token,
    pub last_deadline: Instant,

    code: Option<RpcCodes>,

    read_state: Option<RpcReadStates>,
    read_buffer: Vec<u8>,
    read_pos: usize,

    outbound: VecDeque<WriteBuffer>,
    write_state: Option<RpcWriteStates>,
    write_buffer: WriteBuffer,
    write_pos: usize,

}

pub enum RpcReadStates {
    ReadingHeader,
    ReadingBody,
    Processing,
}

pub enum RpcWriteStates {
    Idle,
    Writing,
}

impl RpcConn {
    pub fn new(server: &mut Server) -> Self {
        Self { 
            token: Token(0),
            stream: None, 
            last_deadline: Instant::now(),

            code: Some(RpcCodes::None),

            read_state: Some(RpcReadStates::ReadingHeader),
            read_pos: 0,
            read_buffer: server.get_buff(), 

            write_state: Some(RpcWriteStates::Idle),
            write_pos: 0,
            write_buffer: WriteBuffer::new(),
            outbound: VecDeque::with_capacity(10)
        }

    }

    pub fn reset(&mut self, server: &mut Server) {
        let _ = server.poll.registry()
            .deregister(self.stream.as_mut().unwrap());

        self.code = None;
        self.read_state = Some(RpcReadStates::ReadingHeader);
        self.read_pos = 0;
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.read_buffer);
        server.put_buff(read_buf);

        self.write_pos = 0;
        self.write_state = Some(RpcWriteStates::Idle);
        let mut write_buf = Vec::with_capacity(0);
        std::mem::swap(&mut write_buf, &mut self.write_buffer.release_buffer());
        server.put_buff(write_buf);

        while self.outbound.len() > 0 {
            let mut buff = self.outbound.pop_front().unwrap();
            let mut write_buf = Vec::with_capacity(0);
            std::mem::swap(&mut write_buf, &mut buff.release_buffer());
            server.put_buff(write_buf);
        }

        self.token = Token(0);
        let _ = self.stream.take();
    }

    pub fn initialize(&mut self, stream: TcpStream, server: &mut Server) {
        self.token = Token(stream.as_raw_fd() as usize);
        self.stream = Some(stream);
        self.write_buffer = WriteBuffer::from_vec(server.get_buff());
        self.read_buffer = server.get_buff();
        self.last_deadline = Instant::now();
    }

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

    pub fn handle_read(&mut self, to_net: &mut MsgProd<NetMsg>) -> NetResult<bool> {
        let mut did_work = false;
        loop {
            match self.read_state.take() {
                Some(RpcReadStates::ReadingHeader) => {
                    if self.read_buffer.len() < HEADER_LEN {
                        self.read_buffer.resize(HEADER_LEN, 0);
                    }
                    match self.stream.as_mut().unwrap().read(&mut self.read_buffer[self.read_pos..HEADER_LEN]) {
                        Ok(0) => return Err(NetError::ConnectionAborted),
                        Ok(n) => {
                            self.read_pos += n;
                            did_work = true;
                            if self.read_pos < HEADER_LEN {
                                self.read_state = Some(RpcReadStates::ReadingHeader);
                                break;
                            }
                        }
                        Err(e) if e.kind() == io::ErrorKind::WouldBlock => break,
                        Err(e) => return Err(NetError::Other(e.to_string())),
                    }
                    
                    self.code = Some(RpcCodes::from_byte(self.read_buffer[0]));

                    let len_buf: [u8; 8] = self.read_buffer[1..9].try_into()
                        .map_err(|_| NetError::MalformedPrefix)?;
                    let length = usize::from_le_bytes(len_buf);


                    self.read_buffer.resize(length, 0);
                    self.read_pos = 0;
                    self.read_state = Some(RpcReadStates::ReadingBody);
                }

                Some(RpcReadStates::ReadingBody) => {
                    match self.stream.as_mut().unwrap().read(&mut self.read_buffer[self.read_pos..]) {
                        Ok(0) => return Err(NetError::ConnectionAborted),
                        Ok(n) => {
                            self.read_pos += n;
                            did_work = true;
                            if self.read_pos < self.read_buffer.len() {
                                self.read_state = Some(RpcReadStates::ReadingBody);
                                break;
                            }
                        }
                        Err(e) if e.kind() == io::ErrorKind::WouldBlock => break,
                        Err(e) => return Err(NetError::Other(e.to_string())),
                    }
                    self.read_pos = 0;
                    self.read_state = Some(RpcReadStates::Processing);
                }

                Some(RpcReadStates::Processing) => {
                    match self.code.take() {
                        Some(RpcCodes::AddPeer) => {
                            did_work = true;
                            let mut msg = to_net.collect();
                            msg.fill_for_internal(
                                RPC, NetManCode::AddPeer, &self.read_buffer
                            );
                            if let Err(_msg) = to_net.push(msg) {

                            }
                        }
                        Some(RpcCodes::RemovePeer) => {
                            did_work = true;
                            let mut msg = to_net.collect();
                            msg.fill_for_internal(
                                RPC, NetManCode::RemovePeer, &self.read_buffer
                            );
                            if let Err(_msg) = to_net.push(msg) {

                            }
                        }
                        _ => {}
                    }

                    self.read_pos = 0;
                    self.read_state = Some(RpcReadStates::ReadingHeader);
                }

                None => return Err(NetError::Other("RpcReadState === None".to_string())),
            }
        }
        Ok(did_work)
    }

    pub fn handle_write(&mut self, server: &mut Server) -> NetResult<bool> {
        let mut did_work = false;
        loop {
            match self.write_state.take() {
                Some(RpcWriteStates::Idle) => {
                    if let Some(mut b) = self.outbound.pop_front() {
                        std::mem::swap(&mut self.write_buffer, &mut b);
                        server.put_buff(b.release_buffer());
                        did_work = true;

                        self.write_pos = 0;

                        let _ = self.enable_writable(&mut server.poll);
                        self.write_state = Some(RpcWriteStates::Writing);

                    } else {

                        let _ = self.disable_writable(&mut server.poll);
                        self.write_state = Some(RpcWriteStates::Idle);
                        break;
                    }

                }
                Some(RpcWriteStates::Writing) => {
                    match self.stream.as_mut().unwrap().write(&mut self.write_buffer[self.write_pos..]) {
                        Ok(0) => return Err(NetError::ConnectionAborted),
                        Ok(n) => {
                            self.write_pos += n;
                            did_work = true;
                            if self.write_pos < self.write_buffer.len() {
                                self.write_state = Some(RpcWriteStates::Writing);
                                break;
                            }
                        }
                        Err(e) if e.kind() == io::ErrorKind::WouldBlock => break,
                        Err(e) => return Err(NetError::Other(e.to_string())),
                    }
                    self.write_state = Some(RpcWriteStates::Idle);
                }
                None => return Err(NetError::Other("RpcWriteState === None".to_string())),
            }
        }
        Ok(did_work)
    }
}
