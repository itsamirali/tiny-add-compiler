/*
 * test_vm.c - Tests for stages [6] and [7] (bytecode generation and the
 * VM), plus end-to-end tests: every program in tests/cases/ is compiled
 * and run, and the output is compared with its .expected file.
 *
 * Format of tests/cases/<name>.expected:
 *     line 1:  "exit: <code>"
 *     rest:    the exact text the user sees: the result ("6") on success,
 *              or the error message (no colors) on failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codegen.h"
#include "ir.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "semantic.h"
#include "test.h"
#include "trace.h"
#include "vm.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

/* Source -> bytecode, with or without the optimizer. */
static bool to_bytecode(Arena *a, const char *src, bool optimize, Bytecode *bc) {
    Diag d;
    memset(&d, 0, sizeof d);
    TokenList tl;
    AstNode *root;
    SemanticInfo info;
    IrProgram ir, opt;
    FoldLog log;
    if (!lexer_run(src, strlen(src), a, &tl, &d) || !parser_run(&tl, a, &root, &d) ||
        !semantic_run(root, a, &info, &d) || !ir_generate(root, a, &ir, &d)) {
        return false;
    }
    if (optimize) {
        if (!optimizer_run(&ir, a, &opt, &log, &d)) {
            return false;
        }
        ir = opt;
    }
    return codegen_run(&ir, a, bc, &d);
}

/* Bytecode as text, e.g. "PUSH 1\nADD\nRET\n". */
static void code_text(const Bytecode *bc, char *buf, size_t size) {
    size_t used = 0;
    buf[0] = '\0';
    for (size_t i = 0; i < bc->count && used < size; i++) {
        int n = bc->code[i].op == OP_PUSH
                    ? snprintf(buf + used, size - used, "PUSH %lu\n",
                               (unsigned long)bc->code[i].operand)
                    : snprintf(buf + used, size - used, "%s\n",
                               opcode_name(bc->code[i].op));
        if (n < 0) {
            break;
        }
        used += (size_t)n;
    }
}

static void expect_code(const char *src, bool optimize, const char *want) {
    Arena a;
    arena_init(&a);
    Bytecode bc;
    CHECK(to_bytecode(&a, src, optimize, &bc));
    char text[512];
    code_text(&bc, text, sizeof text);
    CHECK_EQ_STR(text, want);
    arena_free(&a);
}

/* Run hand-written bytecode and expect a VM error. */
static void expect_vm_error(const Instr *code, size_t count, const char *message) {
    Arena a;
    arena_init(&a);
    Bytecode bc = {(Instr *)code, count};
    Diag d;
    memset(&d, 0, sizeof d);
    uint32_t result = 0;
    CHECK(!vm_run(&bc, &a, NULL, &result, &d));
    CHECK_EQ_STR(diag_stage_name(d.stage), "vm");
    CHECK_EQ_STR(d.message, message);
    CHECK_EQ_INT(diag_exit_code(&d), 2);
    arena_free(&a);
}

/* ------------------------------------------------------------------ */
/* [6] code generation                                                 */
/* ------------------------------------------------------------------ */

static void test_codegen_spec_example_without_opt(void) {
    expect_code("1 + 2 + 3", false, "PUSH 1\nPUSH 2\nADD\nPUSH 3\nADD\nRET\n");
}

static void test_codegen_with_opt(void) {
    expect_code("1 + 2 + 3", true, "PUSH 6\nRET\n");
}

static void test_codegen_single_number(void) {
    expect_code("42", false, "PUSH 42\nRET\n");
    expect_code("42", true, "PUSH 42\nRET\n");
}

