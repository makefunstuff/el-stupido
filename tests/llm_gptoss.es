fn fact(n: i32) -> i32 {
  x := 1
  fo i := 2..n+1 { x := x * i }
  ret x
}
fn main() {
  fo i := 1..13 { printf("%d! = %d\n", i, fact(i)) }
}
