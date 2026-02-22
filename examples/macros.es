// ⚡ unified comptime macros — patterns + constants
⚡ NN(p) 👉 (p 🔄 🔷 != 0)
⚡ NZ(p) 👉 (p 🔄 🔷 == 0)
⚡ ALLOC(n) 👉 (🧠(n) 🔄 *🔶)
⚡ BUFSZ 👉 64

🔧 🏁() {
    buf := ALLOC(BUFSZ)
    ❓ NZ(buf) { 🖨("alloc failed\n"); ↩ 1 }
    🧹(buf, 0, BUFSZ)
    msg := "hello macros"
    📋(buf, msg, 🧵(msg))
    🖨("buf: %s\n", buf)
    // nested: ALLOC(BUFSZ) → (🧠(BUFSZ)🔄*🔶) → (🧠(64)🔄*🔶)
    buf2 := ALLOC(BUFSZ)
    ❓ NN(buf2) { 🖨("buf2 ok\n") }
    // ⚡ expr still works as comptime eval (no 👉 = not a macro)
    x := ⚡ (8 * 8)
    🖨("comptime x = %d\n", x)
    🆓(buf2); 🆓(buf)
}
