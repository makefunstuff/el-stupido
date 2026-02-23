fn main_body(i) {
  if i % 3 == 0 { printf("Fizz")
  }
  el if i % 5 == 0 { printf("Buzz")
  }
  el { print(i)
  }
}
fn fizzbuzz_loop() {
  for i := 1..=30 { main_body(i) }
}

fn main() {
  if i % 5 == 0 && i % 3 != 0 { print("Buzz")
  }
  el if i % 3 == 0 && i % 5 != 0 { print("Fizz")
  }
  el { print(i)
  }
}
