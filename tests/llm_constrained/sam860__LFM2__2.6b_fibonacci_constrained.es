fib(n: number) = n < 2 ? n : fib(n - 1) + fib(n - 2)

fn main() {
  const fib = fib
  // reference to the fib function
  for (let i = 1
  i <= 20
  i++) {
  console.log(fib(i))
  }
  // Helper function to print range using ternary and range notation
  function printRange(start, end) {
  console.log(start..=end)
  }
  printRange(1, 20)
  // Equivalent to printing fib(1)...fib(20)
  // To explicitly print first 20 fibonacci numbers (as requested):
  let fibSequence = []
  for (let i = 1
  i <= 20
  i++) {
  fibSequence.push(fib(i))
  }
  console.log('First 20 Fibonacci numbers:', fibSequence)
  // Optional: print the range using ternary and range notation
  console.log('Range 1..20:', printRange(1, 20))
  // Optional: print the result of the ternary expression (though redundant here)
  console.log('f(1)...f(20):', printRange(1, 20).split('').join(' ') || 'Range not printed')
  // Output the recursive function definition for clarity
  console.log('Recursive Fibonacci function:
  ' + fib + '
  ')
  // Optional: print the recursive call stack (for educational purposes only)
  console.log('Sample recursive call stack (simplified):
  f(20) -> f(19)+f(18) -> ... -> f(2)+f(1) -> 0,1
  ')
  // Note: The actual recursive stack depth is ~21 levels for fib(20)
  console.log('Recursive calls for fib(20):', fib(20).toString())
  // Final summary
  console.log('
  Summary:
  - Defined recursive fib(n) as per specification.
  - Printed first 20 Fibonacci numbers.
  - Demonstrated ternary and range notation.
  - Included optional recursive call stack and function definition.
  ')
}
