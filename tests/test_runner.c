/*
 * test_runner.c - Entry point of the test program (`make test`).
 *
 * Implements the small framework declared in test.h, runs every suite and
 * prints a summary. Exit code: 0 if all checks passed, 1 otherwise.
 */
#include <stdio.h>
#include <string.h>

#include "test.h"

static int g_tests_run;
static int g_tests_failed;
static int g_checks_run;
static int g_checks_failed;

void tr_check(int ok, const char *file, int line, const char *expr) {
    g_checks_run++;
    if (!ok) {
        g_checks_failed++;
        printf("    FAIL %s:%d: CHECK(%s)\n", file, line, expr);
    }
}

void tr_check_int(long long got, long long want, const char *file, int line,
                  const char *got_expr, const char *want_expr) {
    g_checks_run++;
    if (got != want) {
        g_checks_failed++;
        printf("    FAIL %s:%d: %s == %s\n         got:  %lld\n         want: %lld\n",
               file, line, got_expr, want_expr, got, want);
    }
}

void tr_check_str(const char *got, const char *want, const char *file, int line,
                  const char *got_expr, const char *want_expr) {
    g_checks_run++;
    int same = (got == NULL || want == NULL) ? got == want : strcmp(got, want) == 0;
    if (!same) {
        g_checks_failed++;
        printf("    FAIL %s:%d: %s == %s\n         got:  \"%s\"\n         want: \"%s\"\n",
               file, line, got_expr, want_expr, got ? got : "(null)",
               want ? want : "(null)");
    }
}

void tr_begin_suite(const char *name) {
    printf("[%s]\n", name);
}

int tr_failed_checks(void) {
    return g_checks_failed;
}

void tr_run_test(const char *name, void (*fn)(void)) {
    int failed_before = g_checks_failed;
    g_tests_run++;
    fn();
    if (g_checks_failed == failed_before) {
        printf("  PASS %s\n", name);
    } else {
        g_tests_failed++;
        printf("  FAIL %s\n", name);
    }
}

int main(void) {
    suite_arena();
    suite_lexer();
    suite_parser();
    suite_semantic();
    suite_ir_opt();
    suite_vm();

    printf("\n%d test(s), %d check(s): %d test(s) failed, %d check(s) failed\n",
           g_tests_run, g_checks_run, g_tests_failed, g_checks_failed);
    if (g_checks_failed == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("SOME TESTS FAILED\n");
    return 1;
}
