def fib(n): return n if n<2 else fib(n-1)+fib(n-2)
for i in range(1,21): print(fib(i))