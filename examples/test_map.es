// test_map.es — test lib/map.es hash map
📥 map

🏁() {
  mm := map_new()
  🖨("1. new: len=%d\n", map_len(mm))

  // set some keys
  map_set(mm, "name", "grug")
  map_set(mm, "lang", "el-stupido")
  map_set(mm, "year", "2025")
  🖨("2. set: len=%d\n", map_len(mm))

  // get
  🖨("3. get: name='%s' lang='%s' year='%s'\n", map_get(mm, "name"), map_get(mm, "lang"), map_get(mm, "year"))

  // overwrite
  map_set(mm, "year", "2026")
  🖨("4. overwrite: year='%s' len=%d\n", map_get(mm, "year"), map_len(mm))

  // missing key
  miss := map_get(mm, "nope")
  🖨("5. missing: ptr=%d\n", miss 🔄 🔢)

  // delete
  ok := map_del(mm, "lang")
  🖨("6. del: ok=%d len=%d\n", ok, map_len(mm))

  // delete missing
  ok2 := map_del(mm, "nope")
  🖨("7. del missing: ok=%d len=%d\n", ok2, map_len(mm))

  // verify remaining
  🖨("8. remaining: name='%s' year='%s'\n", map_get(mm, "name"), map_get(mm, "year"))

  map_fr(mm)
  🖨("9. free: OK\n")
}
