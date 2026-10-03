/*
 * test_ir_opt.c - Tests for stages [4] and [5]: TAC generation and
 * constant folding.
 */
#include <stdio.h>
#include <string.h>

#include "ir.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "semantic.h"
#include "test.h"
#include "trace.h"

/* Source -> TAC. Returns false if any stage failed. */
static bool to_ir(Arena *a, const char *src, IrProgram *ir) {
    Diag d;
    memset(&d, 0, sizeof d);
    TokenList tl;
    AstNode *root;
    SemanticInfo info;
    return lexer_run(src, strlen(src), a, &tl, &d) &&
           parser_run(&tl, a, &root, &d) && semantic_run(root, a, &info, &d) &&
           ir_generate(root, a, ir, &d);
}

/* All instructions as text, one per line. */
static void ir_text(const IrProgram *ir, char *buf, size_t size) {
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < ir->count && used < size; i++) {
        char line[64];
        ir_format_instr(&ir->instrs[i], line, sizeof line);
        int n = snprintf(buf + used, size - used, "%s\n", line);
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
}

static void expect_ir(const char *src, const char *want) {
    Arena a;
    arena_init(&a);
    IrProgram ir;
    CHECK(to_ir(&a, src, &ir));
    char text[512];
    ir_text(&ir, text, sizeof text);
    CHECK_EQ_STR(text, want);
    arena_free(&a);
}

static void test_tac_spec_example(void) {
    expect_ir("1 + 2 + 3", "t1 = 1 + 2\nt2 = t1 + 3\nreturn t2\n");
}

static void test_tac_single_number(void) {
    expect_ir("42", "return 42\n");
}

static void test_tac_uses_values_not_lexemes(void) {
    expect_ir("007 + 10", "t1 = 7 + 10\nreturn t1\n");
}

static void test_tac_temp_count(void) {
    Arena a;
    arena_init(&a);
    IrProgram ir;
    CHECK(to_ir(&a, "1+1+1+1+1", &ir));
    CHECK_EQ_INT(ir.temp_count, 4);
    CHECK_EQ_INT(ir.count, 5);
    arena_free(&a);
}

static void test_ir_refuses_unchecked_tree(void) {
    Arena a;
    arena_init(&a);
    Diag d;
    memset(&d, 0, sizeof d);
    TokenList tl;
    AstNode *root;
    IrProgram ir;
    CHECK(lexer_run("1 + 2", 5, &a, &tl, &d) && parser_run(&tl, &a, &root, &d));
    /* semantic analysis skipped on purpose */
    CHECK(!ir_generate(root, &a, &ir, &d));
    CHECK_EQ_STR(diag_stage_name(d.stage), "internal");
    arena_free(&a);
}

static void test_fold_spec_example(void) {
    Arena a;
    arena_init(&a);
    IrProgram ir, opt;
    FoldLog log;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(to_ir(&a, "1 + 2 + 3", &ir));
    CHECK(optimizer_run(&ir, &a, &opt, &log, &d));

    char text[256];
    ir_text(&opt, text, sizeof text);
    CHECK_EQ_STR(text, "return 6\n");

    CHECK_EQ_INT(log.count, 2);
    if (log.count == 2) {
        CHECK_EQ_INT(log.steps[0].a, 1);
        CHECK_EQ_INT(log.steps[0].b, 2);
        CHECK_EQ_INT(log.steps[0].result, 3);
        CHECK_EQ_INT(log.steps[1].a, 3); /* t1 was replaced by 3 */
        CHECK_EQ_INT(log.steps[1].b, 3);
        CHECK_EQ_INT(log.steps[1].result, 6);
    }

    /* the input program is not modified */
    ir_text(&ir, text, sizeof text);
    CHECK_EQ_STR(text, "t1 = 1 + 2\nt2 = t1 + 3\nreturn t2\n");
    arena_free(&a);
}

static void test_fold_single_number(void) {
    Arena a;
    arena_init(&a);
    IrProgram ir, opt;
    FoldLog log;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(to_ir(&a, "42", &ir));
    CHECK(optimizer_run(&ir, &a, &opt, &log, &d));
    CHECK_EQ_INT(log.count, 0);
    char text[64];
    ir_text(&opt, text, sizeof text);
    CHECK_EQ_STR(text, "return 42\n");
    arena_free(&a);
}

