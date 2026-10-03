/*
 * codegen.h - Stage [6]: code generation.
 *
 * Translates TAC into stack-machine bytecode (see bytecode.h). TAC is
 * produced in post-order, so when an instruction "t = a + b" runs, any
 * temporary it uses is already on top of the stack. The code generator
 * therefore only has to PUSH the constant operands and then emit ADD;
 * "return x" becomes (PUSH x if x is a constant, then) RET. For
 * "1 + 2 + 3" without optimization the result is:
 *
 *     0000  PUSH 1
 *     0001  PUSH 2
 *     0002  ADD
 *     0003  PUSH 3
 *     0004  ADD
 *     0005  RET
 *
 * Input:  a list of TAC instructions.
 * Output: a bytecode program.
 */
#ifndef TINYADD_CODEGEN_H
#define TINYADD_CODEGEN_H

#include "arena.h"
#include "bytecode.h"
#include "common.h"
#include "diag.h"
#include "ir.h"

/* Generate bytecode. Returns false and fills *diag on out-of-memory or if
 * the TAC uses a temporary that is not on top of the stack (a bug). */
bool codegen_run(const IrProgram *ir, Arena *arena, Bytecode *out, Diag *diag);

#endif /* TINYADD_CODEGEN_H */
