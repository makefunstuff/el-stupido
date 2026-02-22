// 🚫🔤 — 0️⃣ english keywords

📦 V2 {
  x: 🌀
  y: 🌀
}

🔧 v2_l(p: *V2) -> 🌀 { sqrt(p.x * p.x + p.y * p.y) }

🔧 fz(n: 🔢) -> ⬛ {
  ❓ n%15==0{printf("FizzBuzz\n")}
  ❗ ❓ n%3==0{printf("Fizz\n")}
  ❗ ❓ n%5==0{printf("Buzz\n")}
  ❗{printf("%d\n",n)}
}

🔧 fib(n: 🔢) -> 🔢 { n<=1?n:fib(n-1)+fib(n-2) }

main() {
  // ⚡
  B := ⚡ 1 << 12
  printf("⚡=%ld\n", B)

  // 📦 ✨ 🗑
  pt := ✨ V2
  pt.x = 3.0; pt.y = 4.0
  printf("|pt|=%f\n", pt.v2_l())
  🗑 pt

  // 🔁 ❓ ❗ 🛑
  i := 1
  🔁 i <= 20 {
    ❓ i > 15 { 🛑 }
    fz(i)
    i += 1
  }

  printf("fib(10)=%d\n", fib(10))

  // 🔩
  🔩("nop")
  printf("🔩ok\n")
}
