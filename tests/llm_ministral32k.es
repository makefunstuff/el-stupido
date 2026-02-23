fn fact(n: i32) -> i32 {
  res := 1
  fo i := 1..n+1 {
    res := res * i
  }
  ret res
}

fn main() {
  fo i := 1..13 {
    printf("fact(%d) = %d\n", i-1, fact(i-1))
  }
}
