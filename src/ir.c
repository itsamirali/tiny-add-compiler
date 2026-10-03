/*
 * ir.c - Stage [4]: AST to three-address code (see ir.h).
 *
 * gen(Num)      -> the constant itself, no instruction
 * gen(Add l r)  -> a = gen(l); b = gen(r); emit "tN = a + b"; result tN
 * program       -> x = gen(root); emit "return x"
 *
 * The left spine is processed with a loop, bottom-up, which produces
 * exactly the same instructions as the recursive definition above.
 */
#include "ir.h"

#include <stdio.h>

typedef struct {
    IrProgram *prog;
    size_t     cap;
    Arena     *arena;
    Diag      *diag;
} IrGen;

bool ir_append(IrProgram *prog, size_t *cap, Arena *arena, IrInstr instr) {
    if (prog->count == *cap) {
        size_t new_cap = *cap == 0 ? 16 : *cap * 2;
        if (new_cap > SIZE_MAX / sizeof(IrInstr)) {
            return false;
        }
        IrInstr *bigger = arena_grow(arena, prog->instrs, *cap * sizeof(IrInstr),
                                     new_cap * sizeof(IrInstr));
        if (bigger == NULL) {
            return false;
        }
        prog->instrs = bigger;
        *cap = new_cap;
    }
    prog->instrs[prog->count++] = instr;
    return true;
}

static bool emit(IrGen *g, IrInstr instr) {
    if (!ir_append(g->prog, &g->cap, g->arena, instr)) {
        diag_out_of_memory(g->diag);
        return false;
    }
    return true;
}

static bool gen_num(IrGen *g, const AstNode *num, Operand *out) {
    if (num->kind != AST_NUM || !num->checked) {
        SrcLoc none = {0, 0};
        diag_set(g->diag, DIAG_INTERNAL, none,
                 "IR generation needs a checked AST (run semantic analysis first)");
        return false;
    }
    *out = (Operand){OPND_CONST, num->value};
    return true;
}

static bool gen(IrGen *g, AstNode *node, Operand *out) {
    AstNode **adds;
    size_t n_adds;
    AstNode *leaf;
    if (!ast_left_spine(node, g->arena, &adds, &n_adds, &leaf)) {
        diag_out_of_memory(g->diag);
        return false;
    }
    Operand acc;
    if (!gen_num(g, leaf, &acc)) {
        return false;
    }
    for (size_t i = 0; i < n_adds; i++) {
        Operand right;
        if (!gen(g, adds[i]->right, &right)) {
            return false;
        }
        uint32_t t = ++g->prog->temp_count;
        IrInstr add = {IR_ADD, t, acc, right};
        if (!emit(g, add)) {
            return false;
        }
        acc = (Operand){OPND_TEMP, t};
    }
    *out = acc;
    return true;
}

bool ir_generate(AstNode *root, Arena *arena, IrProgram *out, Diag *diag) {
    out->instrs = NULL;
    out->count = 0;
    out->temp_count = 0;
    IrGen g = {out, 0, arena, diag};

    Operand result;
    if (!gen(&g, root, &result)) {
        return false;
    }
    IrInstr ret = {IR_RETURN, 0, result, {OPND_CONST, 0}};
    return emit(&g, ret);
}

int ir_format_operand(Operand op, char *buf, size_t size) {
    if (op.kind == OPND_TEMP) {
        return snprintf(buf, size, "t%lu", (unsigned long)op.value);
    }
    return snprintf(buf, size, "%lu", (unsigned long)op.value);
}

int ir_format_instr(const IrInstr *instr, char *buf, size_t size) {
    char a[16];
    char b[16];
    ir_format_operand(instr->a, a, sizeof a);
    if (instr->op == IR_RETURN) {
        return snprintf(buf, size, "return %s", a);
    }
    ir_format_operand(instr->b, b, sizeof b);
    return snprintf(buf, size, "t%lu = %s + %s", (unsigned long)instr->dest, a, b);
}
