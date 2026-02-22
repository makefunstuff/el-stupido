main() {
  i := 0
  🔁 i < 10 {
    ❓ i == 5 { 🛑 }
    printf("%d ", i)
    i = i + 1
  }
  printf("\n")

  i = 0
  🔁 i < 10 {
    ❓ i % 2 != 0 {
      i = i + 1
      ⏩
    }
    printf("%d ", i)
    i = i + 1
  }
  printf("\n")

  i = 0
  🔁 i < 3 {
    j := 0
    🔁 j < 3 {
      ❓ j == 2 { 🛑 }
      printf("(%d,%d) ", i, j)
      j = j + 1
    }
    i = i + 1
  }
  printf("\n")
}
