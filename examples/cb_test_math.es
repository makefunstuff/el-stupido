fn fact(n: i32) -> i32 {
  if n <= 1 { ret 1 }
  ret n * fact(n - 1)
}

fn fib(n: i32) -> i32 {
  if n <= 1 { ret n }
  ret fib(n - 1) + fib(n - 2)
}

use test
test "factorial of 0" { assert fact(0) == 1 }
test "factorial of 5" { assert fact(5) == 120 }
test "factorial of 10" { assert fact(10) == 3628800 }
test "fib(0) is 0" { assert fib(0) == 0 }
test "fib(10) is 55" { assert fib(10) == 55 }
test "intentional fail" { assert 1 == 2 }
