fn sum_of_squares() {
  sum = 0
  for i := 1..=100 { sum += i * i
  }
}

fn main() {
  sum_of_squares(100) { ... }
}
