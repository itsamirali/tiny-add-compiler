/*
 * semantic.c - Stage [3]: semantic analysis (see semantic.h).
 *
 * Both passes visit the tree in source order (left to right) so that the
 * first error reported is the leftmost one. The left spine is walked with
 * a loop (ast_left_spine); only right children are visited recursively,
 * and for trees built by our parser those are always single Num nodes.
 */
#include "semantic.h"

#include <string.h>

/* ---- pass 1: literals ---- */

static bool check_literal(AstNode *num, Diag *diag) {
    const char *s = num->lexeme;
    size_t len = strlen(s);
    uint64_t value = 0;

    for (size_t i = 0; i < len; i++) {
        value = value * 10u + (uint64_t)(s[i] - '0');
        if (value > UINT32_MAX) {
            /* Stop early: very long literals must not overflow uint64. */
            diag_set(diag, DIAG_SEMANTIC, num->loc,
                     "number %.40s%s does not fit in uint32 (max 4294967295)",
                     s, len > 40 ? "..." : "");
            return false;
        }
    }
    if (value == 0) {
        diag_set(diag, DIAG_SEMANTIC, num->loc,
                 "only positive integers are allowed, found %.40s%s", s,
                 len > 40 ? "..." : "");
        return false;
    }
    num->value = (uint32_t)value;
    num->checked = true;
    return true;
}

static bool check_literals(AstNode *node, Arena *arena, size_t *count,
                           Diag *diag) {
    AstNode **adds;
    size_t n_adds;
    AstNode *leaf;
    if (!ast_left_spine(node, arena, &adds, &n_adds, &leaf)) {
        diag_out_of_memory(diag);
        return false;
    }
    /* `leaf` is never an Add, so it is a Num. */
    if (!check_literal(leaf, diag)) {
        return false;
    }
    (*count)++;
    for (size_t i = 0; i < n_adds; i++) {
        if (!check_literals(adds[i]->right, arena, count, diag)) {
            return false;
        }
    }
    return true;
}

/* ---- pass 2: the running sum ---- */

/* Add `right` to `*total`, reporting overflow at the '+' sign `add`. */
static bool add_checked(const AstNode *add, uint64_t *total, uint64_t right,
                        Diag *diag) {
    uint64_t sum = *total + right; /* both <= UINT32_MAX: cannot wrap */
    if (sum > UINT32_MAX) {
        diag_set(diag, DIAG_SEMANTIC, add->loc,
                 "sum overflows uint32: %llu + %llu = %llu (max 4294967295)",
                 (unsigned long long)*total, (unsigned long long)right,
                 (unsigned long long)sum);
        return false;
    }
    *total = sum;
    return true;
}

static bool sum_tree(AstNode *node, Arena *arena, uint64_t *out, Diag *diag) {
    AstNode **adds;
    size_t n_adds;
    AstNode *leaf;
    if (!ast_left_spine(node, arena, &adds, &n_adds, &leaf)) {
        diag_out_of_memory(diag);
        return false;
    }
    uint64_t total = leaf->value;
    for (size_t i = 0; i < n_adds; i++) {
        uint64_t right;
        if (!sum_tree(adds[i]->right, arena, &right, diag) ||
            !add_checked(adds[i], &total, right, diag)) {
            return false;
        }
    }
    *out = total;
    return true;
}

bool semantic_run(AstNode *root, Arena *arena, SemanticInfo *info, Diag *diag) {
    info->literal_count = 0;
    info->total = 0;

    if (!check_literals(root, arena, &info->literal_count, diag)) {
        return false;
    }
    uint64_t total;
    if (!sum_tree(root, arena, &total, diag)) {
        return false;
    }
    info->total = (uint32_t)total;
    return true;
}
