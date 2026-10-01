#include "remote_input.h"
#include "qa/input_command_save.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct frontend_remote_input {
    frontend_remote_input_options options;
    qa_input_command_builder builder;
    bool angles_ready;
};
static bool fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}
bool frontend_remote_input_create(const frontend_remote_input_options *options,
    frontend_remote_input **out, qa_error *error)
{
    if (!options || !options->source_read || !options->source_current || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 input needs its actual source owner");
    frontend_remote_input *input = calloc(1, sizeof(*input));
    if (!input) return fail(error, QA_ERROR_MEMORY, "Allocating remote Q3 input builder");
    input->options = *options;
    input->builder.kind = QA_MOVEMENT_Q3;
    *out = input;
    return true;
}
void frontend_remote_input_destroy(frontend_remote_input *input) { free(input); }
void frontend_remote_input_clear(frontend_remote_input *input)
{
    if (!input) return;
    qa_input_command_clear(&input->builder);
    input->angles_ready = false;
}
static bool source_valid(const frontend_remote_input_source *source)
{
    const qa_application_q3_client_context *receiver = &source->receiver;
    return source->connection.owner && source->connection.generation && source->epoch &&
        receiver->session && receiver->receiver && receiver->service_owner && receiver->frontend_lifetime &&
        receiver->console && receiver->cvars && source->input_settings &&
        receiver->initialized && receiver->source_client < 64 &&
        !receiver->source_owner && !receiver->source_actor.registry && !receiver->native_source &&
        receiver->command_context.owner == receiver->receiver &&
        receiver->command_context.seat == receiver->seat && receiver->command_context.dialect == QA_CONSOLE_Q3 &&
        source->frame.kind == QA_MOVEMENT_Q3;
}
bool frontend_remote_input_build(frontend_remote_input *input, const qa_seat_input_sample *sample,
    double source_frame_ms, qa_movement_command *out, bool *present, qa_error *error)
{
    if (!input || !sample || !out || !present)
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 input needs its physical receipt and output");
    frontend_remote_input_source source = {0};
    bool admitted = false;
    if (!input->options.source_read(input->options.context, &source, &admitted, error)) return false;
    if (!admitted) { *present = false; return true; }
    if (!source_valid(&source) || !input->options.source_current(input->options.context, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 input lost its actual receiver and command clock");
    qa_input_command_builder next = input->builder;
    if (!input->angles_ready && (!source.has_initial_angles ||
        !qa_input_command_angles(&next, source.initial_angles, error))) {
        if (!source.has_initial_angles)
            fail(error, QA_ERROR_ARGUMENT, "Remote Q3 input lacks its genuine initial snapshot angles");
        return false;
    }
    qa_input_command_tuning tuning;
    qa_movement_command command;
    if (!qa_input_settings_read(source.input_settings, QA_MOVEMENT_Q3, &tuning, error) ||
        !qa_input_command_build(&next, &tuning, sample, &source.frame, source_frame_ms, &command, error)) return false;
    if (!input->options.source_current(input->options.context, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 receiver retired while building physical input");
    input->builder = next;
    input->angles_ready = true;
    *out = command;
    *present = true;
    return true;
}
static bool builder_valid(const qa_input_command_builder *builder, bool angles_ready)
{
    if (builder->kind != QA_MOVEMENT_Q3 || !qa_vec_finite(builder->angles) ||
        !isfinite(builder->mouse.previous.x) || !isfinite(builder->mouse.previous.y) ||
        builder->drift.drifting || builder->drift.velocity != 0 || builder->drift.moving_seconds != 0) return false;
    return angles_ready || (builder->angles.x == 0 && builder->angles.y == 0 && builder->angles.z == 0 &&
        builder->mouse.previous.x == 0 && builder->mouse.previous.y == 0 && !builder->previous_mouse_look);
}
bool frontend_remote_input_checkpoint(const frontend_remote_input *input, qa_buffer *out, qa_error *error)
{
    if (!input || !out || out->data || out->size || !builder_valid(&input->builder, input->angles_ready))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 builder capture requires its actual logical continuation");
    qa_buffer builder = {0};
    if (!qa_input_command_checkpoint(&input->builder, &builder, error)) return false;
    qa_source_save_io io = {0};
    uint8_t magic[4] = {'Q','R','I','N'};
    uint32_t version = 1;
    bool ready = input->angles_ready;
    size_t size = builder.size;
    bool ok = qa_source_save_writer(&io, NULL, error) && qa_source_save_bytes(&io, magic, sizeof(magic)) &&
        qa_source_save_u32(&io, &version) && qa_source_save_bool(&io, &ready) &&
        qa_source_save_count(&io, &size, SIZE_MAX) && qa_source_save_bytes(&io, builder.data, size) &&
        qa_source_save_finish(&io, out);
    qa_buffer_free(&builder);
    qa_source_save_dispose(&io);
    return ok;
}
bool frontend_remote_input_restore(frontend_remote_input *input, qa_bytes bytes, qa_error *error)
{
    if (!input) return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 builder restore needs its actual candidate owner");
    qa_source_save_io io = {0};
    uint8_t magic[4];
    uint32_t version = 0;
    bool ready = false;
    size_t size = 0;
    qa_input_command_builder builder = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && !memcmp(magic, "QRIN", sizeof(magic)) &&
        qa_source_save_u32(&io, &version) && version == 1 && qa_source_save_bool(&io, &ready) &&
        qa_source_save_count(&io, &size, bytes.size) && size <= io.input.size - io.offset;
    if (ok) {
        qa_bytes cut = {io.input.data + io.offset, size};
        io.offset += size;
        ok = qa_source_save_finish(&io, NULL) && qa_input_command_restore(&builder, cut, error) &&
            builder_valid(&builder, ready);
    }
    qa_source_save_dispose(&io);
    if (!ok) {
        if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 input continuation");
        return false;
    }
    input->builder = builder;
    input->angles_ready = ready;
    return true;
}
