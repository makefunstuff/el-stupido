package main

import (
	"fmt"
	"os"
)

func main() {
	for i := 1; i <= 30; i++ {
		if isDivisibleBy15(i) {
			fmt.Println("FizzBuzz")
		} else if isDivisibleBy3(i) {
			fmt.Println("Fizz")
		} else if isDivisibleBy5(i) {
			fmt.Println("Buzz")
		} else {
			fmt.Printf("%d\n", i))
		}
	}
}

func isDivisibleBy15(n int) bool {
	return n%15 == 0
}

func isDivisibleBy3(n int) bool {
	return n%3 == 0
}

func isDivisibleBy5(n int) bool {
	return n%5 == 0
}