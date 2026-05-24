#ifndef CODEGEN_H
#define CODEGEN_H

#include "parser.h"
#include "analyzer.h"
#include <stdio.h>

/* Generate C source from the annotated AST.
   Writes to `out`. Returns 0 on success. */
int codegen(ASTNode *program, Scope *global_scope,
            StructDef *structs, FILE *out);

#endif /* CODEGEN_H */
