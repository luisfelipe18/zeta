#include "lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ── Keyword table ─────────────────────────────────────────────────────── */
static const struct { const char *word; TokenType type; } KEYWORDS[] = {
    {"def",    TOKEN_DEF},    {"let",    TOKEN_LET},
    {"mut",    TOKEN_MUT},    {"impl",   TOKEN_IMPL},
    {"while",  TOKEN_WHILE},  {"struct", TOKEN_STRUCT},
    {"spawn",  TOKEN_SPAWN},  {"await",  TOKEN_AWAIT},
    {"return", TOKEN_RETURN}, {"print",  TOKEN_PRINT},
    {"alloc",  TOKEN_ALLOC},  {"free",   TOKEN_FREE},
    {"if",     TOKEN_IF},     {"else",   TOKEN_ELSE},
    {"and",    TOKEN_AND},    {"or",     TOKEN_OR},
    {"not",    TOKEN_NOT},    {"true",   TOKEN_TRUE},
    {"false",  TOKEN_FALSE},
    {"for",    TOKEN_FOR},    {"in",     TOKEN_IN},
    {"match",  TOKEN_MATCH},  {"fn",     TOKEN_FN},
    {"trait",  TOKEN_TRAIT},  {"type",   TOKEN_TYPE_KW},
    {NULL, TOKEN_ERROR},
};

/* ── Primitives ────────────────────────────────────────────────────────── */

void lexer_init(Lexer *l, const char *src) {
    memset(l, 0, sizeof(*l));
    l->src            = src;
    l->line           = 1;
    l->col            = 1;
    l->at_line_start  = 1;
    l->indent_stack[0] = 0;
    l->indent_top     = 0;
}

/* Current character */
static char lx_cur(const Lexer *l) { return l->src[l->pos]; }

/* Next character (lookahead-1), safe at EOF */
static char lx_nxt(const Lexer *l) {
    return l->src[l->pos] ? l->src[l->pos + 1] : '\0';
}

/* Consume one character, update line/col */
static char lx_eat(Lexer *l) {
    char c = l->src[l->pos++];
    if (c == '\n') { l->line++; l->col = 1; }
    else           { l->col++;              }
    return c;
}

/* Build a token (value is copied in) */
static Token mktok(TokenType type, const char *val, int line, int col) {
    Token t;
    t.type = type;
    t.line = line;
    t.col  = col;
    strncpy(t.value, val ? val : "", MAX_TOKEN_LEN - 1);
    t.value[MAX_TOKEN_LEN - 1] = '\0';
    return t;
}

/* ── Pending-token queue (FIFO) ────────────────────────────────────────── */

static void enqueue(Lexer *l, Token t) {
    if (l->pending_count >= MAX_PENDING) return;
    int tail = (l->pending_head + l->pending_count) % MAX_PENDING;
    l->pending[tail] = t;
    l->pending_count++;
}

static Token dequeue(Lexer *l) {
    Token t = l->pending[l->pending_head];
    l->pending_head = (l->pending_head + 1) % MAX_PENDING;
    l->pending_count--;
    return t;
}

/* ── Indentation logic ─────────────────────────────────────────────────── */

static void handle_indent(Lexer *l, int spaces, int line) {
    int top = l->indent_stack[l->indent_top];

    if (spaces > top) {
        /* Deeper level: push and emit INDENT */
        if (l->indent_top + 1 < MAX_INDENT) {
            l->indent_top++;
            l->indent_stack[l->indent_top] = spaces;
        }
        enqueue(l, mktok(TOKEN_INDENT, "", line, 1));
    } else if (spaces < top) {
        /* Shallower: pop levels and emit one DEDENT per level */
        while (l->indent_top > 0 && l->indent_stack[l->indent_top] > spaces) {
            l->indent_top--;
            enqueue(l, mktok(TOKEN_DEDENT, "", line, 1));
        }
    }
    /* Equal: no token */
}

/* Close all open indent blocks (at EOF) */
static void close_all_blocks(Lexer *l) {
    while (l->indent_top > 0) {
        l->indent_top--;
        enqueue(l, mktok(TOKEN_DEDENT, "", l->line, 1));
    }
    enqueue(l, mktok(TOKEN_EOF, "", l->line, l->col));
}

