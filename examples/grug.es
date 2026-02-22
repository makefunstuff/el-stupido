// grug.es — .grug config parser demo
⚡ NN(p) 👉 (p 🔄 🔷 != 0)
📥 grug

🏁() {
  g := grug_parse("context.grug")
  ❓ g 🔄 🔷 == 0 { 🖨("❌ context.grug\n"); 💀(1) }
  🖨("=== dump ===\n"); grug_dump(g)
  🖨("\n=== lookup ===\n")
  n := grug_get(g, "project", "name"); ❓ NN(n) { 🖨("project.name = %s\n", n) }
  b := grug_get(g, "build", "cmd"); ❓ NN(b) { 🖨("build.cmd = %s\n", b) }
  grug_fr(g)
}
