/*
 * parser.c - Stage [2]: recursive descent parser (see parser.h).
 *
 * Grammar, and the function that implements each rule:
 *
 *     program := expr EOF                 -> parse_program()
 *     expr    := NUMBER ( '+' NUMBER )*   -> parse_expr()
 *     NUMBER                              -> parse_number()
 */
#include "parser.h"

#include <stdio.h>

typedef struct {
    const Token *tokens;
    size_t       count;
    size_t       pos; /* index of the current (not yet consumed) token */
    Arena       *arena;
    Diag        *diag;
} Parser;

static const Token *peek(const Parser *p) {
    return &p->tokens[p->pos];
}

static const Token *advance(Parser *p) {
    const Token *t = &p->tokens[p->pos];
    if (t->kind != TOK_EOF) {
        p->pos++; /* never move past EOF */
    }
    return t;
}

/* Describe a token for "found ..." in error messages. */
static void describe(const Token *t, char *buf, size_t size) {
    switch (t->kind) {
    case TOK_NUMBER:  snprintf(buf, size, "NUMBER '%.40s%s'", t->lexeme,
                               t->length > 40 ? "..." : ""); break;
    case TOK_PLUS:    snprintf(buf, size, "'+'"); break;
    case TOK_EOF:     snprintf(buf, size, "EOF"); break;
    case TOK_INVALID: snprintf(buf, size, "invalid character '%s'", t->lexeme); break;
    }
}

/* NUMBER. `context` is added to the message, e.g. " after '+'". */
static AstNode *parse_number(Parser *p, const char *context) {
    const Token *t = peek(p);
    if (t->kind != TOK_NUMBER) {
        char found[80];
        describe(t, found, sizeof found);
        diag_set(p->diag, DIAG_PARSER, t->loc, "expected NUMBER%s, found %s",
                 context, found);
        return NULL;
    }
    advance(p);
    AstNode *n = ast_new_num(p->arena, t);
    if (n == NULL) {
        diag_out_of_memory(p->diag);
    }
    return n;
}

/* expr := NUMBER ( '+' NUMBER )*
 * The loop turns "1 + 2 + 3" into Add(Add(1, 2), 3): left-associative. */
static AstNode *parse_expr(Parser *p) {
    AstNode *left = parse_number(p, "");
    if (left == NULL) {
        return NULL;
    }
    while (peek(p)->kind == TOK_PLUS) {
        const Token *plus = advance(p);
        AstNode *right = parse_number(p, " after '+'");
        if (right == NULL) {
            return NULL;
        }
        left = ast_new_add(p->arena, left, right, plus->loc);
        if (left == NULL) {
            diag_out_of_memory(p->diag);
            return NULL;
        }
    }
    return left;
}

/* program := expr EOF */
static AstNode *parse_program(Parser *p) {
    AstNode *root = parse_expr(p);
    if (root == NULL) {
        return NULL;
    }
    const Token *t = peek(p);
    if (t->kind != TOK_EOF) {
        char found[80];
        describe(t, found, sizeof found);
        diag_set(p->diag, DIAG_PARSER, t->loc, "expected '+' or EOF, found %s",
                 found);
        return NULL;
    }
    return root;
}

bool parser_run(const TokenList *tokens, Arena *arena, AstNode **out_root,
                Diag *diag) {
    *out_root = NULL;
    if (tokens->count == 0 || tokens->tokens[tokens->count - 1].kind != TOK_EOF) {
        SrcLoc none = {0, 0};
        diag_set(diag, DIAG_INTERNAL, none, "parser needs a token list ending with EOF");
        return false;
    }
    Parser p = {tokens->tokens, tokens->count, 0, arena, diag};
    *out_root = parse_program(&p);
    return *out_root != NULL;
}
