/*
 * arena.h - A tiny arena (region) allocator.
 *
 * Every compile run creates one Arena. All tokens, AST nodes, IR
 * instructions, bytecode and error messages are allocated from it, and the
 * whole arena is released with a single arena_free() at the end. This keeps
 * ownership trivial: no stage ever frees individual objects, and nothing can
 * leak as long as main.c frees the arena.
 *
 * Memory returned by the arena is zero-filled and aligned for any type.
 * Allocation functions return NULL when the system is out of memory; callers
 * must report that as an internal error instead of calling exit().
 */
#ifndef TINYADD_ARENA_H
#define TINYADD_ARENA_H

#include "common.h"

typedef struct ArenaBlock ArenaBlock;

typedef struct {
    ArenaBlock *head;  /* most recently created block, or NULL */
    size_t      total; /* bytes handed out so far (for statistics) */
} Arena;

/* Prepare an empty arena. Does not allocate. */
void arena_init(Arena *arena);

/* Allocate `size` zeroed bytes. Returns NULL on out-of-memory. */
void *arena_alloc(Arena *arena, size_t size);

/* Copy `len` bytes of `src` into the arena and add a '\0'. NULL on OOM. */
char *arena_strndup(Arena *arena, const char *src, size_t len);

/* Grow an array allocated from this arena: returns a new block of
 * `new_size` bytes whose first `old_size` bytes are copied from `old`
 * (which may be NULL when old_size is 0). The old block is simply left in
 * the arena. Returns NULL on out-of-memory. */
void *arena_grow(Arena *arena, void *old, size_t old_size, size_t new_size);

/* Release every block. The arena can be reused after arena_init(). */
void arena_free(Arena *arena);

#endif /* TINYADD_ARENA_H */
