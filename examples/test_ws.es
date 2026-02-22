// test_ws.es — test SHA-1, Base64, WS handshake
📥 ws

🔧 hex(digest: *🔶, dlen: 🔢, out: *🔶) {
  hx := "0123456789abcdef"; i := 0
  🔁 i < dlen {
    b := *(digest+i) 🔄 🔢 & 255
    *(out + i*2) = *(hx + (b >> 4))
    *(out + i*2 + 1) = *(hx + (b & 15))
    i += 1
  }
  *(out + dlen*2) = 0
}

🏁() {
  // 1. SHA-1 of ""
  h: [20]🔶; out: [64]🔶
  sha1("", 0, &h); hex(&h, 20, &out)
  🖨("1. sha1('')  = %s\n", &out)
  🖨("   expected  = da39a3ee5e6b4b0d3255bfef95601890afd80709\n")

  // 2. SHA-1 of "abc"
  sha1("abc", 3, &h); hex(&h, 20, &out)
  🖨("2. sha1(abc) = %s\n", &out)
  🖨("   expected  = a9993e364706816aba3e25717850c26c9cd0d89d\n")

  // 3. Base64 of "Hello"
  b: [32]🔶
  b64_enc("Hello", 5, &b)
  🖨("3. b64(Hello) = %s (want SGVsbG8=)\n", &b)

  // 4. Base64 of "Ma" (2 bytes -> pad 1)
  b64_enc("Ma", 2, &b)
  🖨("4. b64(Ma) = %s (want TWE=)\n", &b)

  // 5. Base64 of "M" (1 byte -> pad 2)
  b64_enc("M", 1, &b)
  🖨("5. b64(M) = %s (want TQ==)\n", &b)

  // 6. WS accept key test (RFC 6455 section 4.2.2 example)
  // Key: "dGhlIHNhbXBsZSBub25jZQ==" -> Accept: "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
  cat: [128]🔶
  ✂(&cat, "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
  sha1(&cat, 🧵(&cat) 🔄 🔢, &h)
  acc: [40]🔶
  b64_enc(&h, 20, &acc)
  🖨("6. accept = %s\n", &acc)
  🖨("   expect = s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\n")
}
