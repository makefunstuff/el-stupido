fn main([]) {
  for i := 2..=100 { if i % 3 == 0 && i % 5 != 0 { print(i)
  } }
}
fn if_else(["cond", "true_body", "false_body"]) {
  if cond then true_body else false_body
}
fn while(["cond", "body"]) {
  while cond do body
}
fn for(["i", "a", "b", "body"]) {
  for i := a..=b { body
  }
}
fn break([]) {
  break
}

