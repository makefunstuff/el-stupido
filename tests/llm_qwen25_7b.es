fn fact(n: i32) -> i32 {
  fo i := 1..n+1 { 
    x := 1
    fo j := 1..i+1 { 
      x := x * j 
    }
    ret x
  }
}

fn main() {
  fo i := 1..13 { 
    printf("factorial(%d) = %d\n", i, fact(i)) 
  }
}
