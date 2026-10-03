/*
 * semantic.h - Stage [3]: semantic analysis.
 *
 * The parser only checks the shape of the program. This stage checks its
 * meaning by walking the AST, in two passes:
 *   pass 1, every literal: it must be greater than 0 and fit in uint32_t;
 *           its value is stored in the Num node (annotation);
 *   pass 2, the sum: adding left to right (in uint64_t), the running total
 *           must never go above UINT32_MAX.
 * The first problem found (leftmost in the source) is reported.
 *
 * Input:  the AST from the parser.
 * Output: the same AST with every Num annotated, plus a short summary
 *         (how many literals were checked and the total) for the trace.
 */
#ifndef TINYADD_SEMANTIC_H
#define TINYADD_SEMANTIC_H

#include "arena.h"
#include "ast.h"
#include "common.h"
#include "diag.h"

typedef struct {
    size_t   literal_count; /* number of Num nodes checked */
    uint32_t total;         /* the sum of all literals     */
} SemanticInfo;

/* Check and annotate the tree. Returns false and fills *diag on error. */
bool semantic_run(AstNode *root, Arena *arena, SemanticInfo *info, Diag *diag);

#endif /* TINYADD_SEMANTIC_H */
