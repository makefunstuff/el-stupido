fn sum_of_squares(n) {
  acc := 0
  for i := 1..=n { acc += i * i }
  return acc
}

fn main() {
  print(sum_of_squares(100))
}
