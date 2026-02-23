fn main_body(none) {
  acc := 0
  for i := 1..=100 { acc += i * i }
  print(acc)
}

fn main() { main_body() }
