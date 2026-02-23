let x = 2;
while (x <= 50) {
    if (x === 2 || !(x % 2 === 0)) {
        let isPrime = true;
        for (let i = 3; i * i <= x; i += 2) {
            if (x % i === 0) {
                isPrime = false;
                break;
            }
        }
        if (isPrime) print(x);
    }
    x += 1;
}