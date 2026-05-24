#include <stdio.h>
#include <stdlib.h>
#include "parser.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "error: cannot open '%s'\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.z>\n", argv[0]);
        return 1;
    }
    char *src = read_file(argv[1]);
    if (!src) return 1;

    printf("=== Parsing: %s ===\n\n", argv[1]);
    ASTNode *tree = parse(src);
    ast_print(tree, 0);
    ast_free(tree);
    free(src);
    return 0;
}
