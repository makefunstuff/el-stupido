fn fact(n: i32) -> i64 {
  x := 1i64
  fo i := 1..(n+1) { x = x * i }
  ret x
}

fn main() {
  fo i := 1..13 { printf("fact(%d) = %lld\n", i, fact(i)) }
}
