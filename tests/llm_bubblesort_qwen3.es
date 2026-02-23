fn main() {
  arr := nw *i32 (40)
  *(arr + 0) := 9
  *(arr + 1) := 3
  *(arr + 2) := 7
  *(arr + 3) := 1
  *(arr + 4) := 8
  *(arr + 5) := 2
  *(arr + 6) := 6
  *(arr + 7) := 4
  *(arr + 8) := 0
  *(arr + 9) := 5
  n := 10
  fo i := 0..n-1 {
    fo j := 0..n-1-i-1 {
      if *(arr + j) > *(arr + j + 1) {
        temp := *(arr + j)
        *(arr + j) := *(arr + j + 1)
        *(arr + j + 1) := temp
      }
    }
  }
  fo i := 0..9 {
    printf("%d ", *(arr + i))
  }
}
