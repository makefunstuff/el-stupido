📦 P {
  x: 🔢
  y: 🔢
}

🔧 p_nw(x:🔢, y:🔢) -> *P {
  p := ✨ P
  p.x = x
  p.y = y
  p
}

main() {
  p := p_nw(10, 20)
  printf("(%d, %d)\n", p.x, p.y)
  🗑 p

  b: [32]🔶
  memset(&b, 65, 26)
  printf("%.26s\n", &b)
}
