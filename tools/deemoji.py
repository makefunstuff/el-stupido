#!/usr/bin/env python3
"""Convert el-stupido .es files from emoji to ASCII mode."""

import sys, re, os

# Type emoji → ASCII types
TYPES = {
    "\U0001f4a7": "i8",  # 💧
    "\U0001f4ca": "i16",  # 📊
    "\U0001f522": "i32",  # 🔢
    "\U0001f537": "i64",  # 🔷
    "\U0001f536": "u8",  # 🔶
    "\U0001f4c8": "u16",  # 📈
    "\U0001f535": "u32",  # 🔵
    "\U0001f48e": "u64",  # 💎
    "\U0001f30a": "f32",  # 🌊
    "\U0001f300": "f64",  # 🌀
    "\U00002b1b": "void",  # ⬛
}

# Function alias emoji → C function names
FN_ALIASES = {
    "\U0001f5a8": "printf",  # 🖨
    "\U0001f4e3": "fprintf",  # 📣
    "\U0001f4dd": "sprintf",  # 📝
    "\U0001f4e2": "puts",  # 📢
    "\U0001f514": "putchar",  # 🔔
    "\U0001f442": "getchar",  # 👂
    "\U0001f4c2": "open",  # 📂
    "\U0001f4d5": "close",  # 📕
    "\U0001f4d6": "read",  # 📖
    "\U0000270f": "write",  # ✏
    "\U0001f516": "lseek",  # 🔖
    "\U0001f9e0": "malloc",  # 🧠
    "\U0001f9e9": "calloc",  # 🧩
    "\U0000267b": "realloc",  # ♻
    "\U0001f193": "free",  # 🆓
    "\U0001f9f9": "memset",  # 🧹
    "\U0001f4cb": "memcpy",  # 📋
    "\U0001f500": "memmove",  # 🔀
    "\U00002696": "memcmp",  # ⚖
    "\U0001f9f5": "strlen",  # 🧵
    "\U00002694": "strcmp",  # ⚔
    "\U0001f5e1": "strncmp",  # 🗡
    "\U00002702": "strcpy",  # ✂
    "\U0001faa1": "strncpy",  # 🪡
    "\U0001f517": "strcat",  # 🔗
    "\U0001f50d": "strchr",  # 🔍
    "\U0001f50e": "strstr",  # 🔎
    "\U0001f170": "atoi",  # 🅰
    "\U0001f171": "atol",  # 🅱
    "\U0001f310": "socket",  # 🌐
    "\U0001f4cc": "bind",  # 📌
    "\U0001f4e1": "listen",  # 📡
    "\U0001f91d": "accept",  # 🤝
    "\U0001f9f2": "connect",  # 🧲
    "\U0001f4e4": "send",  # 📤
    "\U0001f4e9": "recv",  # 📩
    "\U0001f39b": "setsockopt",  # 🎛
    "\U0001f503": "htons",  # 🔃
    "\U0001f502": "htonl",  # 🔂
    "\U0001f519": "ntohs",  # 🔙
    "\U0001f51a": "ntohl",  # 🔚
    "\U0001f3e0": "inet_addr",  # 🏠
    "\U0001f4d0": "sqrt",  # 📐
    "\U0001f3b5": "sin",  # 🎵
    "\U0001f3b6": "cos",  # 🎶
    "\U0001f4aa": "pow",  # 💪
    "\U0001f9ca": "fabs",  # 🧊
    "\U00002b07": "floor",  # ⬇
    "\U00002b06": "ceil",  # ⬆
    "\U0001f4d3": "log",  # 📓
    "\U0001f480": "exit",  # 💀
    "\U0001f374": "fork",  # 🍴
    "\U0001f3c3": "execvp",  # 🏃
    "\U0000231b": "waitpid",  # ⏳
    "\U0001f194": "getpid",  # 🆔
    "\U0001f634": "sleep",  # 😴
    "\U000023f0": "usleep",  # ⏰
    "\U0001f5fa": "mmap",  # 🗺
    "\U0001f6ab": "munmap",  # 🚫
    "\U0001f3c1": "main",  # 🏁
}

# Keyword emoji → ASCII keywords
KEYWORDS = {
    "\U0001f527": "fn",  # 🔧
    "\U00002753": "if",  # ❓
    "\U00002757": "el",  # ❗
    "\U0001f501": "wh",  # 🔁
    "\U000021a9": "ret",  # ↩
    "\U0001f6d1": "brk",  # 🛑
    "\U000023e9": "cont",  # ⏩
    "\U0001f4e6": "st",  # 📦
    "\U0001f50c": "ext",  # 🔌
    "\U0001f4e5": "use",  # 📥
    "\U00002728": "nw",  # ✨
    "\U0001f5d1": "del",  # 🗑
    "\U0001f529": "asm",  # 🔩
    "\U000026a1": "ct",  # ⚡
    "\U000027b0": "fo",  # ➰
    "\U0001f3af": "ma",  # 🎯
    "\U0001f3f7": "en",  # 🏷
    "\U0001f51c": "df",  # 🔜
    "\U0001f504": "as",  # 🔄
    "\U0001f4cf": "sz",  # 📏
    "\U00002205": "null",  # ∅
}

# Macro arrow
MACRO = {
    "\U0001f449": "=>",  # 👉
}

# Variation selector that may follow emoji
VS16 = "\ufe0f"


def convert(text):
    # Build combined map, longest match first
    all_maps = {}
    all_maps.update(TYPES)
    all_maps.update(FN_ALIASES)
    all_maps.update(KEYWORDS)
    all_maps.update(MACRO)

    result = []
    i = 0
    while i < len(text):
        matched = False
        for emoji, replacement in all_maps.items():
            if text[i:].startswith(emoji):
                skip = len(emoji)
                # Skip trailing variation selector
                if i + skip < len(text) and text[i + skip] == VS16:
                    skip += 1
                # For keywords like 'if', 'el', 'fn' etc — add space after if next char is not space/newline/paren/brace
                if emoji in KEYWORDS or emoji in MACRO:
                    next_pos = i + skip
                    if next_pos < len(text) and text[next_pos] not in " \t\n\r(){}":
                        replacement = replacement + " "
                # For 🏁() → fn main()
                if emoji == "\U0001f3c1":
                    replacement = "fn main"
                result.append(replacement)
                i += skip
                matched = True
                break
        if not matched:
            # Skip standalone variation selectors
            if text[i] == VS16:
                i += 1
            else:
                result.append(text[i])
                i += 1
    return "".join(result)


def process_file(path):
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    converted = convert(content)
    if converted != content:
        with open(path, "w", encoding="utf-8") as f:
            f.write(converted)
        print(f"  converted: {path}")
    else:
        print(f"  unchanged: {path}")


if __name__ == "__main__":
    dirs = ["lib", "examples", "tools"]
    for d in dirs:
        base = os.path.join(os.path.dirname(__file__), "..", d)
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if fn.endswith(".es"):
                process_file(os.path.join(base, fn))
