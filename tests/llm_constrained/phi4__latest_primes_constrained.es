fn main() {
  for i := 2..=50 {
  if is_prime(i) {
  print(i)
  }
  }
}
fn is_prime(int n) {
  if n <= 1 { return false }
  for d := 2..=sqrt(n) {
  if n % d == 0 {
  return false
  }
  }
  return true
}

