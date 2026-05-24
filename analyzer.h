#ifndef ANALYZER_H
#define ANALYZER_H

#include "parser.h"

/* ── Z type system ───────────────────────────────────────────────── */
typedef enum {
    ZTYPE_UNKNOWN = 0,
    ZTYPE_VOID,
    ZTYPE_I32,
    ZTYPE_F64,
    ZTYPE_STR,
    ZTYPE_BOOL,
    ZTYPE_PTR,
    ZTYPE_STRUCT,
} ZTypeKind;

typedef struct {
    ZTypeKind kind;
    char      name[64];  /* struct name for ZTYPE_STRUCT */
    int       is_ptr;    /* 1 = pointer to this type     */
} ZType;

/* ── Symbol table ─────────────────────────────────────────────────── */
typedef struct SymEntry {
    char            name[128];
    ZType           type;
    int             is_mut;
    struct SymEntry *next;
} SymEntry;

typedef struct Scope {
    SymEntry    *head;
    struct Scope *parent;
} Scope;

/* ── Struct registry ──────────────────────────────────────────────── */
typedef struct StructField {
    char              name[64];
    ZType             type;
    struct StructField *next;
} StructField;

typedef struct StructDef {
    char            name[64];
    StructField    *fields;
    struct StructDef *next;
} StructDef;

/* ── Public API ───────────────────────────────────────────────────── */
ZType       ztype_from_str(const char *s);
const char *ztype_to_c(ZType t);
const char *ztype_fmt(ZType t);   /* printf format specifier */

Scope    *scope_new(Scope *parent);
void      scope_define(Scope *s, const char *name, ZType type, int is_mut);
SymEntry *scope_lookup(Scope *s, const char *name);

/* Annotates every AST node with its resolved ZType.
   Returns the global scope and struct registry for the codegen. */
void analyze(ASTNode *program, Scope **out_scope, StructDef **out_structs);

#endif /* ANALYZER_H */
