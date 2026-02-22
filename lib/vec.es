// lib/vec.es — dynamic array (void* elements) prelude
📦 Vec { data: **⬛; len: 🔢; cap: 🔢 }

🔧 vec_new() -> *Vec {
  vv := ✨ Vec; vv.len = 0; vv.cap = 16
  vv.data = 🧠(16 * 8) 🔄 **⬛; ↩ vv
}
🔧 vec_grow(vv: *Vec) {
  ❓ vv.len < vv.cap { ↩ }
  vv.cap = vv.cap * 2
  vv.data = ♻(vv.data 🔄 *⬛, (vv.cap * 8) 🔄 💎) 🔄 **⬛
}
🔧 vec_push(vv: *Vec, item: *⬛) {
  vec_grow(vv); *(vv.data + vv.len) = item; vv.len += 1
}
🔧 vec_get(vv: *Vec, idx: 🔢) -> *⬛ { ↩ *(vv.data + idx) }
🔧 vec_len(vv: *Vec) -> 🔢 { ↩ vv.len }
🔧 vec_pop(vv: *Vec) -> *⬛ {
  ❓ vv.len == 0 { ↩ 0 🔄 *⬛ }
  vv.len -= 1; ↩ *(vv.data + vv.len)
}
🔧 vec_fr(vv: *Vec) { 🆓(vv.data 🔄 *⬛); 🆓(vv 🔄 *⬛) }
