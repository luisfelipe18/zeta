#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════
   AST node helpers
   ═══════════════════════════════════════════════════════════════════════ */

ASTNode *ast_new(ASTNodeType type, int line, int col) {
    ASTNode *n = (ASTNode *)calloc(1, sizeof(ASTNode));
    if (!n) { fputs("out of memory\n", stderr); exit(1); }
    n->type = type;
    n->line = line;
    n->col  = col;
    return n;
}

static void ast_push(ASTNode *parent, ASTNode *child) {
    if (parent->nchildren >= parent->cap) {
        int nc = parent->cap ? parent->cap * 2 : 4;
        parent->children = (ASTNode **)realloc(parent->children,
                                                (size_t)nc * sizeof(ASTNode *));
        if (!parent->children) { fputs("out of memory\n", stderr); exit(1); }
        parent->cap = nc;
    }
    parent->children[parent->nchildren++] = child;
}

void ast_free(ASTNode *n) {
    if (!n) return;
    ast_free(n->left);
    ast_free(n->right);
    ast_free(n->body);
    for (int i = 0; i < n->nchildren; i++) ast_free(n->children[i]);
    free(n->children);
    free(n);
}

const char *ast_type_name(ASTNodeType t) {
    switch (t) {
        case AST_PROGRAM:     return "Program";
        case AST_FUNC_DEF:    return "FuncDef";
        case AST_STRUCT_DEF:  return "StructDef";
        case AST_IMPL_DEF:    return "ImplDef";
        case AST_PARAM:       return "Param";
        case AST_FIELD:       return "Field";
        case AST_BLOCK:       return "Block";
        case AST_VAR_DECL:    return "VarDecl";
        case AST_ASSIGN:      return "Assign";
        case AST_IF_STMT:     return "IfStmt";
        case AST_WHILE_STMT:  return "WhileStmt";
        case AST_FOR_STMT:    return "ForStmt";
        case AST_RETURN_STMT: return "ReturnStmt";
        case AST_EXPR_STMT:   return "ExprStmt";
        case AST_BIN_OP:      return "BinOp";
        case AST_UNARY_OP:    return "UnaryOp";
        case AST_CALL:        return "Call";
        case AST_MEMBER:      return "Member";
        case AST_DEREF_EXPR:  return "DerefExpr";
        case AST_INDEX:       return "Index";
        case AST_ARRAY_LIT:   return "ArrayLit";
        case AST_LIST_COMP:   return "ListComp";
        case AST_RANGE:       return "Range";
        case AST_CLOSURE:     return "Closure";
        case AST_MATCH_STMT:  return "MatchStmt";
        case AST_IDENT:       return "Ident";
        case AST_INT_LIT:     return "IntLit";
        case AST_FLOAT_LIT:   return "FloatLit";
        case AST_STR_LIT:     return "StrLit";
        case AST_BOOL_LIT:    return "BoolLit";
        default:              return "Unknown";
    }
}

/* ═══════════════════════════════════════════════════════════════════════
   AST pretty-printer
   ═══════════════════════════════════════════════════════════════════════ */

