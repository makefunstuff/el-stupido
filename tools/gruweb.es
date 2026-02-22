// gruweb — .grug → HTML+CSS+JS transpiler
// usage: gruweb page.grug > index.html
// sections: page(meta) style(CSS) script(JS) *(HTML)
// element key: tag[.class][#id]  value: content
// special: a→text|href  img→src|alt  input→type|placeholder

📦 KV { k: *🔶; val: *🔶; nx: *KV }
📦 Sec { nm: *🔶; kv: *KV; nx: *Sec }
📦 Grug { sec: *Sec; buf: *🔶 }

// ---- grug parser (compact) ----
🔧 gb(p: *🔶, i: 🔢) -> 🔢 { *(p+i) 🔄 🔢 }
🔧 eq4(p: *🔶, a: 🔢, b: 🔢, c: 🔢, d: 🔢) -> 🔢 { gb(p,0)==a && gb(p,1)==b && gb(p,2)==c && gb(p,3)==d }
🔧 is_sec(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x93, 0x82) }
🔧 is_key(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x94, 0x91) }
🔧 is_arr(p: *🔶) -> 🔢 { eq4(p, 0xF0, 0x9F, 0x91, 0x89) }
🔧 skv(p: *🔶) -> *🔶 { ❓ gb(p,0)==0xEF && gb(p,1)==0xB8 && gb(p,2)==0x8F { ↩ p+3 }; ↩ p }
🔧 sw(p: *🔶) -> *🔶 { 🔁 *p==32||*p==9 { p=p+1 }; ↩ p }
🔧 fnl(p: *🔶) -> *🔶 { 🔁 *p!=0&&*p!=10 { p=p+1 }; ↩ p }
🔧 anl(p: *🔶) -> *🔶 { ❓ *p==13{p=p+1}; ❓ *p==10{p=p+1}; ↩ p }
🔧 nt(s: *🔶, e: *🔶) { 🔁 e 🔄 🔷>s 🔄 🔷&&(*(e-1)==32||*(e-1)==9||*(e-1)==13){e=e-1}; *e=0 }
🔧 sd(s: *🔶) -> *🔶 { l:=🧵(s)🔄🔢; d:=🧠(l+1)🔄*🔶; 📋(d,s,l+1); ↩ d }

🔧 slurp(path: *🔶) -> *🔶 {
  fd:=📂(path,0); ❓ fd<0{↩ 0 🔄 *🔶}
  fsz:=🔖(fd,0,2); 🔖(fd,0,0)
  buf:=🧠(fsz+1)🔄*🔶; 📖(fd,buf,fsz); *(buf+fsz)=0; 📕(fd); ↩ buf
}

🔧 grug_parse(path: *🔶) -> *Grug {
  buf:=slurp(path); ❓ buf 🔄 🔷==0{↩ 0 🔄 *Grug}
  g:=✨ Grug; g.sec=0 🔄 *Sec; g.buf=buf
  cur: *Sec=0 🔄 *Sec; p:=buf
  🔁 *p!=0 {
    p=sw(p); ❓ *p==0{🛑}
    ❓ *p==10||*p==13{p=anl(p)}
    ❗ ❓ *p==35{p=anl(fnl(p))}
    ❗ ❓ is_sec(p) {
      p=sw(skv(p+4)); nm:=p; nl:=fnl(p); nx:=anl(nl); nt(nm,nl)
      s:=✨ Sec; s.nm=sd(nm); s.kv=0 🔄 *KV; s.nx=0 🔄 *Sec
      ❓ g.sec 🔄 🔷==0{g.sec=s}❗{t:=g.sec;🔁 t.nx 🔄 🔷!=0{t=t.nx};t.nx=s}
      cur=s; p=nx
    }
    ❗ ❓ is_key(p)&&cur 🔄 🔷!=0 {
      p=sw(skv(p+4)); ks:=p
      🔁 *p!=0&&*p!=10{❓ is_arr(p){🛑};p=p+1}
      ❓ is_arr(p) {
        sep:=p; vs:=sw(skv(sep+4)); nl:=fnl(vs); nx:=anl(nl); nt(ks,sep); nt(vs,nl)
        kv:=✨ KV; kv.k=sd(ks); kv.val=sd(vs); kv.nx=0 🔄 *KV
        ❓ cur.kv 🔄 🔷==0{cur.kv=kv}❗{t:=cur.kv;🔁 t.nx 🔄 🔷!=0{t=t.nx};t.nx=kv}
        p=nx
      }❗{p=anl(fnl(p))}
    }
    ❗{p=anl(fnl(p))}
  }
  ↩ g
}

// ---- helpers ----
🔧 fsec(g: *Grug, nm: *🔶) -> *Sec {
  s:=g.sec; 🔁 s 🔄 🔷!=0{❓ ⚔(s.nm,nm)==0{↩ s};s=s.nx}; ↩ 0 🔄 *Sec
}
🔧 fval(s: *Sec, key: *🔶) -> *🔶 {
  ❓ s 🔄 🔷==0{↩ 0 🔄 *🔶}
  kv:=s.kv; 🔁 kv 🔄 🔷!=0{❓ ⚔(kv.k,key)==0{↩ kv.val};kv=kv.nx}; ↩ 0 🔄 *🔶
}

