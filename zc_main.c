/* zc — Z Compiler  (Phases 1-4+LLVM: Lex → Parse → Analyze → IR → clang)
   Usage:
     zc source.z              compile via LLVM IR → clang -O3 → binary
     zc source.z -o out       compile to ./out
     zc source.z --emit-llvm  print LLVM IR and exit
     zc source.z --emit-c     print generated C and exit  (debug)
     zc source.z --ast        print AST and exit
     zc source.z --backend=c  use C backend + gcc instead of LLVM
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parser.h"
#include "analyzer.h"
#include "codegen.h"
#include "llvmgen.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "zc: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    buf[fread(buf, 1, (size_t)sz, f)] = '\0';
    fclose(f); return buf;
}

int main(int argc, char *argv[]) {
    const char *src_path  = NULL;
    const char *out_path  = NULL;
    int emit_c    = 0;
    int emit_llvm = 0;
    int print_ast = 0;
    int use_c_backend = 0;   /* 0 = LLVM (default), 1 = C + gcc */

    for (int i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--emit-c")    == 0) { emit_c    = 1; }
        else if (strcmp(argv[i], "--emit-llvm") == 0) { emit_llvm = 1; }
        else if (strcmp(argv[i], "--ast")       == 0) { print_ast = 1; }
        else if (strcmp(argv[i], "--backend=c") == 0) { use_c_backend = 1; }
        else if (strcmp(argv[i], "-o") == 0 && i+1 < argc) { out_path = argv[++i]; }
        else if (argv[i][0] != '-') { src_path = argv[i]; }
    }

    if (!src_path) {
        fprintf(stderr,
            "zc — Z Compiler (LLVM backend)\n"
            "Uso: zc <archivo.z> [-o salida] [--emit-llvm] [--emit-c] [--ast]\n"
            "     zc <archivo.z> --backend=c   usar transpilador C + gcc\n");
        return 1;
    }

    /* ── 1. Read source ──────────────────────────────────────────────── */
    char *src = read_file(src_path);
    if (!src) return 1;

    /* ── 2. Parse ────────────────────────────────────────────────────── */
    ASTNode *tree = parse(src);
    if (!tree) { fprintf(stderr, "zc: parse failed\n"); free(src); return 1; }

    if (print_ast) { ast_print(tree, 0); ast_free(tree); free(src); return 0; }

    /* ── 3. Analyze ──────────────────────────────────────────────────── */
    Scope     *global_scope = NULL;
    StructDef *structs      = NULL;
    analyze(tree, &global_scope, &structs);

    /* ── Derive output name ───────────────────────────────────────────── */
    char default_out[512];
    if (!out_path) {
        const char *base = strrchr(src_path, '/');
        base = base ? base + 1 : src_path;
        strncpy(default_out, base, sizeof(default_out)-1);
        default_out[sizeof(default_out)-1] = '\0';
        char *dot = strrchr(default_out, '.');
        if (dot) *dot = '\0';
        out_path = default_out;
    }

    /* ── 4a. Emit C (debug mode) ─────────────────────────────────────── */
    if (emit_c) {
        codegen(tree, global_scope, structs, stdout);
        ast_free(tree); free(src); return 0;
    }

    /* ── 4b. Emit LLVM IR ────────────────────────────────────────────── */
    if (emit_llvm) {
        llvmgen(tree, global_scope, structs, stdout);
        ast_free(tree); free(src); return 0;
    }

    /* ── 5a. C backend: codegen → gcc ───────────────────────────────── */
    if (use_c_backend) {
        char c_path[512];
        snprintf(c_path, sizeof(c_path), "/tmp/_zc_%d.c", (int)getpid());
        FILE *cf = fopen(c_path, "w");
        if (!cf) { fprintf(stderr, "zc: cannot create temp file\n"); return 1; }
        codegen(tree, global_scope, structs, cf);
        fclose(cf);

        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "gcc -O2 -o \"%s\" \"%s\"", out_path, c_path);
        int rc = system(cmd);
        remove(c_path);
        if (rc != 0) {
            fprintf(stderr, "zc: gcc compilation failed (exit %d)\n", rc);
            ast_free(tree); free(src); return 1;
        }
        printf("zc: compiled '%s' → '%s'  (C backend)\n", src_path, out_path);
        ast_free(tree); free(src);
        return 0;
    }

    /* ── 5b. LLVM backend (default): llvmgen → clang -O3 ───────────── */
    char ll_path[512];
    snprintf(ll_path, sizeof(ll_path), "/tmp/_zc_%d.ll", (int)getpid());

    FILE *lf = fopen(ll_path, "w");
    if (!lf) { fprintf(stderr, "zc: cannot create temp file\n"); return 1; }
    llvmgen(tree, global_scope, structs, lf);
    fclose(lf);

    char cmd[1024];
    /* -Wno-override-module: suppress the target triple mismatch warning */
    snprintf(cmd, sizeof(cmd),
        "clang -O3 -Wno-override-module -o \"%s\" \"%s\" 2>&1",
        out_path, ll_path);
    int rc = system(cmd);
    remove(ll_path);

    if (rc != 0) {
        fprintf(stderr, "zc: clang compilation failed (exit %d)\n", rc);
        fprintf(stderr, "    tip: retry with --backend=c for C transpiler mode\n");
        ast_free(tree); free(src); return 1;
    }

    printf("zc: compiled '%s' → '%s'  (LLVM/clang -O3)\n", src_path, out_path);
    ast_free(tree); free(src);
    return 0;
}
