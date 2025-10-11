use std::collections::HashMap;
use std::io::{self, Error, Read, Write};
use std::net::TcpStream;
use std::os::fd::AsRawFd;
use std::time::Instant;
use nix::sys::epoll::EpollEvent;
use nix::sys::socket::{getsockopt, sockopt};
use crate::msging::{Poller, RingCons};
use crate::networker::handlers::code_switcher;
use crate::networker::header::{Header, HEADER_LEN};
use crate::networker::netman::NetMsg;
use crate::networker::{epoll_flags, epoll_flags_write, next_deadline, Messengers, NetMan};

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum ConnDirection {
    Outbound,
    Inbound,
}

#[derive(PartialEq, Eq, Debug, Clone)]
pub enum ConnState {
    ReadingHeader(Option<Handler>),
    ReadingBody(Option<Handler>),
    Processing(Option<Handler>),
    Writing(Option<Box<ConnState>>),
    Failed(String),
    Negotiating(ConnDirection),
}
pub type Handler = fn(&mut Connection, &mut NetMan) -> ConnState;

pub struct Connection {
    pub stream: TcpStream,
    pub fd: i32,
    pub state: Option<ConnState>,
    pub last_deadline: Instant,

    pub header: Header,

    pub read_buf: Vec<u8>,
    pub read_pos: usize, 
    pub read_target: usize,

    pub write_buf: Vec<u8>,
    pub write_pos: usize,
    pub outbound: Vec<Box<NetMsg>>
}

impl Connection {
    pub fn new(stream: TcpStream, net_man: &mut NetMan, dir: ConnDirection) -> Result<Self, TcpStream> {
        Ok(Self {
            header: Header::new(net_man.config.max_buffer_size),
            fd: stream.as_raw_fd(),
            stream,
            read_buf: net_man.get_buff(),
            write_buf: net_man.get_buff(),
            read_pos: 0,
            read_target: HEADER_LEN,
            write_pos: 0,
            state: Some(ConnState::Negotiating(dir)),
            last_deadline: next_deadline(net_man.config.idle_timeout),
            outbound: Vec::with_capacity(3),
        })
    }

    /// CALLS getsockopt and return io::Result corresponding
    pub fn check_socket_error(&mut self) -> io::Result<()> {
        match getsockopt(&self.stream, sockopt::SocketError) {
            Ok(0) => Ok(()),
            Ok(err) => Err(io::Error::from_raw_os_error(err)),
            Err(e) => Err(io::Error::new(io::ErrorKind::Other, e)),
        }
    }

    // -- Reading ---------------------------------------------------------------
    pub fn read(&mut self, len: usize) -> &[u8] {
        let res = &self.read_buf[self.read_pos..self.read_pos+len];
        self.read_pos += len;
        res
    }

    pub fn on_readable(&mut self, net_man: &mut NetMan) -> io::Result<bool> {
        let mut did_work = false;
        loop {
            match self.state.take() {
                Some(ConnState::Negotiating(direction)) => {
                    match direction {
                        ConnDirection::Inbound => {
                            // Receive initial thing...
                            // send out your own thing
                            // and change state to expect test
                        },
                        ConnDirection::Outbound => {
                            // send out initial thing...
                            // change state to receive response
                            // and then send back
                        },
                    };


                    self.state = Some(ConnState::ReadingHeader(None)); // FOR NOW
                    
                    // TODO
                    /*
                    *   this means we are either recieving a SYN
                    *       which means we need to send a SYN/ACK
                    *
                    *   or we have sent a SYN and this is a SYN/ACK
                    *   
                    *   either way we need to finish by upgrading connState
                    *
                    *
                    *   perhaps we can add something to negotiating state...
                    *   just something to keep track of where we are...
                    *       like if we create the conn object through dialing or not
                    *
                    */
                }

                //==================================================================
                //==================================================================
                
                Some(ConnState::ReadingHeader(handler)) => {
                    if self.read_buf.len() < HEADER_LEN {
                        self.read_buf.resize(HEADER_LEN, 0);
                    }

                    match self.stream.read(&mut self.read_buf[..HEADER_LEN]) {
                        Ok(0) => { return Err(io::Error::new(io::ErrorKind::ConnectionAborted, "peer closed")); }
                        Ok(n) => {
                            did_work = true;
                            self.read_pos += n;

                            if self.read_pos < HEADER_LEN { 
                                self.state = Some(ConnState::ReadingHeader(handler));
                                continue; 
                            }

                            match self.header.unmarshal(&self.read_buf) {
                                Ok(len) => {
                                    // Prepare to read body
                                    self.read_buf.clear();
                                    self.read_buf.resize(len, 0);
                                    self.read_pos = 0;
                                    self.read_target = len;
                                    self.state = Some(ConnState::ReadingBody(handler));
                                }
                                Err(e) => {
                                    self.state = Some(ConnState::Failed(e.to_string()));
                                }
                            }
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            self.state = Some(ConnState::ReadingHeader(handler));
                            break;
                        },
                        Err(e) => return Err(e),
                    }
                }

                //==================================================================
                //==================================================================

                Some(ConnState::ReadingBody(handler)) => {
                    match self.stream.read(&mut self.read_buf[self.read_pos..self.read_target]) {
                        Ok(0) => { return Err(io::Error::new(io::ErrorKind::ConnectionAborted, "peer closed")); }
                        Ok(n) => {
                            did_work = true;
                            self.read_pos += n;
                            if self.read_pos < self.read_target { 
                                self.state = Some(ConnState::ReadingBody(handler));
                                continue; 
                            }

                            // full message received
                            self.state = Some(ConnState::Processing(handler));
                        }
                        Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => {
                            self.state = Some(ConnState::ReadingBody(handler));
                            break
                        },
                        Err(e) => return Err(e),
                    }
                }

                //==================================================================
                //==================================================================

                Some(ConnState::Processing(callback)) => {
                    self.read_pos = 0;
                    self.state = match callback {
                        Some(handler) => Some(handler(self, net_man)),
                        None => Some(code_switcher(self, net_man)),
                    };
                    did_work = true;

                    self.read_buf.clear();
                    self.read_buf.resize(HEADER_LEN, 0);
                    self.read_pos = 0;
                    self.read_target = HEADER_LEN;
                    match &self.state {
                        Some(ConnState::Writing(_)) => {
                            let _ = self.enable_writable(&mut net_man.epoll);
                        }
                        Some(ConnState::Failed(reason)) => {
                            return Err(Error::other(reason.clone()));
                        }
                        _ => {}
                    }
                    break;
                },

                //==================================================================
                //==================================================================

                Some(ConnState::Writing(next)) => { 
                    self.state = Some(ConnState::Writing(next));
                    break; 
                }

                Some(ConnState::Failed(reason)) => return Err(Error::other(reason)),
                None => return Err(Error::other("Connstate == None".to_string())),
            }
        }

        Ok(did_work)
    }


