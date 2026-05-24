/* llvmgen.c — Z Compiler LLVM IR backend
   Strategy: alloca-based SSA (every local variable gets an alloca slot).
   LLVM's mem2reg + -O3 promotes them to registers and applies full
   optimization: vectorization, inlining, loop unrolling, etc.

   Pipeline: Z source → this file → <tmpdir>/_zc_PID.ll → clang -O3 → binary
*/
#include "llvmgen.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ═══════════════════════════════════════════════════════════════════
   Generator context
   ═══════════════════════════════════════════════════════════════════ */

#define MAX_VARS    1024
#define MAX_STRINGS 512
#define MAX_STRUCTS 64

typedef struct {
    char    name[128];
    int     slot;       /* SSA id of the alloca (we use %slot.addr) */
    ZType   type;
} LLVar;

typedef struct {
    char *text;         /* original Z string value */
    int   id;           /* global constant index: @.str.N */
    int   byte_len;     /* length including \0 */
} LLStr;

typedef struct LLCG {
    FILE      *out;
    int        tmp;        /* next SSA value id: %0, %1, ... */
    int        lbl;        /* next label id */

    /* Local variable table (cleared per function) */
    LLVar      vars[MAX_VARS];
    int        n_vars;

    /* Global string table (accumulated across all functions) */
    LLStr      strtab[MAX_STRINGS];
    int        n_strtab;

    /* Struct info (for field indices) */
    StructDef *structs;
    Scope     *gscope;
    char       impl_type[64];

    /* Per-function: return type string, "ret_block" label */
    char       ret_type[64];
    int        in_func;

    /* Whether current basic block already has a terminator */
    int        bb_ended;
} LLCG;

/* ── SSA counter helpers ─────────────────────────────────────────── */
static int ll_tmp(LLCG *g)  { return g->tmp++; }
static int ll_lbl(LLCG *g)  { return g->lbl++; }

/* ── Type mapping ────────────────────────────────────────────────── */
static const char *ll_type(ZType t) {
    static char buf[128];
    if (t.kind == ZTYPE_STR)    { snprintf(buf,sizeof(buf),"i8*");  return buf; }
    if (t.kind == ZTYPE_VOID)   { snprintf(buf,sizeof(buf),"void"); return buf; }
    if (t.kind == ZTYPE_STRUCT) {
        if (t.is_ptr) snprintf(buf,sizeof(buf),"%%z_%s*", t.name);
        else          snprintf(buf,sizeof(buf),"%%z_%s",  t.name);
        return buf;
    }
    const char *base;
    switch (t.kind) {
        case ZTYPE_I32:  base="i32";    break;
        case ZTYPE_F64:  base="double"; break;
        case ZTYPE_BOOL: base="i1";     break;
        default:         base="i32";    break;
    }
    if (t.is_ptr) snprintf(buf,sizeof(buf),"%s*", base);
    else          snprintf(buf,sizeof(buf),"%s",   base);
    return buf;
}

/* Type string for alloca (void → i32 fallback) */
static const char *ll_type_alloca(ZType t) {
    if (t.kind == ZTYPE_VOID) {
        ZType i32 = {.kind = ZTYPE_I32};
        return ll_type(i32);
    }
    return ll_type(t);
}

/* Emit GEP to a struct field, handling both direct (%p<slot> is %z_S*)
   and pointer-to-struct (%p<slot> is %z_S** — e.g. 'self' in methods).
   Returns the register number of the i8* / field-type* pointer to the field. */
static int ll_gep_field(LLCG *g, const char *sname, int slot,
                         int obj_is_ptr, int fidx)
{
    int base_r = -1;
    if (obj_is_ptr) {
        /* %p<slot> holds %z_S* — need to load it first */
        base_r = ll_tmp(g);
        fprintf(g->out, "  %%r%d = load %%z_%s*, %%z_%s** %%p%d\n",
                base_r, sname, sname, slot);
    }
    int gep = ll_tmp(g);
    if (base_r >= 0)
        fprintf(g->out,
            "  %%r%d = getelementptr inbounds %%z_%s, %%z_%s* %%r%d, i32 0, i32 %d\n",
            gep, sname, sname, base_r, fidx);
    else
        fprintf(g->out,
            "  %%r%d = getelementptr inbounds %%z_%s, %%z_%s* %%p%d, i32 0, i32 %d\n",
            gep, sname, sname, slot, fidx);
    return gep;
}

/* LLVM type for a struct field list */
static void ll_emit_struct_type(LLCG *g, StructDef *sd) {
    fprintf(g->out, "%%z_%s = type { ", sd->name);
    int first = 1;
    for (StructField *f = sd->fields; f; f = f->next) {
        if (!first) fputs(", ", g->out);
        fputs(ll_type(f->type), g->out);
        first = 0;
    }
    fputs(" }\n", g->out);
}

/* ── Variable table ─────────────────────────────────────────────── */
static void ll_var_clear(LLCG *g) { g->n_vars = 0; }

static int ll_var_find(LLCG *g, const char *name) {
    for (int i = g->n_vars - 1; i >= 0; i--)
        if (strcmp(g->vars[i].name, name) == 0) return g->vars[i].slot;
    return -1;
}

static ZType ll_var_type(LLCG *g, const char *name) {
    for (int i = g->n_vars - 1; i >= 0; i--)
        if (strcmp(g->vars[i].name, name) == 0) return g->vars[i].type;
    /* fall back to global scope */
    SymEntry *e = scope_lookup(g->gscope, name);
    if (e) return e->type;
    return (ZType){.kind = ZTYPE_UNKNOWN};
}

static void ll_var_def(LLCG *g, const char *name, int slot, ZType type) {
    if (g->n_vars >= MAX_VARS) return;
    strncpy(g->vars[g->n_vars].name, name, 127);
    g->vars[g->n_vars].slot = slot;
    g->vars[g->n_vars].type = type;
    g->n_vars++;
}