/* ── Consume a newline (\n or \r\n) ────────────────────────────────────── */
static void eat_newline(Lexer *l) {
    if (lx_cur(l) == '\r') lx_eat(l);
    if (lx_cur(l) == '\n') lx_eat(l);
}

/* ── Main tokenizer ────────────────────────────────────────────────────── */

Token lexer_next(Lexer *l) {
    /* Return buffered tokens first */
    if (l->pending_count > 0)
        return dequeue(l);

    /* ── Line-start: measure indentation ─────────────────────────── */
    while (l->at_line_start) {
        l->at_line_start = 0;

        int spaces     = 0;
        int indent_ln  = l->line;

        /* Count leading spaces/tabs */
        while (lx_cur(l) == ' ' || lx_cur(l) == '\t') {
            spaces += (lx_cur(l) == '\t') ? 4 : 1;
            lx_eat(l);
        }

        char c = lx_cur(l);

        /* Blank line */
        if (c == '\n' || c == '\r') {
            eat_newline(l);
            l->at_line_start = 1;
            continue;
        }

        /* Comment-only line */
        if (c == '#') {
            while (lx_cur(l) && lx_cur(l) != '\n' && lx_cur(l) != '\r')
                lx_eat(l);
            if (lx_cur(l)) { eat_newline(l); }
            l->at_line_start = 1;
            continue;
        }

        /* EOF */
        if (c == '\0') {
            close_all_blocks(l);
            return dequeue(l);
        }

        /* Real content: process indent change */
        handle_indent(l, spaces, indent_ln);
        if (l->pending_count > 0)
            return dequeue(l);
        break;
    }

    /* ── Skip inline whitespace (spaces, tabs, bare CR) ─────────── */
    while (lx_cur(l) == ' ' || lx_cur(l) == '\t' || lx_cur(l) == '\r')
        lx_eat(l);

    /* ── EOF in mid-line ─────────────────────────────────────────── */
    if (lx_cur(l) == '\0') {
        close_all_blocks(l);
        return dequeue(l);
    }

    /* ── Inline comment: skip to end of line, fall into newline ─── */
    if (lx_cur(l) == '#') {
        while (lx_cur(l) && lx_cur(l) != '\n' && lx_cur(l) != '\r')
            lx_eat(l);
    }

    /* ── Newline ─────────────────────────────────────────────────── */
    if (lx_cur(l) == '\n' || lx_cur(l) == '\r') {
        int ln = l->line, col = l->col;
        eat_newline(l);
        l->at_line_start = 1;
        return mktok(TOKEN_NEWLINE, "", ln, col);
    }

    int tln = l->line, tcol = l->col;
    char c = lx_cur(l);

    /* ── String literal ──────────────────────────────────────────── */
    if (c == '"') {
        lx_eat(l);
        char buf[MAX_TOKEN_LEN];
        int  len = 0;
        while (lx_cur(l) != '"' && lx_cur(l) != '\0' && lx_cur(l) != '\n') {
            if (lx_cur(l) == '\\') {
                lx_eat(l);
                char e = lx_eat(l);
                switch (e) {
                    case 'n':  buf[len++] = '\n'; break;
                    case 't':  buf[len++] = '\t'; break;
                    case '"':  buf[len++] = '"';  break;
                    case '\\': buf[len++] = '\\'; break;
                    default:   buf[len++] = e;    break;
                }
            } else {
                buf[len++] = lx_eat(l);
            }
            if (len >= MAX_TOKEN_LEN - 1) break;
        }
        if (lx_cur(l) == '"') lx_eat(l);
        buf[len] = '\0';
        return mktok(TOKEN_STR_LIT, buf, tln, tcol);
    }

    /* ── Number literal ──────────────────────────────────────────── */
    if (isdigit((unsigned char)c)) {
        char buf[MAX_TOKEN_LEN];
        int  len = 0, is_float = 0;
        while (isdigit((unsigned char)lx_cur(l)) && len < MAX_TOKEN_LEN - 1)
            buf[len++] = lx_eat(l);
        if (lx_cur(l) == '.' && isdigit((unsigned char)lx_nxt(l))) {
            is_float = 1;
            buf[len++] = lx_eat(l);
            while (isdigit((unsigned char)lx_cur(l)) && len < MAX_TOKEN_LEN - 1)
                buf[len++] = lx_eat(l);
        }
        buf[len] = '\0';
        return mktok(is_float ? TOKEN_FLOAT_LIT : TOKEN_INT_LIT, buf, tln, tcol);
    }

    /* ── Identifier / keyword ────────────────────────────────────── */
    if (isalpha((unsigned char)c) || c == '_') {
        char buf[MAX_TOKEN_LEN];
        int  len = 0;
        while ((isalnum((unsigned char)lx_cur(l)) || lx_cur(l) == '_')
               && len < MAX_TOKEN_LEN - 1)
            buf[len++] = lx_eat(l);
        buf[len] = '\0';
        TokenType tt = TOKEN_IDENT;
        for (int i = 0; KEYWORDS[i].word; i++)
            if (strcmp(buf, KEYWORDS[i].word) == 0) { tt = KEYWORDS[i].type; break; }
        return mktok(tt, buf, tln, tcol);
    }

    /* ── Operators (consume char first, then inspect next) ───────── */
    lx_eat(l);

    switch (c) {
        case '+':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_PLUS_ASSIGN,  "+=",tln,tcol);}
            return mktok(TOKEN_PLUS,  "+", tln, tcol);
        case '-':
            if (lx_cur(l)=='>'){lx_eat(l);return mktok(TOKEN_ARROW,        "->",tln,tcol);}
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_MINUS_ASSIGN, "-=",tln,tcol);}
            return mktok(TOKEN_MINUS, "-", tln, tcol);
        case '*':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_STAR_ASSIGN,  "*=",tln,tcol);}
            return mktok(TOKEN_STAR,  "*", tln, tcol);
        case '/':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_SLASH_ASSIGN,   "/=",tln,tcol);}
            return mktok(TOKEN_SLASH, "/", tln, tcol);
        case '%':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_PERCENT_ASSIGN, "%=",tln,tcol);}
            return mktok(TOKEN_PERCENT, "%", tln, tcol);
        case '=':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_EQ,           "==",tln,tcol);}
            return mktok(TOKEN_ASSIGN,"=", tln, tcol);
        case '!':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_NEQ,          "!=",tln,tcol);}
            return mktok(TOKEN_BANG,  "!",  tln, tcol);
        case '&':
            if (lx_cur(l)=='&'){lx_eat(l);return mktok(TOKEN_AND_AND,      "&&",tln,tcol);}
            return mktok(TOKEN_ERROR, "&",  tln, tcol);
        /* '|' is now handled below */
        case '<':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_LE,           "<=",tln,tcol);}
            return mktok(TOKEN_LT,    "<", tln, tcol);
        case '>':
            if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_GE,           ">=",tln,tcol);}
            return mktok(TOKEN_GT,    ">", tln, tcol);
        case '.':
            if (lx_cur(l)=='.'){
                lx_eat(l);
                if (lx_cur(l)=='='){lx_eat(l);return mktok(TOKEN_DOTDOT_EQ,"..=",tln,tcol);}
                return mktok(TOKEN_DOTDOT, "..", tln, tcol);
            }
            if (lx_cur(l)=='*'){lx_eat(l);return mktok(TOKEN_DEREF,        ".*",tln,tcol);}
            return mktok(TOKEN_DOT,   ".", tln, tcol);
        case ':': return mktok(TOKEN_COLON,  ":",  tln, tcol);
        case '(': return mktok(TOKEN_LPAREN, "(",  tln, tcol);
        case ')': return mktok(TOKEN_RPAREN, ")",  tln, tcol);
        case '[': return mktok(TOKEN_LBRACKET,"[", tln, tcol);
        case ']': return mktok(TOKEN_RBRACKET,"]", tln, tcol);
        case ',': return mktok(TOKEN_COMMA,  ",",  tln, tcol);
        case '|':
            if (lx_cur(l)=='|'){lx_eat(l);return mktok(TOKEN_OR_OR,        "||",tln,tcol);}
            return mktok(TOKEN_PIPE, "|", tln, tcol);
        default: {
            char buf[2] = {c, '\0'};
            return mktok(TOKEN_ERROR, buf, tln, tcol);
        }
    }
}