static void test_codegen_nested_right_operand(void) {
    /* t1 = 1 + 2; t2 = 3 + 4; t3 = t1 + t2; return t3 */
    IrInstr code[] = {
        {IR_ADD, 1, {OPND_CONST, 1}, {OPND_CONST, 2}},
        {IR_ADD, 2, {OPND_CONST, 3}, {OPND_CONST, 4}},
        {IR_ADD, 3, {OPND_TEMP, 1}, {OPND_TEMP, 2}},
        {IR_RETURN, 0, {OPND_TEMP, 3}, {OPND_CONST, 0}},
    };
    IrProgram ir = {code, 4, 3};
    Arena a;
    arena_init(&a);
    Bytecode bc;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(codegen_run(&ir, &a, &bc, &d));
    char text[256];
    code_text(&bc, text, sizeof text);
    CHECK_EQ_STR(text, "PUSH 1\nPUSH 2\nADD\nPUSH 3\nPUSH 4\nADD\nADD\nRET\n");
    uint32_t result = 0;
    CHECK(vm_run(&bc, &a, NULL, &result, &d));
    CHECK_EQ_INT(result, 10);
    arena_free(&a);
}

static void test_codegen_detects_bad_tac(void) {
    Arena a;
    arena_init(&a);
    Bytecode bc;
    Diag d;

    /* temporaries used in the wrong order */
    IrInstr swapped[] = {
        {IR_ADD, 1, {OPND_CONST, 1}, {OPND_CONST, 2}},
        {IR_ADD, 2, {OPND_CONST, 3}, {OPND_CONST, 4}},
        {IR_ADD, 3, {OPND_TEMP, 2}, {OPND_TEMP, 1}},
        {IR_RETURN, 0, {OPND_TEMP, 3}, {OPND_CONST, 0}},
    };
    IrProgram bad1 = {swapped, 4, 3};
    memset(&d, 0, sizeof d);
    CHECK(!codegen_run(&bad1, &a, &bc, &d));
    CHECK_EQ_STR(d.message, "codegen: temporary is not on top of the stack");

    /* no return at the end */
    IrInstr no_ret[] = {{IR_ADD, 1, {OPND_CONST, 1}, {OPND_CONST, 2}}};
    IrProgram bad2 = {no_ret, 1, 1};
    memset(&d, 0, sizeof d);
    CHECK(!codegen_run(&bad2, &a, &bc, &d));
    CHECK_EQ_STR(d.message, "codegen: TAC does not end with return");
    CHECK_EQ_STR(diag_stage_name(d.stage), "internal");
    arena_free(&a);
}

static void test_disassembly_output(void) {
    Arena a;
    arena_init(&a);
    Bytecode bc;
    CHECK(to_bytecode(&a, "1 + 2 + 3", false, &bc));
    Out o = out_to_memory();
    Trace t;
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_code(&t, &bc);
    /* exactly the listing of SPEC section 3.6 */
    CHECK_EQ_STR(out_text(&o),
                 "=== [6] Code generation (bytecode) ===\n"
                 "0000  PUSH 1\n"
                 "0001  PUSH 2\n"
                 "0002  ADD\n"
                 "0003  PUSH 3\n"
                 "0004  ADD\n"
                 "0005  RET\n");
    out_free(&o);
    arena_free(&a);
}

/* ------------------------------------------------------------------ */
/* [7] VM                                                              */
/* ------------------------------------------------------------------ */

static void test_vm_runs_and_traces(void) {
    Arena a;
    arena_init(&a);
    Bytecode bc;
    CHECK(to_bytecode(&a, "1 + 2 + 3", false, &bc));
    VmTrace vt;
    uint32_t result = 0;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(vm_run(&bc, &a, &vt, &result, &d));
    CHECK_EQ_INT(result, 6);
    CHECK_EQ_INT(vt.count, 6);

    Out o = out_to_memory();
    Trace t;
    trace_init(&t, &o, (TraceStyle){false, true});
    trace_vm(&t, &vt);
    trace_result(&t, result);
    CHECK_EQ_STR(out_text(&o),
                 "=== [7] VM execution ===\n"
                 "0000 PUSH 1   stack: [1]\n"
                 "0001 PUSH 2   stack: [1, 2]\n"
                 "0002 ADD      stack: [3]\n"
                 "0003 PUSH 3   stack: [3, 3]\n"
                 "0004 ADD      stack: [6]\n"
                 "0005 RET      stack: []\n"
                 "\n"
                 "Result: 6\n");
    out_free(&o);
    arena_free(&a);
}

