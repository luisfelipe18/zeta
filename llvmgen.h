#ifndef LLVMGEN_H
#define LLVMGEN_H

#include "parser.h"
#include "analyzer.h"
#include <stdio.h>

/* Generate LLVM IR from the annotated AST.
   Writes textual LLVM IR (.ll format) to `out`.
   Returns 0 on success. */
int llvmgen(ASTNode *program, Scope *global_scope,
            StructDef *structs, FILE *out);

#endif /* LLVMGEN_H */
