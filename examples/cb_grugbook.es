use web
listen 8080
/ tpl "tools/book.grug" "guestbook.grug"
POST /post save "guestbook.grug" "name,msg" /
