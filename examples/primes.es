🏁() {
  n := 1000
  s := 🧠(n) 🔄 *🔶
  🧹(s, 1, n)
  i := 2
  🔁 i * i < n {
    ❓ *(s + i) {
      j := i * i
      🔁 j < n {
        *(s + j) = 0
        j = j + i
      }
    }
    i = i + 1
  }
  c := 0; i = 2
  🔁 i < n {
    ❓ *(s + i) { c = c + 1 }
    i = i + 1
  }
  🖨("primes below %d: %d\n", n, c)
  🗑 s
}
