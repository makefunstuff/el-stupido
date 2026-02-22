// lib/grug.es — 📂🔑👉 config parser prelude
⚡ NN(p) 👉 (p 🔄 🔷 != 0)
⚡ NZ(p) 👉 (p 🔄 🔷 == 0)

📦 KV { k: *🔶; val: *🔶; nx: *KV }
📦 Sec { nm: *🔶; kv: *KV; nx: *Sec }
📦 Grug { sec: *Sec; buf: *🔶 }

🔧 gb(p: *🔶, i: 🔢) -> 🔢 { *(p+i) 🔄 🔢 }
🔧 eq4(p: *🔶, a: 🔢, b: 🔢, c: 🔢, d: 🔢) -> 🔢 { gb(p,0)==a&&gb(p,1)==b&&gb(p,2)==c&&gb(p,3)==d }
🔧 is_sec(p: *🔶) -> 🔢 { eq4(p, 0xF0,0x9F,0x93,0x82) }
🔧 is_key(p: *🔶) -> 🔢 { eq4(p, 0xF0,0x9F,0x94,0x91) }
🔧 is_arr(p: *🔶) -> 🔢 { eq4(p, 0xF0,0x9F,0x91,0x89) }
🔧 skv(p: *🔶) -> *🔶 { ❓ gb(p,0)==0xEF&&gb(p,1)==0xB8&&gb(p,2)==0x8F{↩ p+3}; ↩ p }
🔧 sw(p: *🔶) -> *🔶 { 🔁 *p==32||*p==9{p=p+1}; ↩ p }
🔧 fnl(p: *🔶) -> *🔶 { 🔁 *p!=0&&*p!=10{p=p+1}; ↩ p }
🔧 anl(p: *🔶) -> *🔶 { ❓ *p==13{p=p+1}; ❓ *p==10{p=p+1}; ↩ p }
🔧 nt(s: *🔶, e: *🔶) { 🔁 e 🔄 🔷>s 🔄 🔷&&(*(e-1)==32||*(e-1)==9||*(e-1)==13){e=e-1}; *e=0 }
🔧 sd(s: *🔶) -> *🔶 { l:=🧵(s)🔄🔢; d:=🧠(l+1)🔄*🔶; 📋(d,s,l+1); ↩ d }

🔧 slurp(path: *🔶) -> *🔶 {
  fd:=📂(path,0); ❓ fd<0{↩ 0 🔄 *🔶}
  fsz:=🔖(fd,0,2); 🔖(fd,0,0)
  buf:=🧠(fsz+1)🔄*🔶; 📖(fd,buf,fsz); *(buf+fsz)=0; 📕(fd); ↩ buf
}

🔧 grug_parse(path: *🔶) -> *Grug {
  buf:=slurp(path); ❓ NZ(buf){↩ 0 🔄 *Grug}
  g:=✨ Grug; g.sec=0 🔄 *Sec; g.buf=buf; cur: *Sec=0 🔄 *Sec; p:=buf
  🔁 *p!=0 {
    p=sw(p); ❓ *p==0{🛑}
    ❓ *p==10||*p==13{p=anl(p)}
    ❗ ❓ *p==35{p=anl(fnl(p))}
    ❗ ❓ is_sec(p) {
      p=sw(skv(p+4)); nm:=p; nl:=fnl(p); nx:=anl(nl); nt(nm,nl)
      s:=✨ Sec; s.nm=sd(nm); s.kv=0 🔄 *KV; s.nx=0 🔄 *Sec
      ❓ NZ(g.sec){g.sec=s}❗{t:=g.sec;🔁 NN(t.nx){t=t.nx};t.nx=s}
      cur=s; p=nx
    }
    ❗ ❓ is_key(p)&&NN(cur) {
      p=sw(skv(p+4)); ks:=p
      🔁 *p!=0&&*p!=10{❓ is_arr(p){🛑};p=p+1}
      ❓ is_arr(p) {
        sep:=p; vs:=sw(skv(sep+4)); nl:=fnl(vs); nx:=anl(nl); nt(ks,sep); nt(vs,nl)
        kv:=✨ KV; kv.k=sd(ks); kv.val=sd(vs); kv.nx=0 🔄 *KV
        ❓ NZ(cur.kv){cur.kv=kv}❗{t:=cur.kv;🔁 NN(t.nx){t=t.nx};t.nx=kv}
        p=nx
      }❗{p=anl(fnl(p))}
    }
    ❗{p=anl(fnl(p))}
  }
  ↩ g
}

