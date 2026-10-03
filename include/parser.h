/*
 * parser.h - Stage [2]: syntax analysis.
 *
 * A hand-written recursive descent parser for the grammar
 *
 *     program := expr EOF
 *     expr    := NUMBER ( '+' NUMBER )*
 *
 * There is one C function per grammar rule (parse_program, parse_expr,
 * parse_number). The repetition ( '+' NUMBER )* is a loop that keeps
 * wrapping the tree built so far in a new Add node, which makes '+'
 * left-associative. On the first syntax error the parser stops and reports
 * what it expected and what it found, with line and column.
 *
 * Input:  the token array from the lexer (must end with EOF).
 * Output: the root of the AST.
 */
#ifndef TINYADD_PARSER_H
#define TINYADD_PARSER_H

#include "arena.h"
#include "ast.h"
#include "common.h"
#include "diag.h"
#include "lexer.h"

/* Parse the tokens. Returns true and stores the tree in *out_root, or
 * returns false and fills *diag. */
bool parser_run(const TokenList *tokens, Arena *arena, AstNode **out_root,
                Diag *diag);

#endif /* TINYADD_PARSER_H */
