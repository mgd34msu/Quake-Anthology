#include "internal.h"
#include "qa/persistence_navigation.h"
#include "qa/source_save.h"
#include <math.h>

static bool signature(qa_source_save_io *io)
{
    static const uint8_t expected[8] = {'Q', 'A', 'N', 'A', 'V', 0, 0, 0};
    uint8_t magic[8];
    memcpy(magic, expected, sizeof(magic));
    if (!qa_source_save_bytes(io, magic, sizeof(magic)))
        return false;
    return !memcmp(magic, expected, sizeof(magic)) ? true :
        persistence_fail(io->error, QA_ERROR_FORMAT, "Unsupported navigation continuation schema");
}

static bool fields(qa_source_save_io *io, qa_nav_checkpoint *state,
                    const qa_nav_graph_view *graph)
{
    uint32_t format = (uint32_t)state->map.format;
    if (!qa_source_save_string(io, &state->map.name) ||
        !qa_source_save_u32(io, &format) ||
        !qa_source_save_bytes(io, state->map.digest, sizeof(state->map.digest)) ||
        !qa_source_save_u64(io, &state->generation) ||
        !qa_source_save_u64(io, &state->world_revision))
        return false;
    state->map.format = (qa_bsp_format)format;
    if (state->map.name != graph->map.name || state->map.format != graph->map.format ||
        memcmp(state->map.digest, graph->map.digest, sizeof(state->map.digest)))
        return persistence_fail(io->error, QA_ERROR_FORMAT, "Navigation continuation map identity differs");
    size_t areas = graph->node_count, edges = graph->edge_count;
    if (areas > SIZE_MAX / sizeof(*state->enabled))
        areas = SIZE_MAX / sizeof(*state->enabled);
    if (!qa_source_save_count(io, &state->enabled_count, areas))
        return false;
    if (io->direction == QA_SOURCE_SAVE_READ && state->enabled_count) {
        state->enabled = calloc(state->enabled_count, sizeof(*state->enabled));
        if (!state->enabled)
            return persistence_fail(io->error, QA_ERROR_MEMORY, "Restoring navigation area overrides");
        state->enabled_capacity = state->enabled_count;
    }
    for (size_t i = 0; i < state->enabled_count; ++i)
        if (!qa_source_save_u32(io, &state->enabled[i].id) ||
            !qa_source_save_bool(io, &state->enabled[i].enabled))
            return false;
    size_t blocked = edges > SIZE_MAX / sizeof(*state->blocked) ?
        SIZE_MAX / sizeof(*state->blocked) : edges;
    if (!qa_source_save_count(io, &state->blocked_count, blocked))
        return false;
    if (io->direction == QA_SOURCE_SAVE_READ && state->blocked_count) {
        state->blocked = calloc(state->blocked_count, sizeof(*state->blocked));
        if (!state->blocked)
            return persistence_fail(io->error, QA_ERROR_MEMORY, "Restoring navigation blocked edges");
        state->blocked_capacity = state->blocked_count;
    }
    for (size_t i = 0; i < state->blocked_count; ++i)
        if (!qa_source_save_u32(io, &state->blocked[i]))
            return false;
    size_t admissions = edges > SIZE_MAX / sizeof(*state->admissions) ?
        SIZE_MAX / sizeof(*state->admissions) : edges;
    if (!qa_source_save_count(io, &state->admission_count, admissions))
        return false;
    if (io->direction == QA_SOURCE_SAVE_READ && state->admission_count) {
        state->admissions = calloc(state->admission_count, sizeof(*state->admissions));
        if (!state->admissions)
            return persistence_fail(io->error, QA_ERROR_MEMORY, "Restoring navigation admissions");
        state->admission_capacity = state->admission_count;
    }
    for (size_t i = 0; i < state->admission_count; ++i) {
        qa_nav_saved_admission *admission = state->admissions + i;
        if (!qa_source_save_u32(io, &admission->id) ||
            !qa_source_save_f32(io, &admission->seconds))
            return false;
        if (!isfinite(admission->seconds) || admission->seconds < 0)
            return persistence_fail(io->error, QA_ERROR_FORMAT, "Invalid navigation admission duration");
    }
    return true;
}

bool qa_persistence_navigation_capture(qa_session *session, const qa_navigation *navigation,
                                       qa_buffer *out, qa_error *error)
{
    if (!session || !navigation || !out)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Missing navigation continuation capture owner");
    qa_nav_checkpoint state = {0};
    qa_source_save_io io = {0};
    bool ok = qa_navigation_capture(navigation, &state, error) &&
        qa_source_save_writer(&io, session, error) && signature(&io) &&
        fields(&io, &state, qa_navigation_graph(navigation)) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_nav_checkpoint_free(&state);
    return ok;
}

bool qa_persistence_navigation_restore(qa_session *session, qa_navigation *navigation,
                                       qa_bytes bytes, qa_error *error)
{
    if (!session || !navigation)
        return persistence_fail(error, QA_ERROR_ARGUMENT, "Missing candidate navigation continuation owner");
    qa_nav_checkpoint state = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error) && signature(&io) &&
        fields(&io, &state, qa_navigation_graph(navigation)) && qa_source_save_finish(&io, NULL) &&
        qa_navigation_restore(navigation, &state, error);
    qa_source_save_dispose(&io);
    qa_nav_checkpoint_free(&state);
    return ok;
}
