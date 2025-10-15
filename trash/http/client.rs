use std::{io::{self, Read, Write}, net::{SocketAddr, TcpStream}, os::fd::{AsFd, AsRawFd, BorrowedFd}, time::Instant};
use httparse::Request;
use nix::sys::{epoll::EpollEvent, socket::{getsockopt, sockopt}};

use crate::http::{chunk::ChunkedDecoder, utils::{build_http_response_bytes, HttpReqRes}, handlers::http_handler, server::Server};
use crate::{msging::Poller, networker::utils::{epoll_flags, epoll_flags_write, NetError, NetResult}};

pub enum ClientState {
    Handshake,
    ReadingHeaders,
    ReadingBody { remaining: usize, chunked: bool },
    Responding,
}

const DEFAULT_HEADER_SIZE:usize = 1024;

pub struct Client {
    fd: i32,
    stream: TcpStream,
    pub addr: SocketAddr,
    pub last_deadline: Instant,
    pub state: Option<ClientState>,
    pub header_len: usize,

    pub decoder: ChunkedDecoder,

    pub read_buf: Vec<u8>,
    pub read_pos: usize,

    pub response_buf: Vec<u8>,
    pub reqres: HttpReqRes,
}

impl Client {
    pub fn new(
        stream: TcpStream, 
        addr: SocketAddr,
        server: &mut Server,
    ) -> Self {
        Self {  
            fd: stream.as_raw_fd(),
            stream, 
            addr,
            header_len: 0,
            last_deadline: Instant::now(),
            state: Some(ClientState::Handshake),
            read_buf: server.get_buff(),
            read_pos: 0,
            decoder: ChunkedDecoder::new(),

            response_buf: server.get_buff(),
            reqres: HttpReqRes::default(),
        }
    }

    pub fn strip_and_delete(&mut self, server: &mut Server) { 
        let _ = server.epoll.delete(&self);
        let mut read_buf = Vec::with_capacity(0);
        std::mem::swap(&mut read_buf, &mut self.read_buf);
        server.put_buff(read_buf);

        let mut write_buf = Vec::with_capacity(0);
        std::mem::swap(&mut write_buf, &mut self.response_buf);
        server.put_buff(write_buf);
    }

    /// CALLS getsockopt and return io::Result corresponding
    pub fn check_socket_error(&mut self) -> io::Result<()> {
        match getsockopt(&self.stream, sockopt::SocketError) {
            Ok(0) => Ok(()),
            Ok(err) => Err(io::Error::from_raw_os_error(err)),
            Err(e) => Err(io::Error::new(io::ErrorKind::Other, e)),
        }
    }

    /// Adds EPOLLOUT flag to streams epoll event
    fn enable_writable(&mut self, epoll: &mut Poller) -> nix::Result<()> {
        epoll.modify(&self.stream, &mut EpollEvent::new(
            epoll_flags_write(), self.fd as u64,
        ))
    }

    /// Removes EPOLLOUT flag to streams epoll event
    fn disable_writable(&mut self, epoll: &mut Poller) -> nix::Result<()> {
        epoll.modify(&self.stream, &mut EpollEvent::new(
            epoll_flags(), self.fd as u64
        ))
    }

