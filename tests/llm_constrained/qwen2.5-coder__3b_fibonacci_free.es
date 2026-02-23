{
  "fib": {
    "body": "n < 2 ? n : fib(n-1) + fib(n-2)"
  },
  "print": {
    "args": ["a..=b"],
    "body": "f(a)..=f(b)"
  },
  "for": {
    "args": ["i := 1..=n", "{ print(fib(i)) }"]
  }
}