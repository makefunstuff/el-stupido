fn factorial(n: u32) -> u64 {
  let mut result = 1u64;
  fo i := 1..=n { result *= i as u64; }
  ret result
}

fn main() {
  fo i := 1..=12 {
    printf("factorial(%d) = %llu\n", i, factorial(i));
  }
}
