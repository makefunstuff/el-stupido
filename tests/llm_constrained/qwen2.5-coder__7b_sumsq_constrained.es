fn sum_of_squares(n) {
  acc := 0
  for i := 1..=n { acc += i * i }
  return acc
}

fn main() {
  result := sum_of_squares(100)
  print(result)
}
