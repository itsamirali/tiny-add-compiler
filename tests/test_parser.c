/*
 * test_parser.c - Tests for stage [2], the parser and the AST (including
 * the printed tree and parser error messages).
 */
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "test.h"
#include "trace.h"

/* Lex + parse a string. Returns the root, or NULL with *d filled. */
static AstNode *parse(Arena *a, const char *src, Diag *d) {
    memset(d, 0, sizeof *d);
    TokenList tl;
    if (!lexer_run(src, strlen(src), a, &tl, d)) {
        return NULL;
    }
    AstNode *root = NULL;
    if (!parser_run(&tl, a, &root, d)) {
        return NULL;
    }
    return root;
}

/* Parse and print the tree (no colors) into a string compared with `want`. */
static void check_tree(const char *src, bool ascii, const char *want) {
    Arena a;
    arena_init(&a);
    Diag d;
    AstNode *root = parse(&a, src, &d);
    CHECK(root != NULL);
    if (root != NULL) {
        Out o = out_to_memory();
        Trace t;
        trace_init(&t, &o, (TraceStyle){false, ascii});
        trace_ast(&t, root);
        CHECK_EQ_STR(out_text(&o), want);
        out_free(&o);
    }
    arena_free(&a);
}

static void check_parse_error(const char *src, const char *message, int line,
                              int col) {
    Arena a;
    arena_init(&a);
    Diag d;
    AstNode *root = parse(&a, src, &d);
    CHECK(root == NULL);
    CHECK_EQ_STR(diag_stage_name(d.stage), "parser");
    CHECK_EQ_STR(d.message, message);
    CHECK_EQ_INT(d.loc.line, line);
    CHECK_EQ_INT(d.loc.col, col);
    CHECK_EQ_INT(diag_exit_code(&d), 1);
    arena_free(&a);
}

static void test_single_number(void) {
    Arena a;
    arena_init(&a);
    Diag d;
    AstNode *root = parse(&a, "42", &d);
    CHECK(root != NULL);
    if (root != NULL) {
        CHECK(root->kind == AST_NUM);
        CHECK_EQ_STR(root->lexeme, "42");
        CHECK_EQ_INT(root->loc.col, 1);
    }
    arena_free(&a);
}

static void test_left_associative(void) {
    Arena a;
    arena_init(&a);
    Diag d;
    AstNode *root = parse(&a, "1 + 2 + 3", &d);
    CHECK(root != NULL);
    if (root != NULL) {
        /* Add(Add(1, 2), 3) */
        CHECK(root->kind == AST_ADD);
        CHECK_EQ_INT(root->loc.col, 7); /* the second '+' */
        CHECK(root->right->kind == AST_NUM);
        CHECK_EQ_STR(root->right->lexeme, "3");
        AstNode *inner = root->left;
        CHECK(inner->kind == AST_ADD);
        CHECK_EQ_INT(inner->loc.col, 3); /* the first '+' */
        CHECK_EQ_STR(inner->left->lexeme, "1");
        CHECK_EQ_STR(inner->right->lexeme, "2");
    }
    arena_free(&a);
}

static void test_left_spine_helper(void) {
    Arena a;
    arena_init(&a);
    Diag d;
    AstNode *root = parse(&a, "1 + 2 + 3", &d);
    AstNode **adds;
    size_t n;
    AstNode *leaf;
    CHECK(root != NULL && ast_left_spine(root, &a, &adds, &n, &leaf));
    CHECK_EQ_INT(n, 2);
    if (n == 2) {
        CHECK_EQ_STR(leaf->lexeme, "1");
        CHECK_EQ_STR(adds[0]->right->lexeme, "2"); /* deepest first */
        CHECK_EQ_STR(adds[1]->right->lexeme, "3");
        CHECK(adds[1] == root);
    }
    arena_free(&a);
}

