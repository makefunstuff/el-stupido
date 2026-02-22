// grugmem — KV store @ ~/.grugmem
⚡ NN(p) 👉 (p 🔄 🔷 != 0)
⚡ NZ(p) 👉 (p 🔄 🔷 == 0)
📥 grug

🔧 grug_ls(g: *Grug, sec: *🔶) {
  s:=g.sec; 🔁 NN(s){
    show:=1; ❓ NN(sec){❓ ⚔(s.nm,sec)!=0{show=0}}
    ❓ show{❓ NZ(sec){🖨("[%s]\n",s.nm)};kv:=s.kv;🔁 NN(kv){🖨("%s\n",kv.k);kv=kv.nx}}
    s=s.nx
  }
}

🔧 gmpath() -> *🔶 {
  h:=getenv("HOME"); ❓ NZ(h){h="/tmp"}
  buf:=🧠(512)🔄*🔶; 📝(buf,"%s/.grugmem",h); ↩ buf
}

🏁(argc: 🔢, argv: **🔶) {
  ❓ argc<2{🖨("usage: grugmem <get|set|del|ls|dump> [args]\n");💀(1)}
  cmd:=*(argv+1); path:=gmpath()
  g:=grug_parse(path)
  ❓ NZ(g){g=✨ Grug;g.sec=0 🔄 *Sec;g.buf=0 🔄 *🔶}
  ❓ ⚔(cmd,"get")==0&&argc==4{
    rv:=grug_get(g,*(argv+2),*(argv+3)); ❓ NN(rv){🖨("%s\n",rv)}
  }
  ❗ ❓ ⚔(cmd,"set")==0&&argc==5{grug_set(g,*(argv+2),*(argv+3),*(argv+4));grug_write(g,path)}
  ❗ ❓ ⚔(cmd,"del")==0&&argc==4{grug_del(g,*(argv+2),*(argv+3));grug_write(g,path)}
  ❗ ❓ ⚔(cmd,"ls")==0{sec:*🔶=0 🔄 *🔶;❓ argc==3{sec=*(argv+2)};grug_ls(g,sec)}
  ❗ ❓ ⚔(cmd,"dump")==0{grug_dump(g)}
  ❗{🖨("usage: grugmem <get|set|del|ls|dump> [args]\n");💀(1)}
  grug_fr(g);🆓(path)
}
