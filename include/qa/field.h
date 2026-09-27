#ifndef QA_FIELD_H
#define QA_FIELD_H
#include "qa/common.h"

typedef struct qa_field_controls {
    bool control, shift;
    void *context;
    /* NULL means unavailable. Successful clipboard reads return an owned
     * buffer; empty/NULL data means no text. */
    bool (*clipboard)(void *, qa_buffer *, qa_error *);
} qa_field_controls;
typedef struct qa_text_field qa_text_field;
typedef struct qa_field_view {
    const char *text, *completion;
    size_t cursor, scroll, length, width;
    bool overstrike;
} qa_field_view;
qa_text_field *qa_text_field_create(size_t maximum_scalars, size_t width, qa_error *);
void qa_text_field_destroy(qa_text_field *);
void qa_text_field_clear(qa_text_field *);
void qa_text_field_set(qa_text_field *, const char *utf8);
void qa_text_field_insert(qa_text_field *, qa_bytes utf8);
void qa_text_field_cursor(qa_text_field *, size_t);
void qa_text_field_width(qa_text_field *, size_t);
qa_field_view qa_text_field_read(qa_text_field *);
bool qa_text_field_overstrike(const qa_text_field *);
void qa_text_field_set_overstrike(qa_text_field *, bool);
bool qa_text_field_key(qa_text_field *, int key, const qa_field_controls *, bool *handled,
                       qa_error *);
bool qa_text_field_complete(qa_text_field *, const char *const *names, size_t count, bool reverse,
                            qa_error *);

typedef struct qa_byte_field {
    uint8_t text[256];
    int32_t cursor, scroll, width;
} qa_byte_field;
/* Source Q3 fields preserve byte editing, global overstrike, signed clipboard
 * characters and original scroll arithmetic. Clipboard recursion is bounded. */
void qa_byte_field_clear(qa_byte_field *);
bool qa_byte_field_set(qa_byte_field *, qa_bytes source_bytes, qa_error *);
bool qa_byte_field_key(qa_byte_field *, int key, const qa_field_controls *, bool *overstrike,
                       qa_error *);
bool qa_byte_field_character(qa_byte_field *, int32_t character, const qa_field_controls *,
                             bool *overstrike, qa_error *);

typedef struct qa_console_history qa_console_history;
qa_console_history *qa_console_history_create(size_t capacity, qa_error *);
void qa_console_history_destroy(qa_console_history *);
bool qa_console_history_replace(qa_console_history *, const char *const *, size_t, qa_error *);
bool qa_console_history_add(qa_console_history *, const char *, qa_error *);
size_t qa_console_history_count(const qa_console_history *);
const char *qa_console_history_at(const qa_console_history *, size_t);
bool qa_console_history_previous(qa_console_history *, const char *draft, const char **out,
                                 qa_error *);
const char *qa_console_history_next(qa_console_history *);
#endif
