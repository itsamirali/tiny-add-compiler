/*
 * test_semantic.c - Tests for stage [3], semantic analysis.
 */
#include <string.h>

#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "test.h"
#include "trace.h"

/* Lex + parse + check. Returns true on success; *root is set if parsing
 * worked. */
static bool check_src(Arena *a, const char *src, AstNode **root,
                      SemanticInfo *info, Diag *d) {
    memset(d, 0, sizeof *d);
    *root = NULL;
    TokenList tl;
    if (!lexer_run(src, strlen(src), a, &tl, d) || !parser_run(&tl, a, root, d)) {
        return false;
    }
    return semantic_run(*root, a, info, d);
}

static void expect_ok(const char *src, size_t count, uint32_t total) {
    Arena a;
    arena_init(&a);
    AstNode *root;
    SemanticInfo info;
    Diag d;
    CHECK(check_src(&a, src, &root, &info, &d));
    CHECK_EQ_INT(info.literal_count, count);
    CHECK_EQ_INT(info.total, total);
    arena_free(&a);
}

static void expect_error(const char *src, const char *message, int line, int col) {
    Arena a;
    arena_init(&a);
    AstNode *root;
    SemanticInfo info;
    Diag d;
    CHECK(!check_src(&a, src, &root, &info, &d));
    CHECK(root != NULL); /* it parsed; semantic analysis rejected it */
    CHECK_EQ_STR(diag_stage_name(d.stage), "semantic");
    CHECK_EQ_STR(d.message, message);
    CHECK_EQ_INT(d.loc.line, line);
    CHECK_EQ_INT(d.loc.col, col);
    CHECK_EQ_INT(diag_exit_code(&d), 1);
    arena_free(&a);
}

static void test_valid_programs(void) {
    expect_ok("42", 1, 42);
    expect_ok("2 + 3", 2, 5);
    expect_ok("1 + 2 + 3 + 4", 4, 10);
    expect_ok("007 + 3", 2, 10);
    expect_ok("0000000000000000000000000001", 1, 1);
}

static void test_limits(void) {
    expect_ok("4294967295", 1, 4294967295u);
    expect_ok("4294967294 + 1", 2, 4294967295u);
    expect_ok("2147483648 + 2147483647", 2, 4294967295u);
}

static void test_annotations(void) {
    Arena a;
    arena_init(&a);
    AstNode *root;
    SemanticInfo info;
    Diag d;
    CHECK(check_src(&a, "10 + 020", &root, &info, &d));
    if (root != NULL && root->kind == AST_ADD) {
        CHECK(root->left->checked);
        CHECK_EQ_INT(root->left->value, 10);
        CHECK(root->right->checked);
        CHECK_EQ_INT(root->right->value, 20);
    }
    arena_free(&a);
}

static void test_zero_is_rejected(void) {
    expect_error("0", "only positive integers are allowed, found 0", 1, 1);
    expect_error("5 + 0", "only positive integers are allowed, found 0", 1, 5);
    expect_error("1 + 000", "only positive integers are allowed, found 000", 1, 5);
}

static void test_too_large_literal(void) {
    expect_error("4294967296",
                 "number 4294967296 does not fit in uint32 (max 4294967295)", 1, 1);
    expect_error("1 +\n 99999999999999999999999999999",
                 "number 99999999999999999999999999999 does not fit in uint32 "
                 "(max 4294967295)", 2, 2);
}

static void test_sum_overflow(void) {
    /* reported at the '+' that makes the running sum too big */
    expect_error("4294967295 + 1",
                 "sum overflows uint32: 4294967295 + 1 = 4294967296 (max 4294967295)",
                 1, 12);
    expect_error("4000000000 + 200000000 + 100000000",
                 "sum overflows uint32: 4200000000 + 100000000 = 4300000000 "
                 "(max 4294967295)", 1, 24);
}

static void test_first_error_is_leftmost_literal_problem(void) {
    /* pass 1 (literals) runs before pass 2 (sum): the 0 is reported even
     * though the sum would overflow earlier in the source */
    expect_error("4294967295 + 1 + 0",
                 "only positive integers are allowed, found 0", 1, 18);
    expect_error("0 + 4294967296",
                 "only positive integers are allowed, found 0", 1, 1);
}

static void test_long_program(void) {
    /* 50000 ones: deep left comb, must not overflow the C stack */
    enum { N = 50000 };
    static char src[N * 2 + 1];
    size_t n = 0;
    for (int i = 0; i < N; i++) {
        if (i > 0) {
            src[n++] = '+';
        }
        src[n++] = '1';
    }
    src[n] = '\0';
    expect_ok(src, N, N);
}

static void test_render_spec_example(void) {
    Arena a;
    arena_init(&a);
    AstNode *root;
    SemanticInfo info;
    Diag d;
    const char *src = "5 + 0";
    CHECK(!check_src(&a, src, &root, &info, &d));
    Out o = out_to_memory();
    trace_diag(&o, (TraceStyle){false, false}, &d, src, strlen(src));
    /* exactly the second example of SPEC section 8 */
    CHECK_EQ_STR(out_text(&o),
                 "error[semantic]: only positive integers are allowed, found 0\n"
                 "  --> 1:5\n"
                 "   |\n"
                 " 1 | 5 + 0\n"
                 "   |     ^\n");
    out_free(&o);
    arena_free(&a);
}

static void test_summary_output(void) {
    SemanticInfo info = {3, 6};
    Out o = out_to_memory();
    Trace t;
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_semantic(&t, &info);
    CHECK_EQ_STR(out_text(&o),
                 "=== [3] Semantic analysis ===\n"
                 "[ok] 3 literals checked: all > 0, all fit in uint32\n"
                 "[ok] total sum 6 fits in uint32\n");
    out_free(&o);

    SemanticInfo one = {1, 42};
    o = out_to_memory();
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_semantic(&t, &one);
    CHECK(strstr(out_text(&o), "1 literal checked") != NULL);
    out_free(&o);
}

void suite_semantic(void) {
    tr_begin_suite("semantic");
    RUN_TEST(test_valid_programs);
    RUN_TEST(test_limits);
    RUN_TEST(test_annotations);
    RUN_TEST(test_zero_is_rejected);
    RUN_TEST(test_too_large_literal);
    RUN_TEST(test_sum_overflow);
    RUN_TEST(test_first_error_is_leftmost_literal_problem);
    RUN_TEST(test_long_program);
    RUN_TEST(test_render_spec_example);
    RUN_TEST(test_summary_output);
}
