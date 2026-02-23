fn fact(n: i32) -> i32 {
  x := 1
  fo i := 1..n+1 {
    x := x * i
  }
  ret x
}

fn main() {
  fo i := 1..13 {
    printf("fact(%d) = %d\n", i-1, fact(i-1))
  }
}
