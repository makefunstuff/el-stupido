fn main() {
    fact := 1
    fo i := 1..13 {
        fact := fact * i
        printf("factorial(%d) = %d\n", i, fact)
    }
}
