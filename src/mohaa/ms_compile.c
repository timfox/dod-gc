/*
 * ms_compile.c - lexer, parser and bytecode compiler for MOHAA .scr files.
 * MIT licensed. Hand-written recursive descent; the token rules follow how
 * the shipped scripts are written (whitespace-sensitive '-', identifiers
 * that swallow punctuation, "local.x" field syntax).
 */
#include "ms_internal.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* ================================================================== */
/* arena for tokens and AST                                             */
/* ================================================================== */

typedef struct arena_blk {
    struct arena_blk *next;
    size_t used, cap;
    char data[1];
} arena_blk;

typedef struct arena {
    ms_vm *vm;
    arena_blk *head;
} arena;

static void *arena_alloc(arena *a, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (!a->head || a->head->used + n > a->head->cap) {
        size_t cap = n > 16384 ? n : 16384;
        arena_blk *b = (arena_blk *)ms_alloc(a->vm, sizeof(arena_blk) + cap);
        if (!b)
            return NULL;
        b->cap = cap;
        b->next = a->head;
        a->head = b;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += n;
    memset(p, 0, n);
    return p;
}

/* Drop everything but one block, for reuse by the next top-level statement. */
static void arena_reset(arena *a)
{
    while (a->head && a->head->next) {
        arena_blk *n = a->head->next;
        ms_free(a->vm, a->head);
        a->head = n;
    }
    if (a->head)
        a->head->used = 0;
}

static void arena_free(arena *a)
{
    while (a->head) {
        arena_blk *n = a->head->next;
        ms_free(a->vm, a->head);
        a->head = n;
    }
}

/* ================================================================== */
/* lexer                                                                */
/* ================================================================== */

enum {
    T_EOF = 0, T_EOL, T_IDENT, T_STRING, T_INT, T_FLOAT, T_LISTENER, T_DOLLAR,
    T_LPAREN, T_RPAREN, T_LBRACK, T_RBRACK, T_LBRACE, T_RBRACE,
    T_COLON, T_DCOLON, T_SEMI, T_PERIOD, T_SIZE, T_COMMA, T_TERNARY,
    T_ASSIGN, T_PLUS_EQ, T_MINUS_EQ, T_MUL_EQ, T_DIV_EQ, T_MOD_EQ, T_AND_EQ, T_OR_EQ, T_XOR_EQ,
    T_SHL_EQ, T_SHR_EQ, T_INC, T_DEC,
    T_OROR, T_ANDAND, T_OR, T_XOR, T_AND, T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    T_SHL, T_SHR, T_PLUS, T_MINUS, T_MUL, T_DIV, T_MOD,
    T_NEG, T_NOT, T_COMPL,
    T_IF, T_ELSE, T_WHILE, T_FOR, T_DO, T_SWITCH, T_CASE, T_BREAK, T_CONTINUE,
    T_TRY, T_CATCH, T_MAKEARRAY, T_ENDARRAY, T_NULL, T_NIL
};

typedef struct token {
    int   type;
    int   line;
    const char *str;   /* arena copy for IDENT / STRING */
    int32_t i;         /* INT value or listener scope */
    float f;
} token;

typedef struct lexer {
    const char *src, *end, *p;
    int line;
    int field_ok;      /* previous token can be followed by ".field" */
    int after_dollar;
    int prev_type;
} lexer;

typedef struct parser {
    ms_vm *vm;
    arena ar;
    lexer lx;
    token tok[3];
    int ntok;
    jmp_buf jb;
    char *err;
    size_t errlen;
    const char *path;
} parser;

static void perr(parser *P, int line, const char *fmt, ...)
{
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    snprintf(P->err, P->errlen, "%s:%d: %s", P->path, line, msg);
    longjmp(P->jb, 1);
}

static int is_delim(int c)
{
    return c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']' || c == '\r' ||
           c == '\n' || c == ',' || c == ':' || c == ';' || c == ' ' || c == '\t' || c == 0;
}

static int is_varname(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '"' || c == '$' || c == '\\';
}

static int is_alnum(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int is_nonnumeric(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '"' || c == '\'' ||
           c == '?' || c == '@' || c == '#' || c == '`' || (c & 0x80);
}

static const char *copy_str(parser *P, const char *s, size_t n, int unescape)
{
    char *d = (char *)arena_alloc(&P->ar, n + 1);
    if (!d)
        perr(P, P->lx.line, "out of memory");
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (unescape && c == '\\' && i + 1 < n) {
            char e = s[++i];
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e;
        }
        d[o++] = c;
    }
    d[o] = 0;
    return d;
}

static const struct { const char *w; int t; int sc; } keywords[] = {
    {"if", T_IF, 0}, {"else", T_ELSE, 0}, {"while", T_WHILE, 0}, {"for", T_FOR, 0}, {"do", T_DO, 0},
    {"switch", T_SWITCH, 0}, {"case", T_CASE, 0}, {"break", T_BREAK, 0}, {"continue", T_CONTINUE, 0},
    {"try", T_TRY, 0}, {"catch", T_CATCH, 0}, {"makearray", T_MAKEARRAY, 0}, {"makeArray", T_MAKEARRAY, 0},
    {"endarray", T_ENDARRAY, 0}, {"endArray", T_ENDARRAY, 0}, {"NULL", T_NULL, 0}, {"NIL", T_NIL, 0},
    {"game", T_LISTENER, SC_GAME}, {"level", T_LISTENER, SC_LEVEL}, {"local", T_LISTENER, SC_LOCAL},
    {"parm", T_LISTENER, SC_PARM}, {"self", T_LISTENER, SC_SELF}, {"group", T_LISTENER, SC_GROUP},
    {"owner", T_LISTENER, SC_OWNER},
    {"ifequal", T_EQ, 0}, {"ifstrequal", T_EQ, 0}, {"ifnotequal", T_NE, 0}, {"ifstrnotequal", T_NE, 0},
    {"ifless", T_LT, 0}, {"ifgreater", T_GT, 0}, {"iflessequal", T_LE, 0}, {"ifgreaterequal", T_GE, 0},
    {NULL, 0, 0}
};

static int value_end(int t)
{
    return t == T_IDENT || t == T_STRING || t == T_INT || t == T_FLOAT || t == T_RPAREN ||
           t == T_RBRACK || t == T_LISTENER || t == T_SIZE || t == T_NULL || t == T_NIL;
}

static void lex_next(parser *P, token *t)
{
    lexer *L = &P->lx;
    memset(t, 0, sizeof(*t));
again:
    /* whitespace, continuations, comments */
    for (;;) {
        if (L->p >= L->end)
            break;
        char c = *L->p;
        if (c == ' ' || c == '\t') {
            L->p++;
        } else if (c == '\\' && L->p + 1 < L->end && (L->p[1] == '\n' || L->p[1] == '\r')) {
            L->p++;
            while (L->p < L->end && (*L->p == '\n' || *L->p == '\r')) {
                if (*L->p == '\n')
                    L->line++;
                L->p++;
            }
        } else if (c == '/' && L->p + 1 < L->end && L->p[1] == '/') {
            while (L->p < L->end && *L->p != '\n' && *L->p != '\r')
                L->p++;
        } else if (c == '/' && L->p + 1 < L->end && L->p[1] == '*') {
            L->p += 2;
            while (L->p < L->end && !(L->p[0] == '*' && L->p + 1 < L->end && L->p[1] == '/')) {
                if (*L->p == '\n')
                    L->line++;
                L->p++;
            }
            if (L->p >= L->end)
                perr(P, L->line, "unexpected end of file in comment");
            L->p += 2;
        } else
            break;
    }
    t->line = L->line;
    if (L->p >= L->end) {
        if (L->prev_type != T_EOL && L->prev_type != T_EOF) {
            t->type = T_EOL;   /* every file ends with a newline */
        } else
            t->type = T_EOF;
        goto done;
    }

    char c = *L->p;
    if (c == '\n' || c == '\r') {
        while (L->p < L->end && (*L->p == '\n' || *L->p == '\r' || *L->p == ' ' || *L->p == '\t')) {
            if (*L->p == '\n')
                L->line++;
            L->p++;
        }
        if (L->prev_type == T_EOL)
            goto again;
        t->type = T_EOL;
        goto done;
    }

    /* "$name" and "x.field" */
    if (L->after_dollar && c == '(')
        L->after_dollar = 0;
    if (L->after_dollar) {
        L->after_dollar = 0;
        if (c == '"') {
            const char *s = ++L->p;
            while (L->p < L->end && *L->p != '"' && *L->p != '\n') {
                if (*L->p == '\\' && L->p + 1 < L->end)
                    L->p++;
                L->p++;
            }
            t->type = T_STRING;
            t->str = copy_str(P, s, (size_t)(L->p - s), 1);
            if (L->p < L->end && *L->p == '"')
                L->p++;
        } else {
            const char *s = L->p;
            while (L->p < L->end && is_varname(*L->p))
                L->p++;
            if (L->p == s)
                perr(P, L->line, "bad target name after '$'");
            t->type = T_IDENT;
            t->str = copy_str(P, s, (size_t)(L->p - s), 0);
        }
        L->field_ok = 1;
        goto done_keep_field;
    }
    if (L->field_ok && c == '.') {
        const char *q = L->p + 1;
        while (q < L->end && (*q == ' ' || *q == '\t'))
            q++;
        if (q < L->end && is_varname(*q)) {
            /* a run of digits only is a number, not a field name ("x .5") */
            const char *r = q;
            int digits_only = 1;
            while (r < L->end && is_varname(*r)) {
                if (*r < '0' || *r > '9')
                    digits_only = 0;
                r++;
            }
            if (!digits_only) {
                L->p = q;
                t->type = T_PERIOD;
                L->prev_type = T_PERIOD;
                L->field_ok = 2;   /* the field name comes next */
                return;
            }
        }
    }
    if (L->field_ok == 2) {
        L->field_ok = 0;
        if (c == '"') {
            const char *s = ++L->p;
            while (L->p < L->end && *L->p != '"' && *L->p != '\n')
                L->p++;
            t->type = T_STRING;
            t->str = copy_str(P, s, (size_t)(L->p - s), 1);
            if (L->p < L->end)
                L->p++;
        } else {
            const char *s = L->p;
            while (L->p < L->end && is_varname(*L->p) && *L->p != '"')
                L->p++;
            size_t n = (size_t)(L->p - s);
            if (n == 4 && memcmp(s, "size", 4) == 0) {
                t->type = T_SIZE;
            } else {
                t->type = T_IDENT;
                t->str = copy_str(P, s, n, 0);
            }
        }
        L->field_ok = 1;
        goto done_keep_field;
    }
    L->field_ok = 0;

    if (c == '"') {
        const char *s = ++L->p;
        while (L->p < L->end && *L->p != '"' && *L->p != '\n' && *L->p != '\r') {
            if (*L->p == '\\' && L->p + 1 < L->end)
                L->p++;
            L->p++;
        }
        if (L->p >= L->end || *L->p != '"') {
            /* no closing quote on this line: the quote is just an identifier
               character (shipped scripts rely on this, e.g. m3l1b.scr) */
            L->p = s - 1;
            while (L->p < L->end && !is_delim(*L->p))
                L->p++;
            t->type = T_IDENT;
            t->str = copy_str(P, s - 1, (size_t)(L->p - (s - 1)), 0);
            goto done;
        }
        t->type = T_STRING;
        t->str = copy_str(P, s, (size_t)(L->p - s), 1);
        L->p++;
        goto done;
    }

    /* numbers (or identifiers that start with digits, like "30cal") */
    if ((c >= '0' && c <= '9') || (c == '.' && L->p + 1 < L->end && L->p[1] >= '0' && L->p[1] <= '9')) {
        const char *s = L->p;
        int dot = 0;
        while (L->p < L->end && ((*L->p >= '0' && *L->p <= '9') || *L->p == '.')) {
            if (*L->p == '.')
                dot = 1;
            L->p++;
        }
        if (L->p + 1 < L->end && (*L->p == 'e' || *L->p == 'E') && (L->p[1] == '+' || L->p[1] == '-')) {
            L->p += 2;
            while (L->p < L->end && *L->p >= '0' && *L->p <= '9')
                L->p++;
            dot = 1;
        }
        if (L->p < L->end && is_nonnumeric((unsigned char)*L->p)) {
            while (L->p < L->end && !is_delim(*L->p))
                L->p++;
            t->type = T_IDENT;
            t->str = copy_str(P, s, (size_t)(L->p - s), 0);
            goto done;
        }
        char buf[64];
        size_t n = (size_t)(L->p - s);
        if (n >= sizeof(buf))
            n = sizeof(buf) - 1;
        memcpy(buf, s, n);
        buf[n] = 0;
        if (dot) {
            t->type = T_FLOAT;
            t->f = (float)strtod(buf, NULL);
        } else {
            t->type = T_INT;
            t->i = (int32_t)strtol(buf, NULL, 10);
        }
        goto done;
    }

    /* punctuation and operators */
    {
        char c1 = L->p + 1 < L->end ? L->p[1] : 0;
        char c2 = L->p + 2 < L->end ? L->p[2] : 0;
        char before = L->p > L->src ? L->p[-1] : '\n';
        int len = 1;
        switch (c) {
        case '{': t->type = T_LBRACE; break;
        case '}': t->type = T_RBRACE; break;
        case '(': t->type = T_LPAREN; break;
        case ')': t->type = T_RPAREN; L->field_ok = 1; break;
        case '[': t->type = T_LBRACK; break;
        case ']': t->type = T_RBRACK; L->field_ok = 1; break;
        case ',': t->type = T_COMMA; break;
        case ';': t->type = T_SEMI; break;
        case '?': t->type = T_TERNARY; break;
        case '~': t->type = T_COMPL; break;
        case ':': if (c1 == ':') { t->type = T_DCOLON; len = 2; } else t->type = T_COLON; break;
        case '=': if (c1 == '=') { t->type = T_EQ; len = 2; } else t->type = T_ASSIGN; break;
        case '!': if (c1 == '=') { t->type = T_NE; len = 2; } else t->type = T_NOT; break;
        case '<':
            if (c1 == '<' && c2 == '=') { t->type = T_SHL_EQ; len = 3; }
            else if (c1 == '<') { t->type = T_SHL; len = 2; }
            else if (c1 == '=') { t->type = T_LE; len = 2; }
            else t->type = T_LT;
            break;
        case '>':
            if (c1 == '>' && c2 == '=') { t->type = T_SHR_EQ; len = 3; }
            else if (c1 == '>') { t->type = T_SHR; len = 2; }
            else if (c1 == '=') { t->type = T_GE; len = 2; }
            else t->type = T_GT;
            break;
        case '&':
            if (c1 == '&') { t->type = T_ANDAND; len = 2; }
            else if (c1 == '=') { t->type = T_AND_EQ; len = 2; }
            else t->type = T_AND;
            break;
        case '|':
            if (c1 == '|') { t->type = T_OROR; len = 2; }
            else if (c1 == '=') { t->type = T_OR_EQ; len = 2; }
            else t->type = T_OR;
            break;
        case '^': if (c1 == '=') { t->type = T_XOR_EQ; len = 2; } else t->type = T_XOR; break;
        case '+':
            if (c1 == '+') { t->type = T_INC; len = 2; }
            else if (c1 == '=') { t->type = T_PLUS_EQ; len = 2; }
            else t->type = T_PLUS;
            break;
        case '-':
            if (c1 == '-') { t->type = T_DEC; len = 2; }
            else if (c1 == '=') { t->type = T_MINUS_EQ; len = 2; }
            else if (c1 != ' ' && c1 != '\t' && c1 != '\n' && c1 != '\r' &&
                     (before == ' ' || before == '\t' || before == '(' || before == '[' || before == '\n' ||
                      !value_end(L->prev_type)))
                t->type = T_NEG;
            else
                t->type = T_MINUS;
            break;
        case '*': if (c1 == '=') { t->type = T_MUL_EQ; len = 2; } else t->type = T_MUL; break;
        case '/': if (c1 == '=') { t->type = T_DIV_EQ; len = 2; } else t->type = T_DIV; break;
        case '%': if (c1 == '=') { t->type = T_MOD_EQ; len = 2; } else t->type = T_MOD; break;
        case '$':
            while (L->p + len < L->end && L->p[len] == '$')
                len++;
            t->type = T_DOLLAR;
            L->after_dollar = 1;
            break;
        case '.': t->type = T_PERIOD; break;
        default: len = 0; break;
        }
        if (len) {
            L->p += len;
            if (t->type == T_RPAREN || t->type == T_RBRACK)
                goto done_keep_field;
            goto done;
        }
    }

    /* identifiers and keywords */
    {
        const char *s = L->p;
        const char *q = s;
        while (q < L->end && is_alnum(*q))
            q++;
        size_t an = (size_t)(q - s);
        if (an) {
            for (int k = 0; keywords[k].w; k++) {
                if (strlen(keywords[k].w) == an && memcmp(keywords[k].w, s, an) == 0) {
                    L->p = q;
                    t->type = keywords[k].t;
                    t->i = keywords[k].sc;
                    if (t->type == T_LISTENER) {
                        L->field_ok = 1;
                        goto done_keep_field;
                    }
                    goto done;
                }
            }
        }
        while (L->p < L->end && !is_delim(*L->p))
            L->p++;
        if (L->p == s)
            perr(P, L->line, "bad character '%c'", c);
        t->type = T_IDENT;
        t->str = copy_str(P, s, (size_t)(L->p - s), 0);
    }
done:
    L->field_ok = 0;
done_keep_field:
    L->prev_type = t->type;
}

/* token stream with lookahead */
static token *peek(parser *P, int k)
{
    while (P->ntok <= k)
        lex_next(P, &P->tok[P->ntok++]);
    return &P->tok[k];
}

static int tt(parser *P)
{
    return peek(P, 0)->type;
}

static token next(parser *P)
{
    token t = *peek(P, 0);
    memmove(&P->tok[0], &P->tok[1], sizeof(token) * (size_t)(P->ntok - 1));
    P->ntok--;
    return t;
}

/* The tree of the statement just compiled is no longer needed: free it, keeping the
   strings of tokens already lexed ahead. Compiling one top-level statement at a time
   bounds the compile peak by the largest statement, not the whole file. */
static void parser_reset(parser *P)
{
    char *keep[3] = {NULL, NULL, NULL};
    for (int i = 0; i < P->ntok; i++)
        if (P->tok[i].str && !(keep[i] = ms_strdup(P->vm, P->tok[i].str)))
            perr(P, P->tok[i].line, "out of memory");
    arena_reset(&P->ar);
    for (int i = 0; i < P->ntok; i++)
        if (keep[i]) {
            size_t n = strlen(keep[i]);
            char *d = (char *)arena_alloc(&P->ar, n + 1);
            if (d)
                memcpy(d, keep[i], n + 1);
            ms_strfree(P->vm, keep[i]);
            if (!d)
                perr(P, P->tok[i].line, "out of memory");
            P->tok[i].str = d;
        }
}

static token expect(parser *P, int type, const char *what)
{
    if (tt(P) != type)
        perr(P, peek(P, 0)->line, "expected %s", what);
    return next(P);
}

static void skip_eol(parser *P)
{
    while (tt(P) == T_EOL)
        next(P);
}

/* ================================================================== */
/* AST                                                                  */
/* ================================================================== */

enum {
    N_NOP = 0, N_INT, N_FLOAT, N_STR, N_NIL, N_NULL, N_SCOPE, N_FIELD, N_INDEX, N_SIZE, N_TARGET,
    N_VEC, N_BIN, N_UN, N_NOT, N_AND, N_OR, N_TERN, N_CARR, N_MAKEARRAY, N_ROW, N_CMD, N_METHOD,
    N_BLOCK, N_LABEL, N_CASE, N_IF, N_WHILE, N_DO, N_SWITCH, N_TRY, N_BREAK, N_CONTINUE, N_ASSIGN
};

typedef struct node {
    int kind, line, op;
    int32_t i;
    float f;
    const char *s;
    struct node *a, *b, *c;
    struct node *next;
} node;

static node *mk(parser *P, int kind, int line)
{
    node *n = (node *)arena_alloc(&P->ar, sizeof(node));
    if (!n)
        perr(P, line, "out of memory");
    n->kind = kind;
    n->line = line;
    return n;
}

typedef struct nlist {
    node *head, *tail;
    int n;
} nlist;

static void nl_add(nlist *l, node *n)
{
    if (!n)
        return;
    n->next = NULL;
    if (l->tail)
        l->tail->next = n;
    else
        l->head = n;
    l->tail = n;
    l->n++;
}

static node *parse_expr(parser *P);
static node *parse_nonident(parser *P);
static node *parse_stmt(parser *P);
static node *parse_stmt_decl(parser *P);

static int starts_prim(int t)
{
    return t == T_STRING || t == T_INT || t == T_FLOAT || t == T_IDENT || t == T_LISTENER ||
           t == T_DOLLAR || t == T_LPAREN || t == T_NULL || t == T_NIL || t == T_NEG ||
           t == T_COMPL || t == T_NOT;
}

static node *str_node(parser *P, token *t)
{
    node *n = mk(P, N_STR, t->line);
    n->s = t->str;
    return n;
}

static node *parse_const_item(parser *P)
{
    if (tt(P) == T_IDENT) {
        token t = next(P);
        return str_node(P, &t);
    }
    return parse_nonident(P);
}

static node *parse_carr_rest(parser *P, node *first)
{
    node *n = mk(P, N_CARR, first->line);
    nlist l = {0};
    nl_add(&l, first);
    while (tt(P) == T_DCOLON) {
        next(P);
        nl_add(&l, parse_const_item(P));
    }
    n->a = l.head;
    n->i = l.n;
    return n;
}

/* prim_expr: one event parameter */
static node *parse_prim(parser *P)
{
    node *e;
    if (tt(P) == T_IDENT) {
        token t = next(P);
        e = str_node(P, &t);
    } else
        e = parse_nonident(P);
    if (tt(P) == T_DCOLON)
        e = parse_carr_rest(P, e);
    return e;
}

static node *parse_params(parser *P, int *count)
{
    nlist l = {0};
    while (starts_prim(tt(P)))
        nl_add(&l, parse_prim(P));
    *count = l.n;
    return l.head;
}

static node *parse_base(parser *P)
{
    token t = next(P);
    node *n;
    switch (t.type) {
    case T_DOLLAR:
        n = mk(P, N_TARGET, t.line);
        if (tt(P) == T_LPAREN) {
            next(P);
            skip_eol(P);
            n->a = parse_expr(P);
            skip_eol(P);
            expect(P, T_RPAREN, "')'");
        } else if (tt(P) == T_IDENT || tt(P) == T_STRING) {
            token nt = next(P);
            n->a = str_node(P, &nt);
        } else
            perr(P, t.line, "bad target name after '$'");
        return n;
    case T_STRING: return str_node(P, &t);
    case T_INT: n = mk(P, N_INT, t.line); n->i = t.i; return n;
    case T_FLOAT: n = mk(P, N_FLOAT, t.line); n->f = t.f; return n;
    case T_LISTENER: n = mk(P, N_SCOPE, t.line); n->op = t.i; return n;
    case T_NULL: return mk(P, N_NULL, t.line);
    case T_NIL: return mk(P, N_NIL, t.line);
    case T_LPAREN: {
        skip_eol(P);
        node *e1 = parse_expr(P);
        skip_eol(P);
        if (tt(P) == T_RPAREN) {
            next(P);
            return e1;
        }
        node *e2 = parse_expr(P);
        node *e3 = parse_expr(P);
        skip_eol(P);
        expect(P, T_RPAREN, "')' after vector");
        n = mk(P, N_VEC, t.line);
        n->a = e1;
        n->b = e2;
        n->c = e3;
        return n;
    }
    default:
        perr(P, t.line, "unexpected token in expression");
    }
    return NULL;
}

static node *parse_postfix(parser *P, node *e)
{
    for (;;) {
        int t = tt(P);
        if (t == T_PERIOD) {
            token pt = next(P);
            if (tt(P) == T_SIZE) {
                next(P);
                node *n = mk(P, N_SIZE, pt.line);
                n->a = e;
                e = n;
            } else if (tt(P) == T_IDENT || tt(P) == T_STRING) {
                token ft = next(P);
                node *n = mk(P, N_FIELD, ft.line);
                n->a = e;
                n->s = ft.str;
                e = n;
            } else
                perr(P, pt.line, "expected field name after '.'");
        } else if (t == T_LBRACK) {
            token bt = next(P);
            node *n = mk(P, N_INDEX, bt.line);
            n->a = e;
            n->b = parse_expr(P);
            expect(P, T_RBRACK, "']'");
            e = n;
        } else
            return e;
    }
}

static node *parse_nonident(parser *P)
{
    int t = tt(P);
    if (t == T_NEG || t == T_COMPL || t == T_NOT) {
        token ot = next(P);
        node *n = mk(P, t == T_NOT ? N_NOT : N_UN, ot.line);
        n->op = t == T_NEG ? UO_NEG : UO_COMPL;
        n->a = parse_nonident(P);
        return n;
    }
    return parse_postfix(P, parse_base(P));
}

static node *parse_operand(parser *P)
{
    skip_eol(P);
    int t = tt(P);
    if (t == T_NEG || t == T_COMPL || t == T_NOT || t == T_MINUS) {
        token ot = next(P);
        node *n = mk(P, t == T_NOT ? N_NOT : N_UN, ot.line);
        n->op = (t == T_NEG || t == T_MINUS) ? UO_NEG : UO_COMPL;
        n->a = parse_operand(P);
        return n;
    }
    if (t == T_MAKEARRAY) {
        token mt = next(P);
        node *n = mk(P, N_MAKEARRAY, mt.line);
        nlist rows = {0};
        skip_eol(P);
        while (tt(P) != T_ENDARRAY) {
            if (tt(P) == T_EOF)
                perr(P, mt.line, "makearray without endarray");
            node *row = mk(P, N_ROW, peek(P, 0)->line);
            row->a = parse_params(P, &row->i);
            if (!row->i)
                perr(P, row->line, "bad makearray entry");
            nl_add(&rows, row);
            skip_eol(P);
        }
        next(P);
        n->a = rows.head;
        n->i = rows.n;
        return n;
    }
    if (t == T_IDENT) {
        token it = next(P);
        if (starts_prim(tt(P))) {
            node *n = mk(P, N_CMD, it.line);
            n->s = it.str;
            n->a = parse_params(P, &n->i);
            return n;
        }
        node *s = str_node(P, &it);
        if (tt(P) == T_DCOLON)
            return parse_carr_rest(P, s);
        return s;
    }
    node *e = parse_nonident(P);
    if (tt(P) == T_DCOLON)
        return parse_carr_rest(P, e);
    if (tt(P) == T_IDENT && e->kind != N_UN && e->kind != N_NOT) {
        token it = next(P);
        node *n = mk(P, N_METHOD, it.line);
        n->a = e;
        n->s = it.str;
        n->b = parse_params(P, &n->i);
        return n;
    }
    return e;
}

static int binprec(int t, int *op, int *kind)
{
    *kind = N_BIN;
    switch (t) {
    case T_OROR: *kind = N_OR; return 1;
    case T_ANDAND: *kind = N_AND; return 2;
    case T_OR: *op = BO_OR; return 3;
    case T_XOR: *op = BO_XOR; return 4;
    case T_AND: *op = BO_AND; return 5;
    case T_EQ: *op = BO_EQ; return 6;
    case T_NE: *op = BO_NE; return 6;
    case T_LT: *op = BO_LT; return 7;
    case T_GT: *op = BO_GT; return 7;
    case T_LE: *op = BO_LE; return 7;
    case T_GE: *op = BO_GE; return 7;
    case T_SHL: *op = BO_SHL; return 8;
    case T_SHR: *op = BO_SHR; return 8;
    case T_PLUS: *op = BO_ADD; return 9;
    case T_MINUS: *op = BO_SUB; return 9;
    case T_MUL: *op = BO_MUL; return 10;
    case T_DIV: *op = BO_DIV; return 10;
    case T_MOD: *op = BO_MOD; return 10;
    default: return 0;
    }
}

static node *parse_binary(parser *P, int minprec)
{
    node *left = parse_operand(P);
    for (;;) {
        int op = 0, kind;
        int prec = binprec(tt(P), &op, &kind);
        if (!prec || prec < minprec)
            return left;
        token ot = next(P);
        node *right = parse_binary(P, prec + 1);
        node *n = mk(P, kind, ot.line);
        n->op = op;
        n->a = left;
        n->b = right;
        left = n;
    }
}

static node *parse_expr(parser *P)
{
    node *c = parse_binary(P, 1);
    if (tt(P) == T_TERNARY) {
        token qt = next(P);
        node *n = mk(P, N_TERN, qt.line);
        n->a = c;
        n->b = parse_expr(P);
        expect(P, T_COLON, "':' in ?:");
        n->c = parse_expr(P);
        return n;
    }
    return c;
}

static node *parse_block(parser *P)
{
    token bt = expect(P, T_LBRACE, "'{'");
    node *n = mk(P, N_BLOCK, bt.line);
    nlist l = {0};
    for (;;) {
        while (tt(P) == T_EOL || tt(P) == T_SEMI)
            next(P);
        if (tt(P) == T_RBRACE)
            break;
        if (tt(P) == T_EOF)
            perr(P, bt.line, "missing '}'");
        nl_add(&l, parse_stmt_decl(P));
    }
    next(P);
    n->a = l.head;
    return n;
}

static node *parse_cond_body(parser *P)
{
    while (tt(P) == T_EOL)
        next(P);
    if (tt(P) == T_SEMI) {
        token t = next(P);
        return mk(P, N_NOP, t.line);
    }
    return parse_stmt_decl(P);
}

static node *mk_assign(parser *P, int line, node *lv, node *rhs)
{
    node *n = mk(P, N_ASSIGN, line);
    n->a = lv;
    n->b = rhs;
    return n;
}

static node *parse_stmt_decl(parser *P)
{
    token *pt = peek(P, 0);
    int line = pt->line;
    node *n;
    switch (pt->type) {
    case T_LBRACE:
        return parse_block(P);
    case T_IF:
        next(P);
        n = mk(P, N_IF, line);
        n->a = parse_prim(P);
        n->b = parse_cond_body(P);
        while (tt(P) == T_EOL || tt(P) == T_SEMI)
            next(P);
        if (tt(P) == T_ELSE) {
            next(P);
            n->c = parse_cond_body(P);
        }
        return n;
    case T_WHILE:
        next(P);
        n = mk(P, N_WHILE, line);
        n->a = parse_prim(P);
        n->b = parse_cond_body(P);
        return n;
    case T_FOR: {
        next(P);
        expect(P, T_LPAREN, "'(' after for");
        node *init = NULL;
        if (tt(P) != T_SEMI)
            init = parse_stmt_decl(P);
        expect(P, T_SEMI, "';' in for");
        n = mk(P, N_WHILE, line);
        n->a = parse_expr(P);
        expect(P, T_SEMI, "';' in for");
        nlist inc = {0};
        while (tt(P) != T_RPAREN) {
            if (tt(P) == T_SEMI || tt(P) == T_EOL) {
                next(P);
                continue;
            }
            if (tt(P) == T_EOF)
                perr(P, line, "missing ')' in for");
            nl_add(&inc, parse_stmt_decl(P));
        }
        next(P);
        n->b = parse_cond_body(P);
        node *incb = mk(P, N_BLOCK, line);
        incb->a = inc.head;
        n->c = incb;
        if (!init)
            return n;
        node *blk = mk(P, N_BLOCK, line);
        nlist l = {0};
        nl_add(&l, init);
        nl_add(&l, n);
        blk->a = l.head;
        return blk;
    }
    case T_DO:
        next(P);
        n = mk(P, N_DO, line);
        n->a = parse_cond_body(P);
        while (tt(P) == T_EOL)
            next(P);
        expect(P, T_WHILE, "'while' after do");
        n->b = parse_prim(P);
        return n;
    case T_SWITCH:
        next(P);
        n = mk(P, N_SWITCH, line);
        n->a = parse_prim(P);
        skip_eol(P);
        n->b = parse_block(P);
        return n;
    case T_TRY:
        next(P);
        n = mk(P, N_TRY, line);
        skip_eol(P);
        n->a = parse_block(P);
        skip_eol(P);
        expect(P, T_CATCH, "'catch' after try");
        skip_eol(P);
        n->b = parse_block(P);
        return n;
    case T_BREAK:
        next(P);
        return mk(P, N_BREAK, line);
    case T_CONTINUE:
        next(P);
        return mk(P, N_CONTINUE, line);
    case T_CASE: {
        next(P);
        n = mk(P, N_CASE, line);
        n->a = parse_prim(P);
        int cnt;
        parse_params(P, &cnt);
        expect(P, T_COLON, "':' after case");
        return n;
    }
    case T_SEMI:
        next(P);
        return mk(P, N_NOP, line);
    case T_IDENT: {
        token it = next(P);
        int cnt;
        node *params = parse_params(P, &cnt);
        if (tt(P) == T_COLON) {
            next(P);
            n = mk(P, N_LABEL, line);
            n->s = it.str;
            n->a = params;
            n->i = cnt;
            return n;
        }
        n = mk(P, N_CMD, line);
        n->s = it.str;
        n->a = params;
        n->i = cnt;
        return n;
    }
    default:
        break;
    }
    if (!starts_prim(pt->type))
        perr(P, line, "unexpected token at start of statement");
    node *e = parse_nonident(P);
    int t = tt(P);
    if (t == T_IDENT) {
        token it = next(P);
        n = mk(P, N_METHOD, it.line);
        n->a = e;
        n->s = it.str;
        n->b = parse_params(P, &n->i);
        return n;
    }
    if (t == T_ASSIGN) {
        next(P);
        return mk_assign(P, line, e, parse_expr(P));
    }
    if (t == T_INC || t == T_DEC) {
        next(P);
        node *u = mk(P, N_UN, line);
        u->op = t == T_INC ? UO_INC : UO_DEC;
        u->a = e;
        return mk_assign(P, line, e, u);
    }
    static const struct { int t, op; } compound[] = {
        {T_PLUS_EQ, BO_ADD}, {T_MINUS_EQ, BO_SUB}, {T_MUL_EQ, BO_MUL}, {T_DIV_EQ, BO_DIV},
        {T_MOD_EQ, BO_MOD}, {T_AND_EQ, BO_AND}, {T_OR_EQ, BO_OR}, {T_XOR_EQ, BO_XOR},
        {T_SHL_EQ, BO_SHL}, {T_SHR_EQ, BO_SHR}, {0, 0}
    };
    for (int k = 0; compound[k].t; k++) {
        if (compound[k].t == t) {
            next(P);
            node *b = mk(P, N_BIN, line);
            b->op = compound[k].op;
            b->a = e;
            b->b = parse_expr(P);
            return mk_assign(P, line, e, b);
        }
    }
    perr(P, line, "expected command, assignment or '++' after expression");
    return NULL;
}

static node *parse_stmt(parser *P)
{
    while (tt(P) == T_EOL || tt(P) == T_SEMI)
        next(P);
    if (tt(P) == T_EOF)
        return NULL;
    if (tt(P) == T_RBRACE)
        perr(P, peek(P, 0)->line, "unmatched '}'");
    return parse_stmt_decl(P);
}

/* ================================================================== */
/* code generation                                                      */
/* ================================================================== */

typedef struct patch {
    uint32_t *at;
    uint32_t n, cap;
} patch;

typedef struct ctl {
    int kind;            /* 0 loop, 1 switch */
    patch breaks;
    patch conts;
    int sw;              /* switch index */
    struct ctl *up;
} ctl;

typedef struct cg {
    parser *P;
    ms_vm *vm;
    uint8_t *code;
    uint32_t len, cap;
    char *kbuf;                 /* constants laid out as ms_str, at 8-aligned offsets */
    uint32_t klen, kcap;
    uint32_t *koff;             /* offset of each constant in kbuf */
    uint32_t nconsts, cconsts;
    ms_label *labels;
    uint32_t nlabels, clabels;
    ms_switch *sw;
    uint32_t nsw, csw;
    uint8_t *lines;
    uint32_t nlines, clines;    /* bytes */
    uint32_t pend_pc, pend_line, last_pc, last_line;
    int has_pend;
    ms_pool *kpool;             /* constants after pool_consts */
    ms_str **ktab;
    ctl *ctl;
} cg;

#define GROW(ptr, n, cap, type)                                                                   \
    do {                                                                                          \
        if ((n) >= (cap)) {                                                                       \
            uint32_t nc_ = (cap) ? (cap) + (cap) / 2 : 16;   /* 1.5x: less slack at the peak */                                                \
            type *np_ = (type *)ms_realloc(G->vm, (ptr), sizeof(type) * nc_);                     \
            if (!np_)                                                                             \
                perr(G->P, 0, "out of memory");                                                   \
            (ptr) = np_;                                                                          \
            (cap) = nc_;                                                                          \
        }                                                                                         \
    } while (0)

static void emit_bytes(cg *G, const void *p, uint32_t n)
{
    while (G->len + n > G->cap) {
        uint32_t nc = G->cap ? G->cap + G->cap / 2 : 1024;
        uint8_t *nb = (uint8_t *)ms_realloc(G->vm, G->code, nc);
        if (!nb)
            perr(G->P, 0, "out of memory");
        G->code = nb;
        G->cap = nc;
    }
    memcpy(G->code + G->len, p, n);
    G->len += n;
}

static void e8(cg *G, int v) { uint8_t b = (uint8_t)v; emit_bytes(G, &b, 1); }
static void e16(cg *G, int v) { uint16_t b = (uint16_t)v; emit_bytes(G, &b, 2); }
static void e32(cg *G, uint32_t v) { emit_bytes(G, &v, 4); }
static void ef(cg *G, float v) { emit_bytes(G, &v, 4); }

static uint32_t emit_jump(cg *G, int op)
{
    e8(G, op);
    uint32_t at = G->len;
    e32(G, 0);
    return at;
}

static void patch_to(cg *G, uint32_t at, uint32_t target)
{
    memcpy(G->code + at, &target, 4);
}

static void emit_jump_to(cg *G, int op, uint32_t target)
{
    e8(G, op);
    e32(G, target);
}

static void patch_add(cg *G, patch *p, uint32_t at)
{
    GROW(p->at, p->n, p->cap, uint32_t);
    p->at[p->n++] = at;
}

static void patch_all(cg *G, patch *p, uint32_t target)
{
    for (uint32_t i = 0; i < p->n; i++)
        patch_to(G, p->at[i], target);
    ms_free(G->vm, p->at);
    memset(p, 0, sizeof(*p));
}

static void line_byte(cg *G, uint32_t b)
{
    GROW(G->lines, G->nlines, G->clines, uint8_t);
    G->lines[G->nlines++] = (uint8_t)b;
}

static void line_varint(cg *G, uint32_t v)
{
    while (v >= 0x80) {
        line_byte(G, (v & 0x7F) | 0x80);
        v >>= 7;
    }
    line_byte(G, v);
}

static void flush_line(cg *G)
{
    if (!G->has_pend)
        return;
    int32_t dl = (int32_t)(G->pend_line - G->last_line);
    line_varint(G, G->pend_pc - G->last_pc);
    line_varint(G, dl < 0 ? ((uint32_t)(-dl) << 1) | 1u : (uint32_t)dl << 1);
    G->last_pc = G->pend_pc;
    G->last_line = G->pend_line;
    G->has_pend = 0;
}

static void mark_line(cg *G, int line)
{
    if (G->has_pend && G->pend_line == (uint32_t)line)
        return;
    if (G->has_pend && G->pend_pc == G->len) {
        G->pend_line = (uint32_t)line;
        return;
    }
    flush_line(G);
    G->pend_pc = G->len;
    G->pend_line = (uint32_t)line;
    G->has_pend = 1;
}

#define KHDR ((uint32_t)((sizeof(ms_pool) + 7) & ~(size_t)7))
#define KSIZE(len) ((uint32_t)((offsetof(ms_str, data) + (len) + 1 + 7) & ~(size_t)7))

static uint32_t const_str(cg *G, const char *s)
{
    size_t n = strlen(s);
    uint32_t h = ms_hash(s, n);
    for (uint32_t i = 0; i < G->nconsts; i++) {
        const ms_str *k = (const ms_str *)(G->kbuf + G->koff[i]);
        if (k->hash == h && k->len == n && memcmp(k->data, s, n) == 0)
            return i;
    }
    if (n > 0xFFFFFF)
        perr(G->P, 0, "string constant too long");
    if (G->nconsts > 0xFFFF)
        perr(G->P, 0, "more than 65536 string constants");
    uint32_t need = (G->klen ? G->klen : KHDR) + KSIZE(n);
    if (need > G->kcap) {
        uint32_t nc = G->kcap ? G->kcap + G->kcap / 2 : 1024;
        if (nc < need)
            nc = need;
        char *nb = (char *)ms_realloc(G->vm, G->kbuf, nc);
        if (!nb)
            perr(G->P, 0, "out of memory");
        G->kbuf = nb;
        G->kcap = nc;
    }
    if (!G->klen) {
        memset(G->kbuf, 0, KHDR);
        G->klen = KHDR;
    }
    GROW(G->koff, G->nconsts, G->cconsts, uint32_t);
    ms_str *k = (ms_str *)(G->kbuf + G->klen);
    k->rc = MS_RC_PINNED;
    k->len = (uint32_t)n;
    k->hash = h;
    memcpy(k->data, s, n + 1);
    G->koff[G->nconsts] = G->klen;
    G->klen += KSIZE(n);
    return G->nconsts++;
}

static uint32_t name_id(cg *G, const char *s, int lower)
{
    uint32_t id = lower ? ms_intern_lower(G->vm, s, strlen(s)) : ms_intern(G->vm, s, strlen(s));
    if (id == MS_NAME_NONE)
        perr(G->P, 0, "out of memory");
    if (id > 0xFFFF)
        perr(G->P, 0, "more than 65536 names");
    return id;
}

static void gen_expr(cg *G, node *n);
static void gen_stmt(cg *G, node *n);

static int scope_is_table(int sc)
{
    return sc == SC_LOCAL || sc == SC_LEVEL || sc == SC_GAME || sc == SC_PARM || sc == SC_GROUP;
}

static void gen_args(cg *G, node *a)
{
    for (; a; a = a->next)
        gen_expr(G, a);
}

static void gen_call(cg *G, node *n, int keep)
{
    if (n->kind == N_CMD) {
        gen_args(G, n->a);
        e8(G, OP_CMD);
    } else {
        gen_expr(G, n->a);
        gen_args(G, n->b);
        e8(G, OP_METHOD);
    }
    if (n->i > 255)
        perr(G->P, n->line, "too many parameters");
    e16(G, name_id(G, n->s, 1));
    e8(G, n->i);
    e8(G, keep);
}

static void gen_bool(cg *G, node *n)
{
    /* a && b / a || b produce 0 or 1 */
    uint32_t j1, j2, jend;
    int and_ = n->kind == N_AND;
    gen_expr(G, n->a);
    j1 = emit_jump(G, and_ ? OP_JF : OP_JT);
    gen_expr(G, n->b);
    j2 = emit_jump(G, and_ ? OP_JF : OP_JT);
    e8(G, OP_PUSH_I8);
    e8(G, and_ ? 1 : 0);
    jend = emit_jump(G, OP_JMP);
    patch_to(G, j1, G->len);
    patch_to(G, j2, G->len);
    e8(G, OP_PUSH_I8);
    e8(G, and_ ? 0 : 1);
    patch_to(G, jend, G->len);
}

static void gen_expr(cg *G, node *n)
{
    switch (n->kind) {
    case N_INT:
        if (n->i >= -128 && n->i <= 127) {
            e8(G, OP_PUSH_I8);
            e8(G, n->i);
        } else {
            e8(G, OP_PUSH_INT);
            e32(G, (uint32_t)n->i);
        }
        break;
    case N_FLOAT: e8(G, OP_PUSH_FLOAT); ef(G, n->f); break;
    case N_STR: e8(G, OP_PUSH_STR); e16(G, (int)const_str(G, n->s)); break;
    case N_NIL: e8(G, OP_PUSH_NIL); break;
    case N_NULL: e8(G, OP_PUSH_NULL); break;
    case N_SCOPE: e8(G, OP_PUSH_LISTENER); e8(G, n->op); break;
    case N_FIELD:
        if (n->a->kind == N_SCOPE && scope_is_table(n->a->op)) {
            e8(G, OP_LOAD_SCOPE);
            e8(G, n->a->op);
            e16(G, name_id(G, n->s, 0));
        } else {
            gen_expr(G, n->a);
            e8(G, OP_GET_FIELD);
            e16(G, name_id(G, n->s, 0));
        }
        break;
    case N_INDEX: gen_expr(G, n->a); gen_expr(G, n->b); e8(G, OP_GET_INDEX); break;
    case N_SIZE: gen_expr(G, n->a); e8(G, OP_SIZE); break;
    case N_TARGET: gen_expr(G, n->a); e8(G, OP_TARGETNAME); break;
    case N_VEC: gen_expr(G, n->a); gen_expr(G, n->b); gen_expr(G, n->c); e8(G, OP_MAKE_VEC); break;
    case N_BIN: gen_expr(G, n->a); gen_expr(G, n->b); e8(G, OP_BIN); e8(G, n->op); break;
    case N_UN: gen_expr(G, n->a); e8(G, OP_UN); e8(G, n->op); break;
    case N_NOT: gen_expr(G, n->a); e8(G, OP_NOT); break;
    case N_AND:
    case N_OR: gen_bool(G, n); break;
    case N_TERN: {
        gen_expr(G, n->a);
        uint32_t jf = emit_jump(G, OP_JF);
        gen_expr(G, n->b);
        uint32_t je = emit_jump(G, OP_JMP);
        patch_to(G, jf, G->len);
        gen_expr(G, n->c);
        patch_to(G, je, G->len);
        break;
    }
    case N_CARR:
        gen_args(G, n->a);
        e8(G, OP_MAKE_CARR);
        e16(G, n->i);
        break;
    case N_MAKEARRAY:
        for (node *row = n->a; row; row = row->next) {
            gen_args(G, row->a);
            if (row->i > 1) {
                e8(G, OP_MAKE_CARR);
                e16(G, row->i);
            }
        }
        e8(G, OP_MAKE_ARRAY);
        e16(G, n->i);
        break;
    case N_CMD:
    case N_METHOD: gen_call(G, n, 1); break;
    default: perr(G->P, n->line, "not an expression");
    }
}

/* value to store is already on the stack */
static void gen_store(cg *G, node *lv)
{
    if (lv->kind == N_FIELD) {
        if (lv->a->kind == N_SCOPE && scope_is_table(lv->a->op)) {
            e8(G, OP_STORE_SCOPE);
            e8(G, lv->a->op);
            e16(G, name_id(G, lv->s, 0));
        } else {
            gen_expr(G, lv->a);
            e8(G, OP_SET_FIELD);
            e16(G, name_id(G, lv->s, 0));
        }
        return;
    }
    if (lv->kind == N_INDEX) {
        node *idx[32];
        int n = 0;
        node *b = lv;
        while (b->kind == N_INDEX) {
            if (n == 32)
                perr(G->P, lv->line, "array nesting too deep");
            idx[n++] = b->b;
            b = b->a;
        }
        if (b->kind != N_FIELD)
            perr(G->P, lv->line, "cannot assign to this array");
        int table = b->a->kind == N_SCOPE && scope_is_table(b->a->op);
        if (!table)
            gen_expr(G, b->a);
        for (int k = n - 1; k >= 0; k--)
            gen_expr(G, idx[k]);
        e8(G, OP_STORE_INDEXED);
        e8(G, table ? IB_SCOPE : IB_FIELD);
        e8(G, table ? b->a->op : 0);
        e16(G, name_id(G, b->s, 0));
        e8(G, n);
        return;
    }
    perr(G->P, lv->line, "left side of assignment is not a variable");
}

static ctl *find_ctl(cg *G, int want_loop)
{
    for (ctl *c = G->ctl; c; c = c->up)
        if (!want_loop || c->kind == 0)
            return c;
    return NULL;
}

static void add_label(cg *G, node *n, uint32_t entry, uint32_t body)
{
    uint32_t id = name_id(G, n->s, 1);
    for (uint32_t i = 0; i < G->nlabels; i++)
        if (G->labels[i].name == id)
            perr(G->P, n->line, "duplicate label '%s'", n->s);
    GROW(G->labels, G->nlabels, G->clabels, ms_label);
    G->labels[G->nlabels].name = id;
    G->labels[G->nlabels].entry = entry;
    G->labels[G->nlabels].body = body;
    G->nlabels++;
}

static ctl *innermost_switch(cg *G)
{
    for (ctl *c = G->ctl; c; c = c->up)
        if (c->kind == 1)
            return c;
    return NULL;
}

static ms_value case_value(cg *G, node *v)
{
    ms_value out = ms_nil();
    if (v->kind == N_INT)
        return ms_int(v->i);
    if (v->kind == N_FLOAT)
        return ms_float(v->f);
    if (v->kind == N_UN && v->op == UO_NEG && v->a->kind == N_INT)
        return ms_int(-v->a->i);
    if (v->kind == N_UN && v->op == UO_NEG && v->a->kind == N_FLOAT)
        return ms_float(-v->a->f);
    if (v->kind == N_STR) {
        out = ms_string(G->vm, v->s);
        if (out.type != MS_STRING)
            perr(G->P, v->line, "out of memory");
        return out;
    }
    perr(G->P, v->line, "case label must be a constant");
    return out;
}

static void add_case(cg *G, ms_switch *S, ms_value key, int line)
{
    if (key.type == MS_NIL)
        perr(G->P, line, "out of memory");
    ms_case *nc = (ms_case *)ms_realloc(G->vm, S->cases, sizeof(ms_case) * (S->n + 1));
    if (!nc) {
        ms_release(G->vm, key);
        perr(G->P, line, "out of memory");
    }
    S->cases = nc;
    S->cases[S->n].key = key;
    S->cases[S->n].pc = G->len;
    S->n++;
}

static void gen_stmt(cg *G, node *n)
{
    if (!n)
        return;
    mark_line(G, n->line);
    switch (n->kind) {
    case N_NOP: break;
    case N_BLOCK:
        for (node *s = n->a; s; s = s->next)
            gen_stmt(G, s);
        break;
    case N_LABEL: {
        ctl *sw = innermost_switch(G);
        if (sw && n->i == 0) {
            /* inside a switch, "name:" is a case label ("pistol:"), scoped to that switch */
            if (strcmp(n->s, "default") == 0) {
                G->sw[sw->sw].def = G->len;
                break;
            }
            add_case(G, &G->sw[sw->sw], ms_string(G->vm, n->s), n->line);
            break;
        }
        if (n->i == 0) {
            add_label(G, n, G->len, G->len);
            break;
        }
        uint32_t skip = emit_jump(G, OP_JMP);
        uint32_t entry = G->len;
        int k = 0;
        for (node *p = n->a; p; p = p->next, k++) {
            e8(G, OP_ARG);
            e8(G, k);
            gen_store(G, p);
        }
        patch_to(G, skip, G->len);
        add_label(G, n, entry, G->len);
        break;
    }
    case N_CASE: {
        ctl *sw = innermost_switch(G);
        if (!sw)
            perr(G->P, n->line, "case outside of switch");
        add_case(G, &G->sw[sw->sw], case_value(G, n->a), n->line);
        break;
    }
    case N_IF: {
        gen_expr(G, n->a);
        uint32_t jf = emit_jump(G, OP_JF);
        gen_stmt(G, n->b);
        if (n->c) {
            uint32_t je = emit_jump(G, OP_JMP);
            patch_to(G, jf, G->len);
            gen_stmt(G, n->c);
            patch_to(G, je, G->len);
        } else
            patch_to(G, jf, G->len);
        break;
    }
    case N_WHILE: {
        ctl c;
        memset(&c, 0, sizeof(c));
        c.kind = 0;
        c.up = G->ctl;
        uint32_t top = G->len;
        gen_expr(G, n->a);
        uint32_t jf = emit_jump(G, OP_JF);
        G->ctl = &c;
        gen_stmt(G, n->b);
        G->ctl = c.up;
        patch_all(G, &c.conts, G->len);
        gen_stmt(G, n->c);
        emit_jump_to(G, OP_JMP, top);
        patch_to(G, jf, G->len);
        patch_all(G, &c.breaks, G->len);
        break;
    }
    case N_DO: {
        ctl c;
        memset(&c, 0, sizeof(c));
        c.up = G->ctl;
        uint32_t top = G->len;
        G->ctl = &c;
        gen_stmt(G, n->a);
        G->ctl = c.up;
        patch_all(G, &c.conts, G->len);
        gen_expr(G, n->b);
        emit_jump_to(G, OP_JT, top);
        patch_all(G, &c.breaks, G->len);
        break;
    }
    case N_SWITCH: {
        ctl c;
        memset(&c, 0, sizeof(c));
        c.kind = 1;
        c.up = G->ctl;
        GROW(G->sw, G->nsw, G->csw, ms_switch);
        c.sw = (int)G->nsw;
        memset(&G->sw[G->nsw], 0, sizeof(ms_switch));
        G->sw[G->nsw].def = MS_NAME_NONE;
        G->nsw++;
        gen_expr(G, n->a);
        e8(G, OP_SWITCH);
        e32(G, (uint32_t)c.sw);
        G->ctl = &c;
        gen_stmt(G, n->b);
        G->ctl = c.up;
        if (G->sw[c.sw].def == MS_NAME_NONE)
            G->sw[c.sw].def = G->len;
        patch_all(G, &c.breaks, G->len);
        break;
    }
    case N_TRY: {
        uint32_t jc = emit_jump(G, OP_TRY);
        gen_stmt(G, n->a);
        e8(G, OP_ENDTRY);
        uint32_t je = emit_jump(G, OP_JMP);
        patch_to(G, jc, G->len);
        gen_stmt(G, n->b);
        patch_to(G, je, G->len);
        break;
    }
    case N_BREAK: {
        ctl *c = find_ctl(G, 0);
        if (!c)
            perr(G->P, n->line, "break outside of loop or switch");
        patch_add(G, &c->breaks, emit_jump(G, OP_JMP));
        break;
    }
    case N_CONTINUE: {
        ctl *c = find_ctl(G, 1);
        if (!c)
            perr(G->P, n->line, "continue outside of loop");
        patch_add(G, &c->conts, emit_jump(G, OP_JMP));
        break;
    }
    case N_ASSIGN:
        gen_expr(G, n->b);
        gen_store(G, n->a);
        break;
    case N_CMD:
    case N_METHOD:
        gen_call(G, n, 0);
        break;
    default:
        perr(G->P, n->line, "bad statement");
    }
}

/* ================================================================== */
/* entry points                                                         */
/* ================================================================== */

static void script_free(ms_vm *vm, ms_script *s, int keep_pool)
{
    if (!s)
        return;
    for (uint32_t i = 0; i < s->nswitches; i++) {
        for (uint32_t k = 0; k < s->switches[i].n; k++)
            ms_release(vm, s->switches[i].cases[k].key);
        ms_free(vm, s->switches[i].cases);
    }
    if (keep_pool && s->pool) {
        s->pool->next = vm->retired;
        vm->retired = s->pool;
    } else
        ms_free(vm, s->pool);
    ms_free(vm, s->consts);
    ms_free(vm, s->switches);
    ms_free(vm, s->labels);
    ms_free(vm, s->lines);
    ms_free(vm, s->code);
    ms_strfree(vm, s->path);
    ms_free(vm, s);
}

void ms_script_free(ms_vm *vm, ms_script *s) { script_free(vm, s, 0); }
void ms_script_retire(ms_vm *vm, ms_script *s) { script_free(vm, s, 1); }

/* The constant buffer becomes the script's pool: trim it, point a table into it. */
static void pool_consts(cg *G)
{
    if (!G->nconsts)
        return;
    char *nb = (char *)ms_realloc(G->vm, G->kbuf, G->klen);
    if (nb)
        G->kbuf = nb;
    G->ktab = (ms_str **)ms_alloc(G->vm, sizeof(ms_str *) * G->nconsts);
    if (!G->ktab)
        perr(G->P, 0, "out of memory");
    for (uint32_t i = 0; i < G->nconsts; i++)
        G->ktab[i] = (ms_str *)(G->kbuf + G->koff[i]);
    ms_free(G->vm, G->koff);
    G->koff = NULL;
    G->kpool = (ms_pool *)G->kbuf;
    G->kbuf = NULL;
}

ms_script *ms_compile(ms_vm *vm, const char *path, const char *text, size_t len, char *err, size_t errlen)
{
    parser *P = (parser *)ms_alloc(vm, sizeof(parser));
    cg *G = (cg *)ms_alloc(vm, sizeof(cg));
    ms_script *S = NULL;
    if (!P || !G) {
        snprintf(err, errlen, "%s: out of memory", path);
        ms_free(vm, P);
        ms_free(vm, G);
        return NULL;
    }
    P->vm = vm;
    P->ar.vm = vm;
    P->lx.src = P->lx.p = text;
    P->lx.end = text + len;
    P->lx.line = 1;
    P->lx.prev_type = T_EOL;
    P->err = err;
    P->errlen = errlen;
    P->path = path;
    G->P = P;
    G->vm = vm;

    if (setjmp(P->jb)) {
        for (ctl *c = G->ctl; c; c = c->up) {
            ms_free(vm, c->breaks.at);
            ms_free(vm, c->conts.at);
        }
        ms_free(vm, G->kbuf);
        ms_free(vm, G->koff);
        ms_free(vm, G->kpool);
        ms_free(vm, G->ktab);
        for (uint32_t i = 0; i < G->nsw; i++) {
            for (uint32_t k = 0; k < G->sw[i].n; k++)
                ms_release(vm, G->sw[i].cases[k].key);
            ms_free(vm, G->sw[i].cases);
        }
        ms_free(vm, G->sw);
        ms_free(vm, G->labels);
        ms_free(vm, G->lines);
        ms_free(vm, G->code);
        arena_free(&P->ar);
        ms_free(vm, P);
        ms_free(vm, G);
        return NULL;
    }

    node *st;
    while ((st = parse_stmt(P)) != NULL) {
        gen_stmt(G, st);
        parser_reset(P);
    }
    e8(G, OP_END);
    flush_line(G);
    /* growth leaves slack in the buffers; give the slack back (matters at GameCube sizes) */
    {
        uint8_t *c = (uint8_t *)ms_realloc(vm, G->code, G->len);
        if (c)
            G->code = c;
        if (G->nlines) {
            uint8_t *l = (uint8_t *)ms_realloc(vm, G->lines, G->nlines);
            if (l)
                G->lines = l;
        }
        if (G->nlabels) {
            ms_label *lb = (ms_label *)ms_realloc(vm, G->labels, sizeof(ms_label) * G->nlabels);
            if (lb)
                G->labels = lb;
        }
    }
    pool_consts(G);

    S = (ms_script *)ms_alloc(vm, sizeof(ms_script));
    if (!S)
        perr(P, 0, "out of memory");
    memset(S, 0, sizeof(*S));
    S->path = ms_strdup(vm, path);
    S->code = G->code;
    S->code_len = G->len;
    S->consts = G->ktab;
    S->nconsts = G->nconsts;
    S->pool = G->kpool;
    S->labels = G->labels;
    S->nlabels = G->nlabels;
    S->switches = G->sw;
    S->nswitches = G->nsw;
    S->lines = G->lines;
    S->lines_len = G->nlines;
    arena_free(&P->ar);
    ms_free(vm, P);
    ms_free(vm, G);
    return S;
}

const ms_label *ms_find_label(const ms_script *s, uint32_t name)
{
    for (uint32_t i = 0; i < s->nlabels; i++)
        if (s->labels[i].name == name)
            return &s->labels[i];
    return NULL;
}

static uint32_t read_varint(const uint8_t **p, const uint8_t *end)
{
    uint32_t v = 0;
    for (int sh = 0; *p < end && sh < 35; sh += 7) {
        uint8_t b = *(*p)++;
        v |= (uint32_t)(b & 0x7F) << sh;
        if (!(b & 0x80))
            break;
    }
    return v;
}

uint32_t ms_line_for_pc(const ms_script *s, uint32_t pc)
{
    const uint8_t *p = s->lines, *end = s->lines + s->lines_len;
    uint32_t at = 0, line = 0, cur = 0;
    while (p < end) {
        at += read_varint(&p, end);
        uint32_t z = read_varint(&p, end);
        if (at > pc)
            break;
        line += (z & 1) ? (uint32_t)(-(int32_t)(z >> 1)) : z >> 1;
        cur = line;
    }
    return cur;
}
