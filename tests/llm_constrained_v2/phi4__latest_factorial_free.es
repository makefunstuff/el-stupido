-stupido
fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
fact(n) = product(1..=n)

fn main() {
    for i := 1..=12 { 
        print(fact(i)) 
    }
}