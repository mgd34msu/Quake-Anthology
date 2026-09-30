#include "progress_internal.h"
#include "qa/text.h"
#include <math.h>

static bool nonempty_utf8(qa_bytes text) { return text.size != 0 && qa_utf8_valid(text); }
static size_t row_hash(const progress_row *row) {
    uint64_t value = ((uint64_t)row->participant << 32) | row->event;
    value ^= (uint64_t)(row->source + 1) * UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (size_t)(value ^ (value >> 31));
}
static size_t index_slot(const progress_data *data, const progress_row *row) {
    size_t slot = row_hash(row) & (data->index_capacity - 1);
    while (data->index[slot]) {
        const progress_row *other = data->rows + data->index[slot] - 1;
        if (other->source == row->source && other->participant == row->participant &&
            other->event == row->event)
            break;
        slot = (slot + 1) & (data->index_capacity - 1);
    }
    return slot;
}
static bool reserve(progress_data *data, qa_error *error) {
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 32;
        if (capacity <= data->count || capacity > SIZE_MAX / sizeof(*data->rows))
            goto memory;
        progress_row *rows = realloc(data->rows, capacity * sizeof(*rows));
        if (!rows)
            goto memory;
        data->rows = rows;
        data->capacity = capacity;
    }
    if (data->count + 1 > data->index_capacity / 2) {
        size_t capacity = data->index_capacity ? data->index_capacity * 2 : 64;
        if (capacity <= data->index_capacity || capacity > SIZE_MAX / sizeof(*data->index))
            goto memory;
        size_t *index = calloc(capacity, sizeof(*index));
        if (!index)
            goto memory;
        free(data->index);
        data->index = index;
        data->index_capacity = capacity;
        for (size_t i = 0; i < data->count; ++i)
            data->index[index_slot(data, data->rows + i)] = i + 1;
    }
    return true;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, data->count, "Reserving player progress records");
    return false;
}
void progress_data_free(progress_data *data) {
    qa_strings_destroy(data->strings);
    free(data->rows);
    free(data->index);
    *data = (progress_data){0};
}
static bool event_subject(const qa_progress_event *event, qa_bytes *out, qa_error *error) {
    if (!event || event->source < QA_GAME_Q1 || event->source > QA_GAME_Q3 ||
        event->kind < QA_PROGRESS_ACHIEVEMENT || event->kind > QA_PROGRESS_MATCH_COMPLETED ||
        (event->source == QA_GAME_Q3 && event->kind != QA_PROGRESS_MATCH_COMPLETED) ||
        !nonempty_utf8(event->participant) || !nonempty_utf8(event->event))
        goto invalid;
    qa_bytes subject = event->kind == QA_PROGRESS_ACHIEVEMENT       ? event->value.award
                       : event->kind == QA_PROGRESS_LEVEL_COMPLETED ? event->value.map
                                                                    : event->value.match.map;
    if (!nonempty_utf8(subject) ||
        (event->kind == QA_PROGRESS_MATCH_COMPLETED && !isfinite(event->value.match.score)))
        goto invalid;
    *out = subject;
    return true;
invalid:
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid player progress event");
    return false;
}
bool progress_prepare(progress_data *data, const qa_progress_event *event, progress_row *row,
                      bool *duplicate, qa_error *error) {
    qa_bytes subject;
    if (!event_subject(event, &subject, error))
        return false;
    *row = (progress_row){
        .kind = event->kind,
        .source = event->source,
        .participant = qa_strings_find(data->strings, event->participant),
        .event = qa_strings_find(data->strings, event->event),
        .score = event->kind == QA_PROGRESS_MATCH_COMPLETED ? event->value.match.score : 0};
    *duplicate = row->participant && row->event && data->index_capacity &&
                 data->index[index_slot(data, row)] != 0;
    if (*duplicate)
        return true;
    return reserve(data, error) &&
           qa_strings_intern(data->strings, event->participant, &row->participant, error) &&
           qa_strings_intern(data->strings, event->event, &row->event, error) &&
           qa_strings_intern(data->strings, subject, &row->subject, error);
}
void progress_append(progress_data *data, const progress_row *row) {
    data->rows[data->count] = *row;
    size_t slot = index_slot(data, row);
    data->index[slot] = ++data->count;
}
void progress_view(const progress_data *data, const progress_row *row, qa_progress_event *out) {
    *out = (qa_progress_event){.kind = row->kind,
                               .source = row->source,
                               .participant = qa_strings_text(data->strings, row->participant),
                               .event = qa_strings_text(data->strings, row->event)};
    qa_bytes subject = qa_strings_text(data->strings, row->subject);
    if (row->kind == QA_PROGRESS_ACHIEVEMENT)
        out->value.award = subject;
    else if (row->kind == QA_PROGRESS_LEVEL_COMPLETED)
        out->value.map = subject;
    else {
        out->value.match.map = subject;
        out->value.match.score = row->score;
    }
}
bool progress_checkpoint_data_ready(const progress_data *data, qa_error *error) {
    if (!data || !data->strings || data->count > data->capacity ||
        (data->capacity && (!data->rows || data->capacity < 32 ||
                           (data->capacity & (data->capacity - 1)))) ||
        (data->capacity > 32 && data->count < data->capacity / 2) ||
        (data->index_capacity && (!data->index || data->index_capacity < 64 ||
                                 (data->index_capacity & (data->index_capacity - 1)))) ||
        data->count > data->index_capacity / 2 ||
        (data->index_capacity > 64 && data->count < data->index_capacity / 4) ||
        data->index_capacity > SIZE_MAX / sizeof(*data->index)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid player progress allocation topology");
        return false;
    }
    size_t *index = data->index_capacity ? calloc(data->index_capacity, sizeof(*index)) : NULL;
    if (data->index_capacity && !index) {
        qa_error_set(error, QA_ERROR_MEMORY, data->index_capacity, "Qualifying progress index");
        return false;
    }
    progress_data expected = *data;
    expected.index = index;
    bool ok = true;
    for (size_t i = 0; ok && i < qa_strings_count(data->strings); ++i) {
        if (!nonempty_utf8(qa_strings_text(data->strings, (qa_string_id)(i + 1)))) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Invalid private progress dictionary value");
            ok = false;
        }
    }
    for (size_t i = 0; ok && i < data->count; ++i) {
        const progress_row *row = data->rows + i;
        qa_progress_event event;
        qa_bytes subject;
        progress_view(data, row, &event);
        ok = event_subject(&event, &subject, error);
        if (ok && row->kind != QA_PROGRESS_MATCH_COMPLETED &&
            (row->score != 0 || signbit(row->score))) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Non-match progress row retains a score");
            ok = false;
        }
        if (ok) {
            size_t slot = index_slot(&expected, row);
            if (index[slot]) {
                qa_error_set(error, QA_ERROR_FORMAT, i, "Duplicate private progress row");
                ok = false;
            } else index[slot] = i + 1;
        }
    }
    if (ok && data->index_capacity &&
        memcmp(index, data->index, data->index_capacity * sizeof(*index))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Progress index differs from its source rows");
        ok = false;
    }
    free(index);
    return ok;
}
size_t qa_player_progress_count(const qa_player_progress *store) {
    return store ? store->data.count : 0;
}
bool qa_player_progress_at(const qa_player_progress *store, size_t index, qa_progress_event *out) {
    if (!store || !out || index >= store->data.count)
        return false;
    progress_view(&store->data, store->data.rows + index, out);
    return true;
}
bool qa_player_progress_next(const qa_player_progress *store, qa_bytes participant, size_t *cursor,
                             qa_progress_event *out) {
    if (!store || !cursor || !out || !participant.data)
        return false;
    qa_string_id identity = qa_strings_find(store->data.strings, participant);
    while (identity && *cursor < store->data.count) {
        const progress_row *row = store->data.rows + (*cursor)++;
        if (row->participant == identity) {
            progress_view(&store->data, row, out);
            return true;
        }
    }
    *cursor = store->data.count;
    return false;
}
static bool recover_record(qa_player_progress *store, const qa_progress_event *event,
                           bool *inserted, qa_error *error) {
    qa_bytes subject;
    if (!event_subject(event, &subject, error))
        return false;
    if (event->participant.size > SIZE_MAX - event->event.size ||
        subject.size > SIZE_MAX - event->participant.size - event->event.size) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Player progress event size overflow");
        return false;
    }
    size_t size = event->participant.size + event->event.size + subject.size;
    uint8_t *copy = malloc(size);
    if (!copy) {
        qa_error_set(error, QA_ERROR_MEMORY, size,
                     "Retaining player progress event during recovery");
        return false;
    }
    qa_progress_event retained = *event;
    retained.participant.data = copy;
    retained.event.data = copy + event->participant.size;
    qa_bytes retained_subject = {copy + event->participant.size + event->event.size, subject.size};
    memcpy(copy, event->participant.data, event->participant.size);
    memcpy(copy + event->participant.size, event->event.data, event->event.size);
    memcpy((uint8_t *)retained_subject.data, subject.data, subject.size);
    if (event->kind == QA_PROGRESS_ACHIEVEMENT)
        retained.value.award = retained_subject;
    else if (event->kind == QA_PROGRESS_LEVEL_COMPLETED)
        retained.value.map = retained_subject;
    else
        retained.value.match.map = retained_subject;
    bool ok = qa_player_progress_reload(store, error) &&
              qa_player_progress_record(store, &retained, inserted, error);
    free(copy);
    return ok;
}
bool qa_player_progress_record(qa_player_progress *store, const qa_progress_event *event,
                               bool *inserted, qa_error *error) {
    if (!store || !inserted || store->restore_pending) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing player progress store/output");
        return false;
    }
    if (store->reload_required)
        return recover_record(store, event, inserted, error);
    progress_row row;
    bool duplicate;
    if (!progress_prepare(&store->data, event, &row, &duplicate, error))
        return false;
    if (duplicate) {
        *inserted = false;
        return true;
    }
    qa_buffer bytes = {0};
    if (!progress_encode(&store->data, &row, &bytes, error))
        return false;
    bool ok = qa_fs_root_replace(store->root, store->relative, (qa_bytes){bytes.data, bytes.size},
                                 ++store->nonce, error);
    qa_buffer_free(&bytes);
    if (!ok) {
        store->reload_required = true;
        return false;
    }
    progress_append(&store->data, &row);
    *inserted = true;
    return true;
}
