#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_movement_save.h"

static const uint8_t magic[8] = {'Q', 'A', 'B', 'M', 'O', 'V', 'E', 0};

static bool state_fields(qa_source_save_io *io, qa_bot_move_state *state)
{
    qa_bot_move_input *input = &state->input;
    bool ok = qa_source_save_vec3(io, &input->origin) &&
        qa_source_save_vec3(io, &input->velocity) && qa_source_save_vec3(io, &input->view_offset) &&
        qa_source_save_i32(io, &input->entity) && qa_source_save_i32(io, &input->client) &&
        qa_source_save_f32(io, &input->think_time) && qa_source_save_u32(io, &input->presence) &&
        qa_source_save_vec3(io, &input->view_angles) && qa_source_save_u32(io, &input->flags) &&
        qa_source_save_u32(io, &state->area) && qa_source_save_u32(io, &state->last_area) &&
        qa_source_save_u32(io, &state->last_goal_area) && qa_source_save_u32(io, &state->last_reachability) &&
        qa_source_save_u32(io, &state->reach_area) && qa_source_save_u32(io, &state->jump_reach) &&
        qa_source_save_vec3(io, &state->last_origin) && qa_source_save_f32(io, &state->grapple_visible_time) &&
        qa_source_save_f32(io, &state->last_grapple_distance) && qa_source_save_f32(io, &state->reachability_time) &&
        qa_source_save_u32(io, &state->avoid_reachability) && qa_source_save_f32(io, &state->avoid_time) &&
        qa_source_save_i32(io, &state->avoid_tries);
    for (size_t i = 0; ok && i < QA_BOT_AVOID_SPOTS; ++i)
        ok = qa_source_save_vec3(io, &state->avoid_spots[i].origin) &&
            qa_source_save_f32(io, &state->avoid_spots[i].radius) &&
            qa_source_save_i32(io, &state->avoid_spots[i].type);
    if (ok)
        ok = qa_source_save_u32(io, &state->avoid_count) && state->avoid_count <= QA_BOT_AVOID_SPOTS &&
            qa_source_save_bool(io, &state->walk_progress) && qa_source_save_u32(io, &state->walk_edge);
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot movement state");
    return ok;
}

bool qa_bot_moves_save_capture(const qa_bot_moves *moves, qa_buffer *out, qa_error *error)
{
    if (!moves || moves->busy || !out || !moves->maximum || !moves->slots || !isfinite(moves->time)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot movement owner is absent, active or inconsistent");
        return false;
    }
    qa_source_save_io io = {0};
    uint32_t maximum = moves->maximum;
    float time = moves->time;
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) &&
        qa_source_save_u32(&io, &maximum) && qa_source_save_f32(&io, &time);
    for (size_t i = 0; ok && i < BOT_MOVE_VARIABLE_COUNT; ++i) {
        const char *name = moves->variables[i] ? moves->variables[i]->name : NULL;
        if (name && moves->variables[i] != qa_bot_library_variable(moves->library,
                bot_move_variable_name((bot_move_variable)i)))
            ok = bot_save_fail(&io, QA_ERROR_FORMAT, "Bot movement variable has another source role");
        if (ok)
            ok = bot_save_text(&io, &name);
    }
    for (uint32_t i = 0; ok && i < maximum; ++i) {
        bot_move_slot slot = moves->slots[i];
        ok = qa_source_save_bool(&io, &slot.used) && state_fields(&io, &slot.state);
    }
    if (ok)
        ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool qa_bot_moves_save_restore(qa_bot_moves *moves, qa_bytes bytes, qa_error *error)
{
    if (!moves || moves->busy || !moves->library || !moves->services.navigation) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Detached bot movement owner is absent or active");
        return false;
    }
    qa_source_save_io io = {0};
    uint32_t maximum = 0;
    float time = 0;
    const qa_bot_variable *variables[BOT_MOVE_VARIABLE_COUNT] = {0};
    bot_move_slot *slots = NULL;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) &&
        qa_source_save_u32(&io, &maximum) && maximum == moves->maximum &&
        (!maximum || sizeof(*slots) <= SIZE_MAX / maximum) && qa_source_save_f32(&io, &time) && isfinite(time);
    for (size_t i = 0; ok && i < BOT_MOVE_VARIABLE_COUNT; ++i) {
        const char *name = NULL;
        ok = bot_save_text(&io, &name);
        if (ok && name) {
            variables[i] = qa_bot_library_variable(moves->library,
                bot_move_variable_name((bot_move_variable)i));
            ok = variables[i] && !strcmp(variables[i]->name, name);
        }
        free((void *)name);
    }
    if (ok && maximum > (io.input.size - io.offset) / 778)
        ok = bot_save_fail(&io, QA_ERROR_FORMAT, "Truncated bot movement handle table");
    if (ok) {
        slots = calloc(maximum, sizeof(*slots));
        if (!slots)
            ok = bot_save_fail(&io, QA_ERROR_MEMORY, "Restoring bot movement handles");
    }
    for (uint32_t i = 0; ok && i < maximum; ++i)
        ok = qa_source_save_bool(&io, &slots[i].used) && state_fields(&io, &slots[i].state);
    if (ok)
        ok = qa_source_save_finish(&io, NULL);
    if (ok) {
        moves->busy = true;
        for (uint32_t i = 0; ok && i < maximum; ++i) {
            const qa_bot_move_state *state = &slots[i].state;
            if (!slots[i].used || !state->walk_progress)
                continue;
            qa_bot_navigation *navigation = moves->services.navigation(moves->services.context, state->input.client);
            ok = navigation && qa_navigation_edge(qa_bot_navigation_runtime(navigation), state->walk_edge);
        }
        moves->busy = false;
    }
    if (ok) {
        free(moves->slots);
        moves->slots = slots;
        slots = NULL;
        moves->time = time;
        memcpy(moves->variables, variables, sizeof(variables));
        qa_nav_prediction_result_free(&moves->prediction);
        qa_nav_route_free(&moves->trajectory);
        free(moves->candidates); moves->candidates = NULL; moves->candidate_capacity = 0;
        free(moves->points); moves->points = NULL; moves->point_count = moves->point_capacity = 0;
        free(moves->visited); moves->visited = NULL; moves->visit_generation = 0; moves->visited_capacity = 0;
    }
    if (!ok && (!error || error->code == QA_OK))
        qa_error_set(error, QA_ERROR_FORMAT, io.offset, "Invalid bot movement continuation or imported dependency");
    qa_source_save_dispose(&io);
    free(slots);
    return ok;
}
