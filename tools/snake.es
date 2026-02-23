// snake.es — WebGL snake game (compiles to WASM)
📥 gl

// game state lives at fixed WASM memory addresses
// snake body: x[400] at 65536, y[400] at 67136
// game vars at 68736
📦 GS { slen: 🔢; dir: 🔢; ndir: 🔢; fx: 🔢; fy: 🔢; score: 🔢; over: 🔢; w: 🔢; h: 🔢 }

🔧 bx(i: 🔢) -> *🔢 { ↩ (65536 + i * 4) 🔄 *🔢 }
🔧 by(i: 🔢) -> *🔢 { ↩ (67136 + i * 4) 🔄 *🔢 }
🔧 gs() -> *GS { ↩ 68736 🔄 *GS }

🔧 place_food() {
  g := gs()
  🔁 1 {
    fx := js_random() % g.w; fy := js_random() % g.h
    // check not on snake
    hit := 0; i := 0
    🔁 i < g.slen {
      ❓ *bx(i) == fx && *by(i) == fy { hit = 1 }
      i += 1
    }
    ❓ hit == 0 { g.fx = fx; g.fy = fy; ↩ }
  }
}

🔧 init() {
  g := gs(); g.w = 20; g.h = 20; g.score = 0; g.over = 0
  g.slen = 3; g.dir = 1; g.ndir = 1
  // start in center
  cx := g.w / 2; cy := g.h / 2
  *bx(0) = cx; *by(0) = cy
  *bx(1) = cx - 1; *by(1) = cy
  *bx(2) = cx - 2; *by(2) = cy
  place_food()
}

🔧 set_dir(d: 🔢) {
  g := gs()
  // prevent 180-degree turn
  ❓ d == 0 && g.dir != 2 { g.ndir = 0 }
  ❗ ❓ d == 1 && g.dir != 3 { g.ndir = 1 }
  ❗ ❓ d == 2 && g.dir != 0 { g.ndir = 2 }
  ❗ ❓ d == 3 && g.dir != 1 { g.ndir = 3 }
}

🔧 tick() {
  g := gs()
  ❓ g.over != 0 { ↩ }
  g.dir = g.ndir

  // compute new head position
  hx := *bx(0); hy := *by(0)
  ❓ g.dir == 0 { hy -= 1 }        // up
  ❗ ❓ g.dir == 1 { hx += 1 }     // right
  ❗ ❓ g.dir == 2 { hy += 1 }     // down
  ❗ { hx -= 1 }                    // left

  // wall collision
  ❓ hx < 0 || hx >= g.w || hy < 0 || hy >= g.h { g.over = 1; ↩ }

  // self collision
  i := 0
  🔁 i < g.slen {
    ❓ *bx(i) == hx && *by(i) == hy { g.over = 1; ↩ }
    i += 1
  }

  // check food
  ate := hx == g.fx && hy == g.fy

  // shift body
  ❓ ate != 0 { g.slen += 1 }
  i = g.slen - 1
  🔁 i > 0 { *bx(i) = *bx(i-1); *by(i) = *by(i-1); i -= 1 }
  *bx(0) = hx; *by(0) = hy

  ❓ ate != 0 { g.score += 1; place_food() }
}

🔧 render() {
  g := gs()

  // background
  gl_clear(26, 26, 46)

  // grid lines (subtle)
  i := 0
  🔁 i < g.w {
    j := 0
    🔁 j < g.h {
      gl_rect(i, j, 1, 1, 22, 33, 62)
      j += 1
    }
    i += 1
  }

  // food
  gl_rect(g.fx, g.fy, 1, 1, 233, 69, 96)

  // snake body
  i = g.slen - 1
  🔁 i >= 0 {
    ❓ i == 0 {
      gl_rect(*bx(i), *by(i), 1, 1, 0, 230, 118)   // head: bright green
    } ❗ {
      gl_rect(*bx(i), *by(i), 1, 1, 0, 180, 90)     // body: darker green
    }
    i -= 1
  }
}

🔧 get_score() -> 🔢 { ↩ gs().score }
🔧 is_over() -> 🔢 { ↩ gs().over }
🔧 restart() { init() }
