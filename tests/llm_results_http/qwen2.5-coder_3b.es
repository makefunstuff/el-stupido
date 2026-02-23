use web;

fn main() {
    web::use(web);
    web::listen(9090);

    web::get("/", |fd| {
        http_reply(fd, 200, "text/plain", "Welcome");
    });

    web::get("/hello", |fd| {
        http_reply(fd, 200, "text/plain", "Hello World");
    });

    web::get("/status", status_handler);
}

fn status_handler(fd: i32) {
    http_reply(fd, 200, "text/plain", "OK");
}