static void test_vm_max_value(void) {
    Arena a;
    arena_init(&a);
    Bytecode bc;
    CHECK(to_bytecode(&a, "4294967294 + 1", false, &bc));
    uint32_t result = 0;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(vm_run(&bc, &a, NULL, &result, &d));
    CHECK_EQ_INT(result, 4294967295u);
    arena_free(&a);
}

static void test_vm_stack_underflow(void) {
    Instr add_only[] = {{OP_ADD, 0}};
    expect_vm_error(add_only, 1, "stack underflow: ADD needs two values (at address 0000)");
    Instr one_then_add[] = {{OP_PUSH, 1}, {OP_ADD, 0}};
    expect_vm_error(one_then_add, 2,
                    "stack underflow: ADD needs two values (at address 0001)");
    Instr ret_only[] = {{OP_RET, 0}};
    expect_vm_error(ret_only, 1, "stack underflow: RET needs a value (at address 0000)");
}

static void test_vm_stack_overflow(void) {
    static Instr pushes[VM_STACK_MAX + 1];
    for (size_t i = 0; i < VM_STACK_MAX + 1; i++) {
        pushes[i] = (Instr){OP_PUSH, 1};
    }
    expect_vm_error(pushes, VM_STACK_MAX + 1, "stack overflow (at address 0256)");
}

static void test_vm_add_overflow(void) {
    Instr code[] = {{OP_PUSH, 4294967295u}, {OP_PUSH, 1}, {OP_ADD, 0}, {OP_RET, 0}};
    expect_vm_error(code, 4,
                    "ADD overflow: result does not fit in uint32 (at address 0002)");
}

static void test_vm_halt_and_missing_ret(void) {
    Instr halt[] = {{OP_PUSH, 1}, {OP_HALT, 0}};
    expect_vm_error(halt, 2, "HALT reached before RET, no result (at address 0001)");
    Instr no_ret[] = {{OP_PUSH, 1}};
    expect_vm_error(no_ret, 1,
                    "ran past the end of the program without RET (at address 0001)");
    expect_vm_error(NULL, 0,
                    "ran past the end of the program without RET (at address 0000)");
}

static void test_vm_trace_kept_on_error(void) {
    Arena a;
    arena_init(&a);
    Instr halt[] = {{OP_PUSH, 7}, {OP_HALT, 0}};
    Bytecode bc = {halt, 2};
    VmTrace vt;
    uint32_t result = 0;
    Diag d;
    memset(&d, 0, sizeof d);
    CHECK(!vm_run(&bc, &a, &vt, &result, &d));
    CHECK_EQ_INT(vt.count, 2);
    if (vt.count == 2) {
        CHECK_EQ_INT(vt.steps[1].depth, 1);
        CHECK_EQ_INT(vt.steps[1].stack[0], 7);
    }
    arena_free(&a);
}

/* ------------------------------------------------------------------ */
/* end-to-end: tests/cases/                                            */
/* ------------------------------------------------------------------ */

/* Compile and run like `tinyadd <file>` with no options. The text the
 * user would see is written to `o`; the exit code is returned. */
static int run_program(const char *src, size_t len, Out *o) {
    Arena a;
    arena_init(&a);
    Diag d;
    memset(&d, 0, sizeof d);
    TokenList tl;
    AstNode *root;
    SemanticInfo info;
    IrProgram ir, opt;
    FoldLog log;
    Bytecode bc;
    uint32_t result = 0;
    int code = 0;
    if (lexer_run(src, len, &a, &tl, &d) && parser_run(&tl, &a, &root, &d) &&
        semantic_run(root, &a, &info, &d) && ir_generate(root, &a, &ir, &d) &&
        optimizer_run(&ir, &a, &opt, &log, &d) && codegen_run(&opt, &a, &bc, &d) &&
        vm_run(&bc, &a, NULL, &result, &d)) {
        out_printf(o, "%lu\n", (unsigned long)result);
    } else {
        trace_diag(o, (TraceStyle){false, false}, &d, src, len);
        code = diag_exit_code(&d);
    }
    arena_free(&a);
    return code;
}

