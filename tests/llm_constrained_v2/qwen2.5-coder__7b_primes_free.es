-stupido
main_body {
    for i := 2..=50 {
        is_prime := 1
        for j := 2..=(i - 1) {
            if (i % j == 0) {
                is_prime := 0
                break
            }
        }
        if (is_prime != 0) {
            print(i)
        }
    }
}