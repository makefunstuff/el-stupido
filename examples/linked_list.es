📦 N {
  d: 🔢
  nx: *N
}

🔧 n_nw(d:🔢, nx:*N) -> *N {
  n := ✨ N
  n.d = d
  n.nx = nx
  n
}

🔧 l_pr(h:*N) {
  c := h
  🔁 c 🔄 🔷 != 0 {
    printf("%d", c.d)
    ❓ c.nx 🔄 🔷 != 0 { printf(" -> ") }
    c = c.nx
  }
  printf("\n")
}

🔧 l_rev(h:*N) -> *N {
  p: *N = 0 🔄 *N
  c := h
  🔁 c 🔄 🔷 != 0 {
    nx := c.nx
    c.nx = p
    p = c
    c = nx
  }
  p
}

🔧 l_fr(h:*N) {
  c := h
  🔁 c 🔄 🔷 != 0 {
    nx := c.nx
    🗑 c
    c = nx
  }
}

main() {
  h: *N = 0 🔄 *N
  i := 5
  🔁 i >= 1 {
    h = n_nw(i, h)
    i = i - 1
  }
  printf(">> "); l_pr(h)
  h = l_rev(h)
  printf("<< "); l_pr(h)
  l_fr(h)
}