/* Read a whole file; NULL if it cannot be read. Caller frees. */
static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    size_t cap = 1024, n = 0;
    char *buf = malloc(cap);
    while (buf != NULL) {
        n += fread(buf + n, 1, cap - n - 1, f);
        if (n < cap - 1) {
            break;
        }
        char *bigger = realloc(buf, cap * 2);
        if (bigger == NULL) {
            free(buf);
            buf = NULL;
            break;
        }
        buf = bigger;
        cap *= 2;
    }
    fclose(f);
    if (buf != NULL) {
        buf[n] = '\0';
        *len = n;
    }
    return buf;
}

/* Remove every '\r' (in case an editor added CRLF to .expected). */
static void strip_cr(char *s) {
    char *w = s;
    for (char *r = s; *r != '\0'; r++) {
        if (*r != '\r') {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static const char *const CASES[] = {
    "ok_single",
    "ok_simple",
    "ok_spaces",
    "ok_multiline",
    "ok_long_list",
    "ok_leading_zeros",
    "ok_max_value",
    "ok_max_sum",
    "err_empty",
    "err_lexer_minus",
    "err_lexer_letter",
    "err_parser_double_plus",
    "err_parser_trailing_plus",
    "err_parser_leading_plus",
    "err_parser_two_numbers",
    "err_parser_line_ten",
    "err_semantic_zero",
    "err_semantic_too_big",
    "err_semantic_overflow",
};

static void run_case(const char *name) {
    char path[256];
    size_t src_len = 0, exp_len = 0;

    snprintf(path, sizeof path, "tests/cases/%s.tadd", name);
    char *src = slurp(path, &src_len);
    if (src == NULL) {
        printf("    cannot open %s (run the tests from the project root)\n", path);
    }
    snprintf(path, sizeof path, "tests/cases/%s.expected", name);
    char *expected = slurp(path, &exp_len);
    if (expected == NULL) {
        printf("    cannot open %s (run the tests from the project root)\n", path);
    }

    CHECK(src != NULL);
    CHECK(expected != NULL);
    if (src != NULL && expected != NULL) {
        strip_cr(expected);
        int want_exit = -1;
        int consumed = 0;
        CHECK(sscanf(expected, "exit: %d\n%n", &want_exit, &consumed) == 1);
        Out o = out_to_memory();
        int got_exit = run_program(src, src_len, &o);
        CHECK_EQ_INT(got_exit, want_exit);
        CHECK_EQ_STR(out_text(&o), expected + consumed);
        out_free(&o);
    }
    free(src);
    free(expected);
}

static void test_end_to_end_cases(void) {
    for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        int failed_before = tr_failed_checks();
        run_case(CASES[i]);
        if (tr_failed_checks() != failed_before) {
            printf("    ^ in case tests/cases/%s\n", CASES[i]);
        }
    }
}

void suite_vm(void) {
    tr_begin_suite("vm");
    RUN_TEST(test_codegen_spec_example_without_opt);
    RUN_TEST(test_codegen_with_opt);
    RUN_TEST(test_codegen_single_number);
    RUN_TEST(test_codegen_nested_right_operand);
    RUN_TEST(test_codegen_detects_bad_tac);
    RUN_TEST(test_disassembly_output);
    RUN_TEST(test_vm_runs_and_traces);
    RUN_TEST(test_vm_max_value);
    RUN_TEST(test_vm_stack_underflow);
    RUN_TEST(test_vm_stack_overflow);
    RUN_TEST(test_vm_add_overflow);
    RUN_TEST(test_vm_halt_and_missing_ret);
    RUN_TEST(test_vm_trace_kept_on_error);
    RUN_TEST(test_end_to_end_cases);
}