🔧 grug_get(g: *Grug, sec: *🔶, key: *🔶) -> *🔶 {
  s:=g.sec; 🔁 NN(s){
    ❓ ⚔(s.nm,sec)==0{kv:=s.kv;🔁 NN(kv){❓ ⚔(kv.k,key)==0{↩ kv.val};kv=kv.nx};↩ 0 🔄 *🔶}
    s=s.nx
  }; ↩ 0 🔄 *🔶
}

🔧 fsec(g: *Grug, nm: *🔶) -> *Sec {
  s:=g.sec; 🔁 NN(s){❓ ⚔(s.nm,nm)==0{↩ s};s=s.nx}; ↩ 0 🔄 *Sec
}
🔧 fval(s: *Sec, key: *🔶) -> *🔶 {
  ❓ NZ(s){↩ 0 🔄 *🔶}
  kv:=s.kv; 🔁 NN(kv){❓ ⚔(kv.k,key)==0{↩ kv.val};kv=kv.nx}; ↩ 0 🔄 *🔶
}

🔧 grug_dump(g: *Grug) {
  s:=g.sec; 🔁 NN(s){
    🖨("📂 %s\n",s.nm); kv:=s.kv
    🔁 NN(kv){🖨("  🔑 %s 👉 %s\n",kv.k,kv.val);kv=kv.nx}
    s=s.nx
  }
}

🔧 grug_fr(g: *Grug) {
  s:=g.sec; 🔁 NN(s){
    ns:=s.nx; kv:=s.kv
    🔁 NN(kv){nk:=kv.nx;🗑 kv.k;🗑 kv.val;🗑 kv;kv=nk}
    🗑 s.nm;🗑 s;s=ns
  }; 🗑 g.buf;🗑 g
}

// ---- mutation ----
🔧 grug_sec(g: *Grug, nm: *🔶) -> *Sec {
  s:=g.sec; 🔁 NN(s){❓ ⚔(s.nm,nm)==0{↩ s};s=s.nx}
  s=✨ Sec; s.nm=sd(nm); s.kv=0 🔄 *KV; s.nx=0 🔄 *Sec
  ❓ NZ(g.sec){g.sec=s}❗{t:=g.sec;🔁 NN(t.nx){t=t.nx};t.nx=s}
  ↩ s
}
🔧 grug_set(g: *Grug, sec: *🔶, key: *🔶, val: *🔶) {
  s:=grug_sec(g,sec); kv:=s.kv
  🔁 NN(kv){❓ ⚔(kv.k,key)==0{🗑 kv.val;kv.val=sd(val);↩};kv=kv.nx}
  n:=✨ KV; n.k=sd(key); n.val=sd(val); n.nx=0 🔄 *KV
  ❓ NZ(s.kv){s.kv=n}❗{t:=s.kv;🔁 NN(t.nx){t=t.nx};t.nx=n}
}
🔧 grug_del(g: *Grug, sec: *🔶, key: *🔶) -> 🔢 {
  s:=g.sec; 🔁 NN(s){
    ❓ ⚔(s.nm,sec)==0{
      prev: *KV=0 🔄 *KV; kv:=s.kv
      🔁 NN(kv){
        ❓ ⚔(kv.k,key)==0{
          ❓ NZ(prev){s.kv=kv.nx}❗{prev.nx=kv.nx}
          🗑 kv.k;🗑 kv.val;🗑 kv; ↩ 1
        }; prev=kv; kv=kv.nx
      }; ↩ 0
    }; s=s.nx
  }; ↩ 0
}
🔧 grug_write(g: *Grug, path: *🔶) -> 🔢 {
  fd:=📂(path,577,420); ❓ fd<0{↩ -1}
  buf:[4096]🔶; s:=g.sec
  🔁 NN(s){
    n:=📝(&buf,"📂 %s\n",s.nm)🔄🔷; ✏(fd,&buf,n)
    kv:=s.kv; 🔁 NN(kv){n=📝(&buf,"🔑 %s 👉 %s\n",kv.k,kv.val)🔄🔷;✏(fd,&buf,n);kv=kv.nx}
    ✏(fd,"\n",1); s=s.nx
  }; 📕(fd); ↩ 0
}
