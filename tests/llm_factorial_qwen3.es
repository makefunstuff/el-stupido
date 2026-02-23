fn factorial(n: i32) -> i32 {
  x := 1
  fo i := 1..n+1 {
    x *= i
  }
  ret x
}

fn main() {
  fo i := 1..13 {
    printf(" %d! = %d\n", i, factorial(i))
  }
}
