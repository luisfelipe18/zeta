#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

/* ── AST node types ────────────────────────────────────────────────────── */
typedef enum {
    /* Top-level */
    AST_PROGRAM,
    AST_FUNC_DEF,
    AST_STRUCT_DEF,
    AST_IMPL_DEF,
    AST_PARAM,
    AST_FIELD,
    /* Statements */
    AST_BLOCK,
    AST_VAR_DECL,
    AST_ASSIGN,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_RETURN_STMT,
    AST_EXPR_STMT,
    /* Expressions */
    AST_BIN_OP,
    AST_UNARY_OP,
    AST_CALL,
    AST_MEMBER,
    AST_DEREF_EXPR,
    AST_INDEX,        /* arr[i]           — left=array, right=index  */
    AST_ARRAY_LIT,    /* [1, 2, 3]        — children = elements       */
    AST_LIST_COMP,    /* [expr for v in iter if cond]
                         str=var, left=expr, right=iter, body=cond    */
    AST_RANGE,        /* 0..10 / 0..=10   — left=start, right=end,
                         flag=inclusive                                */
    AST_CLOSURE,      /* |x, y| expr      — children=params, body=expr*/
    AST_MATCH_STMT,   /* match expr: arms  — left=expr, children=arms  */
    AST_IDENT,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_STR_LIT,
    AST_BOOL_LIT,
} ASTNodeType;

/* ── AST node (fat-node: all fields present, unused ones zeroed) ──────── */
typedef struct ASTNode {
    ASTNodeType type;
    int         line, col;

    char str[MAX_TOKEN_LEN];   /* name / operator / literal value        */
    char str2[MAX_TOKEN_LEN];  /* type annotation / return type          */
    int  flag;                  /* is_mut, boolean value                  */

    struct ASTNode *left;      /* condition / left operand / value       */
    struct ASTNode *right;     /* right operand / else-block             */
    struct ASTNode *body;      /* body block for compound statements     */

    /* Dynamic list: params, args, stmts, fields, methods */
    struct ASTNode **children;
    int              nchildren;
    int              cap;

    /* Set by the semantic analyzer (Phase 3) */
    int  ztype;          /* ZTypeKind enum value                    */
    int  ztype_is_ptr;   /* 1 = pointer type                        */
    char ztype_name[64]; /* struct name when ztype == ZTYPE_STRUCT   */
} ASTNode;

/* ── Parser state ────────────────────────────────────────────────────── */
typedef struct {
    Lexer *lex;
    Token  cur;          /* current lookahead token */
    int    had_error;
    char   err[2048];   /* large enough for messages with full token values */
} Parser;

/* ── Public API ──────────────────────────────────────────────────────── */
ASTNode    *ast_new(ASTNodeType type, int line, int col);
void        ast_free(ASTNode *node);
void        ast_print(const ASTNode *node, int depth);
const char *ast_type_name(ASTNodeType t);

ASTNode    *parse(const char *src);

#endif /* PARSER_H */