void ast_print(const ASTNode *n, int d) {
    if (!n) return;
#define IND() do { for(int _i=0;_i<d;_i++) fputs("    ",stdout); } while(0)
    IND();

    switch (n->type) {
    case AST_PROGRAM:
        printf("Program  (%d declarations)\n", n->nchildren);
        for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+1);
        break;

    case AST_FUNC_DEF:
        printf("FuncDef '%s'", n->str);
        if (n->str2[0]) printf(" -> %s", n->str2);
        putchar('\n');
        if (n->nchildren > 0) {
            IND(); printf("  Params:\n");
            for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+2);
        }
        ast_print(n->body, d+1);
        break;

    case AST_STRUCT_DEF:
        printf("StructDef '%s'\n", n->str);
        for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+1);
        break;

    case AST_IMPL_DEF:
        printf("ImplDef '%s'\n", n->str);
        for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+1);
        break;

    case AST_PARAM:
        printf("Param %s'%s'%s%s\n",
               n->flag ? "mut " : "",
               n->str,
               n->str2[0] ? " :" : "",
               n->str2);
        break;

    case AST_FIELD:
        printf("Field '%s' :%s\n", n->str, n->str2);
        break;

    case AST_BLOCK:
        printf("Block  (%d stmts)\n", n->nchildren);
        for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+1);
        break;

    case AST_VAR_DECL:
        printf("VarDecl %s'%s'%s%s\n",
               n->flag ? "mut " : "let ",
               n->str,
               n->str2[0] ? " :" : "",
               n->str2);
        ast_print(n->left, d+1);
        break;

    case AST_ASSIGN:
        printf("Assign  op='%s'\n", n->str);
        ast_print(n->left,  d+1);
        ast_print(n->right, d+1);
        break;

    case AST_IF_STMT:
        printf("IfStmt\n");
        IND(); printf("  Cond:\n");
        ast_print(n->left,  d+2);
        IND(); printf("  Then:\n");
        ast_print(n->body,  d+2);
        if (n->right) {
            IND(); printf("  Else:\n");
            ast_print(n->right, d+2);
        }
        break;

    case AST_WHILE_STMT:
        printf("WhileStmt\n");
        IND(); printf("  Cond:\n");
        ast_print(n->left, d+2);
        IND(); printf("  Body:\n");
        ast_print(n->body, d+2);
        break;

    case AST_FOR_STMT:
        printf("ForStmt  var='%s'\n", n->str);
        IND(); printf("  Iter:\n");
        ast_print(n->left, d+2);
        IND(); printf("  Body:\n");
        ast_print(n->body, d+2);
        break;

    case AST_RETURN_STMT:
        printf("ReturnStmt\n");
        if (n->left) ast_print(n->left, d+1);
        break;

    case AST_EXPR_STMT:
        printf("ExprStmt\n");
        ast_print(n->left, d+1);
        break;

    case AST_BIN_OP:
        printf("BinOp '%s'\n", n->str);
        ast_print(n->left,  d+1);
        ast_print(n->right, d+1);
        break;

    case AST_UNARY_OP:
        printf("UnaryOp '%s'\n", n->str);
        ast_print(n->left, d+1);
        break;

    case AST_CALL:
        printf("Call\n");
        IND(); printf("  Callee:\n");
        ast_print(n->left, d+2);
        if (n->nchildren > 0) {
            IND(); printf("  Args:\n");
            for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+2);
        }
        break;

    case AST_MEMBER:
        printf("Member .%s\n", n->str);
        ast_print(n->left, d+1);
        break;

    case AST_DEREF_EXPR:
        printf("Deref .*\n");
        ast_print(n->left, d+1);
        break;

    case AST_INDEX:
        printf("Index\n");
        ast_print(n->left,  d+1);
        ast_print(n->right, d+1);
        break;

    case AST_ARRAY_LIT:
        printf("ArrayLit (%d elems)\n", n->nchildren);
        for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+1);
        break;

    case AST_LIST_COMP:
        printf("ListComp var='%s'\n", n->str);
        IND(); printf("  Expr:\n");  ast_print(n->left,  d+2);
        IND(); printf("  Iter:\n");  ast_print(n->right, d+2);
        if (n->body) { IND(); printf("  If:\n"); ast_print(n->body, d+2); }
        break;

    case AST_RANGE:
        printf("Range %s\n", n->flag ? "inclusive" : "exclusive");
        ast_print(n->left,  d+1);
        ast_print(n->right, d+1);
        break;

    case AST_CLOSURE:
        printf("Closure\n");
        for (int i = 0; i < n->nchildren; i++) ast_print(n->children[i], d+1);
        if (n->body) ast_print(n->body, d+1);
        break;

    case AST_IDENT:      printf("Ident '%s'\n",  n->str); break;
    case AST_INT_LIT:    printf("IntLit %s\n",   n->str); break;
    case AST_FLOAT_LIT:  printf("FloatLit %s\n", n->str); break;
    case AST_STR_LIT:    printf("StrLit \"%s\"\n", n->str); break;
    case AST_BOOL_LIT:   printf("BoolLit %s\n",  n->str); break;

    default:
        printf("<%s>\n", ast_type_name(n->type));
        break;
    }
#undef IND
}

/* ═══════════════════════════════════════════════════════════════════════
   Parser state helpers
   ═══════════════════════════════════════════════════════════════════════ */

