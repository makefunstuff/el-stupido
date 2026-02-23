for i := 1..=30 {
    if i % 15 == 0 {
        printf("%s\n", "FizzBuzz")
    } else if i % 3 == 0 {
        printf("%s\n", "Fizz")
    } else if i % 5 == 0 {
        printf("%s\n", "Buzz")
    } else {
        printf("%d\n", i)
    }
}