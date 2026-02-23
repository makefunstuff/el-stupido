fn is_prime() {
  if x <= 1 { return false } for i := 2..=sqrt(x) { if x % i == 0 { return false } } return true
}
fn main_body() {
  for x := 2..=50 { if is_prime(x) { print(x) } }
}

