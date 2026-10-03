/*
 * vm.h - Stage [7]: the virtual machine.
 *
 * A small stack-based interpreter for the bytecode in bytecode.h. It runs
 * the program one instruction at a time. When asked, it records the stack
 * after every step, so --trace can show how the result is built. It checks
 * for stack underflow and overflow, ADD overflow, HALT, and running past
 * the last instruction, even though correct bytecode never causes them,
 * and reports such problems as error[vm] instead of crashing.
 *
 * Input:  a bytecode program.
 * Output: the final result (a uint32_t value), or a runtime error.
 */
#ifndef TINYADD_VM_H
#define TINYADD_VM_H

#include "arena.h"
#include "bytecode.h"
#include "common.h"
#include "diag.h"

#define VM_STACK_MAX 256

/* The machine state after one executed instruction. */
typedef struct {
    size_t          pc;    /* address of the instruction that ran */
    Instr           instr; /* the instruction itself              */
    const uint32_t *stack; /* copy of the stack after it (arena)  */
    size_t          depth; /* number of values on the stack       */
} VmStep;

typedef struct {
    VmStep *steps; /* owned by the arena */
    size_t  count;
} VmTrace;

/* Run `code`. If `trace` is not NULL every step is recorded into it (also
 * the steps before a runtime error). Returns true and stores the result,
 * or returns false and fills *diag. */
bool vm_run(const Bytecode *code, Arena *arena, VmTrace *trace,
            uint32_t *result, Diag *diag);

#endif /* TINYADD_VM_H */
