/*
 * optimizer.c - Stage [5]: constant folding (see optimizer.h).
 *
 * One pass over the instructions with a table "temporary -> known
 * constant":
 *   1. replace every temporary operand whose value is known by that value;
 *   2. if an ADD now has two constant operands, compute it, remember the
 *      result for its temporary, log the step and drop the instruction;
 *   3. otherwise copy the (rewritten) instruction to the output.
 */
#include "optimizer.h"

typedef struct {
    bool     known;
    uint32_t value;
} TempValue;

static Operand substitute(Operand op, const TempValue *temps,
                          uint32_t temp_count) {
    if (op.kind == OPND_TEMP && op.value >= 1 && op.value <= temp_count &&
        temps[op.value].known) {
        return (Operand){OPND_CONST, temps[op.value].value};
    }
    return op;
}

static bool log_step(FoldLog *log, size_t *cap, Arena *arena, FoldStep step) {
    if (log->count == *cap) {
        size_t new_cap = *cap == 0 ? 16 : *cap * 2;
        if (new_cap > SIZE_MAX / sizeof(FoldStep)) {
            return false;
        }
        FoldStep *bigger = arena_grow(arena, log->steps, *cap * sizeof(FoldStep),
                                      new_cap * sizeof(FoldStep));
        if (bigger == NULL) {
            return false;
        }
        log->steps = bigger;
        *cap = new_cap;
    }
    log->steps[log->count++] = step;
    return true;
}

bool optimizer_run(const IrProgram *in, Arena *arena, IrProgram *out,
                   FoldLog *log, Diag *diag) {
    out->instrs = NULL;
    out->count = 0;
    out->temp_count = in->temp_count;
    log->steps = NULL;
    log->count = 0;

    size_t out_cap = 0;
    size_t log_cap = 0;

    /* temps[1..temp_count]; index 0 is unused. */
    uint64_t bytes = ((uint64_t)in->temp_count + 1) * sizeof(TempValue);
    if (bytes > SIZE_MAX) {
        diag_out_of_memory(diag);
        return false;
    }
    TempValue *temps = arena_alloc(arena, (size_t)bytes);
    if (temps == NULL) {
        diag_out_of_memory(diag);
        return false;
    }

    for (size_t i = 0; i < in->count; i++) {
        IrInstr ins = in->instrs[i];
        ins.a = substitute(ins.a, temps, in->temp_count);
        if (ins.op == IR_ADD) {
            ins.b = substitute(ins.b, temps, in->temp_count);
            if (ins.a.kind == OPND_CONST && ins.b.kind == OPND_CONST) {
                uint64_t sum = (uint64_t)ins.a.value + ins.b.value;
                /* Semantic analysis guarantees no overflow; if it ever
                 * happened we simply do not fold this instruction. */
                if (sum <= UINT32_MAX && ins.dest >= 1 && ins.dest <= in->temp_count) {
                    temps[ins.dest].known = true;
                    temps[ins.dest].value = (uint32_t)sum;
                    FoldStep step = {ins.dest, ins.a.value, ins.b.value, (uint32_t)sum};
                    if (!log_step(log, &log_cap, arena, step)) {
                        diag_out_of_memory(diag);
                        return false;
                    }
                    continue; /* instruction folded away */
                }
            }
        }
        if (!ir_append(out, &out_cap, arena, ins)) {
            diag_out_of_memory(diag);
            return false;
        }
    }
    return true;
}
