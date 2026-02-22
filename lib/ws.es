// lib/ws.es — WebSocket server prelude (RFC 6455)
// SHA-1, Base64, handshake, frame read/write

// --- SHA-1 (RFC 3174) ---
🔧 sha1_rotl(x: 🔵, n: 🔢) -> 🔵 { ↩ (x << n) | (x >> (32 - n)) }

🔧 sha1(data: *🔶, dlen: 🔢, out: *🔶) {
  h0: 🔵 = 1732584193; h1: 🔵 = 4023233417; h2: 🔵 = 2562383102
  h3: 🔵 = 271733878; h4: 🔵 = 3285377520
  // pad: msg + 0x80 + zeros + 64-bit length (big-endian)
  plen := ((dlen + 8) / 64 + 1) * 64
  pad := 🧩(plen, 1) 🔄 *🔶
  📋(pad, data, dlen 🔄 💎); *(pad + dlen) = 128 🔄 🔶 // 0x80
  // big-endian bit length at end
  bits: 🔵 = dlen 🔄 🔵 * 8
  *(pad + plen - 1) = (bits & 255) 🔄 🔶
  *(pad + plen - 2) = ((bits >> 8) & 255) 🔄 🔶
  *(pad + plen - 3) = ((bits >> 16) & 255) 🔄 🔶
  *(pad + plen - 4) = ((bits >> 24) & 255) 🔄 🔶

  // process 64-byte blocks
  blk := 0
  🔁 blk < plen {
    w: [80]🔵; i := 0
    // load 16 words big-endian
    🔁 i < 16 {
      off := blk + i * 4; bp := pad + off
      w_i := (*(bp) 🔄 🔵 & 255) << 24
      w_i = w_i | ((*(bp+1) 🔄 🔵 & 255) << 16)
      w_i = w_i | ((*(bp+2) 🔄 🔵 & 255) << 8)
      w_i = w_i | (*(bp+3) 🔄 🔵 & 255)
      tp := &w 🔄 *🔵; *(tp + i) = w_i; i += 1
    }
    // extend to 80 words
    🔁 i < 80 {
      tp := &w 🔄 *🔵
      xr := *(tp+i-3) ^ *(tp+i-8) ^ *(tp+i-14) ^ *(tp+i-16)
      *(tp + i) = sha1_rotl(xr, 1); i += 1
    }
    a := h0; b := h1; c := h2; d := h3; e := h4
    i = 0
    🔁 i < 80 {
      tp := &w 🔄 *🔵; f: 🔵 = 0; k: 🔵 = 0
      ❓ i < 20 { f = (b & c) | ((b ^ 4294967295) & d); k = 1518500249 }
      ❗ ❓ i < 40 { f = b ^ c ^ d; k = 1859775393 }
      ❗ ❓ i < 60 { f = (b & c) | (b & d) | (c & d); k = 2400959708 }
      ❗ { f = b ^ c ^ d; k = 3395469782 }
      tmp := sha1_rotl(a, 5) + f + e + k + *(tp+i)
      e = d; d = c; c = sha1_rotl(b, 30); b = a; a = tmp
      i += 1
    }
    h0 = h0 + a; h1 = h1 + b; h2 = h2 + c; h3 = h3 + d; h4 = h4 + e
    blk += 64
  }
  🆓(pad 🔄 *⬛)
  // write 20 bytes big-endian
  hv: [5]🔵; hp := &hv 🔄 *🔵
  *hp = h0; *(hp+1) = h1; *(hp+2) = h2; *(hp+3) = h3; *(hp+4) = h4
  j := 0
  🔁 j < 5 {
    v := *(hp + j)
    *(out + j*4)     = ((v >> 24) & 255) 🔄 🔶
    *(out + j*4 + 1) = ((v >> 16) & 255) 🔄 🔶
    *(out + j*4 + 2) = ((v >> 8) & 255) 🔄 🔶
    *(out + j*4 + 3) = (v & 255) 🔄 🔶
    j += 1
  }
}

// --- Base64 encode ---
🔧 b64_enc(src: *🔶, slen: 🔢, dst: *🔶) -> 🔢 {
  tbl := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
  i := 0; o := 0
  🔁 i < slen {
    a: 🔵 = *(src+i) 🔄 🔵 & 255; i += 1
    b: 🔵 = 0; c: 🔵 = 0; pad := 0
    ❓ i < slen { b = *(src+i) 🔄 🔵 & 255; i += 1 } ❗ { pad += 1 }
    ❓ i < slen { c = *(src+i) 🔄 🔵 & 255; i += 1 } ❗ { pad += 1 }
    triple := (a << 16) | (b << 8) | c
    *(dst+o) = *(tbl + ((triple >> 18) & 63)); o += 1
    *(dst+o) = *(tbl + ((triple >> 12) & 63)); o += 1
    ❓ pad < 2 { *(dst+o) = *(tbl + ((triple >> 6) & 63)) } ❗ { *(dst+o) = 61 🔄 🔶 }; o += 1
    ❓ pad < 1 { *(dst+o) = *(tbl + (triple & 63)) } ❗ { *(dst+o) = 61 🔄 🔶 }; o += 1
  }
  *(dst+o) = 0; ↩ o
}

