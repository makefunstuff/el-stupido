CC = cc
CFLAGS = $(shell llvm-config --cflags) -Wall -Wextra -g -Isrc -Wno-unused-parameter -MMD -MP
LDFLAGS = $(shell llvm-config --ldflags --libs core analysis native) $(shell llvm-config --system-libs)

# LLVM version check (require 17+)
LLVM_VERSION := $(shell llvm-config --version | cut -d. -f1)
LLVM_OK := $(shell test $(LLVM_VERSION) -ge 17 2>/dev/null && echo yes)
ifneq ($(LLVM_OK),yes)
$(error el-stupido requires LLVM 17+. Found LLVM $(LLVM_VERSION). Install a newer LLVM or set PATH.)
endif

SRCS = src/main.c src/lexer.c src/ast.c src/parser.c src/codegen.c src/sexpr.c src/preproc.c src/codebook.c src/normalize.c src/manifest.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

# --- Optional: embedded LLM (build with `make LLAMA=1`) ---
ifdef LLAMA
LLAMA_DIR    = deps/llama.cpp
LLAMA_BUILD  = $(LLAMA_DIR)/build
LLAMA_LIBS   = $(LLAMA_BUILD)/src/libllama.a

CFLAGS  += -DHAS_LLAMA \
           -I$(LLAMA_DIR)/include \
           -I$(LLAMA_DIR)/ggml/include
LDFLAGS += -L$(LLAMA_BUILD)/src \
           -L$(LLAMA_BUILD)/ggml/src \
           -lllama -lggml -lggml-base -lggml-cpu \
           -lstdc++ -lpthread -lgomp
SRCS    += src/llm.c
LDFLAGS += -lm
endif

# default target — must be first recipe
esc: $(OBJS) $(LLAMA_LIBS)
	$(CC) $(OBJS) -o esc $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build llama.cpp static libs if missing (only relevant when LLAMA=1)
ifdef LLAMA
$(LLAMA_LIBS):
	cmake -B $(LLAMA_BUILD) -S $(LLAMA_DIR) \
		-DBUILD_SHARED_LIBS=OFF \
		-DLLAMA_BUILD_TESTS=OFF \
		-DLLAMA_BUILD_TOOLS=OFF \
		-DLLAMA_BUILD_EXAMPLES=OFF \
		-DLLAMA_BUILD_SERVER=OFF \
		-DLLAMA_BUILD_COMMON=OFF \
		-DGGML_CUDA=OFF \
		-DGGML_METAL=OFF \
		-DGGML_VULKAN=OFF \
		-DCMAKE_BUILD_TYPE=Release
	cmake --build $(LLAMA_BUILD) -j$$(nproc) --target llama --target ggml
endif

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) esc *.o

test: esc examples/hello.es
	./esc examples/hello.es -o hello && ./hello

.PHONY: clean test
