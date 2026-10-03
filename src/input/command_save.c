#include "qa/input_command_save.h"
#include "qa/source_save.h"
#include <string.h>

static bool fields(qa_source_save_io *io, qa_input_command_builder *saved)
{
    uint8_t magic[4] = {'Q','I','C','B'};
    uint32_t kind = saved->kind;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QICB", sizeof(magic)) ||
        !qa_source_save_u32(io, &kind) || kind > QA_MOVEMENT_Q3 ||
        !qa_source_save_vec3(io, &saved->angles) || !qa_vec_finite(saved->angles) ||
        !qa_source_save_f32(io, &saved->mouse.previous.x) || !isfinite(saved->mouse.previous.x) ||
        !qa_source_save_f32(io, &saved->mouse.previous.y) || !isfinite(saved->mouse.previous.y) ||
        !qa_source_save_bool(io, &saved->drift.drifting) ||
        !qa_source_save_f32(io, &saved->drift.velocity) || !isfinite(saved->drift.velocity) ||
        !qa_source_save_f32(io, &saved->drift.moving_seconds) || !isfinite(saved->drift.moving_seconds) ||
        !qa_source_save_bool(io, &saved->previous_mouse_look) ||
        !qa_source_save_u8(io, &saved->pending_impulse) ||
        (saved->pending_impulse && kind != QA_MOVEMENT_NETQUAKE &&
         kind != QA_MOVEMENT_QUAKEWORLD)) return false;
    saved->kind = (qa_movement_kind)kind;
    return true;
}
bool qa_input_command_checkpoint(const qa_input_command_builder *builder, qa_buffer *out, qa_error *error)
{
    if (!builder || !out || out->data || out->size) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "input builder capture requires empty output"); return false;
    }
    qa_input_command_builder saved = *builder;
    qa_source_save_io io = {0};
    bool success = qa_source_save_writer(&io, NULL, error) && fields(&io, &saved) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!success && error && error->code == QA_OK)
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid input builder continuation");
    return success;
}
bool qa_input_command_restore(qa_input_command_builder *builder, qa_bytes bytes, qa_error *error)
{
    if (!builder) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "input builder restore requires its owner"); return false; }
    qa_input_command_builder saved = {0};
    qa_source_save_io io = {0};
    bool success = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &saved) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!success) {
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid input builder continuation");
        return false;
    }
    *builder = saved;
    return true;
}