static void test_print_tree_unicode(void) {
    check_tree("1 + 2 + 3", false,
               "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90 [2] Parser (AST) "
               "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\n"
               "Add\n"
               "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 Add\n"
               "\xE2\x94\x82   \xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 Num(1)\n"
               "\xE2\x94\x82   \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 Num(2)\n"
               "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 Num(3)\n");
}

static void test_print_tree_ascii(void) {
    check_tree("1+2+3+4", true,
               "=== [2] Parser (AST) ===\n"
               "Add\n"
               "+-- Add\n"
               "|   +-- Add\n"
               "|   |   +-- Num(1)\n"
               "|   |   `-- Num(2)\n"
               "|   `-- Num(3)\n"
               "`-- Num(4)\n");
}

static void test_print_single_number(void) {
    check_tree("007", true, "=== [2] Parser (AST) ===\nNum(007)\n");
}

static void test_error_double_plus(void) {
    check_parse_error("1 + + 2", "expected NUMBER after '+', found '+'", 1, 5);
}

static void test_error_trailing_plus(void) {
    check_parse_error("1 +", "expected NUMBER after '+', found EOF", 1, 4);
    check_parse_error("1 +\n\n", "expected NUMBER after '+', found EOF", 1, 4);
}

static void test_error_leading_plus(void) {
    check_parse_error("+ 2", "expected NUMBER, found '+'", 1, 1);
}

static void test_error_empty_input(void) {
    check_parse_error("", "expected NUMBER, found EOF", 1, 1);
    check_parse_error("  \n ", "expected NUMBER, found EOF", 1, 1);
}

static void test_error_two_numbers(void) {
    check_parse_error("1 2", "expected '+' or EOF, found NUMBER '2'", 1, 3);
    check_parse_error("1 + 2\n3", "expected '+' or EOF, found NUMBER '3'", 2, 1);
}

static void test_render_spec_example(void) {
    Arena a;
    arena_init(&a);
    Diag d;
    const char *src = "1 +";
    CHECK(parse(&a, src, &d) == NULL);
    Out o = out_to_memory();
    trace_diag(&o, (TraceStyle){false, false}, &d, src, strlen(src));
    /* exactly the first example of SPEC section 8 */
    CHECK_EQ_STR(out_text(&o),
                 "error[parser]: expected NUMBER after '+', found EOF\n"
                 "  --> 1:4\n"
                 "   |\n"
                 " 1 | 1 +\n"
                 "   |    ^\n");
    out_free(&o);
    arena_free(&a);
}

static void test_long_program(void) {
    /* 20000 numbers: the parser loop and the left-spine walk must cope. */
    enum { N = 20000 };
    static char src[N * 2 + 1];
    size_t n = 0;
    for (int i = 0; i < N; i++) {
        if (i > 0) {
            src[n++] = '+';
        }
        src[n++] = '1';
    }
    src[n] = '\0';
    Arena a;
    arena_init(&a);
    Diag d;
    AstNode *root = parse(&a, src, &d);
    CHECK(root != NULL);
    AstNode **adds;
    size_t count = 0;
    AstNode *leaf;
    CHECK(root != NULL && ast_left_spine(root, &a, &adds, &count, &leaf));
    CHECK_EQ_INT(count, N - 1);
    arena_free(&a);
}

void suite_parser(void) {
    tr_begin_suite("parser");
    RUN_TEST(test_single_number);
    RUN_TEST(test_left_associative);
    RUN_TEST(test_left_spine_helper);
    RUN_TEST(test_print_tree_unicode);
    RUN_TEST(test_print_tree_ascii);
    RUN_TEST(test_print_single_number);
    RUN_TEST(test_error_double_plus);
    RUN_TEST(test_error_trailing_plus);
    RUN_TEST(test_error_leading_plus);
    RUN_TEST(test_error_empty_input);
    RUN_TEST(test_error_two_numbers);
    RUN_TEST(test_render_spec_example);
    RUN_TEST(test_long_program);
}
