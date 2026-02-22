// grugbook — guestbook web app via .grug
⚡ NN(p) 👉 (p 🔄 🔷 != 0)
⚡ NZ(p) 👉 (p 🔄 🔷 == 0)
⚡ W(fd,s) 👉 ✏(fd,s,🧵(s))
📥 grug
🔌 signal(🔢, *⬛) -> *⬛
📦 SA { fam: 📈; port: 📈; addr: 🔵; z: [8]🔶 }

🔧 udec(s: *🔶) { i:=0; j:=0
  🔁 *(s+i)!=0{ c:=*(s+i)🔄🔢
    ❓ c==43{*(s+j)=32;j+=1;i+=1}
    ❗ ❓ c==37&&*(s+i+1)!=0&&*(s+i+2)!=0{
      hx:[3]🔶; *((&hx)🔄*🔶)=*(s+i+1); *((&hx)🔄*🔶+1)=*(s+i+2); *((&hx)🔄*🔶+2)=0
      hv:=0;ki:=0;🔁 ki<2{ch:=*((&hx)🔄*🔶+ki)🔄🔢
        ❓ ch>=48&&ch<=57{hv=hv*16+(ch-48)}
        ❗ ❓ ch>=65&&ch<=70{hv=hv*16+(ch-55)}
        ❗ ❓ ch>=97&&ch<=102{hv=hv*16+(ch-87)}
        ki+=1};*(s+j)=hv🔄🔶;j+=1;i+=3
    }❗{*(s+j)=*(s+i);j+=1;i+=1}
  };*(s+j)=0
}

🔧 fv(body: *🔶, key: *🔶, dst: *🔶, dsz: 🔢) -> 🔢 {
  kl:=🧵(key)🔄🔢; p:=body; 🔁 *p!=0{
    ❓ 🗡(p,key,kl)==0&&*(p+kl)==61{
      vs:=p+kl+1; i:=0; 🔁 *(vs+i)!=0&&*(vs+i)!=38&&i<dsz-1{*(dst+i)=*(vs+i);i+=1}
      *(dst+i)=0; udec(dst); ↩ 1
    }; 🔁 *p!=0&&*p!=38{p=p+1}; ❓ *p==38{p=p+1}
  }; *dst=0; ↩ 0
}

🔧 hesc(fd: 🔢, s: *🔶) { i:=0; 🔁 *(s+i)!=0{ c:=*(s+i)🔄🔢
  ❓ c==60{W(fd,"&lt;")}❗ ❓ c==62{W(fd,"&gt;")}❗ ❓ c==38{W(fd,"&amp;")}
  ❗ ❓ c==34{W(fd,"&quot;")}❗{✏(fd,s+i,1)};i+=1
}}

🔧 serve(fd: 🔢, gp: *🔶) {
  buf:[4096]🔶; n:=📖(fd,&buf,4095)🔄🔢; ❓ n<=0{📕(fd);↩}
  *((&buf)🔄*🔶+n)=0; req:=&buf 🔄 *🔶
  ispost:=🗡(req,"POST",4)==0; path:[128]🔶; i:=0
  sp:=🔍(req,32); ❓ NZ(sp){📕(fd);↩}; sp=sp+1; j:=0
  🔁 *(sp+j)!=0&&*(sp+j)!=32&&j<127{*((&path)🔄*🔶+j)=*(sp+j);j+=1}
  *((&path)🔄*🔶+j)=0

  ❓ ispost&&⚔(&path,"/post")==0{
    bl:=🔎(req,"\r\n\r\n"); ❓ NN(bl){bl=bl+4
      nm:[256]🔶; msg:[512]🔶
      fv(bl,"name",&nm,256); fv(bl,"msg",&msg,512)
      ❓ *((&nm)🔄*🔶)!=0&&*((&msg)🔄*🔶)!=0{
        g:=grug_parse(gp); ❓ NZ(g){g=✨ Grug;g.sec=0 🔄 *Sec;g.buf=0 🔄 *🔶}
        ts:[32]🔶; id:[40]🔶; pid:=🆔()🔄🔢
        📝(&ts,"%d",pid); 📝(&id,"msg_%s",&ts)
        sec:=&id 🔄 *🔶; grug_set(g,sec,"name",&nm); grug_set(g,sec,"msg",&msg)
        grug_write(g,gp); grug_fr(g)
      }
    }
    W(fd,"HTTP/1.1 303 See Other\r\nLocation: /\r\nContent-Length: 0\r\n\r\n")
  }
  ❗{
    W(fd,"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n")
    W(fd,"<!DOCTYPE html><html><head><meta charset='utf-8'><title>grugbook</title>")
    W(fd,"<style>*{margin:0;padding:0;box-sizing:border-box}body{background:#1a1a2e;color:#e0e0e0;font:16px/1.6 monospace;padding:2em;max-width:640px;margin:auto}")
    W(fd,".msg{background:#16213e;padding:1em;margin:.5em 0;border-radius:8px;border-left:3px solid #e94560}")
    W(fd,".nm{color:#e94560;font-weight:bold}h1{color:#e94560;margin-bottom:.5em}")
    W(fd,"form{background:#16213e;padding:1em;border-radius:8px;margin-bottom:1em}")
    W(fd,"input,textarea{width:100%;padding:.5em;margin:.3em 0;background:#0f3460;color:#e0e0e0;border:1px solid #e94560;border-radius:4px;font:inherit}")
    W(fd,"button{background:#e94560;color:#fff;border:0;padding:.5em 1.5em;border-radius:4px;cursor:pointer;font:inherit;margin-top:.5em}")
    W(fd,"</style></head><body><h1>grugbook</h1>")
    W(fd,"<form method='post' action='/post'><input name='name' placeholder='your name' required>")
    W(fd,"<textarea name='msg' rows='3' placeholder='leave a message...' required></textarea>")
    W(fd,"<button type='submit'>post</button></form>")
    g:=grug_parse(gp); ❓ NN(g){
      s:=g.sec; 🔁 NN(s){
        nm:=fval(s,"name"); mg:=fval(s,"msg")
        ❓ NN(nm)&&NN(mg){
          W(fd,"<div class='msg'><span class='nm'>"); hesc(fd,nm)
          W(fd,"</span><p>"); hesc(fd,mg); W(fd,"</p></div>")
        }; s=s.nx
      }; grug_fr(g)
    }
    W(fd,"</body></html>")
  }
  📕(fd)
}

🏁() {
  signal(17, 1 🔄 *⬛)
  sfd:=🌐(2,1,0); ❓ sfd<0{🖨("socket fail\n");↩ 1}
  o: 🔢=1; 🎛(sfd,1,2,&o,4)
  a: SA; 🧹(&a,0,16); a.fam=2; a.port=🔃(8080); a.addr=0
  ❓ 📌(sfd,&a,16)<0{🖨("bind fail\n");↩ 1}
  📡(sfd,16); 🖨("grugbook :8080\n")
  gp:="guestbook.grug"
  🔁 1{
    cfd:=🤝(sfd,∅,∅); ❓ cfd<0{🖨("accept fail\n");↩ 1}
    pid:=🍴()
    ❓ pid==0{📕(sfd);serve(cfd,gp);💀(0)}
    📕(cfd)
  }
}
