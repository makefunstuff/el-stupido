fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
fn print_fibonacci(n) {
  for i := 1..=n { print(fib(i)) }
}

fn main() {
  print_fibonacci(20)
}
