// test_str.es — test lib/str.es string builder
📥 str

🏁() {
  s := str_new()
  🖨("1. new: len=%d cap=%d buf='%s'\n", str_len(s), s.cap, str_get(s))

  str_add(s, "hello")
  🖨("2. add: len=%d buf='%s'\n", str_len(s), str_get(s))

  str_add(s, " world")
  🖨("3. add2: len=%d buf='%s'\n", str_len(s), str_get(s))

  str_addc(s, 33)  // '!'
  🖨("4. addc: len=%d buf='%s'\n", str_len(s), str_get(s))

  str_clear(s)
  🖨("5. clear: len=%d buf='%s'\n", str_len(s), str_get(s))

  str_fmt(s, "num=%d", 42)
  🖨("6. fmt: len=%d buf='%s'\n", str_len(s), str_get(s))

  str_fr(s)
  🖨("7. free: OK\n")
}
