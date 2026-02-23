fn main() {
  for i := 2..=50 { if is_prime(i) { print(i) } } bool is_prime(n: int): bool { for d := 2..=(n-1) { if n % d == 0 { return false } } return true }
}
