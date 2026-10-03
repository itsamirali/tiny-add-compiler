/*
 * test.h - A tiny test framework for tinyadd (no external libraries).
 *
 * A test is a `void f(void)` function that uses the CHECK macros below.
 * A suite is a function that calls RUN_TEST for each of its tests; there is
 * one suite per test_*.c file, and test_runner.c calls every suite. A failed
 * CHECK prints the file, line and the failing expression, then the test
 * continues so one run shows all failures. The runner exits with 1 if any
 * check failed.
 */
#ifndef TINYADD_TEST_H
#define TINYADD_TEST_H

#include <string.h>

/* ---- framework internals (defined in test_runner.c) ---- */
void tr_check(int ok, const char *file, int line, const char *expr);
void tr_check_int(long long got, long long want, const char *file, int line,
                  const char *got_expr, const char *want_expr);
void tr_check_str(const char *got, const char *want, const char *file, int line,
                  const char *got_expr, const char *want_expr);
void tr_run_test(const char *name, void (*fn)(void));
void tr_begin_suite(const char *name);
int  tr_failed_checks(void); /* failed checks so far (to name sub-cases) */

/* ---- what tests use ---- */
#define CHECK(cond) tr_check((cond) ? 1 : 0, __FILE__, __LINE__, #cond)
#define CHECK_EQ_INT(got, want)                                                \
    tr_check_int((long long)(got), (long long)(want), __FILE__, __LINE__,      \
                 #got, #want)
#define CHECK_EQ_STR(got, want)                                                \
    tr_check_str((got), (want), __FILE__, __LINE__, #got, #want)
#define RUN_TEST(fn) tr_run_test(#fn, fn)

/* ---- one suite per test file ---- */
void suite_arena(void);
void suite_lexer(void);
void suite_parser(void);
void suite_semantic(void);
void suite_ir_opt(void);
void suite_vm(void);

#endif /* TINYADD_TEST_H */
