/*
 * test_arena.c - Tests for the arena allocator (arena.h).
 */
#include <stdint.h>

#include "arena.h"
#include "test.h"

static void test_alloc_is_zeroed_and_aligned(void) {
    Arena a;
    arena_init(&a);
    for (size_t size = 0; size < 100; size++) {
        unsigned char *p = arena_alloc(&a, size);
        CHECK(p != NULL);
        if (p == NULL) {
            break;
        }
        CHECK((uintptr_t)p % _Alignof(max_align_t) == 0);
        for (size_t i = 0; i < size; i++) {
            CHECK(p[i] == 0);
        }
    }
    arena_free(&a);
}

static void test_allocations_do_not_overlap(void) {
    Arena a;
    arena_init(&a);
    int *x = arena_alloc(&a, sizeof *x);
    int *y = arena_alloc(&a, sizeof *y);
    CHECK(x != NULL && y != NULL);
    if (x != NULL && y != NULL) {
        *x = 11;
        *y = 22;
        CHECK(x != y);
        CHECK_EQ_INT(*x, 11);
        CHECK_EQ_INT(*y, 22);
    }
    arena_free(&a);
}

static void test_large_allocation_gets_own_block(void) {
    Arena a;
    arena_init(&a);
    size_t big = 100000; /* larger than the default block */
    unsigned char *p = arena_alloc(&a, big);
    CHECK(p != NULL);
    if (p != NULL) {
        p[0] = 1;
        p[big - 1] = 2; /* must be writable (ASan would catch overflow) */
        CHECK_EQ_INT(p[big - 1], 2);
    }
    arena_free(&a);
}

static void test_many_small_allocations(void) {
    Arena a;
    arena_init(&a);
    int ok = 1;
    for (int i = 0; i < 10000; i++) {
        int *p = arena_alloc(&a, sizeof *p);
        if (p == NULL) {
            ok = 0;
            break;
        }
        *p = i;
    }
    CHECK(ok);
    arena_free(&a);
    CHECK(a.head == NULL);
    CHECK_EQ_INT(a.total, 0);
}

static void test_strndup_copies_and_terminates(void) {
    Arena a;
    arena_init(&a);
    const char *src = "12345 + 6";
    char *s = arena_strndup(&a, src, 5);
    CHECK_EQ_STR(s, "12345");
    char *empty = arena_strndup(&a, src, 0);
    CHECK_EQ_STR(empty, "");
    arena_free(&a);
}

void suite_arena(void) {
    tr_begin_suite("arena");
    RUN_TEST(test_alloc_is_zeroed_and_aligned);
    RUN_TEST(test_allocations_do_not_overlap);
    RUN_TEST(test_large_allocation_gets_own_block);
    RUN_TEST(test_many_small_allocations);
    RUN_TEST(test_strndup_copies_and_terminates);
}
