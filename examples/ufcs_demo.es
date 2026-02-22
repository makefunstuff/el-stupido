📦 V2 {
  x: 🌀
  y: 🌀
}

🔧 v2_nw(x:🌀, y:🌀) -> *V2 {
  p := ✨ V2
  p.x = x; p.y = y
  p
}

🔧 v2_add(a:*V2, b:*V2) -> *V2 { v2_nw(a.x + b.x, a.y + b.y) }
🔧 v2_len(p:*V2) -> 🌀 { sqrt(p.x * p.x + p.y * p.y) }
🔧 v2_pr(p:*V2) { printf("(%.2f, %.2f)\n", p.x, p.y) }

main() {
  a := v2_nw(3.0, 4.0)
  b := v2_nw(1.0, 2.0)
  printf("a = "); a.v2_pr()
  printf("b = "); b.v2_pr()
  c := a.v2_add(b)
  printf("a+b = "); c.v2_pr()
  printf("|a| = %.4f\n", a.v2_len())
  printf("|b| = %.4f\n", b.v2_len())
  printf("|a+b| = %.4f\n", c.v2_len())
  🗑 a; 🗑 b; 🗑 c
}
