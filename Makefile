# ── OS detection ──────────────────────────────────────────────────────
# Works with: macOS (Darwin), Linux, and Windows via MSYS2/MinGW
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifneq (,$(findstring MINGW,$(UNAME_S)))
    PLATFORM = windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    PLATFORM = windows
else ifeq ($(UNAME_S),Darwin)
    PLATFORM = macos
else ifeq ($(UNAME_S),Linux)
    PLATFORM = linux
else ifdef OS          # OS=Windows_NT is set by cmd.exe
    PLATFORM = windows
else
    PLATFORM = linux   # safe fallback
endif

# ── Compiler & flags ──────────────────────────────────────────────────
CC     = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic

ifeq ($(PLATFORM),windows)
    EXE = .exe
    # Link against Windows sockets and process libs (needed by platform.h)
    LDFLAGS = -lws2_32
else
    EXE     =
    LDFLAGS =
endif

ifeq ($(PLATFORM),linux)
    # Some Linux distros require -lm for math and -ldl for dynamic linking.
    # Neither is used currently, but keep the hook here for future use.
    LDFLAGS +=
endif

# ── Targets ───────────────────────────────────────────────────────────
.PHONY: all clean check

all: z_test$(EXE) zc$(EXE)

# ── z_test: AST viewer ────────────────────────────────────────────────
Z_TEST_OBJS = lexer.o parser.o main.o

z_test$(EXE): $(Z_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── zc: full compiler ─────────────────────────────────────────────────
ZC_OBJS = lexer.o parser.o analyzer.o codegen.o llvmgen.o zc_main.o

zc$(EXE): $(ZC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Compilation rules ─────────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lexer.o:    lexer.c    lexer.h
parser.o:   parser.c   parser.h lexer.h
main.o:     main.c     parser.h lexer.h
analyzer.o: analyzer.c analyzer.h parser.h lexer.h
codegen.o:  codegen.c  codegen.h analyzer.h parser.h lexer.h
llvmgen.o:  llvmgen.c  llvmgen.h analyzer.h parser.h lexer.h platform.h
zc_main.o:  zc_main.c  parser.h analyzer.h codegen.h llvmgen.h platform.h

# ── Smoke test: compile and run all examples ─────────────────────────
check: zc$(EXE)
	@echo "==> hello.z"
	./zc$(EXE) examples/hello.z     -o /tmp/zc_check_hello     && /tmp/zc_check_hello
	@echo "==> primes.z"
	./zc$(EXE) examples/primes.z    -o /tmp/zc_check_primes    && /tmp/zc_check_primes
	@echo "==> fibonacci.z"
	./zc$(EXE) examples/fibonacci.z -o /tmp/zc_check_fibonacci && /tmp/zc_check_fibonacci
	@echo "==> ranges.z"
	./zc$(EXE) examples/ranges.z    -o /tmp/zc_check_ranges    && /tmp/zc_check_ranges
	@echo "==> structs.z"
	./zc$(EXE) examples/structs.z   -o /tmp/zc_check_structs   && /tmp/zc_check_structs
	@echo "==> list_ops.z"
	./zc$(EXE) examples/list_ops.z  -o /tmp/zc_check_list_ops  && /tmp/zc_check_list_ops
	@echo "==> pointers.z"
	./zc$(EXE) examples/pointers.z  -o /tmp/zc_check_pointers  && /tmp/zc_check_pointers
	@echo ""
	@echo "All examples passed."

# ── Clean ─────────────────────────────────────────────────────────────
clean:
	rm -f *.o z_test zc z_test.exe zc.exe
