fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)

fn main() {
  for i := 1..=20 { print(fib(i)) }
}
