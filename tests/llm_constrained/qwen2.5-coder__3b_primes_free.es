-stupido
x := 2
while x <= 50 {
    if x == 2 {
        print(x)
    } else {
        y := 3
        while y * y <= x {
            if x % y == 0 {
                break
            }
            y := y + 2
        }
        if y * y > x {
            print(x)
        }
    }
    x := x + 1
}