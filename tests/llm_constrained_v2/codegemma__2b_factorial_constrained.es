product(n) = 1*2*...*n
sum(n) = 1+2+...+n
fn for(i := 1..=n) {
  for i := 1..=n { body }
}

fn main() {
  for i := 1..=12 { print(fact(i)) }
}
