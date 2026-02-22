// lib/str.es — dynamic string builder prelude
📦 Str { buf: *🔶; len: 🔢; cap: 🔢 }

🔧 str_new() -> *Str {
  s := ✨ Str; s.len = 0; s.cap = 64
  s.buf = 🧠(64) 🔄 *🔶; *(s.buf) = 0; ↩ s
}
🔧 str_grow(s: *Str, need: 🔢) {
  ❓ s.len + need < s.cap { ↩ }
  🔁 s.cap <= s.len + need { s.cap = s.cap * 2 }
  s.buf = ♻(s.buf 🔄 *⬛, s.cap 🔄 💎) 🔄 *🔶
}
🔧 str_add(s: *Str, cs: *🔶) {
  l := 🧵(cs) 🔄 🔢; str_grow(s, l)
  📋(s.buf + s.len, cs, l 🔄 💎); s.len += l; *(s.buf + s.len) = 0
}
🔧 str_addc(s: *Str, c: 🔢) {
  str_grow(s, 1); *(s.buf + s.len) = c 🔄 🔶; s.len += 1; *(s.buf + s.len) = 0
}
🔧 str_fmt(s: *Str, fmt: *🔶, a: 🔷) {
  tmp: [1024]🔶; n := 📝(&tmp, fmt, a) 🔄 🔢
  str_grow(s, n); 📋(s.buf + s.len, &tmp, n 🔄 💎); s.len += n; *(s.buf + s.len) = 0
}
🔧 str_get(s: *Str) -> *🔶 { ↩ s.buf }
🔧 str_len(s: *Str) -> 🔢 { ↩ s.len }
🔧 str_clear(s: *Str) { s.len = 0; *(s.buf) = 0 }
🔧 str_fr(s: *Str) { 🆓(s.buf 🔄 *⬛); 🆓(s 🔄 *⬛) }
