recursive_fib([n]) = n < 2 ? n : recursive_fib(n-1) + recursive_fib(n-2)
ternary([a, b], print([cond? a:b]) prints [print (a..=b)]) = [ cond ? a : b ] => Print ([ f(x)...f(y) )]
fn for_loop([i,1 ..=  n]) {
  [ i:=  1..n
  print ( recursive_fib(i)) ]
}

fn main() {
  'print first 20 fibonacci numbers' => for x in range[1 to 20]: Print ([ ternary? FIBONACCI.recursive_fib:x, TERMINATION] )
}
