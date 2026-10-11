#include "maps/internal.h"
#include "qa/text.h"
#include <math.h>

static bool fail(qa_error *error, qa_actor_id actor, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", text);
    return false;
}
static bool rogue(const qa_q1_game *game, qa_error *error) {
    return (game && !game->destroy_pending && !game->continuation_pending &&
        game->options.program == QA_Q1_ROGUE) ||
        fail(error, (qa_actor_id){0}, "Rogue state requires its live native source program");
}
static const q1_actor *state_const(const qa_q1_game *game, qa_actor_id actor,
    qa_error *error) {
    if (!rogue(game, error)) return NULL;
    const q1_actor *state = q1_entity_const(game, actor);
    qa_bytes classname = state ?
        qa_strings_text(qa_session_strings(game->services.session), state->classname) : (qa_bytes){0};
    if (!state || !state->native || state->kind != Q1_ROGUE_TEAM_STATE ||
        !q1_ref_present(state->owner) || classname.size != sizeof("rogue_team_state") - 1 ||
        memcmp(classname.data, "rogue_team_state", classname.size)) {
        fail(error, actor, "Rogue word has no genuine team-state actor");
        return NULL;
    }
    return state;
}
bool qa_q1_rogue_state_find(const qa_q1_game *game, qa_actor_id player,
    qa_actor_id *out, bool *found, qa_error *error) {
    if (!out || !found || !rogue(game, error)) return false;
    *found = false;
    *out = (qa_actor_id){0};
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q1_actor *state = game->actors[i];
        if (!state || state->kind != Q1_ROGUE_TEAM_STATE ||
            !q1_ref_equal(state->owner, q1_ref_from(game, player))) continue;
        if (!state_const(game, state->id, error)) return false;
        if (*found) return fail(error, player, "Rogue source retains duplicate player state");
        *found = true;
        *out = state->id;
    }
    return true;
}
bool qa_q1_rogue_state_current(const qa_q1_game *game, qa_actor_id player,
    qa_actor_id state, qa_error *error) {
    const q1_actor *actual = state_const(game, state, error);
    return actual && (q1_ref_equal(actual->owner, q1_ref_from(game, player)) ||
        fail(error, state, "Rogue state names a different full source player"));
}
static bool player_current(qa_q1_game_operation *operation, qa_actor_id actor,
    const q1_player *player, uint32_t expected, qa_error *error) {
    uint32_t slot;
    return (qa_q1_game_operation_live(operation) &&
        q1_player_get(operation->game, actor) == player &&
        qa_q1_native_client_slot(operation->game, actor, &slot, error) && slot == expected) ||
        fail(error, actor, "Rogue state lost its genuine physical source client");
}
bool qa_q1_rogue_state(qa_q1_game *game, qa_actor_id player, qa_actor_id *out,
    qa_error *error) {
    if (!out || !rogue(game, error)) return false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_player *client = q1_player_get(game, player);
    uint32_t slot;
    bool okay = false, found;
    qa_actor_id state;
    if (!qa_q1_native_client_slot(game, player, &slot, error) ||
        !player_current(&operation, player, client, slot, error) ||
        !qa_q1_rogue_state_find(game, player, &state, &found, error)) goto finish;
    if (!found) {
        q1_actor *created;
        if (!q1_create(game, "rogue_team_state", Q1_ROGUE_TEAM_STATE, player,
            &created, error)) goto finish;
        state = created->id;
        if (!player_current(&operation, player, client, slot, error) ||
            !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
        qa_actor_id unique;
        if (!qa_q1_rogue_state_find(game, player, &unique, &found, error) ||
            !found || !qa_actor_id_equal(unique, state)) goto finish;
        float mode;
        if (!game->host.cvars) {
            fail(error, player, "Rogue state requires its genuine source policy reader");
            goto finish;
        }
        if (!q1_source_value(game, QA_Q1_SOURCE_TEAMPLAY, 0, &mode, error) ||
            !player_current(&operation, player, client, slot, error) ||
            !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
        bool ctf = mode == 4 || mode == 5 || mode == 6;
        bool keep_color = true;
        if (ctf) {
            float gamecfg;
            if (!q1_source_value(game, QA_Q1_SOURCE_GAMECFG, 0, &gamecfg, error) ||
                !player_current(&operation, player, client, slot, error) ||
                !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
            uint32_t bits = (uint32_t)qa_source_float_to_i32(gamecfg);
            keep_color = ((uint32_t)bits & UINT32_C(8)) != 0;
        }
        double color = keep_color ? client->source_team : -1;
        if (!qa_q1_rogue_number_write(game, state, QA_Q1_ROGUE_FIELD_STEAM, color, error) ||
            !player_current(&operation, player, client, slot, error) ||
            !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
    }
    *out = state;
    okay = true;
finish:
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_rogue_number_read(qa_q1_game *game, qa_actor_id actor,
    qa_q1_rogue_field field, double *out, qa_error *error) {
    if (!out || field < QA_Q1_ROGUE_FIELD_STEAM || field >= QA_Q1_ROGUE_FIELDS)
        return fail(error, actor, "Invalid Rogue source number read");
    const q1_actor *state = state_const(game, actor, error);
    if (!state) return false;
    *out = state->state.rogue_fields[field];
    return true;
}
bool qa_q1_rogue_number_write(qa_q1_game *game, qa_actor_id actor,
    qa_q1_rogue_field field, double value, qa_error *error) {
    float number = (float)value;
    if (field < QA_Q1_ROGUE_FIELD_STEAM || field >= QA_Q1_ROGUE_FIELDS || !isfinite(number))
        return fail(error, actor, "Invalid Rogue source number write");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *state = (q1_actor *)state_const(game, actor, error);
    bool okay = state != NULL;
    if (okay) state->state.rogue_fields[field] = number;
    qa_q1_game_operation_end(&operation);
    return okay;
}
static q1_actor *world(qa_q1_game *game, qa_error *error) {
    if (!rogue(game, error)) return NULL;
    qa_actor_id id = game->maps ? game->maps->world_actor : (qa_actor_id){0};
    q1_actor *actor = q1_entity(game, id);
    if (!actor || !actor->native || !q1_classnamed(game, id, game->runtime_names[Q1_NAME_WORLDSPAWN])) {
        fail(error, id, "Rogue update requires its actual physical source world");
        return NULL;
    }
    return actor;
}
bool qa_q1_rogue_world_update_read(qa_q1_game *game, double *out, qa_error *error) {
    q1_actor *actor = world(game, error);
    if (!actor || !out) return false;
    *out = actor->rogue_next_update;
    return true;
}
bool qa_q1_rogue_world_update_write(qa_q1_game *game, double value, qa_error *error) {
    float number = (float)value;
    if (!isfinite(number)) return fail(error, (qa_actor_id){0}, "Invalid Rogue update time");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *actor = world(game, error);
    bool okay = actor != NULL;
    if (okay) actor->rogue_next_update = number;
    qa_q1_game_operation_end(&operation);
    return okay;
}
