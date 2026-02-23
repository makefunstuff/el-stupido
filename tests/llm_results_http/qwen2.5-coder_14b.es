use web
listen 9090
/ "Welcome"
/hello "Hello World"
/status fn status_handler

fn status_handler(fd: i32, body: *u8) { http_reply(fd, 200, "text/plain", "OK") }