/* ── Portable strdup (strdup is POSIX, not C99) ──────────────────── */
static char *ll_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* ── String literal interning ────────────────────────────────────── */
static int ll_str_intern(LLCG *g, const char *s) {
    for (int i = 0; i < g->n_strtab; i++)
        if (strcmp(g->strtab[i].text, s) == 0) return g->strtab[i].id;
    int id = g->n_strtab;
    g->strtab[id].text = ll_strdup(s);
    /* Byte length: count actual bytes including null */
    int blen = 0;
    for (const char *p = s; ; p++) {
        blen++;
        if (*p == '\0') break;
        /* \n in stored string is already a real newline byte */
    }
    g->strtab[id].byte_len = blen;
    g->n_strtab++;
    return id;
}

/* Escape a raw string for LLVM IR c"..." syntax.
   Real newlines → \0A, nulls → \00, backslash → \\, others verbatim. */
static void ll_escape_str(const char *s, char *buf, int sz) {
    int out = 0;
    for (const char *p = s; *p && out < sz - 5; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n')      { buf[out++]='\\'; buf[out++]='0'; buf[out++]='A'; }
        else if (c == '\t') { buf[out++]='\\'; buf[out++]='0'; buf[out++]='9'; }
        else if (c == '\r') { buf[out++]='\\'; buf[out++]='0'; buf[out++]='D'; }
        else if (c == '"')  { buf[out++]='\\'; buf[out++]='2'; buf[out++]='2'; }
        else if (c == '\\') { buf[out++]='\\'; buf[out++]='5'; buf[out++]='C'; }
        else if (c < 0x20 || c >= 0x80) {
            /* Hex escape */
            static const char *hex = "0123456789ABCDEF";
            buf[out++]='\\'; buf[out++]=hex[c>>4]; buf[out++]=hex[c&0xF];
        } else {
            buf[out++] = (char)c;
        }
    }
    buf[out++]='\\'; buf[out++]='0'; buf[out++]='0'; /* null terminator */
    buf[out] = '\0';
}

/* ── Emit string global declarations ─────────────────────────────── */
static void ll_emit_strtab(LLCG *g) {
    char esc[8192];
    for (int i = 0; i < g->n_strtab; i++) {
        ll_escape_str(g->strtab[i].text, esc, sizeof(esc));
        fprintf(g->out,
            "@.str.%d = private unnamed_addr constant [%d x i8] c\"%s\"\n",
            i, g->strtab[i].byte_len, esc);
    }
}

/* ── Forward declarations ─────────────────────────────────────────── */
static int  ll_gen_expr(LLCG *g, ASTNode *n);
static void ll_gen_stmt(LLCG *g, ASTNode *n);
static void ll_gen_block(LLCG *g, ASTNode *block);
static void ll_emit_print_proper(LLCG *g, ASTNode *call);

/* ═══════════════════════════════════════════════════════════════════
   Expression codegen — returns SSA value id
   ═══════════════════════════════════════════════════════════════════ */

