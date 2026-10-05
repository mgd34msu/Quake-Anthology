#include "remote_q2_restore.h"
#include "remote_q2_private.h"

bool frontend_remote_q2_import_read(const frontend_remote_q2 *row, frontend_remote_q2_view *out, qa_error *error)
{
    if (!row || !out || !row->importing || !row->frontend->source_restoring)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 import observation requires its actual isolated cold owner");
    *out = (frontend_remote_q2_view){row, row->options.domain, row->identity, row->loading_generation,
        row->content_generation, row->received_ns, &row->data, row->content, row->map, &row->map_opening,
        &row->frame, &row->previous, row->images, row->materials, row->sounds, row->world, false, row->retired, row->geometry}; return true;
}