static void p_advance(Parser *p) {
    p->cur = lexer_next(p->lex);
}

static int p_check(const Parser *p, TokenType t) {
    return p->cur.type == t;
}

static int p_match(Parser *p, TokenType t) {
    if (p->cur.type == t) { p_advance(p); return 1; }
    return 0;
}

static int p_expect(Parser *p, TokenType t) {
    if (p->cur.type == t) { p_advance(p); return 1; }
    if (!p->had_error) {
        snprintf(p->err, sizeof(p->err),
                 "[%d:%d] expected %s, got %s '%s'",
                 p->cur.line, p->cur.col,
                 token_type_name(t),
                 token_type_name(p->cur.type),
                 p->cur.value);
        p->had_error = 1;
    }
    return 0;
}

/* Skip zero or more NEWLINE tokens */
static void skip_nl(Parser *p) {
    while (p->cur.type == TOKEN_NEWLINE) p_advance(p);
}

/* Consume NEWLINE, optionally tolerating EOF */
static void consume_nl(Parser *p) {
    if (p->cur.type == TOKEN_NEWLINE) p_advance(p);
    else if (p->cur.type != TOKEN_EOF && p->cur.type != TOKEN_DEDENT) {
        if (!p->had_error) {
            snprintf(p->err, sizeof(p->err),
                     "[%d:%d] expected newline, got %s '%s'",
                     p->cur.line, p->cur.col,
                     token_type_name(p->cur.type), p->cur.value);
            p->had_error = 1;
        }
    }
}