    // -- Writing ---------------------------------------------------------------
    pub fn queue_write(&mut self, data: &[u8]) {
        self.write_buf.extend_from_slice(data);
    }

    fn enable_writable(&mut self, epoll: &mut Poller) -> nix::Result<()> {
        epoll.modify(&self.stream, &mut EpollEvent::new(
            epoll_flags_write(), self.fd as u64,
        ))
    }

    fn disable_writable(&mut self, epoll: &mut Poller) -> nix::Result<()> {
        epoll.modify(&self.stream, &mut EpollEvent::new(
            epoll_flags(), self.fd as u64
        ))
    }

    /// Enqueues an outgoing message to be sent at a later time
    pub fn queue_msg(&mut self, msg: Box<NetMsg>, epoll: &mut Poller,
    ) -> Result<(),Box<NetMsg>> {
        if self.enable_writable(epoll).is_err() {
            return Err(msg);
        }
        Ok(self.outbound.push(msg))
    }


    const HEADER:u8 = 1;
    const BODY:u8 = 2;
    pub fn on_writable(&mut self, 
        net_man: &mut NetMan,
        messengers: &mut Messengers
    ) -> io::Result<bool> {

        // If there appears to be nothing to write
        // check outbound queue for queued msgs...
        if self.write_buf.len() == 0 {
            if let Some(msg) = self.outbound.pop() {
                self.header.code = msg.code;
                self.write_buf.copy_from_slice(&msg.body);
                if let Some(cons) = messengers.get_mut(&msg.from_code) {
                    // Recycle when done
                    cons.recycle(msg);
                }
            } else {
                return Ok(false)
            }
        }

        // WRITE HEADER
        let did_work = self.write_buffer(Self::HEADER)?;
        self.write_pos = 0;
        if !did_work { return Ok(did_work); }

        // WRITE BODY
        let did_work = self.write_buffer(Self::BODY)?;

        // fully written
        self.write_buf.clear();
        self.write_pos = 0;
        if let Some(ConnState::Writing(Some(next_state))) = &self.state {
            self.state = Some(*next_state.clone());
        } else {
            self.state = Some(ConnState::ReadingHeader(None));
        }

        // if no more messages queued
        if self.outbound.len() == 0 {
            let _ = self.disable_writable(&mut net_man.epoll);
        }
        Ok(did_work)
    }

    pub fn write_buffer(&mut self, buffer: u8) -> io::Result<bool> {
        let mut did_work = false;

        let buff: &mut[u8];
        if buffer == Self::HEADER {
            buff = self.header.marshal(self.write_buf.len());
        } else {
            buff = &mut self.write_buf;
        }

        while self.write_pos < buff.len() {
            match self.stream.write(&buff[self.write_pos..]) {
                Ok(0) => { return Err(io::Error::new(io::ErrorKind::ConnectionAborted, "peer closed")); }
                Ok(n) => {
                    self.write_pos += n;
                    did_work = true;
                },
                Err(ref e) if e.kind() == io::ErrorKind::WouldBlock => return Ok(did_work),
                Err(e) => return Err(e),
            }
        }
        Ok(did_work)
    }

    // -- ERROR ---------------------------------------------------------------
    #[allow(unused)]
    pub fn on_error(&mut self, _net_man: &mut NetMan) -> io::Result<bool> {
        println!("ERROR");
        Err(io::Error::other("Socket error"))
    }

    // -- HANGUP ---------------------------------------------------------------
    #[allow(unused)]
    pub fn on_hangup(&mut self, _net_man: &mut NetMan) -> io::Result<bool> {
        println!("HUNG");
        Err(io::Error::other("Socket hangup"))
    }

}

