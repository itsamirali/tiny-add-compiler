/*
 * codegen.c - Stage [6]: TAC to stack bytecode (see codegen.h).
 *
 * The generator keeps a model of the stack (which temporary sits in which
 * slot) only to double-check its own work: every temporary an instruction
 * uses must be on top of the stack. If not, it reports an internal error
 * instead of producing wrong bytecode.
 */
#include "codegen.h"

typedef struct {
    Bytecode *out;
    size_t    cap;
    Arena    *arena;
    Diag     *diag;
    /* Model of the run-time stack: temp number per slot, 0 = a constant. */
    uint32_t *model;
    size_t    depth;
    size_t    model_cap;
} CodeGen;

static bool emit(CodeGen *g, OpCode op, uint32_t operand) {
    Bytecode *out = g->out;
    if (out->count == g->cap) {
        size_t new_cap = g->cap == 0 ? 16 : g->cap * 2;
        if (new_cap > SIZE_MAX / sizeof(Instr)) {
            diag_out_of_memory(g->diag);
            return false;
        }
        Instr *bigger = arena_grow(g->arena, out->code, g->cap * sizeof(Instr),
                                   new_cap * sizeof(Instr));
        if (bigger == NULL) {
            diag_out_of_memory(g->diag);
            return false;
        }
        out->code = bigger;
        g->cap = new_cap;
    }
    out->code[out->count++] = (Instr){op, operand};
    return true;
}

static bool model_push(CodeGen *g, uint32_t temp) {
    if (g->depth == g->model_cap) {
        size_t new_cap = g->model_cap == 0 ? 16 : g->model_cap * 2;
        if (new_cap > SIZE_MAX / sizeof(uint32_t)) {
            diag_out_of_memory(g->diag);
            return false;
        }
        uint32_t *bigger = arena_grow(g->arena, g->model,
                                      g->model_cap * sizeof(uint32_t),
                                      new_cap * sizeof(uint32_t));
        if (bigger == NULL) {
            diag_out_of_memory(g->diag);
            return false;
        }
        g->model = bigger;
        g->model_cap = new_cap;
    }
    g->model[g->depth++] = temp;
    return true;
}

static bool internal_error(CodeGen *g, const char *what) {
    SrcLoc none = {0, 0};
    diag_set(g->diag, DIAG_INTERNAL, none, "codegen: %s", what);
    return false;
}

/* Make sure the operands of one instruction are on top of the stack.
 * Constants are pushed; temporaries must already be there. `n` is 1 or 2. */
static bool load_operands(CodeGen *g, const Operand *ops, size_t n) {
    size_t temps = 0;
    for (size_t i = 0; i < n; i++) {
        if (ops[i].kind == OPND_TEMP) {
            temps++;
        }
    }
    /* The temporaries used must be exactly the top `temps` slots. */
    if (temps > g->depth) {
        return internal_error(g, "temporary used but stack is empty");
    }
    for (size_t i = 0, slot = g->depth - temps; i < n; i++) {
        if (ops[i].kind == OPND_TEMP) {
            if (g->model[slot] != ops[i].value) {
                return internal_error(g, "temporary is not on top of the stack");
            }
            slot++;
        }
    }
    for (size_t i = 0; i < n; i++) {
        if (ops[i].kind == OPND_CONST) {
            if (!emit(g, OP_PUSH, ops[i].value) || !model_push(g, 0)) {
                return false;
            }
        }
    }
    return true;
}

bool codegen_run(const IrProgram *ir, Arena *arena, Bytecode *out, Diag *diag) {
    out->code = NULL;
    out->count = 0;
    CodeGen g = {out, 0, arena, diag, NULL, 0, 0};

    for (size_t i = 0; i < ir->count; i++) {
        const IrInstr *ins = &ir->instrs[i];
        if (ins->op == IR_ADD) {
            Operand ops[2] = {ins->a, ins->b};
            if (!load_operands(&g, ops, 2) || !emit(&g, OP_ADD, 0)) {
                return false;
            }
            g.depth -= 2; /* ADD pops two values ... */
            if (!model_push(&g, ins->dest)) { /* ... and pushes the sum */
                return false;
            }
        } else { /* IR_RETURN */
            if (!load_operands(&g, &ins->a, 1) || !emit(&g, OP_RET, 0)) {
                return false;
            }
            g.depth -= 1;
            return true; /* nothing after return is reachable */
        }
    }
    return internal_error(&g, "TAC does not end with return");
}
