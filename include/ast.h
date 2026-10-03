/*
 * ast.h - The abstract syntax tree (AST).
 *
 * The AST is the tree form of a program, built by the parser. TinyAdd needs
 * only two node kinds: Num (a number literal) and Add (two sub-trees added
 * together). Because '+' is left-associative, "1 + 2 + 3" becomes
 *
 *     Add
 *     ├── Add
 *     │   ├── Num(1)
 *     │   └── Num(2)
 *     └── Num(3)
 *
 * Each node keeps its source location (for Add: the '+' sign). The
 * semantic stage annotates every Num node with its checked numeric value.
 *
 * Trees for long programs are long "left combs" (Add -> Add -> Add ...).
 * To avoid deep recursion, the stages walk the left spine with a loop
 * using ast_left_spine(), and only recurse into right children.
 */
#ifndef TINYADD_AST_H
#define TINYADD_AST_H

#include "arena.h"
#include "common.h"
#include "token.h"

typedef enum {
    AST_NUM, /* a number literal       */
    AST_ADD  /* left + right           */
} AstKind;

typedef struct AstNode AstNode;

struct AstNode {
    AstKind kind;
    SrcLoc  loc;    /* Num: the literal; Add: the '+' sign */

    /* AST_NUM only */
    const char *lexeme;  /* digits exactly as written, e.g. "007"     */
    bool        checked; /* set by semantic analysis                   */
    uint32_t    value;   /* numeric value, valid only when `checked`   */

    /* AST_ADD only */
    AstNode *left;
    AstNode *right;
};

/* Create nodes in the arena. Return NULL on out-of-memory. */
AstNode *ast_new_num(Arena *arena, const Token *number);
AstNode *ast_new_add(Arena *arena, AstNode *left, AstNode *right, SrcLoc plus);

/* Walk down the chain of left children starting at `root`.
 * Stores the Add nodes found on the way in *out_adds, ordered from the
 * deepest one up to `root` (that is, in source order), their number in
 * *out_count, and the leftmost non-Add node in *out_leaf.
 * For "1 + 2 + 3": adds = [Add(1,2), Add(.,3)], leaf = Num(1).
 * Returns false on out-of-memory. */
bool ast_left_spine(AstNode *root, Arena *arena, AstNode ***out_adds,
                    size_t *out_count, AstNode **out_leaf);

#endif /* TINYADD_AST_H */
