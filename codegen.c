#include "codegen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ═══════════════════════════════════════════════════════════════════
   Code generator context
   ═══════════════════════════════════════════════════════════════════ */

typedef struct CG {
    FILE      *out;
    int        indent;
    Scope     *gscope;      /* global scope (from analyzer) */
    Scope     *lscope;      /* local scope (rebuilt during codegen) */
    StructDef *structs;
    char       impl_type[64];
} CG;

/* ── Helpers ──────────────────────────────────────────────────────── */

static void ind(CG *g) {
    for (int i = 0; i < g->indent; i++) fputs("    ", g->out);
}

static ZType cg_lookup_type(CG *g, const char *name) {
    SymEntry *e = scope_lookup(g->lscope ? g->lscope : g->gscope, name);
    if (e) return e->type;
    e = scope_lookup(g->gscope, name);
    if (e) return e->type;
    return (ZType){.kind = ZTYPE_UNKNOWN};
}

static ZType node_type(CG *g, ASTNode *n) {
    if (!n) return (ZType){.kind = ZTYPE_VOID};
    ZType t; memset(&t, 0, sizeof(t));
    t.kind   = (ZTypeKind)n->ztype;
    t.is_ptr = n->ztype_is_ptr;
    strncpy(t.name, n->ztype_name, sizeof(t.name)-1);
    if (t.kind == ZTYPE_UNKNOWN && n->type == AST_IDENT)
        t = cg_lookup_type(g, n->str);
    return t;
}

static void lscope_push(CG *g) {
    g->lscope = scope_new(g->lscope);
}

static void lscope_pop(CG *g) {
    if (g->lscope) g->lscope = g->lscope->parent;
}

static void lscope_def(CG *g, const char *name, ZType t, int is_mut) {
    if (g->lscope) scope_define(g->lscope, name, t, is_mut);
}

/* ── Forward declarations ────────────────────────────────────────── */

static void gen_expr(CG *g, ASTNode *n);
static void gen_stmt(CG *g, ASTNode *n);
static void gen_block(CG *g, ASTNode *block);

/* Emit a condition without extra outer parens for BIN_OP —
   the surrounding `if (...)` already provides parentheses. */
static void gen_cond(CG *g, ASTNode *n) {
    if (n && n->type == AST_BIN_OP) {
        gen_expr(g, n->left);
        fprintf(g->out, " %s ", n->str);
        gen_expr(g, n->right);
    } else {
        gen_expr(g, n);
    }
}

/* ═══════════════════════════════════════════════════════════════════
   String interpolation  "{nombre}" → printf format + args
   ═══════════════════════════════════════════════════════════════════ */

/* Parse "{varexpr}" tokens from a Z format string.
   varexpr can be "name" or "name.*" */
static void gen_print(CG *g, ASTNode *call) {
    /* Expect exactly one string-literal argument */
    if (call->nchildren != 1 || call->children[0]->type != AST_STR_LIT) {
        /* Fallback: plain printf */
        fputs("printf(", g->out);
        gen_expr(g, call->children[0]);
        fputs(")", g->out);
        return;
    }

    const char *src = call->children[0]->str;
    char fmt[4096]  = "";
    char args[4096] = "";

    /* Check whether there are any interpolation markers */
    int has_interp = (strchr(src, '{') != NULL);
    if (!has_interp) {
        fprintf(g->out, "printf(\"%s\\n\")", src);
        return;
    }

    const char *p = src;
    while (*p) {
        if (*p == '{') {
            p++;
            char expr[256] = "";
            int  elen = 0;
            while (*p && *p != '}') expr[elen++] = *p++;
            if (*p == '}') p++;
            expr[elen] = '\0';

            /* Detect "name.*" (deref) vs plain "name" */
            int is_deref = 0;
            char varname[256];
            char *dot_star = strstr(expr, ".*");
            if (dot_star) {
                int vlen = (int)(dot_star - expr);
                strncpy(varname, expr, (size_t)vlen);
                varname[vlen] = '\0';
                is_deref = 1;
            } else {
                strncpy(varname, expr, sizeof(varname)-1);
            }

            /* Determine type */
            ZType t = cg_lookup_type(g, varname);
            if (is_deref && t.is_ptr) { t.is_ptr = 0; }

            strncat(fmt, ztype_fmt(t), sizeof(fmt)-strlen(fmt)-1);
            if (args[0]) strncat(args, ", ", sizeof(args)-strlen(args)-1);
            if (is_deref) strncat(args, "*",  sizeof(args)-strlen(args)-1);
            strncat(args, varname, sizeof(args)-strlen(args)-1);

        } else if (*p == '%') {
            strncat(fmt, "%%", sizeof(fmt)-strlen(fmt)-1); p++;
        } else if (*p == '"') {
            strncat(fmt, "\\\"", sizeof(fmt)-strlen(fmt)-1); p++;
        } else if (*p == '\\') {
            /* Forward escape sequences */
            char esc[3] = {'\\', p[1], 0};
            strncat(fmt, esc, sizeof(fmt)-strlen(fmt)-1);
            p += 2;
        } else {
            char ch[2] = {*p, 0}; p++;
            strncat(fmt, ch, sizeof(fmt)-strlen(fmt)-1);
        }
    }

    if (args[0])
        fprintf(g->out, "printf(\"%s\\n\", %s)", fmt, args);
    else
        fprintf(g->out, "printf(\"%s\\n\")", fmt);
}

