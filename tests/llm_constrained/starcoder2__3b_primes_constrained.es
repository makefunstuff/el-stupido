fn main_body() {
  for i:=1..=50{if i%2==0{print(i)}else{if i%3==0{print(i)}}}
}
fn if(cond, then_body, else_body) {
  if cond then then_body else else_body
}

fn main() { main_body() }
