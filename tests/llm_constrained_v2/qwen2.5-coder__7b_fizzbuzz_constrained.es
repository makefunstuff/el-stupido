fn main_body() {
  for i := 1..=30 { if i % 15 == 0 { printf("FizzBuzz\n", "") }
  el if i % 3 == 0 { printf("Fizz\n", "") }
  el if i % 5 == 0 { printf("Buzz\n", "") }
  el { printf("%d\n", i) } }
}

fn main() { main_body() }
