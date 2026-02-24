use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::Arc;

/// Parsed HTTP request.
pub struct Request {
    pub method: String,
    pub path: String,
    pub query: String,
    body: String,
}

impl Request {
    fn parse(stream: &mut TcpStream) -> Option<Self> {
        let mut buf = [0u8; 8192];
        let n = stream.read(&mut buf).ok()?;
        if n == 0 {
            return None;
        }
        let raw = String::from_utf8_lossy(&buf[..n]);
        let first = raw.lines().next()?;
        let mut parts = first.split_whitespace();
        let method = parts.next()?.to_string();
        let full_path = parts.next()?.to_string();

        let (path, query) = if let Some(idx) = full_path.find('?') {
            (
                full_path[..idx].to_string(),
                full_path[idx + 1..].to_string(),
            )
        } else {
            (full_path, String::new())
        };

        let body = raw
            .find("\r\n\r\n")
            .map(|i| raw[i + 4..].to_string())
            .unwrap_or_default();

        Some(Request {
            method,
            path,
            query,
            body,
        })
    }

    pub fn body(&self) -> &str {
        &self.body
    }

    /// Extract a query parameter by key from the URL query string.
    pub fn query_param(&self, key: &str) -> Option<String> {
        let search = format!("{}=", key);
        let start = self.query.find(&search)?;
        let val_start = start + search.len();
        let val_end = self.query[val_start..]
            .find('&')
            .map(|i| val_start + i)
            .unwrap_or(self.query.len());
        Some(crate::grug::url_decode(&self.query[val_start..val_end]))
    }
}

/// HTTP response writer.
pub struct Response {
    stream: TcpStream,
}

impl Response {
    fn new(stream: TcpStream) -> Self {
        Self { stream }
    }

    /// Send an HTTP response with the given status, content type, and body.
    pub fn send(&mut self, status: u16, content_type: &str, body: &str) {
        let status_text = match status {
            200 => "OK",
            404 => "Not Found",
            500 => "Internal Server Error",
            _ => "OK",
        };
        let header = format!(
            "HTTP/1.1 {} {}\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
            status,
            status_text,
            content_type,
            body.len()
        );
        let _ = self.stream.write_all(header.as_bytes());
        let _ = self.stream.write_all(body.as_bytes());
    }

    pub fn html(&mut self, body: &str) {
        self.send(200, "text/html; charset=utf-8", body);
    }

    pub fn json(&mut self, body: &str) {
        self.send(200, "application/json", body);
    }

    pub fn redirect(&mut self, location: &str) {
        let header = format!(
            "HTTP/1.1 302 Found\r\nLocation: {}\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
            location
        );
        let _ = self.stream.write_all(header.as_bytes());
    }

    pub fn not_found(&mut self) {
        self.send(404, "text/plain", "404 Not Found\n");
    }
}

type HandlerFn = Box<dyn Fn(&Request, &mut Response) + Send + Sync>;

struct Route {
    method: String,
    path: String,
    handler: HandlerFn,
}

/// Thread-per-connection HTTP server.
pub struct HttpServer {
    port: u16,
    routes: Vec<Route>,
}

impl HttpServer {
    pub fn new(port: u16) -> Self {
        Self {
            port,
            routes: Vec::new(),
        }
    }

    /// Register a route handler for the given HTTP method and path.
    pub fn route<F>(&mut self, method: &str, path: &str, handler: F)
    where
        F: Fn(&Request, &mut Response) + Send + Sync + 'static,
    {
        self.routes.push(Route {
            method: method.to_string(),
            path: path.to_string(),
            handler: Box::new(handler),
        });
    }

    /// Start the server. Blocks forever, handling connections in threads.
    pub fn run(self) {
        let listener = TcpListener::bind(format!("0.0.0.0:{}", self.port)).unwrap_or_else(|e| {
            eprintln!("bind :{} failed: {}", self.port, e);
            std::process::exit(1);
        });
        eprintln!("listening on :{}", self.port);

        let routes = Arc::new(self.routes);

        loop {
            let (mut stream, _) = match listener.accept() {
                Ok(conn) => conn,
                Err(_) => continue,
            };
            let routes = Arc::clone(&routes);

            std::thread::spawn(move || {
                let Some(req) = Request::parse(&mut stream) else {
                    return;
                };
                let mut resp = Response::new(stream);

                for route in routes.iter() {
                    if route.method == req.method && route.path == req.path {
                        (route.handler)(&req, &mut resp);
                        return;
                    }
                }

                resp.not_found();
            });
        }
    }
}
