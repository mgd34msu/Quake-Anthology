#ifndef QA_COMMAND_LEXER_H
#define QA_COMMAND_LEXER_H

#include "qa/common.h"
#include "qa/ruleset.h"

typedef struct qa_command_token_span {
    size_t start, size, end;
    bool found;
} qa_command_token_span;

/* Source command whitespace; console_text retains unquoted bytes >= 128. */
bool qa_command_token_space(unsigned char byte, bool console_text);
/* Allocation-free token iteration over a checked byte span. Tokens borrow the
 * source at start/size; resume at end. Quotes have no escape processing. Q3
 * line comments end iteration and block comments are skipped. */
bool qa_command_token_next(const char *text, size_t length, size_t start,
    qa_ruleset_id dialect, bool console_text, qa_command_token_span *, qa_error *);

#endif
