fn fact(n: i32) -> i64 {
  x := i64(1)
  fo i := 1..n {
    x *= i
  }
  ret x
}

fn main() {
  fo i := 1..13 {
    printf("fact(%d) = %lld\n", i, fact(i))
  }
}
