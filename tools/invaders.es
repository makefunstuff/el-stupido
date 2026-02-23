// invaders.es — WebGL Space Invaders game (compiles to WASM)
📥 gl

// game state at fixed WASM memory addresses
// aliens:  ax[50] @ 65536, ay[50] @ 65736
// bullets: bx[20] @ 65936, by[20] @ 66016
// game state struct @ 66096
📦 GS { acount: 🔢; bcount: 🔢; player_x: 🔢; adir: 🔢; atick: 🔢; score: 🔢; over: 🔢; win: 🔢; w: 🔢; h: 🔢 }

🔧 ax(i: 🔢) -> *🔢 { ↩ (65536 + i * 4) 🔄 *🔢 }
🔧 ay(i: 🔢) -> *🔢 { ↩ (65736 + i * 4) 🔄 *🔢 }
🔧 bx(i: 🔢) -> *🔢 { ↩ (65936 + i * 4) 🔄 *🔢 }
🔧 by(i: 🔢) -> *🔢 { ↩ (66016 + i * 4) 🔄 *🔢 }
🔧 gs() -> *GS { ↩ 66096 🔄 *GS }

🔧 init() {
  g := gs()
  g.w = 20; g.h = 20
  g.player_x = 9
  g.acount = 20
  g.bcount = 0
  g.adir = 1
  g.atick = 0
  g.score = 0
  g.over = 0
  g.win = 0

  // spawn aliens in 4 rows x 5 columns
  // NOTE: all vars declared outside loop to avoid WASM stack leak
  i := 0
  🔁 i < 20 {
    *ax(i) = 2 + (i % 5) * 3
    *ay(i) = 1 + (i / 5) * 2
    i += 1
  }
}

🔧 set_input(dir: 🔢, fire: 🔢) {
  g := gs()
  ❓ g.over != 0 || g.win != 0 { ↩ }

  ❓ dir == -1 && g.player_x > 0 { g.player_x -= 1 }
  ❗ ❓ dir == 1 && g.player_x < 19 { g.player_x += 1 }

  ❓ fire != 0 && g.bcount < 10 {
    *bx(g.bcount) = g.player_x
    *by(g.bcount) = 18
    g.bcount += 1
  }
}

🔧 tick() {
  g := gs()
  ❓ g.over != 0 || g.win != 0 { ↩ }

  // all loop vars declared up front to avoid WASM stack leak
  i := 0
  j := 0
  bi := 0
  ai := 0
  hit := 0
  step_down := 0

  // move bullets up
  i = 0
  🔁 i < g.bcount {
    *by(i) -= 1
    ❓ *by(i) < 0 {
      // remove bullet by shifting
      j = i
      🔁 j < g.bcount - 1 {
        *bx(j) = *bx(j + 1)
        *by(j) = *by(j + 1)
        j += 1
      }
      g.bcount -= 1
    } ❗ {
      i += 1
    }
  }

  // move aliens periodically
  g.atick += 1
  ❓ g.atick >= 30 {
    g.atick = 0
    step_down = 0

    i = 0
    🔁 i < g.acount {
      *ax(i) += g.adir
      ❓ *ax(i) <= 0 || *ax(i) >= 19 { step_down = 1 }
      i += 1
    }

    ❓ step_down != 0 {
      g.adir = g.adir * -1
      i = 0
      🔁 i < g.acount {
        *ay(i) += 1
        i += 1
      }
    }
  }

  // check bullet-alien collisions
  bi = 0
  🔁 bi < g.bcount {
    hit = 0
    ai = 0
    🔁 ai < g.acount {
      ❓ *bx(bi) == *ax(ai) && *by(bi) == *ay(ai) {
        // remove alien
        j = ai
        🔁 j < g.acount - 1 {
          *ax(j) = *ax(j + 1)
          *ay(j) = *ay(j + 1)
          j += 1
        }
        g.acount -= 1
        g.score += 10
        hit = 1
      }
      ai += 1
    }
    ❓ hit != 0 {
      // remove bullet
      j = bi
      🔁 j < g.bcount - 1 {
        *bx(j) = *bx(j + 1)
        *by(j) = *by(j + 1)
        j += 1
      }
      g.bcount -= 1
    } ❗ {
      bi += 1
    }
  }

  // win condition
  ❓ g.acount == 0 { g.win = 1 }

  // check if aliens reached the bottom
  i = 0
  🔁 i < g.acount {
    ❓ *ay(i) >= 19 { g.over = 1 }
    i += 1
  }
}

🔧 render() {
  g := gs()
  gl_clear(26, 26, 46)

  // all vars declared up front
  i := 0
  j := 0

  // grid background
  i = 0
  🔁 i < g.w {
    j = 0
    🔁 j < g.h {
      gl_rect(i, j, 1, 1, 22, 33, 62)
      j += 1
    }
    i += 1
  }

  // aliens (red)
  i = 0
  🔁 i < g.acount {
    gl_rect(*ax(i), *ay(i), 1, 1, 220, 50, 50)
    i += 1
  }

  // bullets (green)
  i = 0
  🔁 i < g.bcount {
    gl_rect(*bx(i), *by(i), 1, 1, 100, 230, 100)
    i += 1
  }

  // player (cyan)
  gl_rect(g.player_x, 19, 1, 1, 0, 200, 255)
}

🔧 get_score() -> 🔢 { ↩ gs().score }
🔧 is_over() -> 🔢 { ↩ gs().over }
🔧 is_win() -> 🔢 { ↩ gs().win }
🔧 restart() { init() }
