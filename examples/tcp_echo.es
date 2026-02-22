📦 SA {
  fam: 📈
  port: 📈
  addr: 🔵
  z: [8]🔶
}

main() {
  fd := socket(2, 1, 0)
  ❓ fd < 0 { printf("!\n"); ↩ 1 }
  o: 🔢 = 1
  setsockopt(fd, 1, 2, &o, 4)
  a: SA
  memset(&a, 0, 16)
  a.fam = 2
  a.port = htons(8080)
  a.addr = 0
  ❓ fd.bind(&a, 16) < 0 { printf("!\n"); ↩ 1 }
  fd.listen(5)
  printf(":8080\n")
  🔁 1 {
    c := fd.accept(∅, ∅)
    ❓ c < 0 { printf("!\n"); ↩ 1 }
    printf("+\n")
    buf: [1024]🔶
    🔁 1 {
      n := c.read(&buf, 1024)
      ❓ n <= 0 { printf("-\n"); c.close(); ↩ 0 }
      c.write(&buf, n)
    }
    c.close()
  }
}
