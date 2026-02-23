fn sum([integer] n) {
  acc = 0
  for i = integer 1..= ($input[0]) {
  acc += $i * $i
  }
  expr = $acc end_expr
}

fn main() {
  data = 100
  sum (data)
  exit main end.
}
