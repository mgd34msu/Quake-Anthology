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
static qa_id_index_cursor owner_cursor(const qa_q1_game *game, q1_ref owner) {
    uint32_t slot = 0;
    uint64_t discriminator = 0;
    if (q1_ref_present(owner)) {
        if (owner.kind == QA_ACTOR_REFERENCE_SOURCE) {
            slot = owner.value.source.slot;
            discriminator = owner.value.source.owner;
        } else {
            slot = owner.value.actor.slot;
            discriminator = owner.value.actor.generation;
        }
    }
    return qa_id_index_begin(&game->rogue_state_owners, slot, discriminator);
}
static const q1_actor *owner_state(const qa_q1_game *game, q1_ref owner,
    const q1_actor *ignore) {
    qa_id_index_cursor cursor = owner_cursor(game, owner);
    size_t value;
    while (qa_id_index_next(&game->rogue_state_owners, &cursor, &value)) {
        const q1_actor *state = (const q1_actor *)(uintptr_t)value;
        if (state != ignore && state->id.slot < game->capacity &&
            game->actors[state->id.slot] == state && q1_ref_equal(state->owner, owner))
            return state;
    }
    return NULL;
}
bool q1_source_rogue_states_prepare(qa_q1_game *game, qa_error *error) {
    return game->options.program != QA_Q1_ROGUE ||
        qa_id_index_reserve(&game->rogue_state_owners, game->capacity, error);
}
void q1_source_rogue_states_free(qa_q1_game *game) {
    qa_id_index_destroy(&game->rogue_state_owners);
}
bool q1_source_rogue_state_publish(qa_q1_game *game, q1_actor *state, qa_error *error) {
    if (state->kind != Q1_ROGUE_TEAM_STATE) return true;
    if (owner_state(game, state->owner, state))
        return fail(error, state->id, "Rogue source retains duplicate player state");
    qa_id_index_cursor cursor = owner_cursor(game, state->owner);
    qa_id_index_insert(&game->rogue_state_owners, cursor.key, cursor.discriminator,
        (size_t)(uintptr_t)state);
    return true;
}
void q1_source_rogue_state_unpublish(qa_q1_game *game, const q1_actor *state) {
    if (!state || state->kind != Q1_ROGUE_TEAM_STATE) return;
    qa_id_index_cursor cursor = owner_cursor(game, state->owner);
    qa_id_index_remove_value(&game->rogue_state_owners, cursor.key, cursor.discriminator,
        (size_t)(uintptr_t)state);
}
bool q1_source_rogue_state_owner(qa_q1_game *game, q1_actor *state, q1_ref owner,
    qa_error *error) {
    if (state->kind != Q1_ROGUE_TEAM_STATE) {
        state->owner = owner;
        return true;
    }
    if (q1_ref_equal(state->owner, owner)) {
        state->owner = owner;
        return true;
    }
    if (owner_state(game, owner, state))
        return fail(error, state->id, "Rogue source retains duplicate player state");
    q1_source_rogue_state_unpublish(game, state);
    state->owner = owner;
    qa_id_index_cursor cursor = owner_cursor(game, owner);
    qa_id_index_insert(&game->rogue_state_owners, cursor.key, cursor.discriminator,
        (size_t)(uintptr_t)state);
    return true;
}
static const q1_actor *state_const(const qa_q1_game *game, qa_actor_id actor,
    qa_error *error) {
    if (!rogue(game, error)) return NULL;
    const q1_actor *state = q1_entity_const(game, actor);
    if (!state || !state->native || state->kind != Q1_ROGUE_TEAM_STATE ||
        !q1_ref_present(state->owner) ||
        state->classname != game->runtime_names[Q1_NAME_CLASS_ROGUE_TEAM_STATE]) {
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
    const q1_actor *state = owner_state(game, q1_ref_from(game, player), NULL);
    if (state) {
        if (!state_const(game, state->id, error)) return false;
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
        if (!q1_create(game, game->runtime_names[Q1_NAME_CLASS_ROGUE_TEAM_STATE], Q1_ROGUE_TEAM_STATE, player,
            &created, error)) goto finish;
        state = created->id;
        if (!player_current(&operation, player, client, slot, error) ||
            !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
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
