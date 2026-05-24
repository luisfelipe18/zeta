#include "analyzer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════
   ZType helpers
   ═══════════════════════════════════════════════════════════════════ */

ZType ztype_from_str(const char *s) {
    ZType t; memset(&t, 0, sizeof(t));
    if (!s || !s[0]) { t.kind = ZTYPE_UNKNOWN; return t; }
    if (s[0] == '*') {
        ZType base = ztype_from_str(s + 1);
        t = base; t.is_ptr = 1; return t;
    }
    if (strcmp(s, "i32")  == 0) { t.kind = ZTYPE_I32;  return t; }
    if (strcmp(s, "f64")  == 0) { t.kind = ZTYPE_F64;  return t; }
    if (strcmp(s, "str")  == 0) { t.kind = ZTYPE_STR;  return t; }
    if (strcmp(s, "bool") == 0) { t.kind = ZTYPE_BOOL; return t; }
    if (strcmp(s, "void") == 0) { t.kind = ZTYPE_VOID; return t; }
    t.kind = ZTYPE_STRUCT;
    strncpy(t.name, s, sizeof(t.name) - 1);
    return t;
}

const char *ztype_to_c(ZType t) {
    static char buf[128];
    const char *base;
    switch (t.kind) {
        case ZTYPE_I32:    base = "int";    break;
        case ZTYPE_F64:    base = "double"; break;
        case ZTYPE_STR:    base = "char *"; break;
        case ZTYPE_BOOL:   base = "int";    break;
        case ZTYPE_VOID:   base = "void";   break;
        case ZTYPE_STRUCT: base = t.name;   break;
        default:           base = "int";    break;
    }
    if (t.is_ptr) snprintf(buf, sizeof(buf), "%s *", base);
    else          snprintf(buf, sizeof(buf), "%s",   base);
    return buf;
}

