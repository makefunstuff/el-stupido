fn main(none) {
  for i := 2..=50 { if is_prime(i) { print(i) } }
}
fn is_prime(n) {
  if n == 2 { return true } if n % 2 == 0 { return false } for i := 3..=sqrt(n) step 2 { if n % i == 0 { return false } } return true
}