/* ═══════════════════════════════════════════════════════════════════
   Expression generation
   ═══════════════════════════════════════════════════════════════════ */

/* Is this Call node a method call (callee is AST_MEMBER)? */
static int is_method_call(ASTNode *call) {
    return call->left && call->left->type == AST_MEMBER;
}

/* Is this Call node a struct constructor? (callee is Ident matching a struct name) */
static int is_struct_ctor(CG *g, ASTNode *call) {
    if (!call->left || call->left->type != AST_IDENT) return 0;
    ZType t = cg_lookup_type(g, call->left->str);
    return (t.kind == ZTYPE_STRUCT);
}

static void gen_expr(CG *g, ASTNode *n) {
    if (!n) return;

    switch (n->type) {

    case AST_INT_LIT:   fputs(n->str, g->out); break;
    case AST_FLOAT_LIT: fputs(n->str, g->out); break;
    case AST_BOOL_LIT:
        fputs(strcmp(n->str,"true")==0 ? "1" : "0", g->out); break;
    case AST_STR_LIT:
        fprintf(g->out, "\"%s\"", n->str); break;

    case AST_IDENT:
        fputs(n->str, g->out); break;

    case AST_BIN_OP:
        fputc('(', g->out);
        gen_expr(g, n->left);
        fprintf(g->out, " %s ", n->str);
        gen_expr(g, n->right);
        fputc(')', g->out);
        break;

    case AST_UNARY_OP:
        fprintf(g->out, "(%s", n->str);
        gen_expr(g, n->left);
        fputc(')', g->out);
        break;

    case AST_MEMBER: {
        /* Decide between '.' and '->' based on object's pointer flag */
        ZType obj = node_type(g, n->left);
        if (obj.kind == ZTYPE_UNKNOWN && n->left->type == AST_IDENT)
            obj = cg_lookup_type(g, n->left->str);
        gen_expr(g, n->left);
        fputs(obj.is_ptr ? "->" : ".", g->out);
        fputs(n->str, g->out);
        break;
    }

    case AST_DEREF_EXPR:
        fputc('(', g->out);
        fputc('*', g->out);
        gen_expr(g, n->left);
        fputc(')', g->out);
        break;

    case AST_CALL: {
        /* print() — special */
        if (n->left && n->left->type == AST_IDENT &&
            strcmp(n->left->str, "print") == 0) {
            gen_print(g, n);
            break;
        }

        /* alloc(T) → malloc(sizeof(C_type)) */
        if (n->left && n->left->type == AST_IDENT &&
            strcmp(n->left->str, "alloc") == 0) {
            if (n->nchildren == 1 && n->children[0]->type == AST_IDENT) {
                ZType t = ztype_from_str(n->children[0]->str);
                fprintf(g->out, "malloc(sizeof(%s))", ztype_to_c(t));
            } else {
                fputs("malloc(sizeof(int))", g->out);
            }
            break;
        }

        /* free(ptr) → free(ptr) */
        if (n->left && n->left->type == AST_IDENT &&
            strcmp(n->left->str, "free") == 0) {
            fputs("free(", g->out);
            if (n->nchildren > 0) gen_expr(g, n->children[0]);
            fputc(')', g->out);
            break;
        }

        /* Struct constructor: Vector3(x=0.0, y=0.0, z=0.0) */
        if (is_struct_ctor(g, n)) {
            fprintf(g->out, "(%s){", n->left->str);
            for (int i = 0; i < n->nchildren; i++) {
                if (i) fputs(", ", g->out);
                ASTNode *arg = n->children[i];
                /* Named arg: Assign(Ident(x), value) */
                if (arg->type == AST_ASSIGN && arg->left &&
                    arg->left->type == AST_IDENT) {
                    fprintf(g->out, ".%s = ", arg->left->str);
                    gen_expr(g, arg->right);
                } else {
                    gen_expr(g, arg);
                }
            }
            fputc('}', g->out);
            break;
        }

        /* Method call: jugador.mover(args) → StructType_mover(&jugador, args) */
        if (is_method_call(n)) {
            ASTNode *mem = n->left;          /* AST_MEMBER */
            ASTNode *obj = mem->left;        /* the object */
            const char *method = mem->str;   /* method name */

            /* Get struct type of object */
            ZType obj_t = node_type(g, obj);
            if (obj_t.kind == ZTYPE_UNKNOWN && obj->type == AST_IDENT)
                obj_t = cg_lookup_type(g, obj->str);
            ZType base = obj_t; base.is_ptr = 0;

            if (base.kind == ZTYPE_STRUCT) {
                fprintf(g->out, "%s_%s(", base.name, method);
                /* Pass address of object (methods take *self) */
                fputc('&', g->out);
                gen_expr(g, obj);
                for (int i = 0; i < n->nchildren; i++) {
                    fputs(", ", g->out);
                    gen_expr(g, n->children[i]);
                }
                fputc(')', g->out);
            } else {
                /* Fallback: treat as regular call */
                gen_expr(g, n->left);
                fputc('(', g->out);
                for (int i = 0; i < n->nchildren; i++) {
                    if (i) fputs(", ", g->out);
                    gen_expr(g, n->children[i]);
                }
                fputc(')', g->out);
            }
            break;
        }

        /* Regular function call */
        gen_expr(g, n->left);
        fputc('(', g->out);
        for (int i = 0; i < n->nchildren; i++) {
            if (i) fputs(", ", g->out);
            gen_expr(g, n->children[i]);
        }
        fputc(')', g->out);
        break;
    }

    case AST_ASSIGN:
        gen_expr(g, n->left);
        fprintf(g->out, " %s ", n->str);
        gen_expr(g, n->right);
        break;

    case AST_INDEX:
        gen_expr(g, n->left);
        fputs(".data[", g->out);
        gen_expr(g, n->right);
        fputc(']', g->out);
        break;

    case AST_ARRAY_LIT: {
        /* Emit a compound literal with a fat-pointer struct.
           The element type is inferred from the first child. */
        fputs("_zarr_make(", g->out);
        fprintf(g->out, "%d, sizeof(int), (int[]){", n->nchildren);
        for (int i = 0; i < n->nchildren; i++) {
            if (i) fputs(", ", g->out);
            gen_expr(g, n->children[i]);
        }
        fputs("})", g->out);
        break;
    }

    case AST_LIST_COMP: {
        /* [expr for var in iter if cond]
           Generates a helper call that builds the array at runtime.
           For now emit a readable C block expression via GCC statement-expr. */
        /* We generate a local array using a for loop inside a GCC ({ ... }) */
        fprintf(g->out, "({ _ZArr _lc_%d = _zarr_alloc(64, sizeof(int));\n",
                g->lscope ? (int)(size_t)g->lscope : 0);
        /* iter can be a range or an array */
        if (n->right && n->right->type == AST_RANGE) {
            ASTNode *rng = n->right;
            fputs("for (int ", g->out); fputs(n->str, g->out);
            fputs(" = ", g->out); gen_expr(g, rng->left);
            fputs("; ", g->out); fputs(n->str, g->out);
            fputs(rng->flag ? " <= " : " < ", g->out);
            gen_expr(g, rng->right);
            fputs("; ", g->out); fputs(n->str, g->out); fputs("++) {\n", g->out);
        } else {
            fprintf(g->out, "for (int _i = 0; _i < ");
            gen_expr(g, n->right);
            fprintf(g->out, ".len; _i++) { int %s = ((int*)_lc.data)[_i];\n", n->str);
        }
        if (n->body) {
            fputs("if (", g->out);
            gen_cond(g, n->body);
            fputs(") ", g->out);
        }
        fputs("_zarr_push(&_lc, ", g->out);
        gen_expr(g, n->left);
        fputs("); }\n", g->out);
        /* The last expression is _lc itself */
        fputs("_lc; })", g->out);
        break;
    }

    case AST_RANGE:
        /* Ranges as expressions: just used in for-in loops normally.
           If used standalone, emit start value as a placeholder. */
        gen_expr(g, n->left);
        break;

    default:
        fputs("/* ? */", g->out);
        break;
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Statement generation
   ═══════════════════════════════════════════════════════════════════ */

static void gen_stmt(CG *g, ASTNode *n) {
    if (!n) return;
    switch (n->type) {

    case AST_VAR_DECL: {
        ZType t;
        t.kind      = (ZTypeKind)n->ztype;
        t.is_ptr    = n->ztype_is_ptr;
        strncpy(t.name, n->ztype_name, sizeof(t.name)-1);
        if (t.kind == ZTYPE_UNKNOWN) t = ztype_from_str(n->str2);
        if (t.kind == ZTYPE_UNKNOWN) t.kind = ZTYPE_I32;

        lscope_def(g, n->str, t, n->flag);

        ind(g);
        /* let → const, mut → mutable */
        if (!n->flag && t.kind != ZTYPE_STR)
            fputs("const ", g->out);
        fprintf(g->out, "%s %s = ", ztype_to_c(t), n->str);
        gen_expr(g, n->left);
        fputs(";\n", g->out);
        break;
    }

    case AST_EXPR_STMT:
        ind(g); gen_expr(g, n->left); fputs(";\n", g->out); break;

    case AST_ASSIGN:
        ind(g);
        gen_expr(g, n->left);
        fprintf(g->out, " %s ", n->str);
        gen_expr(g, n->right);
        fputs(";\n", g->out);
        break;

    case AST_RETURN_STMT:
        ind(g);
        fputs("return", g->out);
        if (n->left) { fputc(' ', g->out); gen_expr(g, n->left); }
        fputs(";\n", g->out);
        break;

    case AST_IF_STMT:
        ind(g); fputs("if (", g->out);
        gen_cond(g, n->left);
        fputs(") {\n", g->out);
        gen_block(g, n->body);
        ind(g); fputc('}', g->out);
        if (n->right) {
            fputs(" else {\n", g->out);
            gen_block(g, n->right);
            ind(g); fputc('}', g->out);
        }
        fputc('\n', g->out);
        break;

    case AST_WHILE_STMT:
        ind(g); fputs("while (", g->out);
        gen_cond(g, n->left);
        fputs(") {\n", g->out);
        gen_block(g, n->body);
        ind(g); fputs("}\n", g->out);
        break;

    case AST_FOR_STMT: {
        lscope_push(g);
        lscope_def(g, n->str, (ZType){.kind = ZTYPE_I32}, 1);
        ind(g);
        if (n->left && n->left->type == AST_RANGE) {
            /* for x in start..end  or  start..=end */
            ASTNode *rng = n->left;
            fprintf(g->out, "for (int %s = ", n->str);
            gen_expr(g, rng->left);
            fprintf(g->out, "; %s %s ", n->str, rng->flag ? "<=" : "<");
            gen_expr(g, rng->right);
            fprintf(g->out, "; %s++) {\n", n->str);
        } else {
            /* for x in array  →  iterate over .data */
            fprintf(g->out, "for (int _i_%s = 0; _i_%s < ", n->str, n->str);
            gen_expr(g, n->left);
            fprintf(g->out, ".len; _i_%s++) {\n", n->str);
            /* define var as element */
            g->indent++;
            ind(g);
            fprintf(g->out, "int %s = ((int*)(", n->str);
            gen_expr(g, n->left);
            fprintf(g->out, ".data))[_i_%s];\n", n->str);
            g->indent--;
        }
        gen_block(g, n->body);
        lscope_pop(g);
        ind(g); fputs("}\n", g->out);
        break;
    }

    default:
        ind(g); fputs("/* unsupported stmt */\n", g->out); break;
    }
}

static void gen_block(CG *g, ASTNode *block) {
    if (!block) return;
    lscope_push(g);
    g->indent++;
    for (int i = 0; i < block->nchildren; i++) gen_stmt(g, block->children[i]);
    g->indent--;
    lscope_pop(g);
}

/* ═══════════════════════════════════════════════════════════════════
   Function / struct / impl generation
   ═══════════════════════════════════════════════════════════════════ */

static void gen_param_list(CG *g, ASTNode *fn, const char *impl_type) {
    int first = 1;
    /* self param for methods */
    if (impl_type && impl_type[0]) {
        fprintf(g->out, "%s *self", impl_type);
        first = 0;
    }
    for (int i = 0; i < fn->nchildren; i++) {
        ASTNode *p = fn->children[i];
        if (p->type != AST_PARAM) continue;
        if (strcmp(p->str, "self") == 0) continue; /* already emitted */
        if (!first) fputs(", ", g->out);
        ZType t = ztype_from_str(p->str2[0] ? p->str2 : "int");
        fprintf(g->out, "%s %s", ztype_to_c(t), p->str);
        first = 0;
    }
    if (first) fputs("void", g->out);
}

static void gen_func(CG *g, ASTNode *fn, const char *impl_type) {
    /* Determine C return type */
    ZType ret;
    if (fn->str2[0]) ret = ztype_from_str(fn->str2);
    else             ret = (ZType){.kind = ZTYPE_VOID};
    if (strcmp(fn->str,"main")==0 && (!impl_type||!impl_type[0]))
        ret = (ZType){.kind = ZTYPE_I32};

    /* Function signature */
    if (impl_type && impl_type[0])
        fprintf(g->out, "%s %s_%s(", ztype_to_c(ret), impl_type, fn->str);
    else
        fprintf(g->out, "%s %s(", ztype_to_c(ret), fn->str);

    gen_param_list(g, fn, impl_type);
    fputs(")\n{\n", g->out);

    /* Body */
    lscope_push(g);
    /* Register self */
    if (impl_type && impl_type[0]) {
        ZType st = (ZType){.kind = ZTYPE_STRUCT}; st.is_ptr = 1;
        strncpy(st.name, impl_type, sizeof(st.name)-1);
        lscope_def(g, "self", st, 1);
    }
    /* Register params */
    for (int i = 0; i < fn->nchildren; i++) {
        ASTNode *p = fn->children[i];
        if (p->type == AST_PARAM && strcmp(p->str,"self")!=0)
            lscope_def(g, p->str, ztype_from_str(p->str2), p->flag);
    }

    g->indent = 1;
    for (int i = 0; fn->body && i < fn->body->nchildren; i++)
        gen_stmt(g, fn->body->children[i]);

    /* main always returns 0 */
    if (strcmp(fn->str,"main")==0 && (!impl_type||!impl_type[0])) {
        ind(g); fputs("return 0;\n", g->out);
    }

    lscope_pop(g);
    fputs("}\n", g->out);
}

/* Forward declaration for a function */
static void gen_forward_decl(FILE *out, ASTNode *fn, const char *impl_type) {
    ZType ret;
    if (fn->str2[0]) ret = ztype_from_str(fn->str2);
    else             ret = (ZType){.kind = ZTYPE_VOID};
    if (strcmp(fn->str,"main")==0 && (!impl_type||!impl_type[0]))
        ret = (ZType){.kind = ZTYPE_I32};

    if (impl_type && impl_type[0])
        fprintf(out, "%s %s_%s(", ztype_to_c(ret), impl_type, fn->str);
    else
        fprintf(out, "%s %s(", ztype_to_c(ret), fn->str);

    /* Parameters inline (simplified) */
    int first = 1;
    if (impl_type && impl_type[0]) {
        fprintf(out, "%s *self", impl_type);
        first = 0;
    }
    for (int i = 0; i < fn->nchildren; i++) {
        ASTNode *p = fn->children[i];
        if (p->type != AST_PARAM || strcmp(p->str,"self")==0) continue;
        if (!first) fputs(", ", out);
        ZType t = ztype_from_str(p->str2[0] ? p->str2 : "int");
        fprintf(out, "%s %s", ztype_to_c(t), p->str);
        first = 0;
    }
    if (first) fputs("void", out);
    fputs(");\n", out);
}

/* ═══════════════════════════════════════════════════════════════════
   Top-level code generation
   ═══════════════════════════════════════════════════════════════════ */

int codegen(ASTNode *prog, Scope *global_scope,
            StructDef *structs, FILE *out) {
    CG g; memset(&g, 0, sizeof(g));
    g.out     = out;
    g.gscope  = global_scope;
    g.structs = structs;

    /* ── Preamble ── */
    fputs("/* Generated by zc — Z Compiler  (Phase 4 transpiler) */\n", out);
    fputs("#include <stdio.h>\n"
          "#include <stdlib.h>\n"
          "#include <string.h>\n\n", out);

    fputs("/* Z type aliases */\n"
          "typedef int    zi32;\n"
          "typedef double zf64;\n"
          "typedef char * zstr;\n"
          "typedef int    zbool;\n"
          "#define ztrue  1\n"
          "#define zfalse 0\n\n", out);

    /* Dynamic array runtime */
    fputs("/* Z dynamic array (slice) */\n"
          "typedef struct { void *data; int len; int cap; } _ZArr;\n"
          "static _ZArr _zarr_alloc(int cap, int esz) {\n"
          "    _ZArr a; a.data = malloc((size_t)(cap*esz)); a.len=0; a.cap=cap; return a;\n"
          "}\n"
          "static void _zarr_push(_ZArr *a, int v) {\n"
          "    if (a->len >= a->cap) {\n"
          "        a->cap *= 2;\n"
          "        a->data = realloc(a->data, (size_t)(a->cap * (int)sizeof(int)));\n"
          "    }\n"
          "    ((int*)a->data)[a->len++] = v;\n"
          "}\n"
          "static _ZArr _zarr_make(int n, int esz, void *src) {\n"
          "    _ZArr a; a.data = malloc((size_t)(n*esz)); a.len=n; a.cap=n;\n"
          "    memcpy(a.data, src, (size_t)(n*esz)); return a;\n"
          "}\n\n", out);

    /* ── Struct definitions ── */
    if (structs) {
        fputs("/* Struct definitions */\n", out);
        for (StructDef *sd = structs; sd; sd = sd->next) {
            fprintf(out, "typedef struct {\n");
            for (StructField *f = sd->fields; f; f = f->next)
                fprintf(out, "    %s %s;\n", ztype_to_c(f->type), f->name);
            fprintf(out, "} %s;\n\n", sd->name);
        }
    }

    /* ── Forward declarations ── */
    fputs("/* Forward declarations */\n", out);
    for (int i = 0; i < prog->nchildren; i++) {
        ASTNode *n = prog->children[i];
        if (n->type == AST_FUNC_DEF)
            gen_forward_decl(out, n, NULL);
        else if (n->type == AST_IMPL_DEF)
            for (int j = 0; j < n->nchildren; j++)
                if (n->children[j]->type == AST_FUNC_DEF)
                    gen_forward_decl(out, n->children[j], n->str);
    }
    fputc('\n', out);

    /* ── Function implementations ── */
    for (int i = 0; i < prog->nchildren; i++) {
        ASTNode *n = prog->children[i];
        if (n->type == AST_FUNC_DEF) {
            gen_func(&g, n, NULL);
            fputc('\n', out);
        } else if (n->type == AST_IMPL_DEF) {
            strncpy(g.impl_type, n->str, sizeof(g.impl_type)-1);
            for (int j = 0; j < n->nchildren; j++) {
                if (n->children[j]->type == AST_FUNC_DEF) {
                    gen_func(&g, n->children[j], n->str);
                    fputc('\n', out);
                }
            }
            g.impl_type[0] = '\0';
        }
    }
    return 0;
}
