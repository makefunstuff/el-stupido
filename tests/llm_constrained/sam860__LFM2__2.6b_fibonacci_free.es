fib = n => n < 2 ? n : fib(n-1) + fib(n-2);
for (let i = 1; i <= 20; i++) {
  console.log(fib(i));
}