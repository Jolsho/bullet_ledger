use http::StatusCode;
use httparse::Request;
use std::fmt::Write;

use crate::networker::utils::DEFAULT_BUFFER_SIZE;

pub struct HttpReqRes {
    pub method: String,
    pub path: String,
    pub status: StatusCode,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
}

impl HttpReqRes {
    pub fn default() -> Self {
        HttpReqRes { 
            method: "".to_string(),
            path: "".to_string(),
            status: StatusCode::OK,
            headers: Vec::with_capacity(10),
            body:  Vec::with_capacity(DEFAULT_BUFFER_SIZE),
        }
    }

    pub fn decode(&mut self, req: Request) {
        self.method = req.method.unwrap().to_string();
        self.path = req.path.unwrap().to_string();
        self.headers = req.headers.iter()
            .map(|h| (h.name.to_string(), String::from_utf8_lossy(h.value).into_owned()))
            .collect();
    }

    pub fn reset(&mut self) {
        self.headers.truncate(0);
        self.body.truncate(0);
        self.status = StatusCode::OK;
        self.method.truncate(0);
        self.path.truncate(0);
    }
}


/// Writes an HTTP/1.1 response into a byte buffer.
/// - Adds Content-Length automatically unless `Transfer-Encoding: chunked` is present.
/// - Supports chunked encoding.
/// - Always appends CRLFs properly.
/// - Works efficiently with a preallocated Vec<u8>.
pub fn build_http_response_bytes(resp: &mut HttpReqRes, out: &mut Vec<u8>) {
    out.clear();

    // 1. Map status code → reason phrase
    let reason = resp.status.canonical_reason().unwrap_or("OK");

    // 2. Start the response line
    let _ = write!(StringWriter(out), "HTTP/1.1 {} {}\r\n", resp.status.as_u16(), reason);

    // 3. Determine if Content-Length or Transfer-Encoding are already set
    let has_len = resp.headers.iter()
        .any(|(k, _)| k.eq_ignore_ascii_case("Content-Length"));
    let has_chunked = resp.headers.iter()
        .any(|(k, v)| k.eq_ignore_ascii_case("Transfer-Encoding") && v.eq_ignore_ascii_case("chunked"));

    let mut headers = resp.headers.clone();

    // Automatically add Content-Length if not chunked and not set
    if !has_len && !has_chunked {
        headers.push((
            "Content-Length".to_string(),
            resp.body.len().to_string(),
        ));
    }

    // Add Connection: keep-alive by default
    if !headers.iter().any(|(k, _)| k.eq_ignore_ascii_case("Connection")) {
        headers.push(("Connection".to_string(), "keep-alive".to_string()));
    }

    // If chunked, ensure header is present
    if has_chunked {
        headers.retain(|(k, _)| !k.eq_ignore_ascii_case("Content-Length"));
        headers.push(("Transfer-Encoding".to_string(), "chunked".to_string()));
    }

    // 4. Write headers
    for (k, v) in headers {
        let _ = write!(StringWriter(out), "{}: {}\r\n", k, v);
    }

    // 5. End of headers
    out.extend_from_slice(b"\r\n");

    // 6. Write body
    if has_chunked {
        // Encode body as one or more chunks
        if !resp.body.is_empty() {
            let _ = write!(StringWriter(out), "{:X}\r\n", resp.body.len());
            out.extend_from_slice(&resp.body);
            out.extend_from_slice(b"\r\n");
        }

        // End of chunks
        out.extend_from_slice(b"0\r\n\r\n");
    } else {
        // Normal (non-chunked)
        out.extend_from_slice(&resp.body);
    }
}

/// Helper adapter so we can use `write!()` directly into Vec<u8>
struct StringWriter<'a>(&'a mut Vec<u8>);

impl<'a> std::fmt::Write for StringWriter<'a> {
    fn write_str(&mut self, s: &str) -> std::fmt::Result {
        self.0.extend_from_slice(s.as_bytes());
        Ok(())
    }
}