static int ll_gen_expr(LLCG *g, ASTNode *n) {
    if (!n) return -1;
    int v = -1;

    switch (n->type) {

    case AST_INT_LIT: {
        v = ll_tmp(g);
        fprintf(g->out, "  %%r%d = add i32 0, %s\n", v, n->str);
        break;
    }

    case AST_FLOAT_LIT: {
        v = ll_tmp(g);
        fprintf(g->out, "  %%r%d = fadd double 0.0, %s\n", v, n->str);
        break;
    }

    case AST_BOOL_LIT: {
        v = ll_tmp(g);
        int bval = (strcmp(n->str,"true")==0) ? 1 : 0;
        fprintf(g->out, "  %%r%d = add i1 0, %d\n", v, bval);
        break;
    }

    case AST_STR_LIT: {
        int sid = ll_str_intern(g, n->str);
        v = ll_tmp(g);
        fprintf(g->out,
            "  %%r%d = getelementptr inbounds [%d x i8], [%d x i8]* @.str.%d, i64 0, i64 0\n",
            v, g->strtab[sid].byte_len, g->strtab[sid].byte_len, sid);
        break;
    }

    case AST_IDENT: {
        int slot = ll_var_find(g, n->str);
        ZType t  = ll_var_type(g, n->str);
        if (slot >= 0 && t.kind != ZTYPE_VOID) {
            const char *ty = ll_type_alloca(t);
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = load %s, %s* %%p%d\n", v, ty, ty, slot);
        } else {
            /* Might be a function name — return a placeholder */
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = add i32 0, 0 ; ident '%s' not found\n", v, n->str);
        }
        break;
    }

    case AST_BIN_OP: {
        int l = ll_gen_expr(g, n->left);
        int r = ll_gen_expr(g, n->right);
        v = ll_tmp(g);
        const char *op = n->str;
        ZType lt = {.kind = ZTYPE_I32};
        if (n->left) { lt.kind = (ZTypeKind)n->left->ztype; lt.is_ptr = n->left->ztype_is_ptr; }
        int is_float = (lt.kind == ZTYPE_F64);
        int is_cmp   = (strcmp(op,"<")==0||strcmp(op,">")==0||strcmp(op,"<=")==0||
                        strcmp(op,">=")==0||strcmp(op,"==")==0||strcmp(op,"!=")==0);
        int is_logic  = (strcmp(op,"&&")==0||strcmp(op,"||")==0);

        if (is_logic) {
            const char *llop = (strcmp(op,"&&")==0) ? "and" : "or";
            fprintf(g->out, "  %%r%d = %s i1 %%r%d, %%r%d\n", v, llop, l, r);
        } else if (is_cmp) {
            const char *pred;
            if (is_float) {
                if      (strcmp(op,"<")==0)  pred="olt";
                else if (strcmp(op,">")==0)  pred="ogt";
                else if (strcmp(op,"<=")==0) pred="ole";
                else if (strcmp(op,">=")==0) pred="oge";
                else if (strcmp(op,"==")==0) pred="oeq";
                else                         pred="one";
                fprintf(g->out, "  %%r%d = fcmp %s double %%r%d, %%r%d\n", v, pred, l, r);
            } else {
                if      (strcmp(op,"<")==0)  pred="slt";
                else if (strcmp(op,">")==0)  pred="sgt";
                else if (strcmp(op,"<=")==0) pred="sle";
                else if (strcmp(op,">=")==0) pred="sge";
                else if (strcmp(op,"==")==0) pred="eq";
                else                         pred="ne";
                fprintf(g->out, "  %%r%d = icmp %s i32 %%r%d, %%r%d\n", v, pred, l, r);
            }
        } else {
            /* Arithmetic */
            const char *llop;
            if (is_float) {
                if      (strcmp(op,"+")==0)  llop="fadd";
                else if (strcmp(op,"-")==0)  llop="fsub";
                else if (strcmp(op,"*")==0)  llop="fmul";
                else                         llop="fdiv";
                fprintf(g->out, "  %%r%d = %s double %%r%d, %%r%d\n", v, llop, l, r);
            } else {
                if      (strcmp(op,"+")==0)  llop="add";
                else if (strcmp(op,"-")==0)  llop="sub";
                else if (strcmp(op,"*")==0)  llop="mul";
                else if (strcmp(op,"/")==0)  llop="sdiv";
                else                         llop="srem";
                fprintf(g->out, "  %%r%d = %s i32 %%r%d, %%r%d\n", v, llop, l, r);
            }
        }
        break;
    }

    case AST_UNARY_OP: {
        int operand = ll_gen_expr(g, n->left);
        v = ll_tmp(g);
        if (strcmp(n->str,"-")==0)
            fprintf(g->out, "  %%r%d = sub i32 0, %%r%d\n", v, operand);
        else /* ! */
            fprintf(g->out, "  %%r%d = xor i1 %%r%d, true\n", v, operand);
        break;
    }

    case AST_CALL: {
        /* print() special case */
        if (n->left && n->left->type == AST_IDENT &&
            strcmp(n->left->str,"print")==0) {
            ll_emit_print_proper(g, n);
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = add i32 0, 0\n", v); /* dummy return */
            break;
        }
        /* alloc(T) → malloc */
        if (n->left && n->left->type == AST_IDENT &&
            strcmp(n->left->str,"alloc")==0) {
            if (n->nchildren == 1 && n->children[0]->type == AST_IDENT) {
                ZType t = ztype_from_str(n->children[0]->str);
                int sz = ll_tmp(g);
                fprintf(g->out, "  %%r%d = call i8* @malloc(i64 %s)\n",
                    sz, (t.kind == ZTYPE_F64) ? "8" : "4");
                v = sz;
            } else {
                v = ll_tmp(g);
                fprintf(g->out, "  %%r%d = call i8* @malloc(i64 4)\n", v);
            }
            break;
        }
        /* free(ptr) */
        if (n->left && n->left->type == AST_IDENT &&
            strcmp(n->left->str,"free")==0) {
            int arg = (n->nchildren > 0) ? ll_gen_expr(g, n->children[0]) : -1;
            if (arg >= 0)
                fprintf(g->out, "  call void @free(i8* %%r%d)\n", arg);
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = add i32 0, 0\n", v);
            break;
        }
        /* Method call: obj.method(args) */
        if (n->left && n->left->type == AST_MEMBER) {
            ASTNode *mem = n->left;
            ASTNode *obj = mem->left;
            ZType obj_t = ll_var_type(g, obj ? obj->str : "");
            ZType base = obj_t; base.is_ptr = 0;
            if (base.kind == ZTYPE_STRUCT) {
                int slot = ll_var_find(g, obj ? obj->str : "");
                char mn[1152];  /* struct_name(64) + '_' + method(MAX_TOKEN_LEN) */
                snprintf(mn, sizeof(mn), "%s_%s", base.name, mem->str);
                SymEntry *ret_e = scope_lookup(g->gscope, mn);
                ZType ret_t = ret_e ? ret_e->type : (ZType){.kind=ZTYPE_VOID};
                /* Pre-evaluate all args BEFORE emitting the call instruction */
                int margs[64]; ZType margtypes[64]; int mnargs = 0;
                for (int i = 0; i < n->nchildren && mnargs < 64; i++) {
                    margs[mnargs] = ll_gen_expr(g, n->children[i]);
                    margtypes[mnargs].kind   = (ZTypeKind)n->children[i]->ztype;
                    margtypes[mnargs].is_ptr = n->children[i]->ztype_is_ptr;
                    mnargs++;
                }
                v = ll_tmp(g);
                if (ret_t.kind == ZTYPE_VOID)
                    fprintf(g->out, "  call void @%s(%%z_%s* %%p%d",
                        mn, base.name, slot);
                else
                    fprintf(g->out, "  %%r%d = call %s @%s(%%z_%s* %%p%d",
                        v, ll_type(ret_t), mn, base.name, slot);
                for (int i = 0; i < mnargs; i++)
                    fprintf(g->out, ", %s %%r%d", ll_type(margtypes[i]), margs[i]);
                fputs(")\n", g->out);
                break;
            }
        }
        /* Struct construction: StructName(field=val, ...) */
        if (n->left && n->left->type == AST_IDENT) {
            const char *sname = n->left->str;
            StructDef *sd = NULL;
            for (StructDef *s = g->structs; s; s = s->next)
                if (strcmp(s->name, sname)==0) { sd = s; break; }
            if (sd && n->nchildren > 0 &&
                n->children[0]->type == AST_ASSIGN) {
                /* Alloca a slot for the struct */
                int sslot = ll_tmp(g);
                fprintf(g->out, "  %%r%d = alloca %%z_%s\n", sslot, sname);
                /* For each field initializer: evaluate and GEP-store */
                int fidx_arr[64]; int fval_arr[64]; int fcount = 0;
                for (int i = 0; i < n->nchildren && fcount < 64; i++) {
                    ASTNode *ai = n->children[i];
                    if (ai->type != AST_ASSIGN) continue;
                    const char *fname_f = ai->left ? ai->left->str : "";
                    /* Find field index */
                    int fidx = 0, fcur = 0;
                    for (StructField *f = sd->fields; f; f = f->next, fcur++)
                        if (strcmp(f->name, fname_f)==0) { fidx = fcur; break; }
                    int fval = ll_gen_expr(g, ai->right);
                    fidx_arr[fcount] = fidx;
                    fval_arr[fcount] = fval;
                    fcount++;
                }
                /* Find field types for GEP stores */
                for (int i = 0; i < fcount; i++) {
                    int fidx = fidx_arr[i];
                    ZType ft = {.kind = ZTYPE_I32};
                    int fcur = 0;
                    for (StructField *f = sd->fields; f; f = f->next, fcur++)
                        if (fcur == fidx) { ft = f->type; break; }
                    int gep = ll_tmp(g);
                    fprintf(g->out,
                        "  %%r%d = getelementptr inbounds %%z_%s, %%z_%s* %%r%d, i32 0, i32 %d\n",
                        gep, sname, sname, sslot, fidx);
                    fprintf(g->out, "  store %s %%r%d, %s* %%r%d\n",
                        ll_type(ft), fval_arr[i], ll_type(ft), gep);
                }
                /* Load and return the whole struct */
                v = ll_tmp(g);
                fprintf(g->out, "  %%r%d = load %%z_%s, %%z_%s* %%r%d\n",
                    v, sname, sname, sslot);
                break;
            }
        }
        /* Regular function call */
        if (n->left && n->left->type == AST_IDENT) {
            const char *fname = n->left->str;
            SymEntry *fe = scope_lookup(g->gscope, fname);
            ZType ret_t = fe ? fe->type : (ZType){.kind=ZTYPE_I32};

            /* Pre-evaluate all args */
            int args[64]; ZType argtypes[64]; int nargs = 0;
            for (int i = 0; i < n->nchildren && nargs < 64; i++) {
                args[nargs] = ll_gen_expr(g, n->children[i]);
                argtypes[nargs].kind   = (ZTypeKind)n->children[i]->ztype;
                argtypes[nargs].is_ptr = n->children[i]->ztype_is_ptr;
                nargs++;
            }

            v = ll_tmp(g);
            if (ret_t.kind == ZTYPE_VOID)
                fprintf(g->out, "  call void @%s(", fname);
            else
                fprintf(g->out, "  %%r%d = call %s @%s(", v, ll_type(ret_t), fname);
            for (int i = 0; i < nargs; i++) {
                if (i) fputs(", ", g->out);
                fprintf(g->out, "%s %%r%d", ll_type_alloca(argtypes[i]), args[i]);
            }
            fputs(")\n", g->out);
            if (ret_t.kind == ZTYPE_VOID) {
                fprintf(g->out, "  %%r%d = add i32 0, 0\n", v); /* dummy */
            }
        } else {
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = add i32 0, 0 ; unsupported call\n", v);
        }
        break;
    }

    case AST_MEMBER: {
        /* obj.field — use GEP then load */
        ZType obj_t = ll_var_type(g, n->left ? n->left->str : "");
        ZType base = obj_t; base.is_ptr = 0;
        if (base.kind == ZTYPE_STRUCT) {
            StructDef *sd = NULL;
            for (StructDef *s = g->structs; s; s = s->next)
                if (strcmp(s->name, base.name)==0) { sd = s; break; }
            int fidx = 0;
            ZType ft = {.kind = ZTYPE_I32};
            if (sd) {
                int idx = 0;
                for (StructField *f = sd->fields; f; f = f->next, idx++) {
                    if (strcmp(f->name, n->str)==0) {
                        fidx = idx; ft = f->type; break;
                    }
                }
            }
            int slot = ll_var_find(g, n->left ? n->left->str : "");
            /* obj_t.is_ptr == 1 means %p<slot> stores %z_S* (e.g. 'self') */
            int gep = ll_gep_field(g, base.name, slot, obj_t.is_ptr, fidx);
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = load %s, %s* %%r%d\n",
                v, ll_type(ft), ll_type(ft), gep);
        } else {
            v = ll_tmp(g);
            fprintf(g->out, "  %%r%d = add i32 0, 0 ; member access\n", v);
        }
        break;
    }

    case AST_DEREF_EXPR: {
        int ptr = ll_gen_expr(g, n->left);
        v = ll_tmp(g);
        fprintf(g->out, "  %%r%d = load i32, i32* %%r%d\n", v, ptr);
        break;
    }

    case AST_ASSIGN: {
        /* assignment returns the stored value */
        int rval = ll_gen_expr(g, n->right);
        /* Determine where to store */
        if (n->left && n->left->type == AST_IDENT) {
            const char *name = n->left->str;
            int slot = ll_var_find(g, name);
            ZType t  = ll_var_type(g, name);
            if (slot >= 0) {
                const char *op = n->str;
                if (strcmp(op,"=") != 0) {
                    /* compound assign: load, op, store */
                    int old = ll_tmp(g);
                    fprintf(g->out, "  %%r%d = load %s, %s* %%p%d\n",
                        old, ll_type_alloca(t), ll_type_alloca(t), slot);
                    int result = ll_tmp(g);
                    const char *llop =
                        (strcmp(op,"+=")==0) ? "add"  :
                        (strcmp(op,"-=")==0) ? "sub"  :
                        (strcmp(op,"*=")==0) ? "mul"  :
                        (strcmp(op,"/=")==0) ? "sdiv" : "srem";
                    fprintf(g->out, "  %%r%d = %s %s %%r%d, %%r%d\n",
                        result, llop, ll_type_alloca(t), old, rval);
                    rval = result;
                }
                fprintf(g->out, "  store %s %%r%d, %s* %%p%d\n",
                    ll_type_alloca(t), rval, ll_type_alloca(t), slot);
            }
        } else if (n->left && n->left->type == AST_DEREF_EXPR) {
            /* ptr.* = val */
            int ptr = ll_gen_expr(g, n->left->left);
            fprintf(g->out, "  store i32 %%r%d, i32* %%r%d\n", rval, ptr);
        } else if (n->left && n->left->type == AST_MEMBER) {
            /* obj.field op= val — GEP + optional read-modify-write + store */
            ASTNode *mem = n->left;
            ZType obj_t = ll_var_type(g, mem->left ? mem->left->str : "");
            ZType base = obj_t; base.is_ptr = 0;
            if (base.kind == ZTYPE_STRUCT) {
                StructDef *sd = NULL;
                for (StructDef *s = g->structs; s; s = s->next)
                    if (strcmp(s->name, base.name)==0) { sd=s; break; }
                int fidx = 0;
                ZType ft = {.kind = ZTYPE_I32};
                if (sd) {
                    int idx = 0;
                    for (StructField *f = sd->fields; f; f=f->next, idx++) {
                        if (strcmp(f->name, mem->str)==0) { fidx=idx; ft=f->type; break; }
                    }
                }
                int slot = ll_var_find(g, mem->left ? mem->left->str : "");
                /* obj_t.is_ptr: self is stored as ptr-to-ptr */
                int gep = ll_gep_field(g, base.name, slot, obj_t.is_ptr, fidx);
                /* Compound assign: load current field value, apply op, then store */
                const char *op = n->str;
                if (strcmp(op, "=") != 0) {
                    int old = ll_tmp(g);
                    fprintf(g->out, "  %%r%d = load %s, %s* %%r%d\n",
                        old, ll_type(ft), ll_type(ft), gep);
                    int result = ll_tmp(g);
                    int is_float = (ft.kind == ZTYPE_F64);
                    const char *llop =
                        (strcmp(op,"+=")==0) ? (is_float?"fadd":"add")  :
                        (strcmp(op,"-=")==0) ? (is_float?"fsub":"sub")  :
                        (strcmp(op,"*=")==0) ? (is_float?"fmul":"mul")  :
                        (strcmp(op,"/=")==0) ? (is_float?"fdiv":"sdiv") : (is_float?"frem":"srem");
                    fprintf(g->out, "  %%r%d = %s %s %%r%d, %%r%d\n",
                        result, llop, ll_type(ft), old, rval);
                    rval = result;
                }
                fprintf(g->out, "  store %s %%r%d, %s* %%r%d\n",
                    ll_type(ft), rval, ll_type(ft), gep);
            }
        }
        v = rval;
        break;
    }

    default:
        v = ll_tmp(g);
        fprintf(g->out, "  %%r%d = add i32 0, 0 ; unhandled expr %d\n", v, n->type);
        break;
    }

    return v;
}

