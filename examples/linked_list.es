⚡ NN(p) 👉 (p 🔄 🔷 != 0)

📦 N { d: 🔢; nx: *N }

🔧 mk(d:🔢, nx:*N) -> *N { n:=✨ N; n.d=d; n.nx=nx; n }
🔧 pr(h:*N) { c:=h; 🔁 NN(c){🖨("%d",c.d);❓ NN(c.nx){🖨(" -> ")};c=c.nx}; 🖨("\n") }
🔧 rev(h:*N) -> *N { p:*N=0 🔄 *N; c:=h; 🔁 NN(c){nx:=c.nx;c.nx=p;p=c;c=nx}; p }
🔧 fr(h:*N) { c:=h; 🔁 NN(c){nx:=c.nx;🗑 c;c=nx} }

🏁() {
  h:*N=0 🔄 *N; i:=5
  🔁 i>=1{h=mk(i,h);i-=1}
  🖨(">> "); pr(h); h=rev(h)
  🖨("<< "); pr(h); fr(h)
}
