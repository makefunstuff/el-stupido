fn is_prime(n int) {
  if n <= 1 { return false } for i := 2..=int(math.Sqrt(float64(n))) { if n % i == 0 { return false } } return true
}
fn main_body() {
  for i := 2
  i <= 50
  i++ { if is_prime(i) { print(i) } }
}

fn main() { main_body() }
