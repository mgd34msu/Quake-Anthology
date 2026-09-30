#ifndef QA_TOKENIZER_H
#define QA_TOKENIZER_H
#include "qa/common.h"

typedef struct qa_tokenizer_options {
    const char *punctuation; /* ASCII bytes split into individual tokens. */
    size_t maximum_units; /* Zero permits any length. */
    bool unicode_whitespace, reject_quoted_newlines;
} qa_tokenizer_options;
typedef struct qa_tokenizer { qa_bytes source; size_t offset; qa_tokenizer_options options; } qa_tokenizer;
typedef struct qa_token { qa_bytes text; size_t offset; bool quoted, normalize_crlf; } qa_token;
/* General source text grammar: unsigned ASCII whitespace, line/block comments,
 * quoted tokens without escape processing and a 1023 UTF-16-unit token limit.
 * Tokens borrow source bytes; quoted CRLF loses its LF in the source grammar. */
bool qa_tokenizer_init(qa_tokenizer *, qa_bytes, qa_error *);
/* Options are copied; punctuation storage is borrowed through parsing. */
bool qa_tokenizer_init_options(qa_tokenizer *, qa_bytes, const qa_tokenizer_options *, qa_error *);
bool qa_tokenizer_next(qa_tokenizer *, qa_token *, bool *found, qa_error *);
/* NUL terminated normalized token copy. Capacity includes the terminator. */
bool qa_token_copy(const qa_token *, char *, size_t capacity, qa_error *);
#endif
