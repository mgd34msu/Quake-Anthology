#include "frames_internal.h"
#include "qa/network_q2_wire_save.h"
#include <stdlib.h>

bool qa_q2_save_history(qa_source_save_io *io, qa_q2_frame_history **owner)
{
    if (!io || !owner || (io->direction == QA_SOURCE_SAVE_READ ? *owner != NULL : *owner == NULL)) return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t capacity = reading ? 0 : (*owner)->capacity, next = reading ? 0 : (*owner)->next;
    if (!qa_source_save_count(io, &capacity, SIZE_MAX / sizeof(qa_q2_wire_frame)) || !capacity ||
        !qa_source_save_count(io, &next, capacity - 1)) return false;
    if (reading && capacity > io->input.size - io->offset) {
        qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "Saved Q2 physical history slots exceed their document");
        io->failed = true; return false;
    }
    qa_q2_frame_history *history = reading ? NULL : *owner;
    if (reading && !qa_q2_frame_history_create(capacity, &history, io->error)) return false;
    if (reading) history->next = next;
    bool ok = true;
    for (size_t i = 0; ok && i < capacity; ++i) {
        ok = qa_source_save_bool(io, &history->present[i]);
        if (ok && history->present[i]) ok = qa_q2_save_frame(io, history->frames + i);
        if (ok && history->present[i]) for (size_t j = 0; j < i; ++j) {
            if (history->present[j] && history->frames[j].server_frame == history->frames[i].server_frame) {
                qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "Saved Q2 history repeats a physical frame key");
                io->failed = true; ok = false; break;
            }
        }
    }
    if (reading) { if (!ok) qa_q2_frame_history_destroy(history); else *owner = history; }
    return ok;
}