// parse tag.class#id → tg, cl, xi buffers
🔧 ptag(s: *🔶, tg: *🔶, cl: *🔶, xi: *🔶) {
  m:=0; j:=0; i:=0
  🔁 *(s+i)!=0 {
    c:=*(s+i) 🔄 🔢
    ❓ c==46 { ❓ m==0{*(tg+j)=0}❗❓ m==1{*(cl+j)=0}❗{*(xi+j)=0}; m=1;j=0;i+=1 }
    ❗ ❓ c==35 { ❓ m==0{*(tg+j)=0}❗❓ m==1{*(cl+j)=0}❗{*(xi+j)=0}; m=2;j=0;i+=1 }
    ❗ {
      ❓ m==0{*(tg+j)=*(s+i)} ❗ ❓ m==1{*(cl+j)=*(s+i)} ❗{*(xi+j)=*(s+i)}
      j+=1; i+=1
    }
  }
  ❓ m==0{*(tg+j)=0} ❗ ❓ m==1{*(cl+j)=0} ❗{*(xi+j)=0}
}

// emit class="..." id="..."
🔧 eattr(cl: *🔶, xi: *🔶) {
  ❓ *cl!=0{🖨(" class=\"%s\"",cl)}
  ❓ *xi!=0{🖨(" id=\"%s\"",xi)}
}

// void element check
🔧 isvoid(tg: *🔶) -> 🔢 {
  ❓ ⚔(tg,"img")==0{↩ 1}; ❓ ⚔(tg,"input")==0{↩ 1}
  ❓ ⚔(tg,"br")==0{↩ 1}; ❓ ⚔(tg,"hr")==0{↩ 1}
  ❓ ⚔(tg,"meta")==0{↩ 1}; ❓ ⚔(tg,"link")==0{↩ 1}
  ↩ 0
}

// ---- emit one HTML element ----
🔧 elem(k: *🔶, val: *🔶) {
  tg:[64]🔶; cl:[128]🔶; xi:[64]🔶
  🧹(&tg,0,64); 🧹(&cl,0,128); 🧹(&xi,0,64)
  ptag(k, &tg, &cl, &xi)
  pp: *🔶 = 0 🔄 *🔶

  ❓ ⚔(&tg,"a")==0 {
    pp=🔍(val,124); ❓ pp 🔄 🔷!=0{*pp=0}
    🖨("<a"); ❓ pp 🔄 🔷!=0{🖨(" href=\"%s\"",pp+1)}
    eattr(&cl,&xi); 🖨(">%s</a>\n",val)
  }
  ❗ ❓ ⚔(&tg,"img")==0 {
    pp=🔍(val,124); ❓ pp 🔄 🔷!=0{*pp=0}
    🖨("<img src=\"%s\"",val); ❓ pp 🔄 🔷!=0{🖨(" alt=\"%s\"",pp+1)}
    eattr(&cl,&xi); 🖨(">\n")
  }
  ❗ ❓ ⚔(&tg,"input")==0 {
    pp=🔍(val,124); ❓ pp 🔄 🔷!=0{*pp=0}
    🖨("<input type=\"%s\"",val); ❓ pp 🔄 🔷!=0{🖨(" placeholder=\"%s\"",pp+1)}
    eattr(&cl,&xi); 🖨(">\n")
  }
  ❗ {
    🖨("<%s",&tg); eattr(&cl,&xi)
    ❓ isvoid(&tg){🖨(">\n")}❗{🖨(">%s</%s>\n",val,&tg)}
  }
}

// ---- render complete HTML ----
🔧 render(g: *Grug) {
  pg := fsec(g, "page")
  title := fval(pg, "title")
  lang := fval(pg, "lang")

  🖨("<!DOCTYPE html>\n<html")
  ❓ lang 🔄 🔷!=0{🖨(" lang=\"%s\"",lang)}
  🖨(">\n<head>\n<meta charset=\"utf-8\">\n")
  ❓ title 🔄 🔷!=0{🖨("<title>%s</title>\n",title)}

  // inline CSS
  css := fsec(g, "style")
  ❓ css 🔄 🔷!=0 {
    🖨("<style>\n")
    kv:=css.kv; 🔁 kv 🔄 🔷!=0{🖨("%s{%s}\n",kv.k,kv.val);kv=kv.nx}
    🖨("</style>\n")
  }
  🖨("</head>\n<body>\n")

  // content sections (skip page/style/script)
  s:=g.sec
  🔁 s 🔄 🔷!=0 {
    skip:=0
    ❓ ⚔(s.nm,"page")==0{skip=1}
    ❓ ⚔(s.nm,"style")==0{skip=1}
    ❓ ⚔(s.nm,"script")==0{skip=1}
    ❓ skip==0 {
      tg:[64]🔶; cl:[128]🔶; xi:[64]🔶
      🧹(&tg,0,64); 🧹(&cl,0,128); 🧹(&xi,0,64)
      ptag(s.nm, &tg, &cl, &xi)
      tp := &tg 🔄 *🔶
      ❓ *tp==0 { *tp=100; *(tp+1)=105; *(tp+2)=118; *(tp+3)=0 }
      🖨("<%s",tp); eattr(&cl,&xi); 🖨(">\n")
      kv:=s.kv; 🔁 kv 🔄 🔷!=0{elem(kv.k,kv.val);kv=kv.nx}
      🖨("</%s>\n",tp)
    }
    s=s.nx
  }

  // inline JS
  js:=fsec(g,"script")
  ❓ js 🔄 🔷!=0 {
    🖨("<script>\n")
    kv:=js.kv; 🔁 kv 🔄 🔷!=0{🖨("%s\n",kv.val);kv=kv.nx}
    🖨("</script>\n")
  }

  🖨("</body>\n</html>\n")
}

🏁(argc: 🔢, argv: **🔶) {
  ❓ argc<2{🖨("usage: gruweb page.grug > index.html\n");💀(1)}
  g:=grug_parse(*(argv+1))
  ❓ g 🔄 🔷==0{🖨("error: cannot read %s\n",*(argv+1));💀(1)}
  render(g)
}
