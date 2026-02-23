// lib/wasm.es — browser/WASM prelude (imports from JS glue)
// use: 📥 wasm
// compile: ./esc prog.es --wasm -o prog.wasm

// --- console output (provided by JS glue) ---
🔌 log_int(🔢)                      // console.log(n)
🔌 log_str(*🔶)                     // console.log(str from WASM memory)
🔌 log_float(🌀)                    // console.log(f)

// --- DOM manipulation ---
🔌 dom_set_text(*🔶, *🔶)          // document.querySelector(sel).textContent = str
🔌 dom_set_html(*🔶, *🔶)          // document.querySelector(sel).innerHTML = str
🔌 dom_get_val(*🔶, *🔶, 🔢) -> 🔢  // read input value into buf, returns len
🔌 dom_add_class(*🔶, *🔶)         // el.classList.add(cls)
🔌 dom_rm_class(*🔶, *🔶)          // el.classList.remove(cls)
🔌 dom_on(*🔶, *🔶, 🔢)            // addEventListener(sel, event, callback_id)

// --- timer ---
🔌 set_timeout(🔢, 🔢)             // setTimeout(callback_id, ms)
🔌 set_interval(🔢, 🔢) -> 🔢      // setInterval(callback_id, ms), returns id
🔌 clear_interval(🔢)              // clearInterval(id)

// --- math (WASM native, no import needed) ---
// standard arithmetic, bitwise ops work directly

// --- memory (WASM linear memory) ---
// WASM exports memory automatically via --export-all
// JS glue can read/write WASM memory directly

// --- simple bump allocator (no free, for small programs) ---
🔧 _heap_ptr() -> *🔢 {
  hp: 🔢 = 0; ↩ &hp
}
🔧 walloc(sz: 🔢) -> *🔶 {
  // first call: init heap to 64KB mark (above stack)
  hp := _heap_ptr()
  ❓ *hp == 0 { *hp = 65536 }
  ptr := *hp
  // align to 8
  ptr = (ptr + 7) & -8
  *hp = ptr + sz
  ↩ ptr 🔄 *🔶
}

// --- string helpers (no libc in freestanding WASM) ---
🔧 wstrlen(s: *🔶) -> 🔢 {
  i := 0; 🔁 *(s+i) != 0 { i += 1 }; ↩ i
}
🔧 wstrcpy(dst: *🔶, src: *🔶) -> *🔶 {
  i := 0; 🔁 *(src+i) != 0 { *(dst+i) = *(src+i); i += 1 }
  *(dst+i) = 0; ↩ dst
}
🔧 wmemcpy(dst: *🔶, src: *🔶, n: 🔢) {
  i := 0; 🔁 i < n { *(dst+i) = *(src+i); i += 1 }
}
🔧 wmemset(dst: *🔶, val: 🔢, n: 🔢) {
  i := 0; 🔁 i < n { *(dst+i) = val 🔄 🔶; i += 1 }
}

// --- int to string (for display without printf) ---
🔧 itoa_buf(n: 🔢, buf: *🔶) -> *🔶 {
  ❓ n == 0 { *buf = 48; *(buf+1) = 0; ↩ buf }
  neg := 0; ❓ n < 0 { neg = 1; n = 0 - n }
  i := 0; tmp: [16]🔶
  🔁 n > 0 {
    tp := &tmp 🔄 *🔶
    *(tp+i) = (n % 10 + 48) 🔄 🔶; n = n / 10; i += 1
  }
  j := 0
  ❓ neg != 0 { *buf = 45; j = 1 } // '-'
  k := i - 1
  tp2 := &tmp 🔄 *🔶
  🔁 k >= 0 { *(buf+j) = *(tp2+k); j += 1; k -= 1 }
  *(buf+j) = 0; ↩ buf
}
