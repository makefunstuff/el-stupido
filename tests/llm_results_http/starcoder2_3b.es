

use web
listen 9090
/fn hello (fd: i32, body:*u8) {http_reply(fd,200,"text/plain","Hello World") }
fn status_handler(fd:i32)->i16{
	http_reply(fd,200,"text/plain","Ok")
}