/* ── Type annotation helper ─────────────────────────────────────────── */
/* Writes the type string into buf (size n). Returns 1 on success. */
static int parse_type_str(Parser *p, char *buf, int n) {
    buf[0] = '\0';
    if (p->cur.type == TOKEN_STAR) {
        strncat(buf, "*", (size_t)(n - 1));
        p_advance(p);
    }
    /* Array type: [T] */
    if (p->cur.type == TOKEN_LBRACKET) {
        p_advance(p);
        char inner[MAX_TOKEN_LEN]; inner[0] = '\0';
        parse_type_str(p, inner, MAX_TOKEN_LEN);
        p_match(p, TOKEN_RBRACKET);
        snprintf(buf, (size_t)n, "[%s]", inner);
        return 1;
    }
    if (p->cur.type == TOKEN_IDENT) {
        strncat(buf, p->cur.value, (size_t)(n - (int)strlen(buf) - 1));
        p_advance(p);
        return 1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
   Forward declarations
   ═══════════════════════════════════════════════════════════════════════ */
static ASTNode *parse_expr(Parser *p);
static ASTNode *parse_stmt(Parser *p);
static ASTNode *parse_block(Parser *p);
static ASTNode *parse_func_def(Parser *p);

/* ═══════════════════════════════════════════════════════════════════════
   Expression parsing  (lowest → highest precedence)
   ═══════════════════════════════════════════════════════════════════════

   assign_expr  → or_expr [ ('='|'+='|'-='|'*='|'/=') or_expr ]
   or_expr      → and_expr ( ('||'|'or') and_expr )*
   and_expr     → not_expr ( ('&&'|'and') not_expr )*
   not_expr     → ('!'|'not') not_expr | cmp_expr
   cmp_expr     → add_expr [ ('<'|'>'|'<='|'>='|'=='|'!=') add_expr ]
   add_expr     → mul_expr ( ('+'|'-') mul_expr )*
   mul_expr     → unary ( ('*'|'/') unary )*
   unary        → '-' unary | postfix
   postfix      → primary ( '(' args ')' | '.' IDENT | '.*' )*
   primary      → IDENT | INT_LIT | FLOAT_LIT | STR_LIT | TRUE | FALSE
                | keyword-as-name | '(' expr ')'
   ═══════════════════════════════════════════════════════════════════════ */

static ASTNode *parse_primary(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    ASTNode *n = NULL;

    switch (p->cur.type) {
    case TOKEN_IDENT: {
        n = ast_new(AST_IDENT, ln, col);
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
        break;
    }
    case TOKEN_INT_LIT: {
        n = ast_new(AST_INT_LIT, ln, col);
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
        break;
    }
    case TOKEN_FLOAT_LIT: {
        n = ast_new(AST_FLOAT_LIT, ln, col);
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
        break;
    }
    case TOKEN_STR_LIT: {
        n = ast_new(AST_STR_LIT, ln, col);
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
        break;
    }
    case TOKEN_TRUE:
    case TOKEN_FALSE: {
        n = ast_new(AST_BOOL_LIT, ln, col);
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
        break;
    }
    /* Keywords that appear in expression/call position */
    case TOKEN_PRINT:
    case TOKEN_FREE:
    case TOKEN_ALLOC:
    case TOKEN_SPAWN:
    case TOKEN_AWAIT:
    case TOKEN_NOT: {
        /* treat as identifier-like for call expressions */
        n = ast_new(AST_IDENT, ln, col);
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
        break;
    }
    case TOKEN_LPAREN: {
        p_advance(p);
        n = parse_expr(p);
        p_expect(p, TOKEN_RPAREN);
        break;
    }

    /* Array literal or list comprehension: [expr, ...] or [expr for v in iter if cond] */
    case TOKEN_LBRACKET: {
        p_advance(p);  /* consume '[' */
        if (p_check(p, TOKEN_RBRACKET)) {
            /* empty array [] */
            n = ast_new(AST_ARRAY_LIT, ln, col);
            p_advance(p);
            break;
        }
        ASTNode *first = parse_expr(p);
        if (p->cur.type == TOKEN_FOR) {
            /* list comprehension: [expr for var in iter if cond] */
            p_advance(p);  /* consume 'for' */
            ASTNode *lc = ast_new(AST_LIST_COMP, ln, col);
            lc->left = first;  /* expression to produce */
            if (p->cur.type == TOKEN_IDENT) {
                strncpy(lc->str, p->cur.value, MAX_TOKEN_LEN - 1);
                p_advance(p);
            }
            p_expect(p, TOKEN_IN);
            lc->right = parse_expr(p);  /* iterable */
            if (p->cur.type == TOKEN_IF) {
                p_advance(p);
                lc->body = parse_expr(p);  /* filter condition */
            }
            p_expect(p, TOKEN_RBRACKET);
            n = lc;
        } else {
            /* array literal */
            ASTNode *arr = ast_new(AST_ARRAY_LIT, ln, col);
            ast_push(arr, first);
            while (p_match(p, TOKEN_COMMA)) {
                if (p_check(p, TOKEN_RBRACKET)) break;
                ast_push(arr, parse_expr(p));
            }
            p_expect(p, TOKEN_RBRACKET);
            n = arr;
        }
        break;
    }

    /* Closure: |params| expr  or  |params| -> type: block */
    case TOKEN_PIPE: {
        p_advance(p);  /* consume '|' */
        ASTNode *cl = ast_new(AST_CLOSURE, ln, col);
        while (!p_check(p, TOKEN_PIPE) && !p_check(p, TOKEN_EOF)) {
            int pln = p->cur.line, pcol = p->cur.col;
            ASTNode *param = ast_new(AST_PARAM, pln, pcol);
            if (p->cur.type == TOKEN_MUT) { param->flag = 1; p_advance(p); }
            if (p->cur.type == TOKEN_IDENT) {
                strncpy(param->str, p->cur.value, MAX_TOKEN_LEN - 1);
                p_advance(p);
            }
            if (p_match(p, TOKEN_COLON))
                parse_type_str(p, param->str2, MAX_TOKEN_LEN);
            ast_push(cl, param);
            if (!p_match(p, TOKEN_COMMA)) break;
        }
        p_expect(p, TOKEN_PIPE);
        /* body: either a block or a single expression */
        if (p->cur.type == TOKEN_INDENT) {
            cl->body = parse_block(p);
        } else {
            ASTNode *bk = ast_new(AST_BLOCK, p->cur.line, p->cur.col);
            ASTNode *ret = ast_new(AST_RETURN_STMT, p->cur.line, p->cur.col);
            ret->left = parse_expr(p);
            ast_push(bk, ret);
            cl->body = bk;
        }
        n = cl;
        break;
    }

    default:
        if (!p->had_error) {
            snprintf(p->err, sizeof(p->err),
                     "[%d:%d] unexpected token %s '%s' in expression",
                     ln, col,
                     token_type_name(p->cur.type), p->cur.value);
            p->had_error = 1;
        }
        p_advance(p);   /* skip bad token to avoid infinite loop */
        n = ast_new(AST_IDENT, ln, col);
        strncpy(n->str, "?", MAX_TOKEN_LEN - 1);
        break;
    }
    return n;
}

/* postfix: handles call, member-access, deref-access */
static ASTNode *parse_postfix(Parser *p) {
    ASTNode *n = parse_primary(p);

    for (;;) {
        int ln = p->cur.line, col = p->cur.col;

        if (p->cur.type == TOKEN_LPAREN) {
            /* function / method call */
            p_advance(p);  /* consume '(' */
            ASTNode *call = ast_new(AST_CALL, ln, col);
            call->left = n;
            /* parse arguments */
            while (!p_check(p, TOKEN_RPAREN) &&
                   !p_check(p, TOKEN_EOF)    &&
                   !p_check(p, TOKEN_NEWLINE)) {
                ast_push(call, parse_expr(p));
                if (!p_match(p, TOKEN_COMMA)) break;
            }
            p_expect(p, TOKEN_RPAREN);
            n = call;

        } else if (p->cur.type == TOKEN_DEREF) {
            /* ptr.* */
            ASTNode *dr = ast_new(AST_DEREF_EXPR, ln, col);
            dr->left = n;
            p_advance(p);
            n = dr;

        } else if (p->cur.type == TOKEN_LBRACKET) {
            /* arr[index] */
            p_advance(p);
            ASTNode *idx = ast_new(AST_INDEX, ln, col);
            idx->left  = n;
            idx->right = parse_expr(p);
            p_expect(p, TOKEN_RBRACKET);
            n = idx;

        } else if (p->cur.type == TOKEN_DOT) {
            /* obj.member */
            p_advance(p);
            ASTNode *mem = ast_new(AST_MEMBER, ln, col);
            mem->left = n;
            if (p->cur.type == TOKEN_IDENT) {
                strncpy(mem->str, p->cur.value, MAX_TOKEN_LEN - 1);
                p_advance(p);
            } else {
                strncpy(mem->str, "?", MAX_TOKEN_LEN - 1);
            }
            n = mem;

        } else {
            break;
        }
    }
    return n;
}

static ASTNode *parse_unary(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    if (p->cur.type == TOKEN_MINUS) {
        p_advance(p);
        ASTNode *n = ast_new(AST_UNARY_OP, ln, col);
        strncpy(n->str, "-", MAX_TOKEN_LEN - 1);
        n->left = parse_unary(p);
        return n;
    }
    return parse_postfix(p);
}

static ASTNode *parse_mul(Parser *p) {
    ASTNode *n = parse_unary(p);
    while (p->cur.type == TOKEN_STAR || p->cur.type == TOKEN_SLASH ||
           p->cur.type == TOKEN_PERCENT) {
        int ln = p->cur.line, col = p->cur.col;
        char op[4]; strncpy(op, p->cur.value, 3); op[3] = '\0';
        p_advance(p);
        ASTNode *r = parse_unary(p);
        ASTNode *b = ast_new(AST_BIN_OP, ln, col);
        strncpy(b->str, op, MAX_TOKEN_LEN - 1);
        b->left  = n;
        b->right = r;
        n = b;
    }
    return n;
}

static ASTNode *parse_add(Parser *p) {
    ASTNode *n = parse_mul(p);
    while (p->cur.type == TOKEN_PLUS || p->cur.type == TOKEN_MINUS) {
        int ln = p->cur.line, col = p->cur.col;
        char op[4]; strncpy(op, p->cur.value, 3); op[3] = '\0';
        p_advance(p);
        ASTNode *r = parse_mul(p);
        ASTNode *b = ast_new(AST_BIN_OP, ln, col);
        strncpy(b->str, op, MAX_TOKEN_LEN - 1);
        b->left  = n;
        b->right = r;
        n = b;
    }
    return n;
}

static int is_cmp_op(TokenType t) {
    return t == TOKEN_LT || t == TOKEN_GT || t == TOKEN_LE ||
           t == TOKEN_GE || t == TOKEN_EQ || t == TOKEN_NEQ;
}

static ASTNode *parse_cmp(Parser *p) {
    ASTNode *n = parse_add(p);
    if (is_cmp_op(p->cur.type)) {
        int ln = p->cur.line, col = p->cur.col;
        char op[4]; strncpy(op, p->cur.value, 3); op[3] = '\0';
        p_advance(p);
        ASTNode *r = parse_add(p);
        ASTNode *b = ast_new(AST_BIN_OP, ln, col);
        strncpy(b->str, op, MAX_TOKEN_LEN - 1);
        b->left  = n;
        b->right = r;
        return b;
    }
    return n;
}

static ASTNode *parse_not(Parser *p) {
    if (p->cur.type == TOKEN_BANG || p->cur.type == TOKEN_NOT) {
        int ln = p->cur.line, col = p->cur.col;
        p_advance(p);
        ASTNode *n = ast_new(AST_UNARY_OP, ln, col);
        strncpy(n->str, "!", MAX_TOKEN_LEN - 1);
        n->left = parse_not(p);
        return n;
    }
    return parse_cmp(p);
}

static ASTNode *parse_and(Parser *p) {
    ASTNode *n = parse_not(p);
    while (p->cur.type == TOKEN_AND_AND || p->cur.type == TOKEN_AND) {
        int ln = p->cur.line, col = p->cur.col;
        p_advance(p);
        ASTNode *r = parse_not(p);
        ASTNode *b = ast_new(AST_BIN_OP, ln, col);
        strncpy(b->str, "&&", MAX_TOKEN_LEN - 1);
        b->left  = n;
        b->right = r;
        n = b;
    }
    return n;
}

static ASTNode *parse_or(Parser *p) {
    ASTNode *n = parse_and(p);
    while (p->cur.type == TOKEN_OR_OR || p->cur.type == TOKEN_OR) {
        int ln = p->cur.line, col = p->cur.col;
        p_advance(p);
        ASTNode *r = parse_and(p);
        ASTNode *b = ast_new(AST_BIN_OP, ln, col);
        strncpy(b->str, "||", MAX_TOKEN_LEN - 1);
        b->left  = n;
        b->right = r;
        n = b;
    }
    return n;
}

/* range_expr: or_expr [ ('..' | '..=') or_expr ] */
static ASTNode *parse_range(Parser *p) {
    ASTNode *n = parse_or(p);
    if (p->cur.type == TOKEN_DOTDOT || p->cur.type == TOKEN_DOTDOT_EQ) {
        int ln = p->cur.line, col = p->cur.col;
        int inclusive = (p->cur.type == TOKEN_DOTDOT_EQ);
        p_advance(p);
        ASTNode *r = ast_new(AST_RANGE, ln, col);
        r->left  = n;
        r->right = parse_or(p);
        r->flag  = inclusive;
        return r;
    }
    return n;
}

static int is_assign_op(TokenType t) {
    return t == TOKEN_ASSIGN        || t == TOKEN_PLUS_ASSIGN   ||
           t == TOKEN_MINUS_ASSIGN  || t == TOKEN_STAR_ASSIGN   ||
           t == TOKEN_SLASH_ASSIGN  || t == TOKEN_PERCENT_ASSIGN;
}

static ASTNode *parse_expr(Parser *p) {
    ASTNode *left = parse_range(p);
    if (is_assign_op(p->cur.type)) {
        int ln = p->cur.line, col = p->cur.col;
        char op[4]; strncpy(op, p->cur.value, 3); op[3] = '\0';
        p_advance(p);
        ASTNode *right = parse_range(p);
        ASTNode *a = ast_new(AST_ASSIGN, ln, col);
        strncpy(a->str, op, MAX_TOKEN_LEN - 1);
        a->left  = left;
        a->right = right;
        return a;
    }
    return left;
}

/* ═══════════════════════════════════════════════════════════════════════
   Statement parsing
   ═══════════════════════════════════════════════════════════════════════ */

static ASTNode *parse_var_decl(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    int is_mut = (p->cur.type == TOKEN_MUT);
    p_advance(p);   /* consume 'let' or 'mut' */

    ASTNode *n = ast_new(AST_VAR_DECL, ln, col);
    n->flag = is_mut;

    /* variable name */
    if (p->cur.type == TOKEN_IDENT) {
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
    }

    /* optional type annotation */
    if (p_match(p, TOKEN_COLON)) {
        parse_type_str(p, n->str2, MAX_TOKEN_LEN);
    }

    /* initializer */
    p_expect(p, TOKEN_ASSIGN);
    n->left = parse_expr(p);
    consume_nl(p);
    return n;
}

static ASTNode *parse_if_stmt(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'if' */

    ASTNode *n = ast_new(AST_IF_STMT, ln, col);
    n->left = parse_expr(p);     /* condition */
    p_expect(p, TOKEN_COLON);
    consume_nl(p);
    n->body = parse_block(p);    /* then-block */

    /* optional else */
    if (p->cur.type == TOKEN_ELSE) {
        p_advance(p);   /* consume 'else' */
        p_expect(p, TOKEN_COLON);
        consume_nl(p);
        n->right = parse_block(p);
    }
    return n;
}

static ASTNode *parse_while_stmt(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'while' */

    ASTNode *n = ast_new(AST_WHILE_STMT, ln, col);
    n->left = parse_expr(p);    /* condition */
    p_expect(p, TOKEN_COLON);
    consume_nl(p);
    n->body = parse_block(p);
    return n;
}

static ASTNode *parse_for_stmt(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'for' */

    ASTNode *n = ast_new(AST_FOR_STMT, ln, col);
    if (p->cur.type == TOKEN_IDENT) {
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
    }
    p_expect(p, TOKEN_IN);
    n->left = parse_expr(p);    /* iterable */
    p_expect(p, TOKEN_COLON);
    consume_nl(p);
    n->body = parse_block(p);
    return n;
}

static ASTNode *parse_return_stmt(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'return' */

    ASTNode *n = ast_new(AST_RETURN_STMT, ln, col);
    if (p->cur.type != TOKEN_NEWLINE && p->cur.type != TOKEN_DEDENT
        && p->cur.type != TOKEN_EOF) {
        n->left = parse_expr(p);
    }
    consume_nl(p);
    return n;
}

static ASTNode *parse_expr_stmt(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    ASTNode *n = ast_new(AST_EXPR_STMT, ln, col);
    n->left = parse_expr(p);
    consume_nl(p);
    return n;
}

static ASTNode *parse_stmt(Parser *p) {
    skip_nl(p);
    switch (p->cur.type) {
    case TOKEN_LET:    return parse_var_decl(p);
    case TOKEN_MUT:    return parse_var_decl(p);
    case TOKEN_IF:     return parse_if_stmt(p);
    case TOKEN_WHILE:  return parse_while_stmt(p);
    case TOKEN_FOR:    return parse_for_stmt(p);
    case TOKEN_RETURN: return parse_return_stmt(p);
    default:           return parse_expr_stmt(p);
    }
}

/* ── Block: INDENT stmts+ DEDENT ─────────────────────────────────────── */
static ASTNode *parse_block(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    ASTNode *block = ast_new(AST_BLOCK, ln, col);

    if (!p_expect(p, TOKEN_INDENT)) return block;

    while (!p_check(p, TOKEN_DEDENT) && !p_check(p, TOKEN_EOF)) {
        skip_nl(p);
        if (p_check(p, TOKEN_DEDENT) || p_check(p, TOKEN_EOF)) break;
        ast_push(block, parse_stmt(p));
        if (p->had_error) break;
    }
    p_expect(p, TOKEN_DEDENT);
    return block;
}

/* ═══════════════════════════════════════════════════════════════════════
   Top-level declarations
   ═══════════════════════════════════════════════════════════════════════ */

/* params: ( mut? IDENT (: type)? , ... ) */
static void parse_params(Parser *p, ASTNode *func) {
    while (!p_check(p, TOKEN_RPAREN) && !p_check(p, TOKEN_EOF)) {
        int ln = p->cur.line, col = p->cur.col;
        ASTNode *param = ast_new(AST_PARAM, ln, col);

        if (p->cur.type == TOKEN_MUT) {
            param->flag = 1;
            p_advance(p);
        }
        if (p->cur.type == TOKEN_IDENT) {
            strncpy(param->str, p->cur.value, MAX_TOKEN_LEN - 1);
            p_advance(p);
        }
        if (p_match(p, TOKEN_COLON)) {
            parse_type_str(p, param->str2, MAX_TOKEN_LEN);
        }
        ast_push(func, param);
        if (!p_match(p, TOKEN_COMMA)) break;
    }
}

static ASTNode *parse_func_def(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'def' */

    ASTNode *n = ast_new(AST_FUNC_DEF, ln, col);
    if (p->cur.type == TOKEN_IDENT) {
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
    }
    p_expect(p, TOKEN_LPAREN);
    parse_params(p, n);
    p_expect(p, TOKEN_RPAREN);

    /* optional return type */
    if (p_match(p, TOKEN_ARROW)) {
        parse_type_str(p, n->str2, MAX_TOKEN_LEN);
    }
    p_expect(p, TOKEN_COLON);
    consume_nl(p);
    n->body = parse_block(p);
    return n;
}

static ASTNode *parse_struct_def(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'struct' */

    ASTNode *n = ast_new(AST_STRUCT_DEF, ln, col);
    if (p->cur.type == TOKEN_IDENT) {
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
    }
    p_expect(p, TOKEN_COLON);
    consume_nl(p);
    p_expect(p, TOKEN_INDENT);

    while (!p_check(p, TOKEN_DEDENT) && !p_check(p, TOKEN_EOF)) {
        skip_nl(p);
        if (p_check(p, TOKEN_DEDENT) || p_check(p, TOKEN_EOF)) break;
        int fln = p->cur.line, fcol = p->cur.col;
        ASTNode *field = ast_new(AST_FIELD, fln, fcol);
        if (p->cur.type == TOKEN_IDENT) {
            strncpy(field->str, p->cur.value, MAX_TOKEN_LEN - 1);
            p_advance(p);
        }
        p_expect(p, TOKEN_COLON);
        parse_type_str(p, field->str2, MAX_TOKEN_LEN);
        consume_nl(p);
        ast_push(n, field);
        if (p->had_error) break;
    }
    p_expect(p, TOKEN_DEDENT);
    return n;
}

static ASTNode *parse_impl_def(Parser *p) {
    int ln = p->cur.line, col = p->cur.col;
    p_advance(p);   /* consume 'impl' */

    ASTNode *n = ast_new(AST_IMPL_DEF, ln, col);
    if (p->cur.type == TOKEN_IDENT) {
        strncpy(n->str, p->cur.value, MAX_TOKEN_LEN - 1);
        p_advance(p);
    }
    p_expect(p, TOKEN_COLON);
    consume_nl(p);
    p_expect(p, TOKEN_INDENT);

    while (!p_check(p, TOKEN_DEDENT) && !p_check(p, TOKEN_EOF)) {
        skip_nl(p);
        if (p_check(p, TOKEN_DEDENT) || p_check(p, TOKEN_EOF)) break;
        if (p->cur.type == TOKEN_DEF) {
            ast_push(n, parse_func_def(p));
        } else {
            /* skip unexpected token */
            p_advance(p);
        }
        if (p->had_error) break;
    }
    p_expect(p, TOKEN_DEDENT);
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════
   Entry point
   ═══════════════════════════════════════════════════════════════════════ */

ASTNode *parse(const char *src) {
    Lexer lex;
    lexer_init(&lex, src);

    Parser p;
    memset(&p, 0, sizeof(p));
    p.lex = &lex;
    p_advance(&p);  /* prime the lookahead */

    ASTNode *prog = ast_new(AST_PROGRAM, 1, 1);

    while (!p_check(&p, TOKEN_EOF)) {
        skip_nl(&p);
        if (p_check(&p, TOKEN_EOF)) break;

        switch (p.cur.type) {
        case TOKEN_DEF:    ast_push(prog, parse_func_def(&p));   break;
        case TOKEN_STRUCT: ast_push(prog, parse_struct_def(&p)); break;
        case TOKEN_IMPL:   ast_push(prog, parse_impl_def(&p));   break;
        default:
            /* top-level expression/statement (uncommon but tolerated) */
            ast_push(prog, parse_stmt(&p));
            break;
        }
        if (p.had_error) {
            fprintf(stderr, "parse error: %s\n", p.err);
            break;
        }
    }
    return prog;
}
