Loading model... |-\|/-\ 
▄▄ ▄▄
██ ██
██ ██  ▀▀█▄ ███▄███▄  ▀▀█▄    ▄████ ████▄ ████▄
██ ██ ▄█▀██ ██ ██ ██ ▄█▀██    ██    ██ ██ ██ ██
██ ██ ▀█▄██ ██ ██ ██ ▀█▄██ ██ ▀████ ████▀ ████▀
                                    ██    ██
                                    ▀▀    ▀▀
build      : b1-9051663
model      : sha256-29d8c98fa6b098e200069bfb88b9508dc3e85586d20cba59f8dda9a808165104
modalities : text
available commands:
  /exit or Ctrl+C     stop or exit
  /regen              regenerate the last response
  /clear              clear the chat history
  /read               add a text file
> el-stupido: compiled lang. Built-ins: product(1..=n) sum(1..n).
print(expr) prints value. for i := 1..=n { }. name(args) = expr defines function.
Example: fib(n) = n < 2 ? n : fib(n-1) + fib(n-2)
Task: factorial function using product(1..=n), print factorials 1..12.
Output ONLY code. No explanation. No markdown.
| ```go
package main
import (
	"fmt"
)
func factorial(n int) int {
	if n < 2 {
		return n
	}
	return n * factorial(n-1)
}
func main() {
	for i := 1; i <= 12; i++ {
		fmt.Println(factorial(i))
	}
}
[ Prompt: 3509.2 t/s | Generation: 206.7 t/s ]
> 
> 
> 
> 
> 
> 
> 
> 
> 
> 
> 
> 
