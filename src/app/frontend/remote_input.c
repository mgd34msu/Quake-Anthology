#include "remote_input.h"
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
        receiver->console && receiver->cvars && source->input_settings && source->movement_settings &&
        (receiver->native_source ? source->media_owner != NULL : receiver->initialized) &&
        receiver->source_client < 64 &&
        !receiver->source_owner && !receiver->source_actor.registry &&
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
    if (!qa_input_settings_read_routed(source.input_settings, source.movement_settings, QA_MOVEMENT_Q3, &tuning, error) ||
        !qa_input_command_build(&next, &tuning, sample, &source.frame, source_frame_ms, &command, error)) return false;
    if (!input->options.source_current(input->options.context, &source))
        return fail(error, QA_ERROR_ARGUMENT, "Remote Q3 receiver retired while building physical input");
    input->builder = next;
    input->angles_ready = true;
    *out = command;
    *present = true;
    return true;
}
