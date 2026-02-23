product([1, 2, 3, 4, 5, 6, 7, 8, 9, 10]) = reduce((acc, cur) => acc * cur, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
fact([1..=12]) = product(1..=$index) * $args[$index]

fn main() {
  for (let i = 1
  i <= 12
  i++) { print(fact(i))) }
}
