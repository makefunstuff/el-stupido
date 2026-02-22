// test_vec.es — test lib/vec.es dynamic array
📥 vec

🏁() {
  vv := vec_new()
  🖨("1. new: len=%d\n", vec_len(vv))

  // push some int-sized values as void*
  vec_push(vv, 42 🔄 *⬛)
  vec_push(vv, 100 🔄 *⬛)
  vec_push(vv, 7 🔄 *⬛)
  🖨("2. push: len=%d\n", vec_len(vv))

  // get
  🖨("3. get: [0]=%d [1]=%d [2]=%d\n", vec_get(vv, 0) 🔄 🔢, vec_get(vv, 1) 🔄 🔢, vec_get(vv, 2) 🔄 🔢)

  // pop
  top := vec_pop(vv) 🔄 🔢
  🖨("4. pop: val=%d len=%d\n", top, vec_len(vv))

  // pop again
  top = vec_pop(vv) 🔄 🔢
  🖨("5. pop: val=%d len=%d\n", top, vec_len(vv))

  // push many to trigger grow
  ➰ i := 0..20 { vec_push(vv, i 🔄 *⬛) }
  🖨("6. bulk push: len=%d\n", vec_len(vv))

  // verify first few
  🖨("7. verify: [0]=%d [1]=%d [5]=%d [20]=%d\n",
    vec_get(vv, 0) 🔄 🔢, vec_get(vv, 1) 🔄 🔢,
    vec_get(vv, 5) 🔄 🔢, vec_get(vv, 20) 🔄 🔢)

  vec_fr(vv)
  🖨("8. free: OK\n")
}
