name(fibonacci) = fib(n)
    if n == 0 then
        return 0
    else if n == 1 then
        return 1
    else
        return fibonacci(n-1) + fibonacci(n-2)

print(fib(20))
