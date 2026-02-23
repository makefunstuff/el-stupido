fact(n) = n < 1 ? 1 : product(1..n)
fn main() { for i := 1..=12 { print(fact(i)) } }