// --- WebSocket handshake ---
🔧 ws_handshake(fd: 🔢, req: *🔶) -> 🔢 {
  // find Sec-WebSocket-Key header
  kh := 🔎(req, "Sec-WebSocket-Key: ")
  ❓ kh 🔄 🔷 == 0 { ↩ -1 }
  kh = kh + 19 // skip header name
  kend := 🔍(kh, 13) // find \r
  ❓ kend 🔄 🔷 == 0 { kend = 🔍(kh, 10) } // fallback \n
  ❓ kend 🔄 🔷 == 0 { ↩ -1 }
  klen := (kend - kh) 🔄 🔢

  // concat key + magic GUID
  magic := "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
  mlen := 🧵(magic) 🔄 🔢
  cat: [128]🔶; 📋(&cat, kh, klen 🔄 💎)
  📋((&cat) 🔄 *🔶 + klen, magic, (mlen + 1) 🔄 💎)

  // SHA-1 hash
  hash: [20]🔶
  sha1(&cat, klen + mlen, &hash)

  // Base64 encode
  accept: [40]🔶
  b64_enc(&hash, 20, &accept)

  // send upgrade response
  resp: [256]🔶
  📝(&resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", &accept)
  ✏(fd, &resp, 🧵(&resp))
  ↩ 0
}

// --- WebSocket frame ops ---
// opcodes
🏷 WsOp { WS_TEXT = 1; WS_BIN = 2; WS_CLOSE = 8; WS_PING = 9; WS_PONG = 10 }

🔧 ws_send(fd: 🔢, data: *🔶, dlen: 🔢, op: 🔢) {
  hdr: [10]🔶; hp := &hdr 🔄 *🔶; hsz := 2
  *hp = (128 | op) 🔄 🔶 // FIN + opcode
  ❓ dlen < 126 {
    *(hp+1) = dlen 🔄 🔶
  } ❗ ❓ dlen < 65536 {
    *(hp+1) = 126 🔄 🔶
    *(hp+2) = ((dlen >> 8) & 255) 🔄 🔶
    *(hp+3) = (dlen & 255) 🔄 🔶; hsz = 4
  } ❗ {
    *(hp+1) = 127 🔄 🔶; i := 0
    🔁 i < 8 { *(hp+2+i) = ((dlen >> ((7-i)*8)) & 255) 🔄 🔶; i += 1 }; hsz = 10
  }
  ✏(fd, &hdr, hsz 🔄 💎)
  ❓ dlen > 0 { ✏(fd, data, dlen 🔄 💎) }
}

🔧 ws_text(fd: 🔢, s: *🔶) { ws_send(fd, s, 🧵(s) 🔄 🔢, WS_TEXT) }
🔧 ws_close(fd: 🔢) { ws_send(fd, "" 🔄 *🔶, 0, WS_CLOSE) }

🔧 ws_read(fd: 🔢, buf: *🔶, bsz: 🔢) -> 🔢 {
  // returns payload length, -1 on error/close
  hdr: [14]🔶; n := 📖(fd, &hdr, 2) 🔄 🔢
  ❓ n < 2 { ↩ -1 }
  hp := &hdr 🔄 *🔶
  b0 := *hp 🔄 🔢 & 255; b1 := *(hp+1) 🔄 🔢 & 255
  op := b0 & 15; masked := (b1 >> 7) & 1
  plen: 🔷 = (b1 & 127) 🔄 🔷

  ❓ plen == 126 {
    n = 📖(fd, &hdr, 2) 🔄 🔢; ❓ n < 2 { ↩ -1 }
    plen = ((*(hp) 🔄 🔷 & 255) << 8) | (*(hp+1) 🔄 🔷 & 255)
  } ❗ ❓ plen == 127 {
    n = 📖(fd, &hdr, 8) 🔄 🔢; ❓ n < 8 { ↩ -1 }
    plen = 0; i := 0
    🔁 i < 8 { plen = (plen << 8) | (*(hp+i) 🔄 🔷 & 255); i += 1 }
  }

  mask: [4]🔶; mp := &mask 🔄 *🔶
  ❓ masked != 0 {
    n = 📖(fd, &mask, 4) 🔄 🔢; ❓ n < 4 { ↩ -1 }
  }

  // clamp to buffer
  rd: 🔢 = plen 🔄 🔢; ❓ rd > bsz - 1 { rd = bsz - 1 }
  total := 0
  🔁 total < rd {
    n = 📖(fd, buf + total, (rd - total) 🔄 💎) 🔄 🔢
    ❓ n <= 0 { ↩ -1 }
    total += n
  }

  // unmask
  ❓ masked != 0 {
    i := 0; 🔁 i < total { *(buf+i) = (*(buf+i) 🔄 🔢 ^ (*(mp + (i % 4)) 🔄 🔢)) 🔄 🔶; i += 1 }
  }
  *(buf + total) = 0

  // handle control frames
  ❓ op == WS_CLOSE { ↩ -1 }
  ❓ op == WS_PING { ws_send(fd, buf, total, WS_PONG); ↩ 0 }
  ↩ total
}
