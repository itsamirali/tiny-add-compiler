/*
 * common.h - Shared types and helpers used by every module of tinyadd.
 *
 * This header holds the few things that all compiler stages need to agree
 * on: a position in the source text (line and column), the list of pipeline
 * stages, the process exit codes, and a couple of small macros. It has no
 * logic of its own and depends only on the C standard library.
 */
#ifndef TINYADD_COMMON_H
#define TINYADD_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A position in the source program. Both fields are 1-based. */
typedef struct {
    int line;
    int col;
} SrcLoc;

/* The seven stages of the pipeline, in order. STAGE_NONE means "all". */
typedef enum {
    STAGE_NONE = 0,
    STAGE_LEX,   /* [1] lexer            */
    STAGE_PARSE, /* [2] parser (AST)     */
    STAGE_SEM,   /* [3] semantic checks  */
    STAGE_IR,    /* [4] three-address IR */
    STAGE_OPT,   /* [5] optimizer        */
    STAGE_CODE,  /* [6] bytecode codegen */
    STAGE_VM     /* [7] VM execution     */
} Stage;

/* Process exit codes (SPEC section 6). */
enum {
    TA_EXIT_OK = 0,            /* success                              */
    TA_EXIT_COMPILE_ERROR = 1, /* lexer / parser / semantic error      */
    TA_EXIT_USAGE = 2          /* bad command line or internal error   */
};

/* Number of elements in a fixed-size array. */
#define TA_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Silence "unused parameter" warnings on purpose. */
#define TA_UNUSED(x) ((void)(x))

#endif /* TINYADD_COMMON_H */
