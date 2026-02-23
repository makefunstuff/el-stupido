fn fib(n) {
  if (n < 2) return n
  else return fib(n-1) + fib(n-2)
}

fn main() {
  for (let i = 1
  i <= 20
  i++) console.log(fib(i))
}
