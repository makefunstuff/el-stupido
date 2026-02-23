// lib/http.es — HTTP server helpers prelude
📦 SA { fam: 📈; port: 📈; addr: 🔵; z: [8]🔶 }

🔧 http_listen(port: 🔢) -> 🔢 {
  fd := 🌐(2,1,0); ❓ fd < 0 { ↩ -1 }
  o: 🔢 = 1; 🎛(fd,1,2,&o,4)
  a: SA; 🧹(&a,0,16); a.fam = 2; a.port = 🔃(port 🔄 📈); a.addr = 0
  ❓ 📌(fd,&a,16) < 0 { 📕(fd); ↩ -2 }
  📡(fd, 128); ↩ fd
}

🔧 http_send(fd: 🔢, s: *🔶) { ✏(fd, s, 🧵(s)) }

🔧 http_resp(fd: 🔢, code: 🔢, ctype: *🔶) {
  hdr: [256]🔶
  📝(&hdr, "HTTP/1.1 %d OK\r\nContent-Type: %s\r\nConnection: close\r\n\r\n", code, ctype)
  http_send(fd, &hdr)
}

fn http_reply(fd: i32, code: i32, ctype: *u8, body: *u8) {
  http_resp(fd, code, ctype)
  http_send(fd, body)
}

🔧 http_redirect(fd: 🔢, loc: *🔶) {
  hdr: [512]🔶
  📝(&hdr, "HTTP/1.1 303 See Other\r\nLocation: %s\r\nContent-Length: 0\r\n\r\n", loc)
  http_send(fd, &hdr)
}

🔧 http_ispost(req: *🔶) -> 🔢 { ↩ 🗡(req, "POST", 4) == 0 }
🔧 http_isget(req: *🔶) -> 🔢 { ↩ 🗡(req, "GET", 3) == 0 }

🔧 http_path(req: *🔶, dst: *🔶, dsz: 🔢) {
  sp := 🔍(req, 32); ❓ sp 🔄 🔷 == 0 { *dst = 0; ↩ }
  sp = sp + 1; i := 0
  🔁 *(sp+i) != 0 && *(sp+i) != 32 && i < dsz - 1 { *(dst+i) = *(sp+i); i += 1 }
  *(dst+i) = 0
}

🔧 http_body(req: *🔶) -> *🔶 {
  p := 🔎(req, "\r\n\r\n")
  ❓ p 🔄 🔷 != 0 { ↩ p + 4 }
  ↩ 0 🔄 *🔶
}

🔧 http_udec(s: *🔶) {
  i := 0; j := 0
  🔁 *(s+i) != 0 {
    c := *(s+i) 🔄 🔢
    ❓ c == 43 { *(s+j) = 32; j += 1; i += 1 }
    ❗ ❓ c == 37 && *(s+i+1) != 0 && *(s+i+2) != 0 {
      hv := 0; ki := 0
      🔁 ki < 2 {
        ch := *(s+i+1+ki) 🔄 🔢
        ❓ ch >= 48 && ch <= 57 { hv = hv*16 + (ch-48) }
        ❗ ❓ ch >= 65 && ch <= 70 { hv = hv*16 + (ch-55) }
        ❗ ❓ ch >= 97 && ch <= 102 { hv = hv*16 + (ch-87) }
        ki += 1
      }
      *(s+j) = hv 🔄 🔶; j += 1; i += 3
    } ❗ { *(s+j) = *(s+i); j += 1; i += 1 }
  }
  *(s+j) = 0
}

🔧 http_fval(body: *🔶, key: *🔶, dst: *🔶, dsz: 🔢) -> 🔢 {
  kl := 🧵(key) 🔄 🔢; p := body
  🔁 *p != 0 {
    ❓ 🗡(p, key, kl) == 0 && *(p+kl) == 61 {
      vs := p+kl+1; i := 0
      🔁 *(vs+i) != 0 && *(vs+i) != 38 && i < dsz-1 { *(dst+i) = *(vs+i); i += 1 }
      *(dst+i) = 0; http_udec(dst); ↩ 1
    }
    🔁 *p != 0 && *p != 38 { p = p+1 }
    ❓ *p == 38 { p = p+1 }
  }
  *dst = 0; ↩ 0
}

🔧 http_hesc(fd: 🔢, s: *🔶) {
  i := 0
  🔁 *(s+i) != 0 {
    c := *(s+i) 🔄 🔢
    ❓ c == 60 { http_send(fd, "&lt;") }
    ❗ ❓ c == 62 { http_send(fd, "&gt;") }
    ❗ ❓ c == 38 { http_send(fd, "&amp;") }
    ❗ ❓ c == 34 { http_send(fd, "&quot;") }
    ❗ { ✏(fd, s+i, 1) }
    i += 1
  }
}
