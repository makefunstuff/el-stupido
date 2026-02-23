fn fact(n: i32) -> i64 {
  var r := 1i64
  fo i := 2..(n+1) { r *= i }
  ret r
}
fn main() {
  fo i := 1..13 { printf("fact(%d) = %lld\n", i, fact(i)) }
}
