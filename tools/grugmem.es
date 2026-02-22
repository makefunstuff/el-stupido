// grugmem — KV store @ ~/.grugmem in .grug format
// usage: grugmem get <sec> <key>
//        grugmem set <sec> <key> <val>
//        grugmem del <sec> <key>
//        grugmem ls [sec]
//        grugmem dump

📦 KV { k: *🔶; val: *🔶; nx: *KV }
📦 Sec { nm: *🔶; kv: *KV; nx: *Sec }
📦 Grug { sec: *Sec; buf: *🔶 }

// ---- parser helpers ----
🔧 gb(p: *🔶, i: 🔢) -> 🔢 { *(p + i) 🔄 🔢 }
🔧 eq4(p: *🔶, a: 🔢, b: 🔢, c: 🔢, d: 🔢) -> 🔢 { gb(p,0)==a && gb(p,1)==b && gb(p,2)==c && gb(p,3)==d }
🔧 is_sec(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x93, 0x82) }
🔧 is_key(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x94, 0x91) }
🔧 is_arr(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x91, 0x89) }
🔧 skv(p: *🔶) -> *🔶 { ❓ gb(p,0)==0xEF && gb(p,1)==0xB8 && gb(p,2)==0x8F { ↩ p+3 }; ↩ p }
🔧 sw(p: *🔶) -> *🔶 { 🔁 *p==32 || *p==9 { p=p+1 }; ↩ p }
🔧 fnl(p: *🔶) -> *🔶 { 🔁 *p!=0 && *p!=10 { p=p+1 }; ↩ p }
🔧 anl(p: *🔶) -> *🔶 { ❓ *p==13 { p=p+1 }; ❓ *p==10 { p=p+1 }; ↩ p }
🔧 nt(s: *🔶, e: *🔶) { 🔁 e 🔄 🔷 > s 🔄 🔷 && (*(e-1)==32 || *(e-1)==9 || *(e-1)==13) { e=e-1 }; *e=0 }
🔧 sd(s: *🔶) -> *🔶 { l := 🧵(s) 🔄 🔢; d := 🧠(l+1) 🔄 *🔶; 📋(d,s,l+1); ↩ d }

// ---- file slurp ----
🔧 slurp(path: *🔶) -> *🔶 {
  fd := 📂(path, 0)
  ❓ fd < 0 { ↩ 0 🔄 *🔶 }
  fsz := 🔖(fd, 0, 2); 🔖(fd, 0, 0)
  buf := 🧠(fsz + 1) 🔄 *🔶; 📖(fd, buf, fsz); *(buf + fsz) = 0; 📕(fd)
  ↩ buf
}

// ---- parse .grug ----
🔧 grug_parse(path: *🔶) -> *Grug {
  buf := slurp(path)
  ❓ buf 🔄 🔷 == 0 { ↩ 0 🔄 *Grug }
  g := ✨ Grug; g.sec = 0 🔄 *Sec; g.buf = buf
  cur: *Sec = 0 🔄 *Sec; p := buf
  🔁 *p != 0 {
    p = sw(p); ❓ *p == 0 { 🛑 }
    ❓ *p==10 || *p==13 { p = anl(p) }
    ❗ ❓ *p == 35 { p = anl(fnl(p)) }
    ❗ ❓ is_sec(p) {
      p = sw(skv(p+4)); nm := p; nl := fnl(p); nx := anl(nl); nt(nm, nl)
      s := ✨ Sec; s.nm = sd(nm); s.kv = 0 🔄 *KV; s.nx = 0 🔄 *Sec
      ❓ g.sec 🔄 🔷 == 0 { g.sec = s } ❗ { t := g.sec; 🔁 t.nx 🔄 🔷 != 0 { t = t.nx }; t.nx = s }
      cur = s; p = nx
    }
    ❗ ❓ is_key(p) && cur 🔄 🔷 != 0 {
      p = sw(skv(p+4)); ks := p
      🔁 *p != 0 && *p != 10 { ❓ is_arr(p) { 🛑 }; p = p+1 }
      ❓ is_arr(p) {
        sep := p; vs := sw(skv(sep+4)); nl := fnl(vs); nx := anl(nl); nt(ks, sep); nt(vs, nl)
        kv := ✨ KV; kv.k = sd(ks); kv.val = sd(vs); kv.nx = 0 🔄 *KV
        ❓ cur.kv 🔄 🔷 == 0 { cur.kv = kv } ❗ { t := cur.kv; 🔁 t.nx 🔄 🔷 != 0 { t = t.nx }; t.nx = kv }
        p = nx
      } ❗ { p = anl(fnl(p)) }
    }
    ❗ { p = anl(fnl(p)) }
  }
  ↩ g
}

// ---- query ----
🔧 grug_get(g: *Grug, sec: *🔶, key: *🔶) -> *🔶 {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    ❓ ⚔(s.nm, sec) == 0 {
      kv := s.kv; 🔁 kv 🔄 🔷 != 0 { ❓ ⚔(kv.k, key) == 0 { ↩ kv.val }; kv = kv.nx }
      ↩ 0 🔄 *🔶
    }
    s = s.nx
  }
  ↩ 0 🔄 *🔶
}

// find or create section
🔧 grug_sec(g: *Grug, nm: *🔶) -> *Sec {
  s := g.sec
  🔁 s 🔄 🔷 != 0 { ❓ ⚔(s.nm, nm) == 0 { ↩ s }; s = s.nx }
  s = ✨ Sec; s.nm = sd(nm); s.kv = 0 🔄 *KV; s.nx = 0 🔄 *Sec
  ❓ g.sec 🔄 🔷 == 0 { g.sec = s } ❗ { t := g.sec; 🔁 t.nx 🔄 🔷 != 0 { t = t.nx }; t.nx = s }
  ↩ s
}

