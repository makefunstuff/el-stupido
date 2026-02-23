fn sum_squares(n: number) {
  let acc := 0
  for i := 1..=n do acc := acc + i * i
  return acc
}
fn main() {
  let sum_squares = sum_squares(100)
  print(sum_squares)
}