/* ── Proper printf emission with pre-loaded args ────────────────── */
static void ll_emit_print_proper(LLCG *g, ASTNode *call) {
    if (call->nchildren != 1 || call->children[0]->type != AST_STR_LIT) return;
    const char *src = call->children[0]->str;

    char fmt[4096] = "";
    char argnames[32][256];
    int  argderef[32];
    ZType argtypes[32];
    int  nargs = 0;

    int has_interp = (strchr(src, '{') != NULL);
    if (!has_interp) {
        char combined[4200];
        snprintf(combined, sizeof(combined), "%s\n", src);
        int sid = ll_str_intern(g, combined);
        int ptr = ll_tmp(g);
        fprintf(g->out,
            "  %%r%d = getelementptr inbounds [%d x i8], [%d x i8]* @.str.%d, i64 0, i64 0\n",
            ptr, g->strtab[sid].byte_len, g->strtab[sid].byte_len, sid);
        int r = ll_tmp(g);
        fprintf(g->out, "  %%r%d = call i32 (i8*, ...) @printf(i8* %%r%d)\n", r, ptr);
        return;
    }

    /* argkind: 0=simple var, 1=deref ptr.*, 2=member obj.field */
    int  argkind[32];
    char argobj[32][256];    /* for kind=2: the object name */
    char argfield[32][256];  /* for kind=2: the field name  */

    const char *p = src;
    while (*p) {
        if (*p == '{') {
            p++;
            char expr[256] = ""; int elen = 0;
            while (*p && *p != '}') expr[elen++] = *p++;
            if (*p == '}') p++;
            expr[elen] = '\0';

            ZType t; int kind = 0;
            char *dot_star = strstr(expr, ".*");
            char *dot      = strchr(expr, '.');

            if (dot_star) {
                /* ptr.* — dereference */
                int vlen = (int)(dot_star - expr);
                char varname[256]; strncpy(varname, expr, (size_t)vlen); varname[vlen]='\0';
                t = ll_var_type(g, varname);
                if (t.is_ptr) t.is_ptr = 0;
                kind = 1;
                strncpy(argnames[nargs], varname, 255);
            } else if (dot) {
                /* obj.field — member access */
                int olen = (int)(dot - expr);
                char obj_name[256]; strncpy(obj_name, expr, (size_t)olen); obj_name[olen]='\0';
                const char *field_name = dot + 1;
                ZType obj_t = ll_var_type(g, obj_name);
                ZType base = obj_t; base.is_ptr = 0;
                t.kind = ZTYPE_I32; t.is_ptr = 0; t.name[0] = '\0';
                if (base.kind == ZTYPE_STRUCT) {
                    for (StructDef *sd = g->structs; sd; sd = sd->next) {
                        if (strcmp(sd->name, base.name)==0) {
                            for (StructField *f = sd->fields; f; f=f->next)
                                if (strcmp(f->name, field_name)==0) { t = f->type; break; }
                            break;
                        }
                    }
                }
                kind = 2;
                strncpy(argobj[nargs],   obj_name,   255);
                strncpy(argfield[nargs], field_name, 255);
                strncpy(argnames[nargs], expr,       255);
            } else {
                /* simple variable */
                t = ll_var_type(g, expr);
                kind = 0;
                strncpy(argnames[nargs], expr, 255);
            }
            strncat(fmt, ztype_fmt(t), sizeof(fmt)-strlen(fmt)-1);
            argderef[nargs] = (kind == 1);
            argkind[nargs]  = kind;
            argtypes[nargs] = t;
            nargs++;
        } else if (*p == '%') {
            strncat(fmt, "%%", sizeof(fmt)-strlen(fmt)-1); p++;
        } else {
            char ch[2] = {*p, 0}; p++;
            strncat(fmt, ch, sizeof(fmt)-strlen(fmt)-1);
        }
    }
    strncat(fmt, "\n", sizeof(fmt)-strlen(fmt)-1);

    int sid = ll_str_intern(g, fmt);

    /* Pre-load all arg values */
    int arg_ids[32];
    for (int i = 0; i < nargs; i++) {
        ZType t = argtypes[i];
        if (argkind[i] == 2) {
            /* member access: GEP + load */
            ZType obj_t = ll_var_type(g, argobj[i]);
            ZType base = obj_t; base.is_ptr = 0;
            int slot = ll_var_find(g, argobj[i]);
            int fidx = 0;
            if (base.kind == ZTYPE_STRUCT) {
                for (StructDef *sd = g->structs; sd; sd = sd->next) {
                    if (strcmp(sd->name, base.name)==0) {
                        int idx = 0;
                        for (StructField *f = sd->fields; f; f=f->next, idx++)
                            if (strcmp(f->name, argfield[i])==0) { fidx=idx; break; }
                        break;
                    }
                }
            }
            int gep = ll_gep_field(g, base.name, slot, obj_t.is_ptr, fidx);
            int vr = ll_tmp(g);
            fprintf(g->out, "  %%r%d = load %s, %s* %%r%d\n",
                vr, ll_type(t), ll_type(t), gep);
            arg_ids[i] = vr;
        } else {
            int slot = ll_var_find(g, argnames[i]);
            if (slot >= 0) {
                int vr = ll_tmp(g);
                fprintf(g->out, "  %%r%d = load %s, %s* %%p%d\n",
                    vr, ll_type_alloca(t), ll_type_alloca(t), slot);
                arg_ids[i] = vr;
            } else {
                int vr = ll_tmp(g);
                fprintf(g->out, "  %%r%d = add i32 0, 0\n", vr);
                arg_ids[i] = vr;
            }
        }
    }

    int fptr = ll_tmp(g);
    fprintf(g->out,
        "  %%r%d = getelementptr inbounds [%d x i8], [%d x i8]* @.str.%d, i64 0, i64 0\n",
        fptr, g->strtab[sid].byte_len, g->strtab[sid].byte_len, sid);
    int r = ll_tmp(g);
    fprintf(g->out, "  %%r%d = call i32 (i8*, ...) @printf(i8* %%r%d", r, fptr);
    for (int i = 0; i < nargs; i++) {
        ZType t = argtypes[i];
        if (argderef[i] && t.is_ptr) t.is_ptr = 0;
        /* Extend i1 to i32 for printf */
        if (t.kind == ZTYPE_BOOL) {
            int ext = ll_tmp(g);
            int r2  = ll_tmp(g);
            fprintf(g->out, ")\n  %%r%d = zext i1 %%r%d to i32\n"
                            "  %%r%d = call i32 (i8*, ...) @printf(i8* %%r%d",
                ext, arg_ids[i], r2, fptr);
            arg_ids[i] = ext;
            r = r2;
        }
        fprintf(g->out, ", %s %%r%d",
            (t.kind == ZTYPE_F64) ? "double" :
            (t.kind == ZTYPE_STR) ? "i8*"    : "i32",
            arg_ids[i]);
    }
    fputs(")\n", g->out);
}

