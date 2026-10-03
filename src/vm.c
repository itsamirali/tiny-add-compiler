/*
 * vm.c - Stage [7]: stack virtual machine (see vm.h).
 *
 * The classic fetch-execute loop:
 *     fetch code[pc] -> execute it on the stack -> record the step -> pc++
 */
#include "vm.h"

#include <string.h>

typedef struct {
    VmTrace *trace;
    size_t   cap;
    Arena   *arena;
} Recorder;

/* Save a copy of the stack after one instruction. */
static bool record(Recorder *r, size_t pc, Instr instr, const uint32_t *stack,
                   size_t depth) {
    if (r->trace == NULL) {
        return true;
    }
    VmTrace *t = r->trace;
    if (t->count == r->cap) {
        size_t new_cap = r->cap == 0 ? 16 : r->cap * 2;
        if (new_cap > SIZE_MAX / sizeof(VmStep)) {
            return false;
        }
        VmStep *bigger = arena_grow(r->arena, t->steps, r->cap * sizeof(VmStep),
                                    new_cap * sizeof(VmStep));
        if (bigger == NULL) {
            return false;
        }
        t->steps = bigger;
        r->cap = new_cap;
    }
    uint32_t *copy = NULL;
    if (depth > 0) {
        copy = arena_alloc(r->arena, depth * sizeof(uint32_t));
        if (copy == NULL) {
            return false;
        }
        memcpy(copy, stack, depth * sizeof(uint32_t));
    }
    t->steps[t->count++] = (VmStep){pc, instr, copy, depth};
    return true;
}

static bool vm_error(Diag *diag, size_t pc, const char *what) {
    SrcLoc none = {0, 0};
    diag_set(diag, DIAG_VM, none, "%s (at address %04lu)", what, (unsigned long)pc);
    return false;
}

bool vm_run(const Bytecode *code, Arena *arena, VmTrace *trace,
            uint32_t *result, Diag *diag) {
    uint32_t stack[VM_STACK_MAX];
    size_t depth = 0;
    Recorder rec = {trace, 0, arena};
    if (trace != NULL) {
        trace->steps = NULL;
        trace->count = 0;
    }

    for (size_t pc = 0;; pc++) {
        if (pc >= code->count) {
            return vm_error(diag, pc, "ran past the end of the program without RET");
        }
        Instr in = code->code[pc];

        switch (in.op) {
        case OP_PUSH:
            if (depth == VM_STACK_MAX) {
                return vm_error(diag, pc, "stack overflow");
            }
            stack[depth++] = in.operand;
            break;

        case OP_ADD: {
            if (depth < 2) {
                return vm_error(diag, pc, "stack underflow: ADD needs two values");
            }
            uint64_t sum = (uint64_t)stack[depth - 2] + stack[depth - 1];
            if (sum > UINT32_MAX) {
                return vm_error(diag, pc, "ADD overflow: result does not fit in uint32");
            }
            depth -= 2;
            stack[depth++] = (uint32_t)sum;
            break;
        }

        case OP_RET:
            if (depth < 1) {
                return vm_error(diag, pc, "stack underflow: RET needs a value");
            }
            *result = stack[--depth];
            if (!record(&rec, pc, in, stack, depth)) {
                diag_out_of_memory(diag);
                return false;
            }
            return true;

        case OP_HALT:
            if (!record(&rec, pc, in, stack, depth)) {
                diag_out_of_memory(diag);
                return false;
            }
            return vm_error(diag, pc, "HALT reached before RET, no result");

        default:
            return vm_error(diag, pc, "unknown opcode");
        }

        if (!record(&rec, pc, in, stack, depth)) {
            diag_out_of_memory(diag);
            return false;
        }
    }
}