/* ── Token type name ───────────────────────────────────────────────────── */

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOKEN_DEF:          return "DEF";
        case TOKEN_LET:          return "LET";
        case TOKEN_MUT:          return "MUT";
        case TOKEN_IMPL:         return "IMPL";
        case TOKEN_WHILE:        return "WHILE";
        case TOKEN_STRUCT:       return "STRUCT";
        case TOKEN_SPAWN:        return "SPAWN";
        case TOKEN_AWAIT:        return "AWAIT";
        case TOKEN_RETURN:       return "RETURN";
        case TOKEN_PRINT:        return "PRINT";
        case TOKEN_ALLOC:        return "ALLOC";
        case TOKEN_FREE:         return "FREE";
        case TOKEN_IF:           return "IF";
        case TOKEN_ELSE:         return "ELSE";
        case TOKEN_AND:          return "AND";
        case TOKEN_OR:           return "OR";
        case TOKEN_NOT:          return "NOT";
        case TOKEN_TRUE:         return "TRUE";
        case TOKEN_FALSE:        return "FALSE";
        case TOKEN_INDENT:       return "INDENT";
        case TOKEN_DEDENT:       return "DEDENT";
        case TOKEN_NEWLINE:      return "NEWLINE";
        case TOKEN_IDENT:        return "IDENT";
        case TOKEN_INT_LIT:      return "INT_LIT";
        case TOKEN_FLOAT_LIT:    return "FLOAT_LIT";
        case TOKEN_STR_LIT:      return "STR_LIT";
        case TOKEN_PLUS:         return "PLUS";
        case TOKEN_MINUS:        return "MINUS";
        case TOKEN_STAR:         return "STAR";
        case TOKEN_SLASH:        return "SLASH";
        case TOKEN_ASSIGN:       return "ASSIGN";
        case TOKEN_EQ:           return "EQ";
        case TOKEN_NEQ:          return "NEQ";
        case TOKEN_LT:           return "LT";
        case TOKEN_GT:           return "GT";
        case TOKEN_LE:           return "LE";
        case TOKEN_GE:           return "GE";
        case TOKEN_PLUS_ASSIGN:  return "PLUS_ASSIGN";
        case TOKEN_MINUS_ASSIGN: return "MINUS_ASSIGN";
        case TOKEN_STAR_ASSIGN:  return "STAR_ASSIGN";
        case TOKEN_SLASH_ASSIGN: return "SLASH_ASSIGN";
        case TOKEN_COLON:        return "COLON";
        case TOKEN_ARROW:        return "ARROW";
        case TOKEN_LPAREN:       return "LPAREN";
        case TOKEN_RPAREN:       return "RPAREN";
        case TOKEN_COMMA:        return "COMMA";
        case TOKEN_DOT:          return "DOT";
        case TOKEN_DEREF:        return "DEREF";
        case TOKEN_PERCENT:        return "PERCENT";
        case TOKEN_PERCENT_ASSIGN: return "PERCENT_ASSIGN";
        case TOKEN_FOR:          return "FOR";
        case TOKEN_IN:           return "IN";
        case TOKEN_MATCH:        return "MATCH";
        case TOKEN_FN:           return "FN";
        case TOKEN_TRAIT:        return "TRAIT";
        case TOKEN_TYPE_KW:      return "TYPE";
        case TOKEN_LBRACKET:     return "LBRACKET";
        case TOKEN_RBRACKET:     return "RBRACKET";
        case TOKEN_DOTDOT:       return "DOTDOT";
        case TOKEN_DOTDOT_EQ:    return "DOTDOT_EQ";
        case TOKEN_PIPE:         return "PIPE";
        case TOKEN_AND_AND:      return "AND_AND";
        case TOKEN_OR_OR:        return "OR_OR";
        case TOKEN_BANG:         return "BANG";
        case TOKEN_EOF:          return "EOF";
        case TOKEN_ERROR:        return "ERROR";
        default:                 return "UNKNOWN";
    }
}
