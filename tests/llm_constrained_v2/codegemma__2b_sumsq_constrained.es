fn sum(n) {
  acc := 0
  for i := 1..n { acc += i * i }
}

fn main() {
  print(sum(100))
}
