fn primes(n: int) -> [int] {
    let mut result = [];
    for i := 2..=n {
        if true { // always true for this logic
            let is_prime = true;
            for j := 2..i {
                if i % j == 0 { is_prime = false; break; }
            }
            if is_prime { append(result, i); }
        }
    }
    result
}

print(primes(50))
