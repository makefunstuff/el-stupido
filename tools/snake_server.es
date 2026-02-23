// snake_server.es — serves snake WASM game over HTTP
📥 http

🔧 send_file(fd: 🔢, path: *🔶, ctype: *🔶) {
  ffd := 📂(path, 0)
  ❓ ffd < 0 {
    http_send(fd, "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nnot found")
    ↩
  }
  // get file size
  sz := 🔖(ffd, 0, 2) 🔄 🔢  // seek end
  🔖(ffd, 0, 0)               // seek start
  hdr: [256]🔶
  📝(&hdr, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", ctype, sz)
  http_send(fd, &hdr)
  // stream file in chunks
  buf: [4096]🔶
  🔁 1 {
    n := 📖(ffd, &buf, 4096) 🔄 🔢
    ❓ n <= 0 { 📕(ffd); ↩ }
    ✏(fd, &buf, n 🔄 💎)
  }
}

🔧 serve(fd: 🔢) {
  buf: [4096]🔶; n := 📖(fd, &buf, 4095) 🔄 🔢
  ❓ n <= 0 { 📕(fd); ↩ }
  *((&buf) 🔄 *🔶 + n) = 0; req := &buf 🔄 *🔶
  path: [128]🔶; http_path(req, &path, 128)

  ❓ ⚔(&path, "/snake.wasm") == 0 {
    send_file(fd, "tools/snake.wasm", "application/wasm")
  } ❗ {
    send_file(fd, "tools/snake.html", "text/html; charset=utf-8")
  }
  📕(fd)
}

🏁() {
  signal(17, 1 🔄 *⬛)
  sfd := http_listen(8080)
  ❓ sfd < 0 { 🖨("listen failed\n"); ↩ 1 }
  🖨("snake :8080\n")
  🔁 1 {
    cfd := 🤝(sfd, ∅, ∅); ❓ cfd < 0 { 🖨("accept fail\n"); ↩ 1 }
    pid := 🍴()
    ❓ pid == 0 { 📕(sfd); serve(cfd); 💀(0) }
    📕(cfd)
  }
}
