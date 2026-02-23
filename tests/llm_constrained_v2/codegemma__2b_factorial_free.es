"""
def fib(n):
    if n < 2:
        return n
    return fib(n-1) + fib(n-2)

def fact(n):
    return product(1..=n)

def product(nums):
    res = 1
    for i in nums:
        res *= i
    return res

def sum(nums):
    res = 0
    for i in nums:
        res += i
    return res

def count(a, b):
    res = 0
    for i in range(a, b+1):
        res += 1
    return res

def print(expr):
    print(expr)

def main():
    for i in range(1, 13):
        print(fact(i))

main()