/*
 * bytecode.h - The instruction set of the tinyadd stack machine.
 *
 * This header is the contract between the code generator (which writes
 * bytecode) and the VM (which runs it). The machine has a value stack and
 * four instructions:
 *
 *     PUSH <n>  push the constant n
 *     ADD       pop two values, push their sum
 *     RET       pop the result and stop
 *     HALT      stop immediately without a result (a safety stop; the
 *               code generator never emits it, the VM reports an error)
 *
 * A program is an array of instructions; the "address" of an instruction
 * is its index in the array (0000, 0001, ...).
 */
#ifndef TINYADD_BYTECODE_H
#define TINYADD_BYTECODE_H

#include "common.h"

typedef enum {
    OP_PUSH,
    OP_ADD,
    OP_RET,
    OP_HALT
} OpCode;

typedef struct {
    OpCode   op;
    uint32_t operand; /* used by PUSH only */
} Instr;

typedef struct {
    Instr *code;  /* owned by the arena */
    size_t count;
} Bytecode;

/* Mnemonic of an opcode: "PUSH", "ADD", "RET" or "HALT". */
static inline const char *opcode_name(OpCode op) {
    switch (op) {
    case OP_PUSH: return "PUSH";
    case OP_ADD:  return "ADD";
    case OP_RET:  return "RET";
    case OP_HALT: return "HALT";
    }
    return "???";
}

#endif /* TINYADD_BYTECODE_H */
