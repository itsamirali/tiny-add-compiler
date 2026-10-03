/*
 * ir.h - Stage [4]: intermediate representation (three-address code).
 *
 * The checked AST is flattened into a list of simple instructions, each
 * with at most one operator, using temporaries t1, t2, ... For example
 * "1 + 2 + 3" becomes:
 *
 *     t1 = 1 + 2
 *     t2 = t1 + 3
 *     return t2
 *
 * An operand is either a constant or a temporary. There are only two
 * instructions: ADD (t = a + b) and RETURN (return a). The optimizer and
 * the code generator both work on this representation.
 *
 * Input:  the checked (annotated) AST.
 * Output: a list of TAC instructions ending with RETURN.
 */
#ifndef TINYADD_IR_H
#define TINYADD_IR_H

#include "arena.h"
#include "ast.h"
#include "common.h"
#include "diag.h"

typedef enum {
    OPND_CONST, /* a constant number, e.g. 3   */
    OPND_TEMP   /* a temporary, e.g. t2         */
} OperandKind;

typedef struct {
    OperandKind kind;
    uint32_t    value; /* the constant, or the temporary's number (2 for t2) */
} Operand;

typedef enum {
    IR_ADD,   /* dest = a + b */
    IR_RETURN /* return a     */
} IrOp;

typedef struct {
    IrOp     op;
    uint32_t dest; /* IR_ADD: number of the temporary written */
    Operand  a;    /* first operand (the value for RETURN)    */
    Operand  b;    /* IR_ADD: second operand                  */
} IrInstr;

typedef struct {
    IrInstr *instrs;     /* owned by the arena               */
    size_t   count;
    uint32_t temp_count; /* temporaries are t1 .. t<temp_count> */
} IrProgram;

/* Generate TAC from a checked AST. Returns false and fills *diag on error
 * (only possible for out-of-memory or an unchecked tree). */
bool ir_generate(AstNode *root, Arena *arena, IrProgram *out, Diag *diag);

/* Append one instruction to `prog` (used by ir.c and the optimizer).
 * `cap` is the caller's capacity counter. Returns false on out-of-memory. */
bool ir_append(IrProgram *prog, size_t *cap, Arena *arena, IrInstr instr);

/* Plain text of an operand ("3" or "t2") or an instruction
 * ("t1 = 1 + 2", "return t2"). Returns the number of characters written
 * (like snprintf). Used by tests and by trace.c for column widths. */
int ir_format_operand(Operand op, char *buf, size_t size);
int ir_format_instr(const IrInstr *instr, char *buf, size_t size);

#endif /* TINYADD_IR_H */
