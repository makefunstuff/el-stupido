fn main(none) {
  for i := 2..=50 { if is_prime(i) { print(i) } }
}
fn is_prime(n) {
  for j := 2..=n-1 { if n % j == 0 { break } } return n > 1
}

