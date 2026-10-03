/*
 * main.c - Command line interface of tinyadd.
 *
 * Parses the command line (SPEC section 6), loads the program text either
 * from a file or from -e, and runs the seven-stage pipeline:
 *
 *     lexer -> parser -> semantic -> IR -> optimizer -> codegen -> VM
 *
 * After each stage it asks trace.c to print that stage when --trace or a
 * matching --stage=<name> was given. Stages never print or exit; when one
 * fails it returns a Diag, which is printed here (to stderr). This is the
 * only place that decides the process exit code.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "codegen.h"
#include "common.h"
#include "diag.h"
#include "ir.h"
#include "lexer.h"
#include "optimizer.h"
#include "parser.h"
#include "semantic.h"
#include "trace.h"
#include "vm.h"

typedef struct {
    const char *file;     /* path of the .tadd file, or NULL         */
    const char *expr;     /* program text given with -e, or NULL     */
    Stage       stage;    /* --stage=<name>, STAGE_NONE if not given */
    bool        trace;    /* --trace: print every stage              */
    bool        no_opt;   /* --no-opt: skip the optimizer            */
    bool        ascii;    /* --ascii: plain ASCII output             */
    bool        no_color; /* --no-color: no ANSI colors              */
    bool        help;     /* --help                                  */
} Options;

static const struct {
    const char *name;
    Stage       stage;
} STAGE_NAMES[] = {
    {"lex", STAGE_LEX}, {"parse", STAGE_PARSE}, {"sem", STAGE_SEM},
    {"ir", STAGE_IR},   {"opt", STAGE_OPT},     {"code", STAGE_CODE},
    {"vm", STAGE_VM},
};

static void print_usage(FILE *out) {
    fputs("Usage:\n"
          "  tinyadd [options] <file.tadd>\n"
          "  tinyadd [options] -e \"<program>\"\n"
          "\n"
          "Options:\n"
          "  -e <program>      compile and run <program> given on the command line\n"
          "  --trace           show every compiler stage, step by step\n"
          "  --stage=<name>    show only one stage: lex | parse | sem | ir | opt | code | vm\n"
          "  --no-opt          disable the optimizer (constant folding)\n"
          "  --ascii           use plain ASCII instead of Unicode box characters\n"
          "  --no-color        disable colored output\n"
          "  --help            show this help and exit\n"
          "\n"
          "Exit codes: 0 success, 1 compile error, 2 usage or internal error.\n",
          out);
}

static void usage_error(const char *msg, const char *arg) {
    if (arg != NULL) {
        fprintf(stderr, "tinyadd: error: %s '%s'\n", msg, arg);
    } else {
        fprintf(stderr, "tinyadd: error: %s\n", msg);
    }
    fputs("Try 'tinyadd --help' for more information.\n", stderr);
}

static bool stage_from_name(const char *name, Stage *out) {
    for (size_t i = 0; i < TA_ARRAY_LEN(STAGE_NAMES); i++) {
        if (strcmp(name, STAGE_NAMES[i].name) == 0) {
            *out = STAGE_NAMES[i].stage;
            return true;
        }
    }
    return false;
}

/* Fill `opt` from argv. Returns false (after printing why) on bad usage. */
static bool parse_args(int argc, char **argv, Options *opt) {
    memset(opt, 0, sizeof *opt);
    opt->stage = STAGE_NONE;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            opt->help = true;
        } else if (strcmp(arg, "--trace") == 0) {
            opt->trace = true;
        } else if (strcmp(arg, "--no-opt") == 0) {
            opt->no_opt = true;
        } else if (strcmp(arg, "--ascii") == 0) {
            opt->ascii = true;
        } else if (strcmp(arg, "--no-color") == 0) {
            opt->no_color = true;
        } else if (strncmp(arg, "--stage=", 8) == 0) {
            if (opt->stage != STAGE_NONE) {
                usage_error("--stage given more than once", NULL);
                return false;
            }
            if (!stage_from_name(arg + 8, &opt->stage)) {
                usage_error("unknown stage", arg + 8);
                return false;
            }
        } else if (strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) {
                usage_error("-e needs a program, e.g. -e \"1 + 2\"", NULL);
                return false;
            }
            if (opt->expr != NULL) {
                usage_error("-e given more than once", NULL);
                return false;
            }
            opt->expr = argv[++i];
        } else if (arg[0] == '-') {
            usage_error("unknown option", arg);
            return false;
        } else {
            if (opt->file != NULL) {
                usage_error("only one input file is allowed, extra", arg);
                return false;
            }
            opt->file = arg;
        }
    }

    if (opt->help) {
        return true;
    }
    if (opt->file != NULL && opt->expr != NULL) {
        usage_error("give either a file or -e, not both", NULL);
        return false;
    }
    if (opt->file == NULL && opt->expr == NULL) {
        usage_error("no input; give a file or -e \"<program>\"", NULL);
        return false;
    }
    if (opt->trace && opt->stage != STAGE_NONE) {
        usage_error("--trace and --stage cannot be used together", NULL);
        return false;
    }
    return true;
}

/* Read a whole file into a malloc'ed, '\0'-terminated buffer.
 * Returns NULL on failure and sets *reason to a short explanation. */
static char *read_file(const char *path, size_t *out_len, const char **reason) {
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        *reason = errno != 0 ? strerror(errno) : "cannot open file";
        return NULL;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (buf == NULL) {
        fclose(f);
        *reason = "out of memory";
        return NULL;
    }

    for (;;) {
        if (cap - len < 2) { /* keep room for the final '\0' */
            char *bigger = cap <= SIZE_MAX / 2 ? realloc(buf, cap * 2) : NULL;
            if (bigger == NULL) {
                free(buf);
                fclose(f);
                *reason = "out of memory";
                return NULL;
            }
            buf = bigger;
            cap *= 2;
        }
        size_t n = fread(buf + len, 1, cap - len - 1, f);
        len += n;
        if (n == 0) {
            break;
        }
    }

    if (ferror(f)) {
        free(buf);
        fclose(f);
        *reason = "read error";
        return NULL;
    }
    fclose(f);

    buf[len] = '\0';
    *out_len = len;
    return buf;
}

