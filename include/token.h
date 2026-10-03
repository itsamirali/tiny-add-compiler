/*
 * token.h - The token type produced by the lexer.
 *
 * A token is the smallest meaningful piece of a TinyAdd program. There are
 * only four kinds: NUMBER (a run of decimal digits), PLUS ('+'), EOF (end of
 * input) and INVALID (a character the language does not know). Every token
 * remembers its kind, its text (lexeme) and where it starts in the source
 * (line and column), so later stages can point at it in errors.
 *
 * NUMBER tokens keep their digits as text; turning them into numbers (and
 * rejecting 0 or too-large values) is the job of the semantic stage.
 */
#ifndef TINYADD_TOKEN_H
#define TINYADD_TOKEN_H

#include "common.h"

typedef enum {
    TOK_NUMBER,  /* 1, 42, 007 ...                    */
    TOK_PLUS,    /* +                                 */
    TOK_EOF,     /* end of input (always the last one) */
    TOK_INVALID  /* an unknown character; stops lexing */
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *lexeme; /* '\0'-terminated copy owned by the arena. For EOF
                           it is "". For INVALID it is a printable form of
                           the bad character, e.g. "-", "\x01" or "۱".   */
    size_t      length; /* number of source bytes the token covers */
    SrcLoc      loc;    /* where the token starts */
} Token;

/* "NUMBER", "PLUS", "EOF" or "INVALID". Implemented in lexer.c. */
const char *token_kind_name(TokenKind kind);

#endif /* TINYADD_TOKEN_H */
