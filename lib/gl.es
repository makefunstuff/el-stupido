// lib/gl.es — WebGL bridge imports (provided by JS glue)
// compile with: ./esc prog.es --wasm -o prog.wasm

🔌 gl_clear(🔢, 🔢, 🔢)                    // clear canvas (r,g,b 0-255)
🔌 gl_rect(🔢, 🔢, 🔢, 🔢, 🔢, 🔢, 🔢)    // draw rect (x,y,w,h, r,g,b)
🔌 js_random() -> 🔢                        // random i32 from JS
