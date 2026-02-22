// ws_echo.es — WebSocket echo server with inline HTML page
📥 http
📥 ws

🔧 handle(fd: 🔢) {
  buf: [4096]🔶; n := 📖(fd, &buf, 4095) 🔄 🔢
  ❓ n <= 0 { 📕(fd); ↩ }
  *((&buf) 🔄 *🔶 + n) = 0; req := &buf 🔄 *🔶

  // check for websocket upgrade
  ❓ 🔎(req, "Upgrade: websocket") 🔄 🔷 != 0 {
    ❓ ws_handshake(fd, req) < 0 { 📕(fd); ↩ }
    🖨("ws: client connected\n")
    ws_text(fd, "welcome to el-stupido ws!")
    mbuf: [4096]🔶
    🔁 1 {
      ml := ws_read(fd, &mbuf, 4096)
      ❓ ml < 0 { 🖨("ws: client disconnected\n"); 📕(fd); ↩ }
      ❓ ml > 0 {
        🖨("ws: recv '%s'\n", &mbuf)
        // echo back with prefix
        resp: [4200]🔶
        📝(&resp, "echo: %s", &mbuf)
        ws_text(fd, &resp)
      }
    }
  }

  // serve HTML page
  path: [128]🔶; http_path(req, &path, 128)
  http_resp(fd, 200, "text/html")
  http_send(fd, "<!DOCTYPE html><html><head><meta charset='utf-8'><title>ws echo</title>")
  http_send(fd, "<style>*{margin:0;padding:0;box-sizing:border-box}body{background:#1a1a2e;color:#e0e0e0;font:16px/1.6 monospace;padding:2em;max-width:640px;margin:auto}")
  http_send(fd, "#log{background:#16213e;padding:1em;border-radius:8px;height:300px;overflow-y:auto;margin-bottom:1em}")
  http_send(fd, ".s{color:#0f0}.r{color:#e94560}")
  http_send(fd, "input{width:80%;padding:.5em;background:#0f3460;color:#e0e0e0;border:1px solid #e94560;border-radius:4px;font:inherit}")
  http_send(fd, "button{background:#e94560;color:#fff;border:0;padding:.5em 1em;border-radius:4px;cursor:pointer;font:inherit;margin-left:.5em}")
  http_send(fd, "</style></head><body><h1>ws echo</h1><div id='log'></div>")
  http_send(fd, "<input id='msg' placeholder='type a message...' autofocus>")
  http_send(fd, "<button onclick='snd()'>send</button><script>")
  http_send(fd, "let ws=new WebSocket('ws://'+location.host+'/ws');")
  http_send(fd, "let log=document.getElementById('log'),inp=document.getElementById('msg');")
  http_send(fd, "function add(c,t){let d=document.createElement('div');d.className=c;d.textContent=t;log.appendChild(d);log.scrollTop=log.scrollHeight}")
  http_send(fd, "ws.onopen=()=>add('s','connected');")
  http_send(fd, "ws.onmessage=e=>add('r',e.data);")
  http_send(fd, "ws.onclose=()=>add('s','disconnected');")
  http_send(fd, "function snd(){if(inp.value){ws.send(inp.value);add('s','> '+inp.value);inp.value=''}}")
  http_send(fd, "inp.onkeydown=e=>{if(e.key==='Enter')snd()}")
  http_send(fd, "</script></body></html>")
  📕(fd)
}

🏁() {
  signal(17, 1 🔄 *⬛) // SIGCHLD ignore
  sfd := http_listen(8080)
  ❓ sfd < 0 { 🖨("listen failed: %d\n", sfd); ↩ 1 }
  🖨("ws echo :8080\n")
  🔁 1 {
    cfd := 🤝(sfd, ∅, ∅); ❓ cfd < 0 { 🖨("accept fail\n"); ↩ 1 }
    pid := 🍴()
    ❓ pid == 0 { 📕(sfd); handle(cfd); 💀(0) }
    📕(cfd)
  }
}
