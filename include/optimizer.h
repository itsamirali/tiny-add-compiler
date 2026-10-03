/*
 * optimizer.h - Stage [5]: constant folding.
 *
 * Any TAC instruction whose two operands are both constants is computed at
 * compile time ("folded"), and every later use of its temporary is
 * replaced by the result (constant propagation). For TinyAdd this folds
 * the whole program into a single "return <constant>". Each folding step
 * is recorded ("folded: 1 + 2 -> 3") so the trace can explain it. The CLI
 * flag --no-opt skips this stage.
 *
 * Input:  a list of TAC instructions.
 * Output: a new, optimized list of TAC instructions plus the folding log.
 */
#ifndef TINYADD_OPTIMIZER_H
#define TINYADD_OPTIMIZER_H

#include "arena.h"
#include "common.h"
#include "diag.h"
#include "ir.h"

/* One folding step: t<dest> = a + b was replaced by the constant result. */
typedef struct {
    uint32_t dest;
    uint32_t a;
    uint32_t b;
    uint32_t result;
} FoldStep;

typedef struct {
    FoldStep *steps; /* owned by the arena */
    size_t    count;
} FoldLog;

/* Fold constants in `in`, writing the result to `out` (`in` is not
 * changed). Returns false and fills *diag only on out-of-memory. */
bool optimizer_run(const IrProgram *in, Arena *arena, IrProgram *out,
                   FoldLog *log, Diag *diag);

#endif /* TINYADD_OPTIMIZER_H */
