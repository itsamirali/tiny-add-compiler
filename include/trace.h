/*
 * trace.h - Human-readable printing of every stage.
 *
 * Compiler stages never print; they only return data. This module turns
 * that data into the report described in SPEC section 7: one titled,
 * numbered section per stage (token table, AST tree, semantic summary, TAC,
 * folding log, disassembly, VM stack trace). It also prints diagnostics in
 * the SPEC section 8 format (stage, message, line:col, source line, '^').
 *
 * Output style:
 *   - colors (ANSI escape codes) are used only when the stream is a
 *     terminal, NO_COLOR is not set and --no-color was not given;
 *   - --ascii replaces Unicode box-drawing characters with plain ASCII.
 *
 * All printing goes through an `Out`, which is either a FILE* (stdout,
 * stderr) or a growing memory buffer. Tests use the memory buffer to
 * compare the exact text.
 */
#ifndef TINYADD_TRACE_H
#define TINYADD_TRACE_H

#include <stdio.h>

#include "ast.h"
#include "bytecode.h"
#include "common.h"
#include "diag.h"
#include "ir.h"
#include "lexer.h"
#include "optimizer.h"
#include "semantic.h"
#include "vm.h"

/* ---- output destination ---- */

typedef struct {
    FILE  *file; /* if not NULL, everything is written here          */
    char  *buf;  /* otherwise: malloc'ed text, always '\0'-terminated */
    size_t len;
    size_t cap;
    bool   oom;  /* a memory write failed; the text is incomplete    */
} Out;

Out  out_to_file(FILE *file);
Out  out_to_memory(void);
void out_write(Out *out, const char *text, size_t len);
void out_puts(Out *out, const char *text);
void out_printf(Out *out, const char *fmt, ...);
const char *out_text(const Out *out); /* memory text ("" if empty) */
void out_free(Out *out);              /* frees the memory buffer    */

/* ---- style ---- */

typedef struct {
    bool color; /* use ANSI colors           */
    bool ascii; /* use only ASCII characters */
} TraceStyle;

/* Prepare the terminal (on Windows: UTF-8 output and ANSI colors).
 * Call once at program start. Does nothing on Linux/macOS. */
void trace_platform_init(void);

/* Decide the style for `stream` from the flags, NO_COLOR and isatty(). */
TraceStyle trace_style_for(FILE *stream, bool no_color_flag, bool ascii_flag);

/* ---- stage report ---- */

typedef struct {
    Out       *out;
    TraceStyle style;
    int        sections; /* sections printed so far (for blank lines) */
} Trace;

void trace_init(Trace *t, Out *out, TraceStyle style);

void trace_source(Trace *t, const char *source, size_t len);
void trace_tokens(Trace *t, const TokenList *tokens);
void trace_ast(Trace *t, const AstNode *root);
void trace_semantic(Trace *t, const SemanticInfo *info);
void trace_ir(Trace *t, const IrProgram *ir);
/* `before_after`: also show the TAC before and after side by side (used
 * by --stage=opt, where stage [4] is not printed). */
void trace_opt(Trace *t, const IrProgram *before, const IrProgram *after,
               const FoldLog *log, bool before_after);
void trace_opt_disabled(Trace *t);
void trace_code(Trace *t, const Bytecode *code);
void trace_vm(Trace *t, const VmTrace *vm);
void trace_result(Trace *t, uint32_t result);

/* ---- errors ---- */

/* Print `diag` in the SPEC section 8 format, using `source` to show the
 * offending line. */
void trace_diag(Out *out, TraceStyle style, const Diag *diag,
                const char *source, size_t source_len);

#endif /* TINYADD_TRACE_H */
