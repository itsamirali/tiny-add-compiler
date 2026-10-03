/*
 * test_lexer.c - Tests for stage [1], the lexer, and for diagnostics
 * rendering of lexer errors.
 */
#include <string.h>

#include "lexer.h"
#include "test.h"
#include "trace.h"

/* Lex a '\0'-terminated string. */
static bool lex(Arena *a, const char *src, TokenList *out, Diag *d) {
    memset(d, 0, sizeof *d);
    return lexer_run(src, strlen(src), a, out, d);
}

static void check_token(const Token *t, TokenKind kind, const char *lexeme,
                        int line, int col) {
    CHECK_EQ_STR(token_kind_name(t->kind), token_kind_name(kind));
    CHECK_EQ_STR(t->lexeme, lexeme);
    CHECK_EQ_INT(t->loc.line, line);
    CHECK_EQ_INT(t->loc.col, col);
}

static void test_spec_example(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "1 + 2 + 3", &tl, &d));
    CHECK_EQ_INT(tl.count, 6);
    if (tl.count == 6) {
        check_token(&tl.tokens[0], TOK_NUMBER, "1", 1, 1);
        check_token(&tl.tokens[1], TOK_PLUS, "+", 1, 3);
        check_token(&tl.tokens[2], TOK_NUMBER, "2", 1, 5);
        check_token(&tl.tokens[3], TOK_PLUS, "+", 1, 7);
        check_token(&tl.tokens[4], TOK_NUMBER, "3", 1, 9);
        check_token(&tl.tokens[5], TOK_EOF, "", 1, 10);
    }
    arena_free(&a);
}

static void test_single_number(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "42", &tl, &d));
    CHECK_EQ_INT(tl.count, 2);
    if (tl.count == 2) {
        check_token(&tl.tokens[0], TOK_NUMBER, "42", 1, 1);
        check_token(&tl.tokens[1], TOK_EOF, "", 1, 3);
    }
    arena_free(&a);
}

static void test_no_spaces(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "10+20", &tl, &d));
    CHECK_EQ_INT(tl.count, 4);
    if (tl.count == 4) {
        check_token(&tl.tokens[0], TOK_NUMBER, "10", 1, 1);
        check_token(&tl.tokens[1], TOK_PLUS, "+", 1, 3);
        check_token(&tl.tokens[2], TOK_NUMBER, "20", 1, 4);
    }
    arena_free(&a);
}

static void test_extra_whitespace_and_lines(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    /* a tab counts as one column; only '\n' starts a new line */
    CHECK(lex(&a, "  10 +\n\t20  \n\n", &tl, &d));
    CHECK_EQ_INT(tl.count, 4);
    if (tl.count == 4) {
        check_token(&tl.tokens[0], TOK_NUMBER, "10", 1, 3);
        check_token(&tl.tokens[1], TOK_PLUS, "+", 1, 6);
        check_token(&tl.tokens[2], TOK_NUMBER, "20", 2, 2);
        /* EOF right after the last real token, not at the end of file */
        check_token(&tl.tokens[3], TOK_EOF, "", 2, 4);
    }
    arena_free(&a);
}

static void test_crlf_line_endings(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "1 +\r\n2\r\n", &tl, &d));
    CHECK_EQ_INT(tl.count, 4);
    if (tl.count == 4) {
        check_token(&tl.tokens[2], TOK_NUMBER, "2", 2, 1);
        check_token(&tl.tokens[3], TOK_EOF, "", 2, 2);
    }
    arena_free(&a);
}

static void test_empty_and_blank_input(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "", &tl, &d));
    CHECK_EQ_INT(tl.count, 1);
    if (tl.count == 1) {
        check_token(&tl.tokens[0], TOK_EOF, "", 1, 1);
    }
    CHECK(lex(&a, "   \n\t \n", &tl, &d));
    CHECK_EQ_INT(tl.count, 1);
    if (tl.count == 1) {
        check_token(&tl.tokens[0], TOK_EOF, "", 1, 1);
    }
    arena_free(&a);
}

static void test_eof_after_trailing_plus(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "1 +\n", &tl, &d));
    CHECK_EQ_INT(tl.count, 3);
    if (tl.count == 3) {
        check_token(&tl.tokens[2], TOK_EOF, "", 1, 4);
    }
    arena_free(&a);
}

static void test_zero_leading_zeros_and_long_numbers(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    /* The lexer accepts any digits; the semantic stage judges values. */
    CHECK(lex(&a, "0 + 007 + 99999999999999999999999", &tl, &d));
    CHECK_EQ_INT(tl.count, 6);
    if (tl.count == 6) {
        check_token(&tl.tokens[0], TOK_NUMBER, "0", 1, 1);
        check_token(&tl.tokens[2], TOK_NUMBER, "007", 1, 5);
        check_token(&tl.tokens[4], TOK_NUMBER, "99999999999999999999999", 1, 11);
    }
    arena_free(&a);
}

static void test_many_tokens(void) {
    Arena a;
    arena_init(&a);
    /* 1000 numbers: "1+1+...+1" forces the token array to grow */
    char src[2000];
    size_t n = 0;
    for (int i = 0; i < 1000; i++) {
        if (i > 0) {
            src[n++] = '+';
        }
        src[n++] = '1';
    }
    src[n] = '\0';
    TokenList tl;
    Diag d;
    CHECK(lex(&a, src, &tl, &d));
    CHECK_EQ_INT(tl.count, 2000);
    if (tl.count == 2000) {
        check_token(&tl.tokens[1998], TOK_NUMBER, "1", 1, 1999);
        check_token(&tl.tokens[1999], TOK_EOF, "", 1, 2000);
    }
    arena_free(&a);
}

