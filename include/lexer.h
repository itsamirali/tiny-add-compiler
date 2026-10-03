/*
 * lexer.h - Stage [1]: lexical analysis.
 *
 * The lexer reads the program text character by character and groups the
 * characters into tokens (see token.h). Spaces, tabs, '\r' and newlines are
 * skipped, but the lexer keeps counting lines and columns so every token
 * knows where it came from (a tab counts as one column; only '\n' starts a
 * new line). The first unknown character stops the lexer with an error
 * that points at its line and column.
 *
 * Input:  the source text (any bytes; it does not need to end with '\0').
 * Output: an array of tokens. On success it ends with an EOF token, which
 *         is placed right after the last real token (1:1 for empty input),
 *         so "expected ..., found EOF" errors point at a visible place.
 *         On error it holds the tokens read so far and ends with the
 *         INVALID token.
 */
#ifndef TINYADD_LEXER_H
#define TINYADD_LEXER_H

#include "arena.h"
#include "common.h"
#include "diag.h"
#include "token.h"

typedef struct {
    Token *tokens; /* owned by the arena */
    size_t count;  /* includes the final EOF / INVALID token */
} TokenList;

/* Split `source` (exactly `length` bytes) into tokens.
 * Returns true on success. On failure returns false, fills *diag, and *out
 * still describes the tokens read so far (possibly zero of them). */
bool lexer_run(const char *source, size_t length, Arena *arena, TokenList *out,
               Diag *diag);

#endif /* TINYADD_LEXER_H */
