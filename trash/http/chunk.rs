pub enum ChunkedState {
    ReadingSize,          // reading the hex length of the next chunk
    ReadingData(usize),   // reading exactly N bytes of chunk data
    ReadingCRLF,          // consuming trailing \r\n after chunk
    Finished,             // zero-length chunk received
}

pub struct ChunkedDecoder {
    state: ChunkedState,
    buf: Vec<u8>,          // temporary buffer for reading
    current_chunk: usize,  // size of current chunk
    pub body: Vec<u8>,     // fully decoded body
}

impl ChunkedDecoder {
    pub fn new() -> Self {
        Self {
            state: ChunkedState::ReadingSize,
            buf: Vec::new(),
            current_chunk: 0,
            body: Vec::new(),
        }
    }

    /// Feed new bytes from the socket into the decoder
    /// Returns Ok(true) when the body is complete
    pub fn feed(&mut self, input: &[u8]) -> Result<bool, std::io::Error> {
        let mut i = 0;
        while i < input.len() {
            match self.state {
                ChunkedState::ReadingSize => {
                    if input[i] == b'\n' {
                        // parse hex size
                        let line = std::str::from_utf8(&self.buf).unwrap().trim();
                        self.current_chunk = usize::from_str_radix(line, 16).unwrap();
                        self.buf.clear();
                        if self.current_chunk == 0 {
                            self.state = ChunkedState::Finished;
                            return Ok(true);
                        } else {
                            self.state = ChunkedState::ReadingData(self.current_chunk);
                        }
                        i += 1;
                    } else if input[i] != b'\r' {
                        self.buf.push(input[i]);
                        i += 1;
                    } else {
                        i += 1; // skip \r
                    }
                }

                ChunkedState::ReadingData(ref mut remaining) => {
                    let to_take = std::cmp::min(*remaining, input.len() - i);
                    self.body.extend_from_slice(&input[i..i + to_take]);
                    *remaining -= to_take;
                    i += to_take;
                    if *remaining == 0 {
                        self.state = ChunkedState::ReadingCRLF;
                    }
                }

                ChunkedState::ReadingCRLF => {
                    if input[i] == b'\n' {
                        self.state = ChunkedState::ReadingSize;
                    }
                    i += 1;
                }

                ChunkedState::Finished => return Ok(true),
            }
        }
        Ok(false)
    }
}