/* ═══════════════════════════════════════════════════════════════════
   Statement codegen
   ═══════════════════════════════════════════════════════════════════ */

static void ll_gen_stmt(LLCG *g, ASTNode *n) {
    if (!n || g->bb_ended) return;

    switch (n->type) {

    case AST_VAR_DECL: {
        /* Determine type */
        ZType t;
        t.kind      = (ZTypeKind)n->ztype;
        t.is_ptr    = n->ztype_is_ptr;
        strncpy(t.name, n->ztype_name, sizeof(t.name)-1);
        if (t.kind == ZTYPE_UNKNOWN) t = ztype_from_str(n->str2);
        if (t.kind == ZTYPE_UNKNOWN) t.kind = ZTYPE_I32;

        /* Alloca */
        int slot = ll_tmp(g);
        fprintf(g->out, "  %%p%d = alloca %s\n", slot, ll_type_alloca(t));

        /* Initializer */
        if (n->left) {
            int val = ll_gen_expr(g, n->left);
            fprintf(g->out, "  store %s %%r%d, %s* %%p%d\n",
                ll_type_alloca(t), val, ll_type_alloca(t), slot);
        }

        ll_var_def(g, n->str, slot, t);
        break;
    }

    case AST_EXPR_STMT:
        ll_gen_expr(g, n->left);
        break;

    case AST_ASSIGN: {
        ll_gen_expr(g, n);  /* handled in gen_expr */
        break;
    }

    case AST_RETURN_STMT: {
        if (n->left) {
            int v = ll_gen_expr(g, n->left);
            fprintf(g->out, "  ret %s %%r%d\n", g->ret_type, v);
        } else {
            fputs("  ret void\n", g->out);
        }
        g->bb_ended = 1;
        break;
    }

    case AST_IF_STMT: {
        int then_l = ll_lbl(g);
        int else_l = ll_lbl(g);
        int end_l  = ll_lbl(g);

        /* Condition */
        int cond = ll_gen_expr(g, n->left);
        /* Ensure i1 */
        if (n->left && n->left->ztype != ZTYPE_BOOL) {
            int c2 = ll_tmp(g);
            fprintf(g->out, "  %%r%d = icmp ne i32 %%r%d, 0\n", c2, cond);
            cond = c2;
        }
        fprintf(g->out, "  br i1 %%r%d, label %%L%d, label %%L%d\n",
            cond, then_l, n->right ? else_l : end_l);
        g->bb_ended = 0;

        /* Then block */
        fprintf(g->out, "L%d:\n", then_l);
        ll_gen_block(g, n->body);
        if (!g->bb_ended)
            fprintf(g->out, "  br label %%L%d\n", end_l);
        g->bb_ended = 0;

        /* Else block */
        if (n->right) {
            fprintf(g->out, "L%d:\n", else_l);
            ll_gen_block(g, n->right);
            if (!g->bb_ended)
                fprintf(g->out, "  br label %%L%d\n", end_l);
            g->bb_ended = 0;
        }

        fprintf(g->out, "L%d:\n", end_l);
        break;
    }

    case AST_WHILE_STMT: {
        int cond_l = ll_lbl(g);
        int body_l = ll_lbl(g);
        int end_l  = ll_lbl(g);

        fprintf(g->out, "  br label %%L%d\n", cond_l);
        fprintf(g->out, "L%d:\n", cond_l);

        int cond = ll_gen_expr(g, n->left);
        if (n->left && n->left->ztype != ZTYPE_BOOL) {
            int c2 = ll_tmp(g);
            fprintf(g->out, "  %%r%d = icmp ne i32 %%r%d, 0\n", c2, cond);
            cond = c2;
        }
        fprintf(g->out, "  br i1 %%r%d, label %%L%d, label %%L%d\n",
            cond, body_l, end_l);
        g->bb_ended = 0;

        fprintf(g->out, "L%d:\n", body_l);
        ll_gen_block(g, n->body);
        if (!g->bb_ended)
            fprintf(g->out, "  br label %%L%d\n", cond_l);
        g->bb_ended = 0;

        fprintf(g->out, "L%d:\n", end_l);
        break;
    }

    case AST_FOR_STMT: {
        /* for var in range or array */
        int cond_l = ll_lbl(g);
        int body_l = ll_lbl(g);
        int end_l  = ll_lbl(g);

        /* Allocate loop variable */
        int var_slot = ll_tmp(g);
        fprintf(g->out, "  %%p%d = alloca i32\n", var_slot);
        ll_var_def(g, n->str, var_slot, (ZType){.kind = ZTYPE_I32});

        if (n->left && n->left->type == AST_RANGE) {
            /* for x in start..end */
            ASTNode *rng = n->left;
            int start = ll_gen_expr(g, rng->left);
            fprintf(g->out, "  store i32 %%r%d, i32* %%p%d\n", start, var_slot);
            int end_v = ll_gen_expr(g, rng->right);

            /* end_v needs to be stored too */
            int end_slot = ll_tmp(g);
            fprintf(g->out, "  %%p%d = alloca i32\n", end_slot);
            fprintf(g->out, "  store i32 %%r%d, i32* %%p%d\n", end_v, end_slot);

            fprintf(g->out, "  br label %%L%d\n", cond_l);
            fprintf(g->out, "L%d:\n", cond_l);

            int cur  = ll_tmp(g);
            int lim  = ll_tmp(g);
            int cond = ll_tmp(g);
            fprintf(g->out, "  %%r%d = load i32, i32* %%p%d\n", cur,  var_slot);
            fprintf(g->out, "  %%r%d = load i32, i32* %%p%d\n", lim,  end_slot);
            fprintf(g->out, "  %%r%d = icmp %s i32 %%r%d, %%r%d\n",
                cond, rng->flag ? "sle" : "slt", cur, lim);
            fprintf(g->out, "  br i1 %%r%d, label %%L%d, label %%L%d\n",
                cond, body_l, end_l);
            g->bb_ended = 0;

            fprintf(g->out, "L%d:\n", body_l);
            ll_gen_block(g, n->body);
            if (!g->bb_ended) {
                /* i++ */
                int v1 = ll_tmp(g), v2 = ll_tmp(g);
                fprintf(g->out, "  %%r%d = load i32, i32* %%p%d\n", v1, var_slot);
                fprintf(g->out, "  %%r%d = add i32 %%r%d, 1\n", v2, v1);
                fprintf(g->out, "  store i32 %%r%d, i32* %%p%d\n", v2, var_slot);
                fprintf(g->out, "  br label %%L%d\n", cond_l);
            }
            g->bb_ended = 0;
        } else {
            /* for x in array — simplified: treat as 0..arr.len */
            fprintf(g->out, "  ; for-in array: not fully implemented in LLVM backend\n");
            fprintf(g->out, "  store i32 0, i32* %%p%d\n", var_slot);
            fprintf(g->out, "  br label %%L%d\n", end_l);
        }

        fprintf(g->out, "L%d:\n", end_l);
        break;
    }

    default:
        fprintf(g->out, "  ; unhandled stmt type %d\n", n->type);
        break;
    }
}

