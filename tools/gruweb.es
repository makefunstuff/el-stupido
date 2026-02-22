// gruweb — .grug → HTML+CSS+JS
⚡ NN(p) 👉 (p 🔄 🔷 != 0)
⚡ NZ(p) 👉 (p 🔄 🔷 == 0)
📥 grug

🔧 ptag(s: *🔶, tg: *🔶, cl: *🔶, xi: *🔶) {
  m:=0; j:=0; i:=0
  🔁 *(s+i)!=0 {
    c:=*(s+i)🔄🔢
    ❓ c==46{❓ m==0{*(tg+j)=0}❗❓ m==1{*(cl+j)=0}❗{*(xi+j)=0};m=1;j=0;i+=1}
    ❗ ❓ c==35{❓ m==0{*(tg+j)=0}❗❓ m==1{*(cl+j)=0}❗{*(xi+j)=0};m=2;j=0;i+=1}
    ❗{❓ m==0{*(tg+j)=*(s+i)}❗❓ m==1{*(cl+j)=*(s+i)}❗{*(xi+j)=*(s+i)};j+=1;i+=1}
  }
  ❓ m==0{*(tg+j)=0}❗❓ m==1{*(cl+j)=0}❗{*(xi+j)=0}
}

🔧 eattr(cl: *🔶, xi: *🔶) {
  ❓ *cl!=0{🖨(" class=\"%s\"",cl)}; ❓ *xi!=0{🖨(" id=\"%s\"",xi)}
}

🔧 isvoid(tg: *🔶) -> 🔢 {
  ❓ ⚔(tg,"img")==0{↩ 1}; ❓ ⚔(tg,"input")==0{↩ 1}
  ❓ ⚔(tg,"br")==0{↩ 1}; ❓ ⚔(tg,"hr")==0{↩ 1}
  ❓ ⚔(tg,"meta")==0{↩ 1}; ❓ ⚔(tg,"link")==0{↩ 1}; ↩ 0
}

🔧 elem(k: *🔶, val: *🔶) {
  tg:[64]🔶; cl:[128]🔶; xi:[64]🔶
  🧹(&tg,0,64); 🧹(&cl,0,128); 🧹(&xi,0,64)
  ptag(k,&tg,&cl,&xi); pp:*🔶=0 🔄 *🔶
  ❓ ⚔(&tg,"a")==0{
    pp=🔍(val,124); ❓ NN(pp){*pp=0}
    🖨("<a"); ❓ NN(pp){🖨(" href=\"%s\"",pp+1)}
    eattr(&cl,&xi); 🖨(">%s</a>\n",val)
  }
  ❗ ❓ ⚔(&tg,"img")==0{
    pp=🔍(val,124); ❓ NN(pp){*pp=0}
    🖨("<img src=\"%s\"",val); ❓ NN(pp){🖨(" alt=\"%s\"",pp+1)}
    eattr(&cl,&xi); 🖨(">\n")
  }
  ❗ ❓ ⚔(&tg,"input")==0{
    pp=🔍(val,124); ❓ NN(pp){*pp=0}
    🖨("<input type=\"%s\"",val); ❓ NN(pp){🖨(" placeholder=\"%s\"",pp+1)}
    eattr(&cl,&xi); 🖨(">\n")
  }
  ❗{🖨("<%s",&tg);eattr(&cl,&xi);❓ isvoid(&tg){🖨(">\n")}❗{🖨(">%s</%s>\n",val,&tg)}}
}

🔧 render(g: *Grug) {
  pg:=fsec(g,"page"); title:=fval(pg,"title"); lang:=fval(pg,"lang")
  🖨("<!DOCTYPE html>\n<html")
  ❓ NN(lang){🖨(" lang=\"%s\"",lang)}
  🖨(">\n<head>\n<meta charset=\"utf-8\">\n")
  ❓ NN(title){🖨("<title>%s</title>\n",title)}
  css:=fsec(g,"style")
  ❓ NN(css){🖨("<style>\n");kv:=css.kv;🔁 NN(kv){🖨("%s{%s}\n",kv.k,kv.val);kv=kv.nx};🖨("</style>\n")}
  🖨("</head>\n<body>\n")
  s:=g.sec; 🔁 NN(s){
    skip:=0
    ❓ ⚔(s.nm,"page")==0{skip=1}; ❓ ⚔(s.nm,"style")==0{skip=1}; ❓ ⚔(s.nm,"script")==0{skip=1}
    ❓ skip==0{
      tg:[64]🔶; cl:[128]🔶; xi:[64]🔶
      🧹(&tg,0,64);🧹(&cl,0,128);🧹(&xi,0,64)
      ptag(s.nm,&tg,&cl,&xi)
      tp:=&tg 🔄 *🔶; ❓ *tp==0{*tp=100;*(tp+1)=105;*(tp+2)=118;*(tp+3)=0}
      🖨("<%s",tp);eattr(&cl,&xi);🖨(">\n")
      kv:=s.kv;🔁 NN(kv){elem(kv.k,kv.val);kv=kv.nx}
      🖨("</%s>\n",tp)
    }; s=s.nx
  }
  js:=fsec(g,"script")
  ❓ NN(js){🖨("<script>\n");kv:=js.kv;🔁 NN(kv){🖨("%s\n",kv.val);kv=kv.nx};🖨("</script>\n")}
  🖨("</body>\n</html>\n")
}

🏁(argc: 🔢, argv: **🔶) {
  ❓ argc<2{🖨("usage: gruweb page.grug > index.html\n");💀(1)}
  g:=grug_parse(*(argv+1))
  ❓ NZ(g){🖨("error: cannot read %s\n",*(argv+1));💀(1)}
  render(g)
}
