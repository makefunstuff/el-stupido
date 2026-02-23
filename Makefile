CC = cc
CFLAGS = $(shell llvm-config --cflags) -Wall -Wextra -g -Isrc -Wno-unused-parameter -MMD -MP
LDFLAGS = $(shell llvm-config --ldflags --libs core analysis native) $(shell llvm-config --system-libs)

# LLVM version check (require 17+)
LLVM_VERSION := $(shell llvm-config --version | cut -d. -f1)
LLVM_OK := $(shell test $(LLVM_VERSION) -ge 17 2>/dev/null && echo yes)
ifneq ($(LLVM_OK),yes)
$(error el-stupido requires LLVM 17+. Found LLVM $(LLVM_VERSION). Install a newer LLVM or set PATH.)
endif

SRCS = src/main.c src/lexer.c src/ast.c src/parser.c src/codegen.c src/sexpr.c src/preproc.c src/codebook.c src/normalize.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

esc: $(OBJS)
	$(CC) $(OBJS) -o esc $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) esc *.o

test: esc examples/hello.es
	./esc examples/hello.es -o hello && ./hello

.PHONY: clean test
