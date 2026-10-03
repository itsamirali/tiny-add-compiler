/*
 * lexer.c - Stage [1]: lexical analysis (see lexer.h).
 *
 * The lexer is a simple loop over the input bytes:
 *   - space, tab, '\r'  -> skip (one column each)
 *   - '\n'              -> skip, go to the next line
 *   - '0'..'9'          -> read the whole run of digits as one NUMBER
 *   - '+'               -> PLUS
 *   - anything else     -> INVALID token + error, stop
 */
#include "lexer.h"

#include <stdio.h>
#include <string.h>

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
    case TOK_NUMBER:  return "NUMBER";
    case TOK_PLUS:    return "PLUS";
    case TOK_EOF:     return "EOF";
    case TOK_INVALID: return "INVALID";
    }
    return "?";
}

/* All the state of one lexer run. */
typedef struct {
    const char *src;
    size_t      len;
    size_t      pos;      /* index of the next byte to read */
    int         line;     /* current line (1-based)         */
    int         col;      /* current column (1-based)       */
    SrcLoc      end_last; /* position just after the last real token */
    size_t      cap;      /* allocated slots in out->tokens */
    Arena      *arena;
    TokenList  *out;
    Diag       *diag;
} Lexer;

static bool is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

/* Append one token. `text` is copied into the arena as the lexeme. */
static bool push_token(Lexer *lx, TokenKind kind, const char *text,
                       size_t text_len, size_t src_len, SrcLoc loc) {
    TokenList *out = lx->out;
    if (out->count == lx->cap) {
        size_t new_cap = lx->cap == 0 ? 16 : lx->cap * 2;
        if (new_cap > SIZE_MAX / sizeof(Token)) {
            diag_out_of_memory(lx->diag);
            return false;
        }
        Token *bigger = arena_grow(lx->arena, out->tokens,
                                   lx->cap * sizeof(Token),
                                   new_cap * sizeof(Token));
        if (bigger == NULL) {
            diag_out_of_memory(lx->diag);
            return false;
        }
        out->tokens = bigger;
        lx->cap = new_cap;
    }

    const char *lexeme = arena_strndup(lx->arena, text, text_len);
    if (lexeme == NULL) {
        diag_out_of_memory(lx->diag);
        return false;
    }
    Token *t = &out->tokens[out->count++];
    t->kind = kind;
    t->lexeme = lexeme;
    t->length = src_len;
    t->loc = loc;
    return true;
}

/* Decode one multi-byte UTF-8 character starting at `s` (at most `avail`
 * bytes). Returns its length in bytes (2..4) and stores the code point, or
 * returns 0 if the bytes are not valid UTF-8. */
static size_t utf8_decode(const unsigned char *s, size_t avail,
                          unsigned long *code_point) {
    unsigned char c = s[0];
    size_t n;
    unsigned long v;
    unsigned long min;
    if (c >= 0xC2 && c <= 0xDF) {
        n = 2; v = c & 0x1Fu; min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        n = 3; v = c & 0x0Fu; min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        n = 4; v = c & 0x07u; min = 0x10000;
    } else {
        return 0;
    }
    if (avail < n) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        v = (v << 6) | (s[i] & 0x3Fu);
    }
    if (v < min || v > 0x10FFFFul || (v >= 0xD800ul && v <= 0xDFFFul)) {
        return 0; /* overlong form, out of range, or a surrogate */
    }
    *code_point = v;
    return n;
}

/* Report the unknown character at the current position and add it as an
 * INVALID token (so --stage=lex can show where lexing stopped). */
static void bad_character(Lexer *lx) {
    const unsigned char *s = (const unsigned char *)lx->src + lx->pos;
    size_t avail = lx->len - lx->pos;
    SrcLoc loc = {lx->line, lx->col};
    char shown[16];
    size_t src_len = 1;

    if (s[0] >= 0x21 && s[0] <= 0x7E) {
        /* printable ASCII, e.g. '-' or 'a' */
        shown[0] = (char)s[0];
        shown[1] = '\0';
        diag_set(lx->diag, DIAG_LEXER, loc, "unexpected character '%s'", shown);
    } else if (s[0] < 0x80) {
        /* control character such as '\x01' or DEL */
        snprintf(shown, sizeof shown, "\\x%02X", (unsigned)s[0]);
        diag_set(lx->diag, DIAG_LEXER, loc, "unexpected character '%s'", shown);
    } else {
        unsigned long cp = 0;
        size_t n = utf8_decode(s, avail, &cp);
        if (n > 0) {
            /* a real Unicode character, e.g. a Persian digit */
            memcpy(shown, s, n);
            shown[n] = '\0';
            src_len = n;
            diag_set(lx->diag, DIAG_LEXER, loc,
                     "unexpected character '%s' (U+%04lX)", shown, cp);
        } else {
            snprintf(shown, sizeof shown, "\\x%02X", (unsigned)s[0]);
            diag_set(lx->diag, DIAG_LEXER, loc,
                     "unexpected byte '%s' (not valid UTF-8)", shown);
        }
    }
    /* If this fails we are out of memory; the lexer error is kept anyway. */
    (void)push_token(lx, TOK_INVALID, shown, strlen(shown), src_len, loc);
}

bool lexer_run(const char *source, size_t length, Arena *arena, TokenList *out,
               Diag *diag) {
    Lexer lx = {
        .src = source, .len = length, .pos = 0,
        .line = 1, .col = 1, .end_last = {1, 1},
        .cap = 0, .arena = arena, .out = out, .diag = diag,
    };
    out->tokens = NULL;
    out->count = 0;

    while (lx.pos < lx.len) {
        unsigned char c = (unsigned char)lx.src[lx.pos];

        if (c == ' ' || c == '\t' || c == '\r') {
            lx.pos++;
            lx.col++;
            continue;
        }
        if (c == '\n') {
            lx.pos++;
            lx.line++;
            lx.col = 1;
            continue;
        }

        SrcLoc loc = {lx.line, lx.col};

        if (is_digit(c)) {
            size_t start = lx.pos;
            while (lx.pos < lx.len && is_digit((unsigned char)lx.src[lx.pos])) {
                lx.pos++;
            }
            size_t n = lx.pos - start;
            if (!push_token(&lx, TOK_NUMBER, lx.src + start, n, n, loc)) {
                return false;
            }
            lx.col += (int)n;
            lx.end_last = (SrcLoc){lx.line, lx.col};
            continue;
        }

        if (c == '+') {
            if (!push_token(&lx, TOK_PLUS, "+", 1, 1, loc)) {
                return false;
            }
            lx.pos++;
            lx.col++;
            lx.end_last = (SrcLoc){lx.line, lx.col};
            continue;
        }

        bad_character(&lx);
        return false;
    }

    return push_token(&lx, TOK_EOF, "", 0, 0, lx.end_last);
}