/* Should stage `s` be printed? */
static bool show(const Options *opt, Stage s) {
    return opt->trace || opt->stage == s;
}

/* Run the stages one after the other, printing what was asked for.
 * Returns false if a stage failed (then *diag describes the error).
 * With --stage=<name> it stops successfully right after that stage. */
static bool run_stages(const Options *opt, const char *source, size_t len,
                       Arena *arena, Trace *tr, Diag *diag) {
    if (opt->trace) {
        trace_source(tr, source, len);
    }

    /* [1] lexer: text -> tokens (tokens read so far are shown on error) */
    TokenList tokens;
    bool ok = lexer_run(source, len, arena, &tokens, diag);
    if (show(opt, STAGE_LEX)) {
        trace_tokens(tr, &tokens);
    }
    if (!ok) {
        return false;
    }
    if (opt->stage == STAGE_LEX) {
        return true;
    }

    /* [2] parser: tokens -> AST */
    AstNode *ast;
    if (!parser_run(&tokens, arena, &ast, diag)) {
        return false;
    }
    if (show(opt, STAGE_PARSE)) {
        trace_ast(tr, ast);
    }
    if (opt->stage == STAGE_PARSE) {
        return true;
    }

    /* [3] semantic analysis: check and annotate the AST */
    SemanticInfo info;
    if (!semantic_run(ast, arena, &info, diag)) {
        return false;
    }
    if (show(opt, STAGE_SEM)) {
        trace_semantic(tr, &info);
    }
    if (opt->stage == STAGE_SEM) {
        return true;
    }

    /* [4] IR generation: AST -> three-address code */
    IrProgram ir;
    if (!ir_generate(ast, arena, &ir, diag)) {
        return false;
    }
    if (show(opt, STAGE_IR)) {
        trace_ir(tr, &ir);
    }
    if (opt->stage == STAGE_IR) {
        return true;
    }

    /* [5] optimizer: constant folding (skipped with --no-opt) */
    IrProgram final_ir = ir;
    if (opt->no_opt) {
        if (show(opt, STAGE_OPT)) {
            trace_opt_disabled(tr);
        }
    } else {
        FoldLog log;
        if (!optimizer_run(&ir, arena, &final_ir, &log, diag)) {
            return false;
        }
        if (show(opt, STAGE_OPT)) {
            trace_opt(tr, &ir, &final_ir, &log, opt->stage == STAGE_OPT);
        }
    }
    if (opt->stage == STAGE_OPT) {
        return true;
    }

    /* [6] code generation: TAC -> bytecode */
    Bytecode code;
    if (!codegen_run(&final_ir, arena, &code, diag)) {
        return false;
    }
    if (show(opt, STAGE_CODE)) {
        trace_code(tr, &code);
    }
    if (opt->stage == STAGE_CODE) {
        return true;
    }

    /* [7] VM: run the bytecode */
    bool print_vm = show(opt, STAGE_VM);
    VmTrace vm_trace;
    uint32_t result = 0;
    ok = vm_run(&code, arena, print_vm ? &vm_trace : NULL, &result, diag);
    if (print_vm) {
        trace_vm(tr, &vm_trace);
    }
    if (!ok) {
        return false;
    }
    if (print_vm) {
        trace_result(tr, result);
    } else {
        out_printf(tr->out, "%lu\n", (unsigned long)result); /* plain run */
    }
    return true;
}

/* Compile and run one program; returns the process exit code. */
static int run_pipeline(const Options *opt, const char *source, size_t len) {
    Out out = out_to_file(stdout);
    Out err = out_to_file(stderr);
    Trace tr;
    trace_init(&tr, &out, trace_style_for(stdout, opt->no_color, opt->ascii));
    TraceStyle err_style = trace_style_for(stderr, opt->no_color, opt->ascii);

    Arena arena;
    arena_init(&arena);
    Diag diag;
    memset(&diag, 0, sizeof diag);

    int code = TA_EXIT_OK;
    if (!run_stages(opt, source, len, &arena, &tr, &diag)) {
        if (tr.sections > 0) {
            out_puts(&out, "\n"); /* separate the report from the error */
        }
        fflush(stdout); /* keep stdout/stderr in order on a terminal */
        trace_diag(&err, err_style, &diag, source, len);
        code = diag_exit_code(&diag);
    }
    fflush(stdout);
    arena_free(&arena);
    return code;
}

int main(int argc, char **argv) {
    trace_platform_init();

    Options opt;
    if (!parse_args(argc, argv, &opt)) {
        return TA_EXIT_USAGE;
    }
    if (opt.help) {
        print_usage(stdout);
        return TA_EXIT_OK;
    }

    /* Load the program text. `owned` is non-NULL only if we must free it. */
    char *owned = NULL;
    const char *source;
    size_t source_len;
    if (opt.file != NULL) {
        const char *reason = NULL;
        owned = read_file(opt.file, &source_len, &reason);
        if (owned == NULL) {
            fprintf(stderr, "tinyadd: error: cannot read '%s': %s\n", opt.file,
                    reason);
            return TA_EXIT_USAGE;
        }
        source = owned;
    } else {
        source = opt.expr;
        source_len = strlen(opt.expr);
    }

    int code = run_pipeline(&opt, source, source_len);
    free(owned);
    return code;
}
