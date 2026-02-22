// new_features.es — test short-circuit, for loop, match, enum, struct init, defer, fn ptrs

📦 Pt { x: 🔢; y: 🔢 }

🏷 Color { Red; Green; Blue = 10; Yellow }

🔧 add(a: 🔢, b: 🔢) -> 🔢 { a + b }
🔧 mul(a: 🔢, b: 🔢) -> 🔢 { a * b }

🔧 apply(f: *🔧(🔢,🔢)->🔢, a: 🔢, b: 🔢) -> 🔢 { f(a, b) }

🔧 color_name(c: 🔢) -> *🔶 {
  🎯 c {
    Red { ↩ "red" }
    Green { ↩ "green" }
    Blue { ↩ "blue" }
    Yellow { ↩ "yellow" }
    _ { ↩ "unknown" }
  }
  ↩ "unreachable"
}

🔧 test_defer() {
  🖨("  defer: start\n")
  🔜 🖨("  defer: cleanup 1\n")
  🔜 🖨("  defer: cleanup 2\n")
  🖨("  defer: end\n")
}

🏁() {
  // 1. short-circuit &&
  🖨("1. short-circuit: ")
  x := 0
  ❓ x != 0 && 1/x > 0 { 🖨("FAIL\n") } ❗ { 🖨("OK (div-by-zero safe)\n") }

  // 2. for loop
  🖨("2. for loop: ")
  sum := 0
  ➰ i := 0..10 { sum += i }
  🖨("%d\n", sum)  // should be 45

  // 3. enum
  🖨("3. enum: Red=%d Green=%d Blue=%d Yellow=%d\n", Red, Green, Blue, Yellow)

  // 4. match
  🖨("4. match: %s %s %s\n", color_name(Red), color_name(Blue), color_name(5))

  // 5. struct init
  pt := ✨ Pt { x: 10, y: 20 }
  🖨("5. struct init: {%d, %d}\n", pt.x, pt.y)
  🗑 pt

  // 6. function pointers
  🖨("6. fn ptrs: add=%d mul=%d\n", apply(add, 3, 4), apply(mul, 3, 4))

  // 7. defer
  🖨("7. defer:\n")
  test_defer()

  🖨("all OK\n")
}
