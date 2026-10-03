/*
 * trace.c - Printing of every stage (see trace.h).
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L /* for isatty() and fileno() */
#endif

#include "trace.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

/* =====================================================================
 * Out: a FILE* or a growing memory buffer
 * ===================================================================== */

Out out_to_file(FILE *file) {
    Out o = {file, NULL, 0, 0, false};
    return o;
}

Out out_to_memory(void) {
    Out o = {NULL, NULL, 0, 0, false};
    return o;
}

void out_write(Out *out, const char *text, size_t len) {
    if (out->file != NULL) {
        fwrite(text, 1, len, out->file);
        return;
    }
    if (out->oom || len == 0) {
        return;
    }
    if (len > SIZE_MAX - out->len - 1) {
        out->oom = true;
        return;
    }
    size_t need = out->len + len + 1;
    if (need > out->cap) {
        size_t cap = out->cap == 0 ? 256 : out->cap;
        while (cap < need) {
            cap = cap > SIZE_MAX / 2 ? need : cap * 2;
        }
        char *bigger = realloc(out->buf, cap);
        if (bigger == NULL) {
            out->oom = true;
            return;
        }
        out->buf = bigger;
        out->cap = cap;
    }
    memcpy(out->buf + out->len, text, len);
    out->len += len;
    out->buf[out->len] = '\0';
}

void out_puts(Out *out, const char *text) {
    out_write(out, text, strlen(text));
}

void out_printf(Out *out, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (out->file != NULL) {
        vfprintf(out->file, fmt, args);
        va_end(args);
        return;
    }
    va_list again;
    va_copy(again, args);
    char small[256];
    int n = vsnprintf(small, sizeof small, fmt, args);
    va_end(args);
    if (n >= 0 && (size_t)n < sizeof small) {
        out_write(out, small, (size_t)n);
    } else if (n >= 0) {
        char *big = malloc((size_t)n + 1);
        if (big == NULL) {
            out->oom = true;
        } else {
            vsnprintf(big, (size_t)n + 1, fmt, again);
            out_write(out, big, (size_t)n);
            free(big);
        }
    }
    va_end(again);
}

const char *out_text(const Out *out) {
    return out->buf != NULL ? out->buf : "";
}

void out_free(Out *out) {
    free(out->buf);
    out->buf = NULL;
    out->len = 0;
    out->cap = 0;
}

/* =====================================================================
 * Style: colors and symbols
 * ===================================================================== */

static bool g_console_colors_ok = true; /* false if Windows refuses ANSI */

void trace_platform_init(void) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
    DWORD handles[2] = {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (int i = 0; i < 2; i++) {
        HANDLE h = GetStdHandle(handles[i]);
        DWORD mode = 0;
        if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
            if (!SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                g_console_colors_ok = false;
            }
        }
    }
#endif
}

