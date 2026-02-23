use web
listen 3000
/about "team dashboard v1.0"
/health { http_resp(fd,200,"text/plain"); http_send(fd,"ok") }
crud notes author text+
