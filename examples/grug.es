// grug.es — 📂🔑👉 config parser

📦 KV { k: *🔶; val: *🔶; nx: *KV }
📦 Sec { nm: *🔶; kv: *KV; nx: *Sec }
📦 Grug { sec: *Sec; buf: *🔶 }

// byte at offset as 🔢
🔧 gb(p: *🔶, i: 🔢) -> 🔢 { *(p + i) 🔄 🔢 }

// 4-byte emoji match
🔧 eq4(p: *🔶, a: 🔢, b: 🔢, c: 🔢, d: 🔢) -> 🔢 {
  gb(p,0)==a && gb(p,1)==b && gb(p,2)==c && gb(p,3)==d
}

// 📂 F0 9F 93 82
🔧 is_sec(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x93, 0x82) }
// 🔑 F0 9F 94 91
🔧 is_key(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x94, 0x91) }
// 👉 F0 9F 91 89
🔧 is_arr(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x91, 0x89) }

// skip variation selector U+FE0F (EF B8 8F)
🔧 skv(p: *🔶) -> *🔶 {
  ❓ gb(p,0)==0xEF && gb(p,1)==0xB8 && gb(p,2)==0x8F { ↩ p + 3 }
  ↩ p
}

// skip spaces+tabs
🔧 sw(p: *🔶) -> *🔶 {
  🔁 *p == 32 || *p == 9 { p = p + 1 }
  ↩ p
}

// find newline or end
🔧 fnl(p: *🔶) -> *🔶 {
  🔁 *p != 0 && *p != 10 { p = p + 1 }
  ↩ p
}

// advance past newline
🔧 anl(p: *🔶) -> *🔶 {
  ❓ *p == 13 { p = p + 1 }
  ❓ *p == 10 { p = p + 1 }
  ↩ p
}

// trim trailing ws, null-terminate
🔧 nt(s: *🔶, e: *🔶) {
  🔁 e 🔄 🔷 > s 🔄 🔷 && (*(e-1)==32 || *(e-1)==9 || *(e-1)==13) {
    e = e - 1
  }
  *e = 0
}

// strdup
🔧 sd(s: *🔶) -> *🔶 {
  l := 🧵(s) 🔄 🔢
  d := 🧠(l + 1) 🔄 *🔶
  📋(d, s, l + 1)
  ↩ d
}

// read whole file
🔧 slurp(path: *🔶) -> *🔶 {
  fd := 📂(path, 0)
  ❓ fd < 0 { ↩ 0 🔄 *🔶 }
  fsz := 🔖(fd, 0, 2)
  🔖(fd, 0, 0)
  buf := 🧠(fsz + 1) 🔄 *🔶
  📖(fd, buf, fsz)
  *(buf + fsz) = 0
  📕(fd)
  ↩ buf
}

// parse .grug file
🔧 grug_parse(path: *🔶) -> *Grug {
  buf := slurp(path)
  ❓ buf 🔄 🔷 == 0 { ↩ 0 🔄 *Grug }

  g := ✨ Grug
  g.sec = 0 🔄 *Sec
  g.buf = buf
  cur: *Sec = 0 🔄 *Sec
  p := buf

  🔁 *p != 0 {
    p = sw(p)
    ❓ *p == 0 { 🛑 }

    // blank
    ❓ *p == 10 || *p == 13 {
      p = anl(p)
    }
    // # comment
    ❗ ❓ *p == 35 {
      p = anl(fnl(p))
    }
    // 📂 section
    ❗ ❓ is_sec(p) {
      p = sw(skv(p + 4))
      nm := p
      nl := fnl(p)
      nx := anl(nl)
      nt(nm, nl)

      s := ✨ Sec
      s.nm = sd(nm)
      s.kv = 0 🔄 *KV
      s.nx = 0 🔄 *Sec

      ❓ g.sec 🔄 🔷 == 0 { g.sec = s }
      ❗ {
        t := g.sec
        🔁 t.nx 🔄 🔷 != 0 { t = t.nx }
        t.nx = s
      }
      cur = s
      p = nx
    }
    // 🔑 key 👉 value
    ❗ ❓ is_key(p) && cur 🔄 🔷 != 0 {
      p = sw(skv(p + 4))
      ks := p

      // scan for 👉
      🔁 *p != 0 && *p != 10 {
        ❓ is_arr(p) { 🛑 }
        p = p + 1
      }

      ❓ is_arr(p) {
        sep := p
        vs := sw(skv(sep + 4))
        nl := fnl(vs)
        nx := anl(nl)
        nt(ks, sep)
        nt(vs, nl)

        kv := ✨ KV
        kv.k = sd(ks)
        kv.val = sd(vs)
        kv.nx = 0 🔄 *KV

        ❓ cur.kv 🔄 🔷 == 0 { cur.kv = kv }
        ❗ {
          t := cur.kv
          🔁 t.nx 🔄 🔷 != 0 { t = t.nx }
          t.nx = kv
        }
        p = nx
      } ❗ {
        p = anl(fnl(p))
      }
    }
    // skip unknown
    ❗ {
      p = anl(fnl(p))
    }
  }

  ↩ g
}

// lookup: grug_get(g, "section", "key") -> value or null
🔧 grug_get(g: *Grug, sec: *🔶, key: *🔶) -> *🔶 {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    ❓ ⚔(s.nm, sec) == 0 {
      kv := s.kv
      🔁 kv 🔄 🔷 != 0 {
        ❓ ⚔(kv.k, key) == 0 { ↩ kv.val }
        kv = kv.nx
      }
      ↩ 0 🔄 *🔶
    }
    s = s.nx
  }
  ↩ 0 🔄 *🔶
}

// dump all
🔧 grug_dump(g: *Grug) {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    🖨("📂 %s\n", s.nm)
    kv := s.kv
    🔁 kv 🔄 🔷 != 0 {
      🖨("  🔑 %s 👉 %s\n", kv.k, kv.val)
      kv = kv.nx
    }
    s = s.nx
  }
}

// free all
🔧 grug_fr(g: *Grug) {
  s := g.sec
  🔁 s 🔄 🔷 != 0 {
    ns := s.nx
    kv := s.kv
    🔁 kv 🔄 🔷 != 0 {
      nk := kv.nx
      🗑 kv.k; 🗑 kv.val; 🗑 kv
      kv = nk
    }
    🗑 s.nm; 🗑 s
    s = ns
  }
  🗑 g.buf; 🗑 g
}

🏁() {
  g := grug_parse("context.grug")
  ❓ g 🔄 🔷 == 0 {
    🖨("❌ context.grug\n")
    💀(1)
  }

  🖨("=== dump ===\n")
  grug_dump(g)

  🖨("\n=== lookup ===\n")
  n := grug_get(g, "project", "name")
  ❓ n 🔄 🔷 != 0 { 🖨("project.name = %s\n", n) }
  b := grug_get(g, "build", "cmd")
  ❓ b 🔄 🔷 != 0 { 🖨("build.cmd = %s\n", b) }

  grug_fr(g)
}