static bool stream_is_terminal(FILE *stream) {
#if defined(_WIN32)
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

TraceStyle trace_style_for(FILE *stream, bool no_color_flag, bool ascii_flag) {
    TraceStyle s = {false, ascii_flag};
    const char *no_color_env = getenv("NO_COLOR");
    bool env_off = no_color_env != NULL && no_color_env[0] != '\0';
    if (!no_color_flag && !env_off && g_console_colors_ok) {
        s.color = stream_is_terminal(stream);
    }
    return s;
}

/* ANSI escape codes */
#define A_RESET   "\x1b[0m"
#define A_BOLD    "\x1b[1m"
#define A_DIM     "\x1b[2m"
#define A_RED     "\x1b[1;31m"
#define A_GREEN   "\x1b[32m"
#define A_BGREEN  "\x1b[1;32m"
#define A_YELLOW  "\x1b[33m"
#define A_BYELLOW "\x1b[1;33m"
#define A_BLUE    "\x1b[1;34m"
#define A_MAGENTA "\x1b[35m"
#define A_CYAN    "\x1b[36m"
#define A_BCYAN   "\x1b[1;36m"

/* Return the escape code if colors are on, "" otherwise. */
static const char *col(TraceStyle s, const char *code) {
    return s.color ? code : "";
}

/* Unicode symbol or its ASCII replacement. */
static const char *sym(TraceStyle s, const char *unicode, const char *ascii) {
    return s.ascii ? ascii : unicode;
}

#define SYM_BAR(s)    sym((s), "\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90", "===") /* ═══ */
#define SYM_TEE(s)    sym((s), "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 ", "+-- ")  /* ├── */
#define SYM_LAST(s)   sym((s), "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 ", "`-- ")  /* └── */
#define SYM_PIPE(s)   sym((s), "\xE2\x94\x82   ", "|   ")                          /* │   */
#define SYM_CHECK(s)  sym((s), "\xE2\x9C\x94", "[ok]")                            /* ✔   */
#define SYM_ARROW(s)  sym((s), "\xE2\x86\x92", "->")                              /* →   */

/* Number of terminal columns used by a UTF-8 string (1 per character). */
static size_t display_width(const char *s) {
    size_t w = 0;
    for (; *s != '\0'; s++) {
        if (((unsigned char)*s & 0xC0u) != 0x80u) {
            w++;
        }
    }
    return w;
}

static void pad(Out *out, size_t n) {
    for (size_t i = 0; i < n; i++) {
        out_write(out, " ", 1);
    }
}

static size_t digits(unsigned long long n) {
    size_t d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

/* =====================================================================
 * Report sections
 * ===================================================================== */

void trace_init(Trace *t, Out *out, TraceStyle style) {
    t->out = out;
    t->style = style;
    t->sections = 0;
}

static void section(Trace *t, const char *title) {
    TraceStyle s = t->style;
    if (t->sections++ > 0) {
        out_puts(t->out, "\n");
    }
    out_printf(t->out, "%s%s %s %s%s\n", col(s, A_BCYAN), SYM_BAR(s), title,
               SYM_BAR(s), col(s, A_RESET));
}

void trace_source(Trace *t, const char *source, size_t len) {
    section(t, "Source");
    if (len == 0) {
        out_printf(t->out, "%s(empty)%s\n", col(t->style, A_DIM), col(t->style, A_RESET));
        return;
    }
    out_write(t->out, source, len);
    if (source[len - 1] != '\n') {
        out_puts(t->out, "\n");
    }
}

/* ---- [1] tokens ---- */

static const char *kind_color(TokenKind k) {
    switch (k) {
    case TOK_NUMBER:  return A_GREEN;
    case TOK_PLUS:    return A_YELLOW;
    case TOK_EOF:     return A_DIM;
    case TOK_INVALID: return A_RED;
    }
    return "";
}

void trace_tokens(Trace *t, const TokenList *tokens) {
    TraceStyle s = t->style;
    Out *o = t->out;
    section(t, "[1] Lexer");

    size_t idx_w = digits(tokens->count > 0 ? tokens->count - 1 : 0);
    size_t lex_w = 6; /* strlen("LEXEME") */
    for (size_t i = 0; i < tokens->count; i++) {
        size_t w = display_width(tokens->tokens[i].lexeme);
        if (w > lex_w) {
            lex_w = w;
        }
    }

    out_printf(o, "%s %*s  %-9s%-*s  LINE:COL%s\n", col(s, A_BOLD), (int)idx_w, "#",
               "KIND", (int)lex_w, "LEXEME", col(s, A_RESET));
    for (size_t i = 0; i < tokens->count; i++) {
        const Token *tok = &tokens->tokens[i];
        const char *kc = col(s, kind_color(tok->kind));
        const char *name = token_kind_name(tok->kind);
        out_printf(o, " %*lu  %s%s%s", (int)idx_w, (unsigned long)i, kc, name,
                   col(s, A_RESET));
        pad(o, 9 - strlen(name));
        out_printf(o, "%s%s%s%s", col(s, A_BOLD), kc, tok->lexeme, col(s, A_RESET));
        pad(o, lex_w + 2 - display_width(tok->lexeme));
        out_printf(o, "%s%d:%d%s\n", col(s, A_DIM), tok->loc.line, tok->loc.col,
                   col(s, A_RESET));
    }
}

/* ---- [2] AST ---- */

/* A growing prefix string ("│   │   ") for the tree printer. */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
    bool   oom;
} Prefix;

static void prefix_append(Prefix *p, const char *s) {
    size_t n = strlen(s);
    if (p->oom) {
        return;
    }
    if (p->len + n + 1 > p->cap) {
        size_t cap = p->cap == 0 ? 64 : p->cap * 2;
        while (cap < p->len + n + 1) {
            cap *= 2;
        }
        char *bigger = realloc(p->data, cap);
        if (bigger == NULL) {
            p->oom = true;
            return;
        }
        p->data = bigger;
        p->cap = cap;
    }
    memcpy(p->data + p->len, s, n + 1);
    p->len += n;
}

static void prefix_truncate(Prefix *p, size_t len) {
    if (!p->oom && len <= p->len) {
        p->len = len;
        p->data[len] = '\0';
    }
}

static void print_label(Trace *t, const AstNode *n) {
    TraceStyle s = t->style;
    if (n->kind == AST_ADD) {
        out_printf(t->out, "%sAdd%s\n", col(s, A_BYELLOW), col(s, A_RESET));
    } else {
        out_printf(t->out, "%sNum(%s%s%s%s)%s\n", col(s, A_GREEN), col(s, A_BOLD),
                   n->lexeme, col(s, A_RESET), col(s, A_GREEN), col(s, A_RESET));
    }
}

static void print_branch(Trace *t, const Prefix *p, const char *branch,
                         const AstNode *child) {
    TraceStyle s = t->style;
    out_printf(t->out, "%s%s%s%s", col(s, A_DIM), p->oom ? "" : p->data, branch,
               col(s, A_RESET));
    print_label(t, child);
}

/* Print the children of `node` below it. The left spine is followed with a
 * loop and the right children are remembered on a small stack, so even a
 * program with thousands of numbers does not need deep recursion. */
static void print_children(Trace *t, const AstNode *node, Prefix *p) {
    size_t n = 0;
    for (const AstNode *q = node; q->kind == AST_ADD; q = q->left) {
        n++;
    }
    if (n == 0) {
        return;
    }
    typedef struct {
        const AstNode *node;
        size_t         prefix_len;
    } Pending;
    Pending *stack = malloc(n * sizeof *stack);
    if (stack == NULL) {
        out_puts(t->out, "(out of memory while printing the tree)\n");
        return;
    }

    size_t top = 0;
    const AstNode *cur = node;
    while (cur->kind == AST_ADD) {
        print_branch(t, p, SYM_TEE(t->style), cur->left);
        stack[top++] = (Pending){cur->right, p->len};
        prefix_append(p, SYM_PIPE(t->style));
        cur = cur->left;
    }
    while (top > 0) {
        Pending item = stack[--top];
        prefix_truncate(p, item.prefix_len);
        print_branch(t, p, SYM_LAST(t->style), item.node);
        if (item.node->kind == AST_ADD) {
            prefix_append(p, "    ");
            print_children(t, item.node, p);
            prefix_truncate(p, item.prefix_len);
        }
    }
    free(stack);
}

void trace_ast(Trace *t, const AstNode *root) {
    section(t, "[2] Parser (AST)");
    print_label(t, root);
    Prefix p = {NULL, 0, 0, false};
    prefix_append(&p, "");
    print_children(t, root, &p);
    free(p.data);
}

/* ---- [3] semantic ---- */

void trace_semantic(Trace *t, const SemanticInfo *info) {
    TraceStyle s = t->style;
    section(t, "[3] Semantic analysis");
    out_printf(t->out, "%s%s%s %lu literal%s checked: all > 0, all fit in uint32\n",
               col(s, A_BGREEN), SYM_CHECK(s), col(s, A_RESET),
               (unsigned long)info->literal_count,
               info->literal_count == 1 ? "" : "s");
    out_printf(t->out, "%s%s%s total sum %s%lu%s fits in uint32\n",
               col(s, A_BGREEN), SYM_CHECK(s), col(s, A_RESET), col(s, A_BOLD),
               (unsigned long)info->total, col(s, A_RESET));
}

/* ---- [4] TAC ---- */

static void print_operand(Trace *t, Operand op) {
    TraceStyle s = t->style;
    if (op.kind == OPND_TEMP) {
        out_printf(t->out, "%st%lu%s", col(s, A_CYAN), (unsigned long)op.value,
                   col(s, A_RESET));
    } else {
        out_printf(t->out, "%s%lu%s", col(s, A_GREEN), (unsigned long)op.value,
                   col(s, A_RESET));
    }
}

/* Print one instruction (no newline); returns its width in columns. */
static size_t print_instr(Trace *t, const IrInstr *ins) {
    TraceStyle s = t->style;
    if (ins->op == IR_RETURN) {
        out_printf(t->out, "%sreturn%s ", col(s, A_BLUE), col(s, A_RESET));
        print_operand(t, ins->a);
    } else {
        print_operand(t, (Operand){OPND_TEMP, ins->dest});
        out_puts(t->out, " = ");
        print_operand(t, ins->a);
        out_puts(t->out, " + ");
        print_operand(t, ins->b);
    }
    char plain[64];
    int n = ir_format_instr(ins, plain, sizeof plain);
    return n > 0 ? (size_t)n : 0;
}

void trace_ir(Trace *t, const IrProgram *ir) {
    section(t, "[4] Intermediate code (TAC)");
    for (size_t i = 0; i < ir->count; i++) {
        print_instr(t, &ir->instrs[i]);
        out_puts(t->out, "\n");
    }
}

/* ---- [5] optimizer ---- */

void trace_opt(Trace *t, const IrProgram *before, const IrProgram *after,
               const FoldLog *log, bool before_after) {
    TraceStyle s = t->style;
    Out *o = t->out;
    section(t, "[5] Optimizer (constant folding)");

    if (before_after) {
        size_t w = 7; /* strlen("Before:") */
        for (size_t i = 0; i < before->count; i++) {
            char plain[64];
            int n = ir_format_instr(&before->instrs[i], plain, sizeof plain);
            if (n > 0 && (size_t)n > w) {
                w = (size_t)n;
            }
        }
        w += 12;
        out_printf(o, "%sBefore:%s", col(s, A_BOLD), col(s, A_RESET));
        pad(o, w - 7);
        out_printf(o, "%sAfter:%s\n", col(s, A_BOLD), col(s, A_RESET));
        size_t rows = before->count > after->count ? before->count : after->count;
        for (size_t i = 0; i < rows; i++) {
            size_t used = 0;
            if (i < before->count) {
                used = print_instr(t, &before->instrs[i]);
            }
            if (i < after->count) {
                pad(o, w - used);
                print_instr(t, &after->instrs[i]);
            }
            out_puts(o, "\n");
        }
        out_puts(o, "\n");
    }

    if (log->count == 0) {
        out_printf(o, "%s(nothing to fold)%s\n", col(s, A_DIM), col(s, A_RESET));
    }
    for (size_t i = 0; i < log->count; i++) {
        const FoldStep *f = &log->steps[i];
        out_printf(o, "%sfolded:%s %s%lu%s + %s%lu%s %s %s%lu%s\n", col(s, A_DIM),
                   col(s, A_RESET), col(s, A_GREEN), (unsigned long)f->a,
                   col(s, A_RESET), col(s, A_GREEN), (unsigned long)f->b,
                   col(s, A_RESET), SYM_ARROW(s), col(s, A_BGREEN),
                   (unsigned long)f->result, col(s, A_RESET));
    }

    if (!before_after) {
        for (size_t i = 0; i < after->count; i++) {
            print_instr(t, &after->instrs[i]);
            out_puts(o, "\n");
        }
    }
}

void trace_opt_disabled(Trace *t) {
    section(t, "[5] Optimizer (constant folding)");
    out_printf(t->out, "%s(disabled with --no-opt: TAC is used unchanged)%s\n",
               col(t->style, A_DIM), col(t->style, A_RESET));
}

/* ---- [6] bytecode ---- */

/* Plain text of an instruction, e.g. "PUSH 6" or "ADD". */
static int format_bytecode(Instr in, char *buf, size_t size) {
    if (in.op == OP_PUSH) {
        return snprintf(buf, size, "PUSH %lu", (unsigned long)in.operand);
    }
    return snprintf(buf, size, "%s", opcode_name(in.op));
}

static size_t print_bytecode(Trace *t, Instr in) {
    TraceStyle s = t->style;
    out_printf(t->out, "%s%s%s", col(s, A_BLUE), opcode_name(in.op), col(s, A_RESET));
    if (in.op == OP_PUSH) {
        out_printf(t->out, " %s%lu%s", col(s, A_GREEN), (unsigned long)in.operand,
                   col(s, A_RESET));
    }
    char plain[32];
    int n = format_bytecode(in, plain, sizeof plain);
    return n > 0 ? (size_t)n : 0;
}

void trace_code(Trace *t, const Bytecode *code) {
    TraceStyle s = t->style;
    section(t, "[6] Code generation (bytecode)");
    for (size_t i = 0; i < code->count; i++) {
        out_printf(t->out, "%s%04lu%s  ", col(s, A_DIM), (unsigned long)i, col(s, A_RESET));
        print_bytecode(t, code->code[i]);
        out_puts(t->out, "\n");
    }
}

/* ---- [7] VM ---- */

void trace_vm(Trace *t, const VmTrace *vm) {
    TraceStyle s = t->style;
    Out *o = t->out;
    section(t, "[7] VM execution");

    size_t w = 0;
    for (size_t i = 0; i < vm->count; i++) {
        char plain[32];
        int n = format_bytecode(vm->steps[i].instr, plain, sizeof plain);
        if (n > 0 && (size_t)n > w) {
            w = (size_t)n;
        }
    }
    w += 3;

    for (size_t i = 0; i < vm->count; i++) {
        const VmStep *st = &vm->steps[i];
        out_printf(o, "%s%04lu%s ", col(s, A_DIM), (unsigned long)st->pc, col(s, A_RESET));
        size_t used = print_bytecode(t, st->instr);
        pad(o, w - used);
        out_puts(o, "stack: [");
        for (size_t k = 0; k < st->depth; k++) {
            out_printf(o, "%s%s%lu%s", k > 0 ? ", " : "", col(s, A_YELLOW),
                       (unsigned long)st->stack[k], col(s, A_RESET));
        }
        out_puts(o, "]\n");
    }
}

void trace_result(Trace *t, uint32_t result) {
    TraceStyle s = t->style;
    out_printf(t->out, "\n%sResult: %lu%s\n", col(s, A_BGREEN), (unsigned long)result,
               col(s, A_RESET));
}

/* =====================================================================
 * Diagnostics (SPEC section 8)
 *
 *     error[parser]: expected NUMBER after '+', found EOF
 *       --> 1:4
 *        |
 *      1 | 1 +
 *        |    ^
 * ===================================================================== */

void trace_diag(Out *out, TraceStyle s, const Diag *diag, const char *source,
                size_t source_len) {
    out_printf(out, "%serror[%s]%s%s: %s%s\n", col(s, A_RED),
               diag_stage_name(diag->stage), col(s, A_RESET), col(s, A_BOLD),
               diag->message, col(s, A_RESET));

    size_t line_len = 0;
    const char *line = NULL;
    if (diag->loc.line > 0 && source != NULL) {
        line = diag_find_line(source, source_len, diag->loc.line, &line_len);
    }
    if (line == NULL) {
        return;
    }

    int w = (int)digits((unsigned long long)diag->loc.line);
    const char *blue = col(s, A_BLUE);
    const char *reset = col(s, A_RESET);

    out_printf(out, "%*s%s-->%s %d:%d\n", w + 1, "", blue, reset, diag->loc.line,
               diag->loc.col);
    out_printf(out, "%*s%s |%s\n", w + 1, "", blue, reset);

    /* the source line (control characters shown as '?') */
    out_printf(out, "%s %*d |%s", blue, w, diag->loc.line, reset);
    if (line_len > 0) {
        out_puts(out, " ");
        for (size_t i = 0; i < line_len; i++) {
            unsigned char c = (unsigned char)line[i];
            char ch = (c < 0x20 && c != '\t') || c == 0x7F ? '?' : (char)c;
            out_write(out, &ch, 1);
        }
    }
    out_puts(out, "\n");

    /* the marker line: copy tabs so '^' lines up under the error */
    out_printf(out, "%*s%s |%s ", w + 1, "", blue, reset);
    for (int i = 0; i + 1 < diag->loc.col; i++) {
        char ch = ((size_t)i < line_len && line[i] == '\t') ? '\t' : ' ';
        out_write(out, &ch, 1);
    }
    out_printf(out, "%s^%s\n", col(s, A_RED), reset);
}
