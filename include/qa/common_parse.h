#ifndef QA_COMMON_PARSE_H
#define QA_COMMON_PARSE_H

#include "qa/common.h"

enum { QA_COMMON_TOKEN_CAPACITY = 1024 };
typedef enum qa_common_end { QA_COMMON_TERMINATED, QA_COMMON_UNINITIALIZED } qa_common_end;

/* Source bytes are immutable and borrowed. Initialize once per source; parser
 * state can be shared by several cursors, as in the original engine. */
typedef struct qa_common_cursor {
    qa_bytes source;
    size_t terminator, offset;
    qa_common_end end;
    bool ended;
} qa_common_cursor;
typedef struct qa_common_cursor_state {
    size_t offset;
    bool ended;
} qa_common_cursor_state;

/* Zero initialization starts a parser. The extra byte safely terminates the
 * 1024-byte partial token exposed by a failed quoted read. Successful tokens
 * fit the source's 1024-byte storage, including NUL. Read these fields without
 * mutating them; overwrite_token and restore enforce the source boundaries. */
typedef struct qa_common_parser {
    char token[QA_COMMON_TOKEN_CAPACITY + 1];
    size_t token_length;
    char name[QA_COMMON_TOKEN_CAPACITY];
    size_t name_length;
    int32_t line;
} qa_common_parser;
typedef struct qa_common_parser_state {
    qa_bytes token, name;
    int32_t line;
} qa_common_parser_state;
typedef bool (*qa_common_print)(void *, qa_bytes, qa_error *);

bool qa_common_cursor_init(qa_common_cursor *, qa_bytes, qa_common_end, qa_error *);
qa_common_cursor_state qa_common_cursor_capture(const qa_common_cursor *);
bool qa_common_cursor_restore(qa_common_cursor *, qa_common_cursor_state, qa_error *);
/* Captured token/name spans borrow the parser until its next mutation. Source
 * restoration rejects negative lines or strings of 1024 bytes or more, even
 * though a failed read can expose such a token to the caller. Neither source
 * bytes nor an owning file identity are part of these continuation records. */
qa_common_parser_state qa_common_parser_capture(const qa_common_parser *);
bool qa_common_parser_restore(qa_common_parser *, const qa_common_parser_state *, qa_error *);
void qa_common_parser_reset(qa_common_parser *);
/* Beginning a session resets line before processing name, leaving token alone.
 * Print callbacks receive a temporary byte span with an additional final NUL.
 * Lengths preserve embedded NUL bytes admitted by a restored source state. */
bool qa_common_parser_begin(qa_common_parser *, const char *name, qa_common_print, void *,
                            qa_error *);
bool qa_common_parser_diagnostic(const qa_common_parser *, bool warning, const char *message,
                                 qa_common_print, void *, qa_error *);
bool qa_common_overwrite_token(qa_common_parser *, qa_bytes, qa_error *);

/* True includes EOF, a suppressed line break, and empty/oversized word tokens.
 * Inspect cursor.ended separately. Failure keeps the newly written partial
 * token and line, but does not commit cursor.offset or cursor.ended. */
bool qa_common_parse(qa_common_parser *, qa_common_cursor *, bool allow_line_breaks, qa_error *);
bool qa_common_skip_line(qa_common_parser *, qa_common_cursor *, qa_error *);
bool qa_common_match(qa_common_parser *, qa_common_cursor *, const char *, qa_error *);
bool qa_common_skip_braced(qa_common_parser *, qa_common_cursor *, qa_error *);
/* Matrix readers write each reached cell, preserving prior writes on failure.
 * Negative dimensions perform zero iterations, matching source signed loops. */
bool qa_common_matrix_1d(qa_common_parser *, qa_common_cursor *, int32_t x, float *matrix,
                         size_t capacity, size_t offset, qa_error *);
bool qa_common_matrix_2d(qa_common_parser *, qa_common_cursor *, int32_t y, int32_t x,
                         float *matrix, size_t capacity, size_t offset, qa_error *);
bool qa_common_matrix_3d(qa_common_parser *, qa_common_cursor *, int32_t z, int32_t y, int32_t x,
                         float *matrix, size_t capacity, size_t offset, qa_error *);
/* One owned, NUL-terminated output; size excludes NUL. Output remains unchanged
 * on failure. Compression preserves quoted bytes and drops trailing space. */
bool qa_common_compress(qa_bytes, qa_common_end, qa_buffer *, qa_error *);

#endif
