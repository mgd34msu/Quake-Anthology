#include "actions_private.h"
#include "save_fields.h"
#include "qa/bot_actions_save.h"
#include <limits.h>

static const uint8_t magic[8] = {'Q', 'A', 'B', 'A', 'C', 'T', 'N', 0};

static bool input_fields(qa_source_save_io *io, qa_bot_input *input)
{
    return qa_source_save_f32(io, &input->think_time) &&
        qa_source_save_vec3(io, &input->direction) && qa_source_save_f32(io, &input->speed) &&
        qa_source_save_vec3(io, &input->view_angles) &&
        qa_source_save_u32(io, &input->action_flags) && qa_source_save_i32(io, &input->weapon);
}

bool qa_bot_actions_capture(const qa_bot_actions *actions, qa_buffer *out, qa_error *error)
{
    if (!actions || !out || actions->restoring || actions->capacity > INT32_MAX / 40 ||
        actions->capacity > SIZE_MAX / sizeof(qa_bot_input) ||
        (!!actions->inputs != (actions->initialized && actions->capacity != 0))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot action owner is absent, restoring or inconsistent");
        return false;
    }
    qa_source_save_io io = {0};
    bool initialized = actions->initialized;
    uint32_t capacity = actions->capacity;
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) &&
        qa_source_save_bool(&io, &initialized) && qa_source_save_u32(&io, &capacity);
    for (uint32_t i = 0; ok && initialized && i < capacity; ++i) {
        qa_bot_input input = actions->inputs[i];
        ok = input_fields(&io, &input);
    }
    if (ok)
        ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_actions_restore_bytes(qa_bot_actions *actions, qa_bytes bytes, qa_error *error)
{
    if (!actions || actions->restoring) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Detached bot actions are absent or restoring");
        return false;
    }
    qa_source_save_io io = {0};
    qa_bot_input *inputs = NULL;
    bool initialized = false;
    uint32_t capacity = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        qa_source_save_bool(&io, &initialized) && qa_source_save_u32(&io, &capacity);
    if (ok && (capacity > INT32_MAX / 40 || capacity > SIZE_MAX / sizeof(*inputs) ||
        (initialized && capacity > (io.input.size - io.offset) / 40)))
        ok = bot_save_fail(&io, QA_ERROR_FORMAT, "Invalid or truncated bot action slots");
    if (ok && initialized && capacity) {
        inputs = calloc(capacity, sizeof(*inputs));
        if (!inputs)
            ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring bot action slots");
    }
    for (uint32_t i = 0; ok && initialized && i < capacity; ++i)
        ok = input_fields(&io, inputs + i);
    if (ok)
        ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        free(actions->inputs);
        actions->inputs = inputs;
        actions->capacity = capacity;
        actions->initialized = initialized;
        inputs = NULL;
    }
    free(inputs);
    return ok;
}
