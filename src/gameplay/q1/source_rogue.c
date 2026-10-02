#include "maps/internal.h"
#include "qa/source_number.h"
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
        !state->owner.registry || classname.size != sizeof("rogue_team_state") - 1 ||
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
            !qa_actor_id_equal(state->owner, player)) continue;
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
    return actual && (qa_actor_id_equal(actual->owner, player) ||
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
        qa_string_id name;
        float mode;
        if (!game->services.cvar) {
            fail(error, player, "Rogue state requires its genuine source policy reader");
            goto finish;
        }
        if (!qa_strings_intern_cstr(qa_session_strings(game->services.session), "teamplay", &name, error))
            goto finish;
        if (!game->services.cvar(q1_cvar_context(game), name, &mode, error) ||
            !player_current(&operation, player, client, slot, error) ||
            !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
        bool ctf = mode == 4 || mode == 5 || mode == 6;
        bool keep_color = true;
        if (ctf) {
            float gamecfg;
            if (!qa_strings_intern_cstr(qa_session_strings(game->services.session), "gamecfg", &name, error) ||
                !game->services.cvar(q1_cvar_context(game), name, &gamecfg, error) ||
                !player_current(&operation, player, client, slot, error) ||
                !qa_q1_rogue_state_current(game, player, state, error)) goto finish;
            double bits = isfinite(gamecfg) ? fmod(trunc((double)gamecfg), 4294967296.0) : 0;
            if (bits < 0) bits += 4294967296.0;
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
bool qa_q1_rogue_field_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_rogue_field field, qa_bytes *out, qa_error *error) {
    if (!out || field < QA_Q1_ROGUE_FIELD_STEAM || field >= QA_Q1_ROGUE_FIELDS)
        return fail(error, actor, "Invalid Rogue source field read");
    const q1_actor *state = state_const(game, actor, error);
    if (!state) return false;
    qa_string_id word = state->state.rogue_fields[field];
    *out = word ? qa_strings_text(qa_session_strings(game->services.session), word) : (qa_bytes){0};
    return true;
}
bool qa_q1_rogue_field_write(qa_q1_game *game, qa_actor_id actor,
    qa_q1_rogue_field field, qa_bytes text, qa_error *error) {
    if (field < QA_Q1_ROGUE_FIELD_STEAM || field >= QA_Q1_ROGUE_FIELDS ||
        (text.size && !text.data)) return fail(error, actor, "Invalid Rogue source field write");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    const q1_actor *before = state_const(game, actor, error);
    qa_string_id word;
    bool okay = before && qa_strings_intern(qa_session_strings(game->services.session),
        text, &word, error) && qa_q1_game_operation_live(&operation) &&
        state_const(game, actor, error) == before;
    if (okay) ((q1_actor *)before)->state.rogue_fields[field] = word;
    qa_q1_game_operation_end(&operation);
    return okay;
}
/* Number.parseFloat admits a decimal prefix, not strtod's hexadecimal or
 * case-insensitive infinity grammar. The entity then checks binary64 finite
 * before publishing Math.fround; a finite huge decimal can consequently read
 * as Infinity even though a literal stored Infinity reads as zero. */
bool q1_source_number_read(qa_bytes text, double *out, qa_error *error) {
    size_t cursor = 0;
    while (cursor < text.size) {
        size_t begin = cursor;
        uint32_t scalar;
        if (!qa_utf8_next(text, &cursor, &scalar)) break;
        if (!qa_unicode_whitespace(scalar)) { cursor = begin; break; }
    }
    size_t start = cursor;
    if (cursor < text.size && (text.data[cursor] == '+' || text.data[cursor] == '-')) ++cursor;
    size_t digits = 0;
    while (cursor < text.size && text.data[cursor] >= '0' && text.data[cursor] <= '9') {
        ++cursor; ++digits;
    }
    if (cursor < text.size && text.data[cursor] == '.') {
        ++cursor;
        while (cursor < text.size && text.data[cursor] >= '0' && text.data[cursor] <= '9') {
            ++cursor; ++digits;
        }
    }
    if (!digits) { *out = 0; return true; }
    size_t end = cursor;
    if (cursor < text.size && (text.data[cursor] == 'e' || text.data[cursor] == 'E')) {
        ++cursor;
        if (cursor < text.size && (text.data[cursor] == '+' || text.data[cursor] == '-')) ++cursor;
        size_t exponent = cursor;
        while (cursor < text.size && text.data[cursor] >= '0' && text.data[cursor] <= '9') ++cursor;
        if (cursor != exponent) end = cursor;
    }
    double value;
    if (!qa_parse_ecmascript_number((qa_bytes){text.data + start, end - start}, &value, error))
        return false;
    *out = isfinite(value) ? qa_source_fround(value) : 0;
    return true;
}
bool qa_q1_rogue_number_read(qa_q1_game *game, qa_actor_id state,
    qa_q1_rogue_field field, double *out, qa_error *error) {
    if (!out) return fail(error, state, "Rogue number requires an output");
    qa_bytes text;
    return qa_q1_rogue_field_read(game, state, field, &text, error) &&
        q1_source_number_read(text, out, error);
}
bool qa_q1_rogue_number_write(qa_q1_game *game, qa_actor_id state,
    qa_q1_rogue_field field, double value, qa_error *error) {
    char text[32];
    return qa_format_ecmascript_number(qa_source_fround(value), text, error) &&
        qa_q1_rogue_field_write(game, state, field,
            (qa_bytes){(const uint8_t *)text, strlen(text)}, error);
}
static q1_actor *world(qa_q1_game *game, qa_error *error) {
    if (!rogue(game, error)) return NULL;
    qa_actor_id id = game->maps ? game->maps->world_actor : (qa_actor_id){0};
    q1_actor *actor = q1_entity(game, id);
    if (!actor || !actor->native || !q1_classnamed(game, id, "worldspawn")) {
        fail(error, id, "Rogue update requires its actual physical source world");
        return NULL;
    }
    return actor;
}
bool qa_q1_rogue_world_update_read(qa_q1_game *game, double *out, qa_error *error) {
    q1_actor *actor = world(game, error);
    if (!actor || !out) return false;
    qa_bytes text = actor->rogue_next_update ?
        qa_strings_text(qa_session_strings(game->services.session), actor->rogue_next_update) :
        (qa_bytes){0};
    return q1_source_number_read(text, out, error);
}
bool qa_q1_rogue_world_update_write(qa_q1_game *game, double value, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    q1_actor *actor = world(game, error);
    char text[32];
    qa_string_id word;
    bool okay = actor && qa_format_ecmascript_number(qa_source_fround(value), text, error) &&
        qa_strings_intern_cstr(qa_session_strings(game->services.session), text, &word, error) &&
        qa_q1_game_operation_live(&operation) && world(game, error) == actor;
    if (okay) actor->rogue_next_update = word;
    qa_q1_game_operation_end(&operation);
    return okay;
}
