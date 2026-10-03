/*
 * diag.h - Diagnostics (error reporting) shared by all stages.
 *
 * A stage that finds a problem does not print it and does not exit. It
 * fills in a Diag record (which stage, the message, line:column) and
 * returns false. main.c then asks trace.c to print the record in the
 * SPEC section 8 format: the stage, the message, the location, the source
 * line and a '^' under the error position. Compilation stops at the first
 * error, so one Diag is enough.
 */
#ifndef TINYADD_DIAG_H
#define TINYADD_DIAG_H

#include "common.h"

/* Which part of the compiler reported the problem. */
typedef enum {
    DIAG_LEXER,    /* error[lexer]    - exit code 1 */
    DIAG_PARSER,   /* error[parser]   - exit code 1 */
    DIAG_SEMANTIC, /* error[semantic] - exit code 1 */
    DIAG_VM,       /* error[vm]       - exit code 2 (bad bytecode) */
    DIAG_INTERNAL  /* error[internal] - exit code 2 (e.g. out of memory) */
} DiagStage;

#define DIAG_MESSAGE_MAX 256

typedef struct {
    bool      is_set;                    /* false until an error is stored  */
    DiagStage stage;                     /* who reported it                 */
    SrcLoc    loc;                       /* line 0 means "no source position" */
    char      message[DIAG_MESSAGE_MAX]; /* one line, no trailing newline   */
} Diag;

/* Store an error. `fmt` is a printf-style format. Too-long messages are
 * cut. If `diag` already holds an error, the first one is kept. */
void diag_set(Diag *diag, DiagStage stage, SrcLoc loc, const char *fmt, ...);

/* Shortcut for running out of memory inside a stage. */
void diag_out_of_memory(Diag *diag);

/* "lexer", "parser", "semantic", "vm" or "internal". */
const char *diag_stage_name(DiagStage stage);

/* Process exit code for this error: 1 for compile errors, 2 otherwise. */
int diag_exit_code(const Diag *diag);

/* Find line number `line` (1-based) inside `source`. Returns a pointer to
 * its first character and stores its length (without "\n" or "\r\n") in
 * *out_len. Returns NULL if the line does not exist. */
const char *diag_find_line(const char *source, size_t source_len, int line,
                           size_t *out_len);

#endif /* TINYADD_DIAG_H */
