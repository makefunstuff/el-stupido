// 📚 el-stupido — 🌐 libc
// 🏁=main 🖨=printf 📣=fprintf 📝=sprintf 📢=puts 🔔=putchar 👂=getchar
// 📂=open 📕=close 📖=read ✏=write 🔖=lseek
// 🧠=malloc 🧩=calloc ♻=realloc 🆓=free 🧹=memset 📋=memcpy 🔀=memmove ⚖=memcmp
// 🧵=strlen ⚔=strcmp 🗡=strncmp ✂=strcpy 🪡=strncpy 🔗=strcat 🔍=strchr 🔎=strstr 🅰=atoi 🅱=atol
// 🌐=socket 📌=bind 📡=listen 🤝=accept 🧲=connect 📤=send 📩=recv 🎛=setsockopt
// 🔃=htons 🔂=htonl 🔙=ntohs 🔚=ntohl 🏠=inet_addr
// 📐=sqrt 🎵=sin 🎶=cos 💪=pow 🧊=fabs ⬇=floor ⬆=ceil 📓=log
// 💀=exit 🍴=fork 🏃=execvp ⏳=waitpid 🆔=getpid 😴=sleep ⏰=usleep
// 🗺=mmap 🚫=munmap

// 🖨️ I/O
🔌 printf(*🔶, ...) -> 🔢
🔌 fprintf(*⬛, *🔶, ...) -> 🔢
🔌 sprintf(*🔶, *🔶, ...) -> 🔢
🔌 puts(*🔶) -> 🔢
🔌 putchar(🔢) -> 🔢
🔌 getchar() -> 🔢

// 📁
🔌 open(*🔶, 🔢, ...) -> 🔢
🔌 close(🔢) -> 🔢
🔌 read(🔢, *⬛, 💎) -> 🔷
🔌 write(🔢, *⬛, 💎) -> 🔷
🔌 lseek(🔢, 🔷, 🔢) -> 🔷

// 🧠
🔌 malloc(💎) -> *⬛
🔌 calloc(💎, 💎) -> *⬛
🔌 realloc(*⬛, 💎) -> *⬛
🔌 free(*⬛)
🔌 memset(*⬛, 🔢, 💎) -> *⬛
🔌 memcpy(*⬛, *⬛, 💎) -> *⬛
🔌 memmove(*⬛, *⬛, 💎) -> *⬛
🔌 memcmp(*⬛, *⬛, 💎) -> 🔢

// 🔤
🔌 strlen(*🔶) -> 💎
🔌 strcmp(*🔶, *🔶) -> 🔢
🔌 strncmp(*🔶, *🔶, 💎) -> 🔢
🔌 strcpy(*🔶, *🔶) -> *🔶
🔌 strncpy(*🔶, *🔶, 💎) -> *🔶
🔌 strcat(*🔶, *🔶) -> *🔶
🔌 strchr(*🔶, 🔢) -> *🔶
🔌 strstr(*🔶, *🔶) -> *🔶
🔌 atoi(*🔶) -> 🔢
🔌 atol(*🔶) -> 🔷

// 🌐
🔌 socket(🔢, 🔢, 🔢) -> 🔢
🔌 bind(🔢, *⬛, 🔵) -> 🔢
🔌 listen(🔢, 🔢) -> 🔢
🔌 accept(🔢, *⬛, *🔵) -> 🔢
🔌 connect(🔢, *⬛, 🔵) -> 🔢
🔌 send(🔢, *⬛, 💎, 🔢) -> 🔷
🔌 recv(🔢, *⬛, 💎, 🔢) -> 🔷
🔌 setsockopt(🔢, 🔢, 🔢, *⬛, 🔵) -> 🔢
🔌 htons(📈) -> 📈
🔌 htonl(🔵) -> 🔵
🔌 ntohs(📈) -> 📈
🔌 ntohl(🔵) -> 🔵
🔌 inet_addr(*🔶) -> 🔵

// 📐
🔌 sqrt(🌀) -> 🌀
🔌 sin(🌀) -> 🌀
🔌 cos(🌀) -> 🌀
🔌 pow(🌀, 🌀) -> 🌀
🔌 fabs(🌀) -> 🌀
🔌 floor(🌀) -> 🌀
🔌 ceil(🌀) -> 🌀
🔌 log(🌀) -> 🌀

// ⚙️
🔌 exit(🔢)
🔌 fork() -> 🔢
🔌 execvp(*🔶, **🔶) -> 🔢
🔌 waitpid(🔢, *🔢, 🔢) -> 🔢
🔌 getpid() -> 🔢
🔌 getenv(*🔶) -> *🔶
🔌 sleep(🔵) -> 🔵
🔌 usleep(🔵) -> 🔢

// 🗺️
🔌 mmap(*⬛, 💎, 🔢, 🔢, 🔢, 🔷) -> *⬛
🔌 munmap(*⬛, 💎) -> 🔢
