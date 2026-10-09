#ifndef QA_CONSOLE_BUFFER_H
#define QA_CONSOLE_BUFFER_H
#include "qa/console.h"

typedef struct qa_console_cell {
    uint32_t scalar;
    uint8_t color;
    bool alternate;
} qa_console_cell;
typedef struct qa_console_row {
    uint64_t sequence;
    const qa_console_cell *cells;
    size_t count;
    double time_ms;
    bool notify, wrapped;
} qa_console_row;
typedef struct qa_console_buffer qa_console_buffer;
qa_console_buffer *qa_console_buffer_create(qa_ruleset_id, size_t width,
                                            size_t character_capacity, qa_error *);
void qa_console_buffer_destroy(qa_console_buffer *);
void qa_console_buffer_dialect(qa_console_buffer *, qa_ruleset_id);
/* All cells remain borrowed until the buffer changes. Printing uses retained
 * storage; resize prepares and publishes a complete replacement atomically. */
bool qa_console_buffer_print(qa_console_buffer *, qa_bytes utf8, double time_ms, qa_error *);
bool qa_console_buffer_resize(qa_console_buffer *, size_t width, qa_error *);
void qa_console_buffer_clear(qa_console_buffer *);
void qa_console_buffer_clear_notify(qa_console_buffer *);
void qa_console_buffer_scroll(qa_console_buffer *, int64_t rows);
void qa_console_buffer_bottom(qa_console_buffer *);
void qa_console_buffer_top(qa_console_buffer *);
size_t qa_console_buffer_width(const qa_console_buffer *);
size_t qa_console_buffer_backscroll(const qa_console_buffer *);
size_t qa_console_buffer_count(const qa_console_buffer *);
bool qa_console_buffer_row(const qa_console_buffer *, size_t index, qa_console_row *);
/* Returns the number of visible rows and writes the oldest row index. */
size_t qa_console_buffer_visible(const qa_console_buffer *, size_t maximum, size_t *first);
bool qa_console_row_notifies(const qa_console_row *, double now_ms, double duration_ms);
bool qa_console_buffer_dump(const qa_console_buffer *, qa_buffer *utf8, qa_error *);
#endif