const char *ztype_fmt(ZType t) {
    if (t.is_ptr) return "%p";
    switch (t.kind) {
        case ZTYPE_I32:  return "%d";
        case ZTYPE_F64:  return "%g";
        case ZTYPE_STR:  return "%s";
        case ZTYPE_BOOL: return "%d";
        default:         return "%d";
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Scope / symbol table
   ═══════════════════════════════════════════════════════════════════ */

Scope *scope_new(Scope *parent) {
    Scope *s = (Scope *)calloc(1, sizeof(Scope));
    s->parent = parent;
    return s;
}

void scope_define(Scope *s, const char *name, ZType type, int is_mut) {
    /* Update existing entry if in the same scope */
    for (SymEntry *e = s->head; e; e = e->next) {
        if (strcmp(e->name, name) == 0) { e->type = type; e->is_mut = is_mut; return; }
    }
    SymEntry *e = (SymEntry *)calloc(1, sizeof(SymEntry));
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->type   = type;
    e->is_mut = is_mut;
    e->next   = s->head;
    s->head   = e;
}

SymEntry *scope_lookup(Scope *s, const char *name) {
    for (Scope *sc = s; sc; sc = sc->parent)
        for (SymEntry *e = sc->head; e; e = e->next)
            if (strcmp(e->name, name) == 0) return e;
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════════
   Internal analyzer state
   ═══════════════════════════════════════════════════════════════════ */

typedef struct {
    Scope     *current;
    char       impl_type[64];
    StructDef *structs;
} Ana;

static StructDef *struct_find(Ana *a, const char *name) {
    for (StructDef *sd = a->structs; sd; sd = sd->next)
        if (strcmp(sd->name, name) == 0) return sd;
    return NULL;
}

static StructField *field_find(StructDef *sd, const char *name) {
    for (StructField *f = sd->fields; f; f = f->next)
        if (strcmp(f->name, name) == 0) return f;
    return NULL;
}

/* ── Annotate node and return its ZType ─────────────────────────── */
static ZType node_set(ASTNode *n, ZType t) {
    if (!n) return t;
    n->ztype        = t.kind;
    n->ztype_is_ptr = t.is_ptr;
    strncpy(n->ztype_name, t.name, sizeof(n->ztype_name) - 1);
    return t;
}

static ZType infer(Ana *a, ASTNode *n);

static ZType infer(Ana *a, ASTNode *n) {
    ZType unk; memset(&unk, 0, sizeof(unk));
    if (!n) return (ZType){.kind = ZTYPE_VOID};

    ZType r = unk;
    switch (n->type) {
    case AST_INT_LIT:   r = (ZType){.kind = ZTYPE_I32};  break;
    case AST_FLOAT_LIT: r = (ZType){.kind = ZTYPE_F64};  break;
    case AST_STR_LIT:   r = (ZType){.kind = ZTYPE_STR};  break;
    case AST_BOOL_LIT:  r = (ZType){.kind = ZTYPE_BOOL}; break;

    case AST_IDENT: {
        SymEntry *e = scope_lookup(a->current, n->str);
        if (e) r = e->type;
        break;
    }

    case AST_BIN_OP: {
        const char *op = n->str;
        int is_cmp = (strcmp(op,"<")==0||strcmp(op,">")==0||strcmp(op,"<=")==0||
                      strcmp(op,">=")==0||strcmp(op,"==")==0||strcmp(op,"!=")==0||
                      strcmp(op,"&&")==0||strcmp(op,"||")==0);
        infer(a, n->left); infer(a, n->right);
        r = is_cmp ? (ZType){.kind = ZTYPE_BOOL} : infer(a, n->left);
        if (r.kind == ZTYPE_UNKNOWN) r = infer(a, n->right);
        break;
    }

    case AST_UNARY_OP:
        infer(a, n->left);
        r = (strcmp(n->str, "!") == 0) ? (ZType){.kind = ZTYPE_BOOL} : infer(a, n->left);
        break;

    case AST_CALL: {
        /* Resolve callee */
        char fname[256] = {0};
        if (n->left && n->left->type == AST_IDENT) {
            strncpy(fname, n->left->str, sizeof(fname)-1);
        } else if (n->left && n->left->type == AST_MEMBER) {
            /* method call: infer object type → mangle */
            ZType obj = infer(a, n->left->left);
            ZType base = obj; base.is_ptr = 0;
            if (base.kind == ZTYPE_STRUCT)
                snprintf(fname, sizeof(fname), "%s_%s", base.name, n->left->str);
        }
        if (fname[0]) {
            SymEntry *e = scope_lookup(a->current, fname);
            if (e) r = e->type;
        }
        /* Infer arg types */
        for (int i = 0; i < n->nchildren; i++) infer(a, n->children[i]);
        break;
    }

    case AST_MEMBER: {
        ZType obj = infer(a, n->left);
        ZType base = obj; base.is_ptr = 0;
        if (base.kind == ZTYPE_STRUCT) {
            StructDef *sd = struct_find(a, base.name);
            if (sd) { StructField *f = field_find(sd, n->str); if (f) r = f->type; }
        }
        break;
    }

    case AST_DEREF_EXPR: {
        ZType ptr = infer(a, n->left);
        if (ptr.is_ptr) { r = ptr; r.is_ptr = 0; }
        break;
    }

    case AST_ASSIGN:
        infer(a, n->left); r = infer(a, n->right); break;

    default: break;
    }
    return node_set(n, r);
}

/* ── Statement analysis ─────────────────────────────────────────── */

static void ana_block(Ana *a, ASTNode *block);

static void ana_stmt(Ana *a, ASTNode *n) {
    if (!n) return;
    switch (n->type) {
    case AST_VAR_DECL: {
        ZType t;
        if (n->str2[0]) t = ztype_from_str(n->str2);
        else {
            t = infer(a, n->left);
            if (t.kind == ZTYPE_UNKNOWN) t.kind = ZTYPE_I32; /* safe fallback */
        }
        node_set(n, t);
        /* Also infer the initializer expression */
        infer(a, n->left);
        scope_define(a->current, n->str, t, n->flag);
        break;
    }
    case AST_ASSIGN:  infer(a, n->left); infer(a, n->right); break;
    case AST_EXPR_STMT: infer(a, n->left); break;
    case AST_RETURN_STMT: if (n->left) infer(a, n->left); break;
    case AST_IF_STMT:
        infer(a, n->left);
        ana_block(a, n->body);
        if (n->right) ana_block(a, n->right);
        break;
    case AST_WHILE_STMT:
        infer(a, n->left);
        ana_block(a, n->body);
        break;
    case AST_FOR_STMT: {
        infer(a, n->left);
        Scope *s = scope_new(a->current);
        a->current = s;
        scope_define(s, n->str, (ZType){.kind = ZTYPE_I32}, 1);
        ana_block(a, n->body);
        a->current = s->parent;
        break;
    }
    default: break;
    }
}

static void ana_block(Ana *a, ASTNode *block) {
    if (!block) return;
    Scope *s = scope_new(a->current);
    a->current = s;
    for (int i = 0; i < block->nchildren; i++) ana_stmt(a, block->children[i]);
    a->current = s->parent;
}

/* ── First pass: collect signatures ────────────────────────────── */

static void collect_func(Ana *a, ASTNode *fn, const char *prefix) {
    char mangled[256];
    if (prefix && prefix[0])
        snprintf(mangled, sizeof(mangled), "%s_%s", prefix, fn->str);
    else
        strncpy(mangled, fn->str, sizeof(mangled)-1);

    ZType ret;
    if (fn->str2[0]) ret = ztype_from_str(fn->str2);
    else             ret = (ZType){.kind = ZTYPE_VOID};
    if (strcmp(fn->str,"main")==0 && (!prefix||!prefix[0]))
        ret = (ZType){.kind = ZTYPE_I32};

    scope_define(a->current, mangled, ret, 0);
}

static void collect_defs(Ana *a, ASTNode *prog) {
    for (int i = 0; i < prog->nchildren; i++) {
        ASTNode *n = prog->children[i];
        if (n->type == AST_FUNC_DEF) {
            collect_func(a, n, NULL);
        } else if (n->type == AST_STRUCT_DEF) {
            StructDef *sd = (StructDef *)calloc(1, sizeof(StructDef));
            strncpy(sd->name, n->str, sizeof(sd->name)-1);
            for (int j = n->nchildren-1; j >= 0; j--) {
                ASTNode *fld = n->children[j];
                if (fld->type != AST_FIELD) continue;
                StructField *f = (StructField *)calloc(1, sizeof(StructField));
                strncpy(f->name, fld->str,  sizeof(f->name)-1);
                f->type  = ztype_from_str(fld->str2);
                f->next  = sd->fields;
                sd->fields = f;
            }
            sd->next   = a->structs;
            a->structs = sd;
            ZType t = (ZType){.kind = ZTYPE_STRUCT};
            strncpy(t.name, n->str, sizeof(t.name)-1);
            scope_define(a->current, n->str, t, 0);
        } else if (n->type == AST_IMPL_DEF) {
            for (int j = 0; j < n->nchildren; j++)
                if (n->children[j]->type == AST_FUNC_DEF)
                    collect_func(a, n->children[j], n->str);
        }
    }
}

/* ── Second pass: analyze bodies ────────────────────────────────── */

static void analyze_func(Ana *a, ASTNode *fn, const char *impl_type) {
    Scope *fs = scope_new(a->current);
    a->current = fs;

    if (impl_type && impl_type[0]) {
        ZType st = (ZType){.kind = ZTYPE_STRUCT}; st.is_ptr = 1;
        strncpy(st.name, impl_type, sizeof(st.name)-1);
        scope_define(fs, "self", st, 1);
    }
    for (int i = 0; i < fn->nchildren; i++) {
        ASTNode *p = fn->children[i];
        if (p->type == AST_PARAM && strcmp(p->str,"self")!=0)
            scope_define(fs, p->str, ztype_from_str(p->str2), p->flag);
    }

    ana_block(a, fn->body);
    a->current = fs->parent;
}

/* ═══════════════════════════════════════════════════════════════════
   Public entry point
   ═══════════════════════════════════════════════════════════════════ */

void analyze(ASTNode *prog, Scope **out_scope, StructDef **out_structs) {
    Ana a; memset(&a, 0, sizeof(a));
    a.current = scope_new(NULL);

    collect_defs(&a, prog);

    for (int i = 0; i < prog->nchildren; i++) {
        ASTNode *n = prog->children[i];
        if (n->type == AST_FUNC_DEF) {
            analyze_func(&a, n, NULL);
        } else if (n->type == AST_IMPL_DEF) {
            strncpy(a.impl_type, n->str, sizeof(a.impl_type)-1);
            for (int j = 0; j < n->nchildren; j++)
                if (n->children[j]->type == AST_FUNC_DEF)
                    analyze_func(&a, n->children[j], n->str);
            a.impl_type[0] = '\0';
        }
    }

    if (out_scope)   *out_scope   = a.current;
    if (out_structs) *out_structs = a.structs;
}