static void test_unknown_character(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(!lex(&a, "1 + 2 - 3", &tl, &d));
    CHECK(d.is_set);
    CHECK_EQ_STR(diag_stage_name(d.stage), "lexer");
    CHECK_EQ_STR(d.message, "unexpected character '-'");
    CHECK_EQ_INT(d.loc.line, 1);
    CHECK_EQ_INT(d.loc.col, 7);
    CHECK_EQ_INT(diag_exit_code(&d), 1);
    /* tokens read so far, ending with the INVALID token */
    CHECK_EQ_INT(tl.count, 4);
    if (tl.count == 4) {
        check_token(&tl.tokens[2], TOK_NUMBER, "2", 1, 5);
        check_token(&tl.tokens[3], TOK_INVALID, "-", 1, 7);
    }
    arena_free(&a);
}

static void test_error_on_second_line(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(!lex(&a, "1 +\n  x", &tl, &d));
    CHECK_EQ_STR(d.message, "unexpected character 'x'");
    CHECK_EQ_INT(d.loc.line, 2);
    CHECK_EQ_INT(d.loc.col, 3);
    arena_free(&a);
}

static void test_control_character(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(!lex(&a, "1\x01", &tl, &d));
    CHECK_EQ_STR(d.message, "unexpected character '\\x01'");
    CHECK_EQ_INT(d.loc.col, 2);
    arena_free(&a);
}

static void test_nul_byte_inside_source(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    memset(&d, 0, sizeof d);
    const char src[] = {'1', '\0', '2'};
    CHECK(!lexer_run(src, sizeof src, &a, &tl, &d));
    CHECK_EQ_STR(d.message, "unexpected character '\\x00'");
    CHECK_EQ_INT(d.loc.col, 2);
    arena_free(&a);
}

static void test_persian_digit(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    /* U+06F1 EXTENDED ARABIC-INDIC DIGIT ONE, UTF-8 bytes DB B1 */
    CHECK(!lex(&a, "1 + \xDB\xB1", &tl, &d));
    CHECK_EQ_STR(d.message, "unexpected character '\xDB\xB1' (U+06F1)");
    CHECK_EQ_INT(d.loc.col, 5);
    if (tl.count == 3) {
        CHECK_EQ_STR(tl.tokens[2].lexeme, "\xDB\xB1");
        CHECK_EQ_INT(tl.tokens[2].length, 2);
    }
    arena_free(&a);
}

static void test_invalid_utf8(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(!lex(&a, "\xFF", &tl, &d));
    CHECK_EQ_STR(d.message, "unexpected byte '\\xFF' (not valid UTF-8)");
    CHECK(!lex(&a, "\xDB", &tl, &d)); /* truncated sequence */
    CHECK_EQ_STR(d.message, "unexpected byte '\\xDB' (not valid UTF-8)");
    arena_free(&a);
}

static void test_render_lexer_error(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    const char *src = "1 + 2 - 3";
    CHECK(!lex(&a, src, &tl, &d));
    Out o = out_to_memory();
    TraceStyle plain = {false, false};
    trace_diag(&o, plain, &d, src, strlen(src));
    CHECK_EQ_STR(out_text(&o),
                 "error[lexer]: unexpected character '-'\n"
                 "  --> 1:7\n"
                 "   |\n"
                 " 1 | 1 + 2 - 3\n"
                 "   |       ^\n");
    out_free(&o);
    arena_free(&a);
}

static void test_render_keeps_tabs_aligned(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    const char *src = "\t1 +\t$";
    CHECK(!lex(&a, src, &tl, &d));
    Out o = out_to_memory();
    TraceStyle plain = {false, false};
    trace_diag(&o, plain, &d, src, strlen(src));
    CHECK_EQ_STR(out_text(&o),
                 "error[lexer]: unexpected character '$'\n"
                 "  --> 1:6\n"
                 "   |\n"
                 " 1 | \t1 +\t$\n"
                 "   | \t   \t^\n");
    out_free(&o);
    arena_free(&a);
}

static void test_token_table(void) {
    Arena a;
    arena_init(&a);
    TokenList tl;
    Diag d;
    CHECK(lex(&a, "1 + 23", &tl, &d));
    Out o = out_to_memory();
    Trace t;
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_tokens(&t, &tl);
    CHECK_EQ_STR(out_text(&o),
                 "=== [1] Lexer ===\n"
                 " #  KIND     LEXEME  LINE:COL\n"
                 " 0  NUMBER   1       1:1\n"
                 " 1  PLUS     +       1:3\n"
                 " 2  NUMBER   23      1:5\n"
                 " 3  EOF              1:7\n");
    out_free(&o);
    arena_free(&a);
}

void suite_lexer(void) {
    tr_begin_suite("lexer");
    RUN_TEST(test_spec_example);
    RUN_TEST(test_single_number);
    RUN_TEST(test_no_spaces);
    RUN_TEST(test_extra_whitespace_and_lines);
    RUN_TEST(test_crlf_line_endings);
    RUN_TEST(test_empty_and_blank_input);
    RUN_TEST(test_eof_after_trailing_plus);
    RUN_TEST(test_zero_leading_zeros_and_long_numbers);
    RUN_TEST(test_many_tokens);
    RUN_TEST(test_unknown_character);
    RUN_TEST(test_error_on_second_line);
    RUN_TEST(test_control_character);
    RUN_TEST(test_nul_byte_inside_source);
    RUN_TEST(test_persian_digit);
    RUN_TEST(test_invalid_utf8);
    RUN_TEST(test_render_lexer_error);
    RUN_TEST(test_render_keeps_tabs_aligned);
    RUN_TEST(test_token_table);
}
