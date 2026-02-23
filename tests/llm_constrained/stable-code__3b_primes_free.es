i := 2
while i <= 50 {
    isPrime := true
    j := 2
    while j*j <= i {
        if i%j == 0 {
            isPrime = false
            break
        }
        j := j + 1
    }
    if isPrime {
        print(i)
    }
    i := i + 1
}