static void test_fold_partial(void) {
    /* Hand-written TAC with an unknown temporary: only some of it folds.
     *   t1 = t9 + 1     (t9 unknown -> stays)
     *   t2 = 2 + 3      (folds to 5)
     *   t3 = t1 + t2    (becomes t3 = t1 + 5)
     *   return t3 */
    Arena a;
    arena_init(&a);
    IrInstr code[] = {
        {IR_ADD, 1, {OPND_TEMP, 9}, {OPND_CONST, 1}},
        {IR_ADD, 2, {OPND_CONST, 2}, {OPND_CONST, 3}},
        {IR_ADD, 3, {OPND_TEMP, 1}, {OPND_TEMP, 2}},
        {IR_RETURN, 0, {OPND_TEMP, 3}, {OPND_CONST, 0}},
    };
    IrProgram ir = {code, 4, 3};
    IrProgram opt;
    FoldLog log;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(optimizer_run(&ir, &a, &opt, &log, &d));
    char text[256];
    ir_text(&opt, text, sizeof text);
    CHECK_EQ_STR(text, "t1 = t9 + 1\nt3 = t1 + 5\nreturn t3\n");
    CHECK_EQ_INT(log.count, 1);
    arena_free(&a);
}

static void test_fold_long_program(void) {
    enum { N = 1000 };
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
    IrProgram ir, opt;
    FoldLog log;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(to_ir(&a, src, &ir));
    CHECK(optimizer_run(&ir, &a, &opt, &log, &d));
    CHECK_EQ_INT(log.count, N - 1);
    char text[64];
    ir_text(&opt, text, sizeof text);
    CHECK_EQ_STR(text, "return 1000\n");
    arena_free(&a);
}

static void test_trace_opt_sections(void) {
    Arena a;
    arena_init(&a);
    IrProgram ir, opt;
    FoldLog log;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(to_ir(&a, "1 + 2 + 3", &ir));
    CHECK(optimizer_run(&ir, &a, &opt, &log, &d));

    /* --trace form (matches SPEC section 7) */
    Out o = out_to_memory();
    Trace t;
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_opt(&t, &ir, &opt, &log, false);
    CHECK_EQ_STR(out_text(&o),
                 "=== [5] Optimizer (constant folding) ===\n"
                 "folded: 1 + 2 -> 3\n"
                 "folded: 3 + 3 -> 6\n"
                 "return 6\n");
    out_free(&o);

    /* --stage=opt form: before/after side by side (SPEC section 3.5) */
    o = out_to_memory();
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_opt(&t, &ir, &opt, &log, true);
    CHECK_EQ_STR(out_text(&o),
                 "=== [5] Optimizer (constant folding) ===\n"
                 "Before:                After:\n"
                 "t1 = 1 + 2             return 6\n"
                 "t2 = t1 + 3\n"
                 "return t2\n"
                 "\n"
                 "folded: 1 + 2 -> 3\n"
                 "folded: 3 + 3 -> 6\n");
    out_free(&o);

    /* the TAC section itself */
    o = out_to_memory();
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_ir(&t, &ir);
    CHECK_EQ_STR(out_text(&o),
                 "=== [4] Intermediate code (TAC) ===\n"
                 "t1 = 1 + 2\n"
                 "t2 = t1 + 3\n"
                 "return t2\n");
    out_free(&o);
    arena_free(&a);
}

void suite_ir_opt(void) {
    tr_begin_suite("ir_opt");
    RUN_TEST(test_tac_spec_example);
    RUN_TEST(test_tac_single_number);
    RUN_TEST(test_tac_uses_values_not_lexemes);
    RUN_TEST(test_tac_temp_count);
    RUN_TEST(test_ir_refuses_unchecked_tree);
    RUN_TEST(test_fold_spec_example);
    RUN_TEST(test_fold_single_number);
    RUN_TEST(test_fold_partial);
    RUN_TEST(test_fold_long_program);
    RUN_TEST(test_trace_opt_sections);
}
