fn main() {
  for i := 2..=50 {
  is_prime := true
  for j := 2
  j < i
  j++ {
  if i % j == 0 {
  is_prime = false
  break
  }
  }
  if is_prime {
  print(i)
  }
  }
}
