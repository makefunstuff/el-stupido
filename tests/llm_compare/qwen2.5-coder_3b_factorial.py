Certainly! Here is the Python program that prints factorials from 1 to 12:

for i in range(1, 13):
    factorial = 1
    for j in range(1, i + 1):
        factorial *= j
    print(factorial)

This program uses a nested loop structure. The outer loop iterates over numbers from 1 to 12, and the inner loop calculates the factorial of each number by multiplying all integers from 1 up to that number. Finally, it prints the result for each factorial.
