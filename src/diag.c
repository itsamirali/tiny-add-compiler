/*
 * diag.c - Diagnostics (see diag.h).
 *
 * Only builds and inspects error records. Printing them (with colors and
 * the '^' marker) is done by trace.c.
 */
#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void diag_set(Diag *diag, DiagStage stage, SrcLoc loc, const char *fmt, ...) {
    if (diag->is_set) {
        return; /* keep the first error */
    }
    diag->is_set = true;
    diag->stage = stage;
    diag->loc = loc;

    va_list args;
    va_start(args, fmt);
    vsnprintf(diag->message, sizeof diag->message, fmt, args);
    va_end(args);
}

void diag_out_of_memory(Diag *diag) {
    SrcLoc none = {0, 0};
    diag_set(diag, DIAG_INTERNAL, none, "out of memory");
}

const char *diag_stage_name(DiagStage stage) {
    switch (stage) {
    case DIAG_LEXER:    return "lexer";
    case DIAG_PARSER:   return "parser";
    case DIAG_SEMANTIC: return "semantic";
    case DIAG_VM:       return "vm";
    case DIAG_INTERNAL: return "internal";
    }
    return "internal";
}

int diag_exit_code(const Diag *diag) {
    switch (diag->stage) {
    case DIAG_LEXER:
    case DIAG_PARSER:
    case DIAG_SEMANTIC:
        return TA_EXIT_COMPILE_ERROR;
    case DIAG_VM:
    case DIAG_INTERNAL:
        return TA_EXIT_USAGE;
    }
    return TA_EXIT_USAGE;
}

const char *diag_find_line(const char *source, size_t source_len, int line,
                           size_t *out_len) {
    if (line < 1) {
        return NULL;
    }
    size_t pos = 0;
    for (int current = 1; current < line; current++) {
        const char *nl = memchr(source + pos, '\n', source_len - pos);
        if (nl == NULL) {
            return NULL;
        }
        pos = (size_t)(nl - source) + 1;
    }
    size_t end = pos;
    while (end < source_len && source[end] != '\n') {
        end++;
    }
    if (end > pos && source[end - 1] == '\r') {
        end--; /* hide the '\r' of CRLF line endings */
    }
    *out_len = end - pos;
    return source + pos;
}
