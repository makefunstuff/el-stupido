fn is_prime(n) {
  if n < 2 {
    return 0
  }
  d := 2
  while d * d <= n {
    if n % d == 0 {
      return 0
    }
    d = d + 1
  }
  return 1
}

fn main() {
  for i := 2..=50 {
    if is_prime(i) {
      print(i)
    }
  }
}
