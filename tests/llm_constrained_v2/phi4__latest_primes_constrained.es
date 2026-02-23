fn main() {
  for i := 2..=50 { if is_prime(i) { print(i) } }
}
fn is_prime(n) {
  if n <= 1 { return false }
  el { for d := 2..=sqrt(n) { if n % d == 0 { return false } } return true }
}

