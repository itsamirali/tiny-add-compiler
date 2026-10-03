/*
 * arena.c - Implementation of the arena allocator (see arena.h).
 *
 * The arena is a linked list of blocks. Allocations are carved from the
 * newest block; when it is full a new block is added. Freeing walks the
 * list once.
 */
#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_DEFAULT_BLOCK 4096u

struct ArenaBlock {
    ArenaBlock *next;     /* older block */
    size_t      capacity; /* usable bytes in data[] */
    size_t      used;     /* bytes already handed out */
    max_align_t data[];   /* storage; max_align_t keeps it aligned */
};

/* Round `n` up to a multiple of the strictest fundamental alignment.
 * Returns 0 if the result would overflow. */
static size_t align_up(size_t n) {
    const size_t a = _Alignof(max_align_t);
    if (n > SIZE_MAX - (a - 1)) {
        return 0;
    }
    return (n + (a - 1)) & ~(a - 1);
}

static ArenaBlock *block_new(size_t capacity) {
    if (capacity > SIZE_MAX - offsetof(ArenaBlock, data)) {
        return NULL;
    }
    ArenaBlock *b = malloc(offsetof(ArenaBlock, data) + capacity);
    if (b == NULL) {
        return NULL;
    }
    b->next = NULL;
    b->capacity = capacity;
    b->used = 0;
    return b;
}

void arena_init(Arena *arena) {
    arena->head = NULL;
    arena->total = 0;
}

void *arena_alloc(Arena *arena, size_t size) {
    /* Zero-size requests still get a unique pointer. */
    size_t need = align_up(size == 0 ? 1 : size);
    if (need == 0) {
        return NULL;
    }

    ArenaBlock *b = arena->head;
    if (b == NULL || b->capacity - b->used < need) {
        size_t cap = need > ARENA_DEFAULT_BLOCK ? need : ARENA_DEFAULT_BLOCK;
        b = block_new(cap);
        if (b == NULL) {
            return NULL;
        }
        b->next = arena->head;
        arena->head = b;
    }

    unsigned char *p = (unsigned char *)b->data + b->used;
    b->used += need;
    arena->total += need;
    memset(p, 0, need);
    return p;
}

char *arena_strndup(Arena *arena, const char *src, size_t len) {
    if (len == SIZE_MAX) {
        return NULL;
    }
    char *dst = arena_alloc(arena, len + 1);
    if (dst == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
    return dst;
}

void *arena_grow(Arena *arena, void *old, size_t old_size, size_t new_size) {
    void *p = arena_alloc(arena, new_size);
    if (p == NULL) {
        return NULL;
    }
    if (old != NULL && old_size > 0) {
        memcpy(p, old, old_size < new_size ? old_size : new_size);
    }
    return p;
}

void arena_free(Arena *arena) {
    ArenaBlock *b = arena->head;
    while (b != NULL) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    arena->head = NULL;
    arena->total = 0;
}
