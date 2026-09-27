#ifndef QA_PROGRESS_INTERNAL_H
#define QA_PROGRESS_INTERNAL_H

#include "qa/player_progress.h"
#include "qa/strings.h"
#include <stdlib.h>
#include <string.h>

typedef struct progress_row {
    qa_progress_kind kind;
    qa_game_family source;
    qa_string_id participant, event, subject;
    double score;
} progress_row;
typedef struct progress_data {
    qa_strings *strings;
    progress_row *rows;
    size_t count, capacity;
    size_t *index, index_capacity;
} progress_data;
struct qa_player_progress {
    qa_fs_root *root;
    char *relative;
    uint64_t nonce;
    bool reload_required;
    progress_data data;
};

void progress_data_free(progress_data *);
bool progress_prepare(progress_data *, const qa_progress_event *, progress_row *, bool *duplicate,
                      qa_error *);
void progress_append(progress_data *, const progress_row *);
void progress_view(const progress_data *, const progress_row *, qa_progress_event *);
bool progress_decode(qa_bytes, progress_data *, qa_error *);
bool progress_encode(const progress_data *, const progress_row *, qa_buffer *, qa_error *);

#endif