// ---- mutate ----
🔧 grug_set(g: *Grug, sec: *🔶, key: *🔶, val: *🔶) {
  s := grug_sec(g, sec); kv := s.kv
  🔁 kv 🔄 🔷 != 0 {
    ❓ ⚔(kv.k, key) == 0 { 🗑 kv.val; kv.val = sd(val); ↩ }
    kv = kv.nx
  }
  n := ✨ KV; n.k = sd(key); n.val = sd(val); n.nx = 0 🔄 *KV
  ❓ s.kv 🔄 🔷 == 0 { s.kv = n } ❗ { t := s.kv; 🔁 t.nx 🔄 🔷 != 0 { t = t.nx }; t.nx = n }
}

🔧 grug_del(g: *Grug, sec: *🔶, key: *🔶) -> 🔢 {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    ❓ ⚔(s.nm, sec) == 0 {
      prev: *KV = 0 🔄 *KV; kv := s.kv
      🔁 kv 🔄 🔷 != 0 {
        ❓ ⚔(kv.k, key) == 0 {
          ❓ prev 🔄 🔷 == 0 { s.kv = kv.nx } ❗ { prev.nx = kv.nx }
          🗑 kv.k; 🗑 kv.val; 🗑 kv; ↩ 1
        }
        prev = kv; kv = kv.nx
      }
      ↩ 0
    }
    s = s.nx
  }
  ↩ 0
}

// ---- serialize ----
🔧 grug_write(g: *Grug, path: *🔶) -> 🔢 {
  fd := 📂(path, 577, 420)
  ❓ fd < 0 { ↩ -1 }
  buf: [4096]🔶; s := g.sec
  🔁 s 🔄 🔷 != 0 {
    n := 📝(&buf, "📂 %s\n", s.nm) 🔄 🔷; ✏(fd, &buf, n)
    kv := s.kv
    🔁 kv 🔄 🔷 != 0 {
      n = 📝(&buf, "🔑 %s 👉 %s\n", kv.k, kv.val) 🔄 🔷; ✏(fd, &buf, n)
      kv = kv.nx
    }
    ✏(fd, "\n", 1)
    s = s.nx
  }
  📕(fd); ↩ 0
}

// ---- output ----
🔧 grug_dump(g: *Grug) {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    🖨("📂 %s\n", s.nm); kv := s.kv
    🔁 kv 🔄 🔷 != 0 { 🖨("  🔑 %s 👉 %s\n", kv.k, kv.val); kv = kv.nx }
    s = s.nx
  }
}

🔧 grug_ls(g: *Grug, sec: *🔶) {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    show := 1
    ❓ sec 🔄 🔷 != 0 { ❓ ⚔(s.nm, sec) != 0 { show = 0 } }
    ❓ show {
      ❓ sec 🔄 🔷 == 0 { 🖨("[%s]\n", s.nm) }
      kv := s.kv; 🔁 kv 🔄 🔷 != 0 { 🖨("%s\n", kv.k); kv = kv.nx }
    }
    s = s.nx
  }
}

// ---- cleanup ----
🔧 grug_fr(g: *Grug) {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    ns := s.nx; kv := s.kv
    🔁 kv 🔄 🔷 != 0 { nk := kv.nx; 🗑 kv.k; 🗑 kv.val; 🗑 kv; kv = nk }
    🗑 s.nm; 🗑 s; s = ns
  }
  ❓ g.buf 🔄 🔷 != 0 { 🗑 g.buf }
  🗑 g
}

// ---- path: $HOME/.grugmem ----
🔧 gmpath() -> *🔶 {
  h := getenv("HOME")
  ❓ h 🔄 🔷 == 0 { h = "/tmp" }
  buf := 🧠(512) 🔄 *🔶; 📝(buf, "%s/.grugmem", h)
  ↩ buf
}

// ---- main ----
🏁(argc: 🔢, argv: **🔶) {
  ❓ argc < 2 { 🖨("usage: grugmem <get|set|del|ls|dump> [sec] [key] [val]\n"); 💀(1) }
  cmd := *(argv + 1)
  path := gmpath()
  g := grug_parse(path)
  ❓ g 🔄 🔷 == 0 { g = ✨ Grug; g.sec = 0 🔄 *Sec; g.buf = 0 🔄 *🔶 }

  ❓ ⚔(cmd, "get") == 0 && argc == 4 {
    rv := grug_get(g, *(argv+2), *(argv+3))
    ❓ rv 🔄 🔷 != 0 { 🖨("%s\n", rv) }
  }
  ❗ ❓ ⚔(cmd, "set") == 0 && argc == 5 {
    grug_set(g, *(argv+2), *(argv+3), *(argv+4))
    grug_write(g, path)
  }
  ❗ ❓ ⚔(cmd, "del") == 0 && argc == 4 {
    grug_del(g, *(argv+2), *(argv+3))
    grug_write(g, path)
  }
  ❗ ❓ ⚔(cmd, "ls") == 0 {
    sec: *🔶 = 0 🔄 *🔶
    ❓ argc == 3 { sec = *(argv+2) }
    grug_ls(g, sec)
  }
  ❗ ❓ ⚔(cmd, "dump") == 0 { grug_dump(g) }
  ❗ { 🖨("usage: grugmem <get|set|del|ls|dump> [sec] [key] [val]\n"); 💀(1) }

  grug_fr(g); 🆓(path)
}