    pub fn on_readable(&mut self, server: &mut Server) -> NetResult<bool> {
        let mut did_work = false;

        loop {
            match self.state.take() {

                //==================================================================
                Some(ClientState::Handshake) => {
                    // For TLS later; plain HTTP just moves to headers
                    self.state = Some(ClientState::ReadingHeaders);
                }

                //==================================================================
                Some(ClientState::ReadingHeaders) => {
                    if self.read_buf.len() != DEFAULT_HEADER_SIZE {
                        self.read_pos = 0;
                        self.read_buf.resize(DEFAULT_HEADER_SIZE, 0);
                    }

                    match self.stream.read(&mut self.read_buf[self.read_pos..]) {
                        Ok(0) => return Err(NetError::SocketFailed("Connection closed".into())),
                        Ok(n) => {
                            self.read_pos += n;
                            did_work = true;
                        },
                        Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                            self.state = Some(ClientState::ReadingHeaders);
                            break;
                        }
                        Err(e) => return Err(NetError::SocketFailed(e.to_string())),
                    };
                    let mut headers = [httparse::EMPTY_HEADER; 32];
                    let mut req = Request::new(&mut headers);
                    
                    match req.parse(&self.read_buf) {
                        Ok(httparse::Status::Complete(header_len)) => {
                            self.header_len = header_len;

                            // Determine body length
                            let content_len = req.headers.iter()
                                .find(|h| h.name.eq_ignore_ascii_case("content-length"))
                                .and_then(|h| std::str::from_utf8(h.value).ok()?.parse::<usize>().ok());

                            let chunked = req.headers.iter()
                                .any(|h| h.name.eq_ignore_ascii_case("transfer-encoding") && h.value.eq_ignore_ascii_case(b"chunked"));

                            self.reqres.decode(req);

                            if chunked {
                                self.state = Some(ClientState::ReadingBody { remaining: 0, chunked: true });
                            } else if let Some(len) = content_len {
                                self.read_pos = 0;
                                self.read_buf.resize(len, 0);
                                self.state = Some(ClientState::ReadingBody { remaining: len, chunked: false });
                            } else {
                                self.state = Some(ClientState::Responding);
                            }
                        }
                        Ok(httparse::Status::Partial) => {
                            self.state = Some(ClientState::ReadingHeaders);
                            break;
                        }
                        Err(e) => return Err(NetError::Other(format!("Header parse error: {}", e))),
                    }
                }

                //==================================================================
                Some(ClientState::ReadingBody { mut remaining, chunked }) => {
                    if chunked {
                        self.decoder = ChunkedDecoder::new();

                        match self.stream.read(&mut self.read_buf[self.read_pos..]) {
                            Ok(0) => return Err(NetError::SocketFailed("Connection closed".into())),
                            Ok(n) => {
                                did_work = true;
                                let complete = self.decoder.feed(&self.read_buf[self.read_pos..self.read_pos+n])
                                    .map_err(|e| NetError::Decoding(e.to_string()))?;

                                if complete {
                                    self.read_pos = 0;
                                    self.state = Some(ClientState::Responding);
                                } else {
                                    self.state = Some(ClientState::ReadingBody {remaining,chunked})
                                }
                                break;
                            }
                            Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                                self.state = Some(ClientState::ReadingBody { remaining, chunked });
                                break;
                            }
                            Err(e) => return Err(NetError::SocketFailed(e.to_string())),
                        }
                    } else {
                        // Content-Length body
                        if remaining > 0 {
                            match self.stream.read(&mut self.read_buf[self.read_pos..]) {
                                Ok(0) => return Err(NetError::SocketFailed("Connection closed".into())),
                                Ok(n) => {
                                    did_work = true;
                                    let consumed = std::cmp::min(n, remaining);
                                    remaining -= consumed;
                                    self.read_pos += consumed;
                                    if remaining == 0 {
                                        std::mem::swap(&mut self.reqres.body, &mut self.read_buf);
                                        self.read_pos = 0;
                                        self.state = Some(ClientState::Responding); 
                                    } else {
                                        self.state = Some(ClientState::ReadingBody { remaining, chunked });
                                    };
                                }
                                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => {
                                    self.state = Some(ClientState::ReadingBody { remaining, chunked });
                                    break;
                                }
                                Err(e) => return Err(NetError::SocketFailed(e.to_string())),
                            }
                        } else {
                            self.read_pos = 0;
                            self.state = Some(ClientState::Responding);
                        }
                    }
                }

                //==================================================================
                Some(ClientState::Responding) => {
                    http_handler(&mut self.reqres);
                    build_http_response_bytes(&mut self.reqres, &mut self.response_buf);
                    self.state = Some(ClientState::Responding);
                    did_work = true;
                    let _ = self.enable_writable(&mut server.epoll);
                    self.reqres.reset();
                    break;
                }

                None => return Err(NetError::Other("ClientState == None".to_string())),
            }
        }

        Ok(did_work)
    }

    pub fn on_writable(&mut self, server: &mut Server) -> NetResult<bool> {
        let mut did_work = false;
        while !self.response_buf.is_empty() {
            match self.stream.write(&self.response_buf) {
                Ok(n) => { 
                    did_work = true;
                    self.response_buf.drain(..n); 
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                Err(e) => return Err(NetError::SocketFailed(e.to_string())),
            }
        }
        if self.response_buf.len() == 0 {
            let _ = self.disable_writable(&mut server.epoll);
        }
        Ok(did_work)
    }
}

impl AsFd for Client {
    fn as_fd(&self) -> std::os::unix::prelude::BorrowedFd<'_> {
        unsafe { BorrowedFd::borrow_raw(self.fd) } 
    }
}
