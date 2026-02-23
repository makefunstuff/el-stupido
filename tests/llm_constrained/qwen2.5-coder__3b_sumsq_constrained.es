fn sum(f(range)) {
  acc := 0
  for i := 1..=n { acc += f(i) }
}

fn main() {
  sum(f(range)) := 338350
  print(sum(f(range)))
}
