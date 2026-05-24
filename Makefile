CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -pedantic

.PHONY: all clean

all: z_test zc

# ── z_test: AST viewer ────────────────────────────────────────────────
Z_TEST_OBJS = lexer.o parser.o main.o

z_test: $(Z_TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── zc: full compiler ─────────────────────────────────────────────────
ZC_OBJS = lexer.o parser.o analyzer.o codegen.o llvmgen.o zc_main.o

zc: $(ZC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Compilation rules ─────────────────────────────────────────────────
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lexer.o:    lexer.c    lexer.h
parser.o:   parser.c   parser.h lexer.h
main.o:     main.c     parser.h lexer.h
analyzer.o: analyzer.c analyzer.h parser.h lexer.h
codegen.o:  codegen.c  codegen.h analyzer.h parser.h lexer.h
llvmgen.o:  llvmgen.c  llvmgen.h analyzer.h parser.h lexer.h
zc_main.o:  zc_main.c  parser.h analyzer.h codegen.h llvmgen.h

clean:
	rm -f *.o z_test zc
