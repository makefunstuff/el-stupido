// lib/map.es — string-keyed hash map prelude
📦 ME { key: *🔶; val: *🔶; nx: *ME }
📦 Map { bkts: **ME; cap: 🔢; len: 🔢 }

🔧 map_hash(key: *🔶, cap: 🔢) -> 🔢 {
  h: 🔵 = 5381; i := 0
  🔁 *(key+i) != 0 { h = h * 33 + (*(key+i) 🔄 🔵); i += 1 }
  ↩ (h 🔄 🔢) % cap
}
🔧 msd(s: *🔶) -> *🔶 {
  l := 🧵(s) 🔄 🔢; d := 🧠(l+1) 🔄 *🔶; 📋(d, s, l+1); ↩ d
}
🔧 map_new() -> *Map {
  m := ✨ Map; m.cap = 64; m.len = 0
  m.bkts = 🧩(64, 8) 🔄 **ME; ↩ m
}
🔧 map_set(m: *Map, key: *🔶, val: *🔶) {
  h := map_hash(key, m.cap); e := *(m.bkts + h)
  🔁 e 🔄 🔷 != 0 {
    ❓ ⚔(e.key, key) == 0 { 🆓(e.val 🔄 *⬛); e.val = msd(val); ↩ }
    e = e.nx
  }
  ne := ✨ ME; ne.key = msd(key); ne.val = msd(val); ne.nx = *(m.bkts + h)
  *(m.bkts + h) = ne; m.len += 1
}
🔧 map_get(m: *Map, key: *🔶) -> *🔶 {
  h := map_hash(key, m.cap); e := *(m.bkts + h)
  🔁 e 🔄 🔷 != 0 {
    ❓ ⚔(e.key, key) == 0 { ↩ e.val }
    e = e.nx
  }; ↩ 0 🔄 *🔶
}
🔧 map_del(m: *Map, key: *🔶) -> 🔢 {
  h := map_hash(key, m.cap); e := *(m.bkts + h); prev: *ME = 0 🔄 *ME
  🔁 e 🔄 🔷 != 0 {
    ❓ ⚔(e.key, key) == 0 {
      ❓ prev 🔄 🔷 == 0 { *(m.bkts + h) = e.nx } ❗ { prev.nx = e.nx }
      🆓(e.key 🔄 *⬛); 🆓(e.val 🔄 *⬛); 🆓(e 🔄 *⬛); m.len -= 1; ↩ 1
    }; prev = e; e = e.nx
  }; ↩ 0
}
🔧 map_len(m: *Map) -> 🔢 { ↩ m.len }
🔧 map_fr(m: *Map) {
  ➰ i := 0..m.cap {
    e := *(m.bkts + i)
    🔁 e 🔄 🔷 != 0 {
      nx := e.nx; 🆓(e.key 🔄 *⬛); 🆓(e.val 🔄 *⬛); 🆓(e 🔄 *⬛); e = nx
    }
  }
  🆓(m.bkts 🔄 *⬛); 🆓(m 🔄 *⬛)
}
