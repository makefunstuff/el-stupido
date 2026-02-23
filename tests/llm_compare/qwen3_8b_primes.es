fn is_prime(n) -> bool {
    if n < 2 { return false }
    for i := 2..=n-1 {
        if n % i == 0 { return false }
    }
    return true
}

fn main() {
    for i := 2..=50 {
        if is_prime(i) {
            print(i)
        }
    }
}
