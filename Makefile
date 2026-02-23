CC = cc
CFLAGS = $(shell llvm-config --cflags) -Wall -Wextra -g -Isrc -Wno-unused-parameter
LDFLAGS = $(shell llvm-config --ldflags --libs core analysis native) $(shell llvm-config --system-libs)

SRCS = src/main.c src/lexer.c src/ast.c src/parser.c src/codegen.c src/sexpr.c src/preproc.c src/codebook.c
OBJS = $(SRCS:.c=.o)

esc: $(OBJS)
	$(CC) $(OBJS) -o esc $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) esc *.o

test: esc examples/hello.es
	./esc examples/hello.es -o hello && ./hello

.PHONY: clean test
