fn main() {
  for i := 2..=50 {
  if i % 2 == 0 {
  continue
  }
  for j := 3..=(i-1) step 2 {
  if i % j == 0 {
  break
  }
  }
  print(i)
  }
}
