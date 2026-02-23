-stupido
main_body {
    for i := 2..=50 {
        if true {
            x := 1
            while x <= sqrt(i) {
                if i % x == 0 {
                    break
                }
                x = x + 1
            }
            if x > sqrt(i) {
                print(i)
            }
        }
    }
}