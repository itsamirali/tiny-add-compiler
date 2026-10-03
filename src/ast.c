/*
 * ast.c - AST node construction helpers (see ast.h).
 */
#include "ast.h"

AstNode *ast_new_num(Arena *arena, const Token *number) {
    AstNode *n = arena_alloc(arena, sizeof *n);
    if (n == NULL) {
        return NULL;
    }
    n->kind = AST_NUM;
    n->loc = number->loc;
    n->lexeme = number->lexeme;
    return n;
}

AstNode *ast_new_add(Arena *arena, AstNode *left, AstNode *right, SrcLoc plus) {
    AstNode *n = arena_alloc(arena, sizeof *n);
    if (n == NULL) {
        return NULL;
    }
    n->kind = AST_ADD;
    n->loc = plus;
    n->left = left;
    n->right = right;
    return n;
}

bool ast_left_spine(AstNode *root, Arena *arena, AstNode ***out_adds,
                    size_t *out_count, AstNode **out_leaf) {
    /* First count the Add nodes on the left spine... */
    size_t count = 0;
    AstNode *node = root;
    while (node->kind == AST_ADD) {
        count++;
        node = node->left;
    }
    *out_leaf = node;
    *out_count = count;
    *out_adds = NULL;
    if (count == 0) {
        return true;
    }

    /* ...then store them, the deepest one first (source order). */
    if (count > SIZE_MAX / sizeof(AstNode *)) {
        return false;
    }
    AstNode **adds = arena_alloc(arena, count * sizeof(AstNode *));
    if (adds == NULL) {
        return false;
    }
    size_t i = count;
    node = root;
    while (node->kind == AST_ADD) {
        adds[--i] = node;
        node = node->left;
    }
    *out_adds = adds;
    return true;
}
