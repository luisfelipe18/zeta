#ifndef LEXER_H
#define LEXER_H

#define MAX_TOKEN_LEN 1024
#define MAX_INDENT    256
#define MAX_PENDING   64

typedef enum {
    /* Keywords */
    TOKEN_DEF,    TOKEN_LET,    TOKEN_MUT,    TOKEN_IMPL,
    TOKEN_WHILE,  TOKEN_STRUCT, TOKEN_SPAWN,  TOKEN_AWAIT,
    TOKEN_RETURN, TOKEN_PRINT,  TOKEN_ALLOC,  TOKEN_FREE,
    TOKEN_IF,     TOKEN_ELSE,   TOKEN_AND,    TOKEN_OR,
    TOKEN_NOT,    TOKEN_TRUE,   TOKEN_FALSE,
    TOKEN_FOR,    TOKEN_IN,
    TOKEN_MATCH,  TOKEN_FN,
    TOKEN_TRAIT,  TOKEN_TYPE_KW,

    /* Indentation */
    TOKEN_INDENT, TOKEN_DEDENT, TOKEN_NEWLINE,

    /* Literals */
    TOKEN_IDENT,
    TOKEN_INT_LIT,
    TOKEN_FLOAT_LIT,
    TOKEN_STR_LIT,

    /* Operators */
    TOKEN_PLUS,         TOKEN_MINUS,        TOKEN_STAR,         TOKEN_SLASH,
    TOKEN_ASSIGN,       TOKEN_EQ,           TOKEN_NEQ,
    TOKEN_AND_AND,      TOKEN_OR_OR,        TOKEN_BANG,
    TOKEN_LT,           TOKEN_GT,           TOKEN_LE,           TOKEN_GE,
    TOKEN_PLUS_ASSIGN,  TOKEN_MINUS_ASSIGN,
    TOKEN_STAR_ASSIGN,  TOKEN_SLASH_ASSIGN,
    TOKEN_PERCENT,      TOKEN_PERCENT_ASSIGN,
    TOKEN_COLON,        TOKEN_ARROW,
    TOKEN_LPAREN,       TOKEN_RPAREN,
    TOKEN_LBRACKET,     TOKEN_RBRACKET,
    TOKEN_COMMA,        TOKEN_DOT,          TOKEN_DEREF,
    TOKEN_DOTDOT,       TOKEN_DOTDOT_EQ,
    TOKEN_PIPE,

    /* Special */
    TOKEN_EOF,
    TOKEN_ERROR,
} TokenType;

typedef struct {
    TokenType type;
    char      value[MAX_TOKEN_LEN];
    int       line;
    int       col;
} Token;

typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
    int         at_line_start;

    /* Indentation stack: stores leading-space counts */
    int indent_stack[MAX_INDENT];
    int indent_top;

    /* Pending token queue (for multi-DEDENT sequences) */
    Token pending[MAX_PENDING];
    int   pending_count;
    int   pending_head;
} Lexer;

void        lexer_init(Lexer *l, const char *src);
Token       lexer_next(Lexer *l);
const char *token_type_name(TokenType t);

#endif /* LEXER_H */
