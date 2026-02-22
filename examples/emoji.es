🔧 fizz(n: 🔢) -> 🔢 {
  ❓ n % 15 == 0 { printf("FizzBuzz\n") }
  ❗ ❓ n % 3 == 0 { printf("Fizz\n") }
  ❗ ❓ n % 5 == 0 { printf("Buzz\n") }
  ❗ { printf("%d\n", n) }
  ↩ 0
}

main() {
  i := 1
  🔁 i <= 20 {
    fizz(i)
    i += 1
  }
}
