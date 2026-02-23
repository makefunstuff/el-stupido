use web;
listen 9090;

/ "Welcome"

/hello "Hello World"

/status fn status_handler {
    http_reply(fd, 200, "text/plain", "OK")
}
