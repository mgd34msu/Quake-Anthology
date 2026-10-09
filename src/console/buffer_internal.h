#ifndef QA_CONSOLE_BUFFER_INTERNAL_H
#define QA_CONSOLE_BUFFER_INTERNAL_H

#include "qa/console_buffer.h"

typedef struct row_state {
    uint64_t sequence;
    size_t count;
    double time;
    bool notify, wrapped;
} row_state;
struct qa_console_buffer {
    qa_ruleset_id dialect;
    row_state *rows;
    qa_console_cell *cells;
    size_t character_capacity, capacity, width, first, count, write, x, backscroll;
    uint64_t sequence;
};

#endif
