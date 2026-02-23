fn sum(range) {
  acc := 0
  for i := range { acc += i * i }
  return acc
}

fn main() {
  print(sum(1..=100))
}