static void ll_gen_block(LLCG *g, ASTNode *block) {
    if (!block) return;
    g->bb_ended = 0;
    for (int i = 0; i < block->nchildren; i++) {
        ll_gen_stmt(g, block->children[i]);
        if (g->bb_ended) break;
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Function codegen
   ═══════════════════════════════════════════════════════════════════ */

static void ll_gen_func(LLCG *g, ASTNode *fn, const char *impl_type) {
    /* Return type */
    ZType ret;
    if (fn->str2[0]) ret = ztype_from_str(fn->str2);
    else             ret = (ZType){.kind = ZTYPE_VOID};
    int is_main = (strcmp(fn->str,"main")==0 && (!impl_type||!impl_type[0]));
    if (is_main) ret = (ZType){.kind = ZTYPE_I32};

    strncpy(g->ret_type, ll_type(ret), 63);

    /* Function signature */
    if (impl_type && impl_type[0])
        fprintf(g->out, "define %s @%s_%s(", ll_type(ret), impl_type, fn->str);
    else
        fprintf(g->out, "define %s @%s(", ll_type(ret), fn->str);

    /* Parameters */
    ll_var_clear(g);
    g->tmp = 0; g->lbl = 0; g->bb_ended = 0;

    int first = 1;
    if (impl_type && impl_type[0]) {
        fprintf(g->out, "%%z_%s* %%self.param", impl_type);
        first = 0;
    }
    for (int i = 0; i < fn->nchildren; i++) {
        ASTNode *p = fn->children[i];
        if (p->type != AST_PARAM || strcmp(p->str,"self")==0) continue;
        ZType pt = ztype_from_str(p->str2[0] ? p->str2 : "i32");
        if (!first) fputs(", ", g->out);
        fprintf(g->out, "%s %%%s.param", ll_type_alloca(pt), p->str);
        first = 0;
    }
    if (first && ret.kind == ZTYPE_VOID) fputs("", g->out);
    fputs(") {\nentry:\n", g->out);

    /* Alloca + store for each param */
    if (impl_type && impl_type[0]) {
        int slot = g->tmp++;
        fprintf(g->out, "  %%p%d = alloca %%z_%s*\n", slot, impl_type);
        fprintf(g->out, "  store %%z_%s* %%self.param, %%z_%s** %%p%d\n",
            impl_type, impl_type, slot);
        ZType st = {.kind = ZTYPE_STRUCT, .is_ptr = 1};
        strncpy(st.name, impl_type, 63);
        ll_var_def(g, "self", slot, st);
    }
    for (int i = 0; i < fn->nchildren; i++) {
        ASTNode *p = fn->children[i];
        if (p->type != AST_PARAM || strcmp(p->str,"self")==0) continue;
        ZType pt = ztype_from_str(p->str2[0] ? p->str2 : "i32");
        int slot = g->tmp++;
        fprintf(g->out, "  %%p%d = alloca %s\n", slot, ll_type_alloca(pt));
        fprintf(g->out, "  store %s %%%s.param, %s* %%p%d\n",
            ll_type_alloca(pt), p->str, ll_type_alloca(pt), slot);
        ll_var_def(g, p->str, slot, pt);
    }

    /* Body */
    ll_gen_block(g, fn->body);

    /* Implicit returns */
    if (!g->bb_ended) {
        if (is_main)
            fputs("  ret i32 0\n", g->out);
        else if (ret.kind == ZTYPE_VOID)
            fputs("  ret void\n", g->out);
        else
            fprintf(g->out, "  ret %s 0\n", ll_type(ret));
    }

    fputs("}\n\n", g->out);
}



/* ═══════════════════════════════════════════════════════════════════
   Top-level entry point
   ═══════════════════════════════════════════════════════════════════ */

int llvmgen(ASTNode *prog, Scope *global_scope,
            StructDef *structs, FILE *out) {

    /* We use a two-pass approach:
       Pass 1: generate function bodies into a temp buffer,
               accumulating string literals.
       Pass 2: emit headers (with string table), then the buffer. */

    LLCG g; memset(&g, 0, sizeof(g));
    g.gscope  = global_scope;
    g.structs = structs;

    /* ── Pass 1: generate all function bodies into a temp file ── */
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s_zc_ll_body_%d.ll", zc_tmpdir(), ZC_GETPID());
    FILE *body = fopen(tmp_path, "w");
    if (!body) return 1;
    g.out = body;

    for (int i = 0; i < prog->nchildren; i++) {
        ASTNode *n = prog->children[i];
        if (n->type == AST_FUNC_DEF) {
            ll_gen_func(&g, n, NULL);
        } else if (n->type == AST_IMPL_DEF) {
            strncpy(g.impl_type, n->str, 63);
            for (int j = 0; j < n->nchildren; j++)
                if (n->children[j]->type == AST_FUNC_DEF)
                    ll_gen_func(&g, n->children[j], n->str);
            g.impl_type[0] = '\0';
        }
    }
    fclose(body);

    /* ── Pass 2: write final .ll file ── */
    g.out = out;

    /* Module header */
    /* No target triple: clang will use the host's default triple.
       This makes the IR portable across macOS, Linux, and Windows. */
    fprintf(out, "; Generated by zc LLVM backend\n\n");

    /* External declarations */
    fputs("declare i32 @printf(i8*, ...)\n", out);
    fputs("declare i8* @malloc(i64)\n", out);
    fputs("declare void @free(i8*)\n", out);
    fputs("declare i8* @realloc(i8*, i64)\n\n", out);

    /* Struct type definitions */
    for (StructDef *sd = structs; sd; sd = sd->next)
        ll_emit_struct_type(&g, sd);
    if (structs) fputc('\n', out);

    /* String table */
    ll_emit_strtab(&g);
    if (g.n_strtab) fputc('\n', out);

    /* NOTE: In LLVM IR, 'declare' is only for EXTERNAL functions (printf etc).
       Z-defined functions are all emitted as 'define' in the body section
       and are mutually visible within the module — no forward declares needed. */

    /* Append function bodies */
    FILE *bf = fopen(tmp_path, "r");
    if (bf) {
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), bf)) > 0)
            fwrite(chunk, 1, n, out);
        fclose(bf);
    }
    remove(tmp_path);

    return 0;
}
