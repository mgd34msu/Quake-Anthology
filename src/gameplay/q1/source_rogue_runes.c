#include "maps/internal.h"
#include "qa/text.h"
#include <stdio.h>

static bool fail(qa_error *error, qa_actor_id actor, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", text);
    return false;
}
static bool current(qa_q1_game *g, qa_error *error) {
    return (g && !g->destroy_pending && !g->continuation_pending &&
        g->options.program == QA_Q1_ROGUE && g->source_rogue_tag.current &&
        g->source_rogue_tag.current(g->source_rogue_tag.context, g, error)) ||
        fail(error, (qa_actor_id){0}, "Rogue runes lost their actual native composition");
}
static q1_rogue_rune_player *find(const qa_q1_game *g, qa_actor_id actor) {
    for (q1_rogue_rune_player *row = g ? g->rogue_rune_players : NULL; row; row = row->next)
        if (qa_actor_id_equal(row->actor, actor)) return row;
    return NULL;
}
static bool player_current(qa_q1_game *g, qa_actor_id actor,
    const q1_rogue_rune_player *row, qa_error *error) {
    return current(g, error) &&
        ((qa_actors_get(qa_session_actors(g->services.session), actor) && find(g, actor) == row) ||
         fail(error, actor, "Rogue rune state changed during source callback"));
}
static q1_rogue_rune_player *state(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!current(g, error) || !qa_actors_get(qa_session_actors(g->services.session), actor)) {
        fail(error, actor, "Rogue rune operation requires a live canonical actor");
        return NULL;
    }
    q1_rogue_rune_player *found = find(g, actor);
    if (found) return found;
    q1_rogue_rune_player *row = calloc(1, sizeof(*row));
    if (!row) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Creating Rogue rune carrier state");
        return NULL;
    }
    row->actor = actor;
    q1_rogue_rune_player **tail = &g->rogue_rune_players;
    while (*tail) tail = &(*tail)->next;
    *tail = row;
    return row;
}
void q1_source_rogue_runes_release(qa_q1_game *g, qa_actor_id actor) {
    q1_rogue_rune_player **link = &g->rogue_rune_players;
    while (*link) {
        q1_rogue_rune_player *row = *link;
        if (qa_actor_id_equal(row->actor, actor)) {
            *link = row->next;
            free(row);
            return;
        }
        link = &row->next;
    }
}
void q1_source_rogue_runes_free(qa_q1_game *g) {
    while (g->rogue_rune_players) {
        q1_rogue_rune_player *next = g->rogue_rune_players->next;
        free(g->rogue_rune_players);
        g->rogue_rune_players = next;
    }
}
bool qa_q1_source_rogue_runes_read(const qa_q1_game *g, qa_actor_id actor,
    uint32_t *out, bool *found, qa_error *error) {
    if (!g || !out || !found || g->destroy_pending || g->continuation_pending ||
        g->options.program != QA_Q1_ROGUE ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return fail(error, actor, "Rogue rune observation lost its actual actor");
    const q1_rogue_rune_player *row = find(g, actor);
    *found = row != NULL;
    *out = row ? row->rune : 0;
    return true;
}
static q1_actor *world(qa_q1_game *g, qa_error *error) {
    q1_actor *e = g->maps ? q1_entity(g, g->maps->world_actor) : NULL;
    if (!e || !e->native || !q1_classnamed(g, e->id, "worldspawn")) {
        fail(error, (qa_actor_id){0}, "Rogue rune cursor lost its source worldspawn");
        return NULL;
    }
    return e;
}
static bool next_spawn(qa_q1_game *g, qa_vec3 *out, qa_error *error) {
    q1_actor *owner = world(g, error);
    if (!owner || !current(g, error) || world(g, error) != owner) return false;
    const q1_actor *previous = q1_entity_const(g, owner->rogue_rune_spawn);
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    const qa_actor_record *before = previous && previous->native &&
        q1_classnamed(g, previous->id, "info_player_deathmatch") ? qa_actors_get(actors, previous->id) : NULL;
    const qa_actor_record *first = NULL, *next = NULL;
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *spot = g->actors[i];
        if (!spot || !spot->native || !q1_alive(g, spot->id) ||
            !q1_classnamed(g, spot->id, "info_player_deathmatch")) continue;
        const qa_actor_record *record = qa_actors_get(actors, spot->id);
        if (!record || record->owner != g->options.provider || !record->has_source)
            return fail(error, spot->id, "Rogue rune spawn lost its physical source ordinal");
        if (!first || record->source_slot < first->source_slot) first = record;
        if (before && record->source_slot > before->source_slot &&
            (!next || record->source_slot < next->source_slot)) next = record;
    }
    const qa_actor_record *chosen = next ? next : first;
    if (!chosen) return fail(error, (qa_actor_id){0}, "Rogue runes require an actual deathmatch spawn");
    qa_actor_id selected = chosen->id;
    owner->rogue_rune_spawn = selected;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, selected, &body, error) ||
        !current(g, error) || world(g, error) != owner) return false;
    *out = body.origin;
    return true;
}
static bool entity_current(qa_q1_game *g, qa_actor_id actor, const q1_actor *e, qa_error *error) {
    return current(g, error) && ((q1_entity(g, actor) == e && e && e->native &&
        e->kind == Q1_SOURCE_ROGUE_RUNE) || fail(error, actor, "Rogue rune entity changed during callback"));
}
static qa_vec3 velocity(qa_q1_game *g) {
    double x = -300 + (double)q1_random(g) * 600;
    double y = -300 + (double)q1_random(g) * 600;
    return qa_v3((float)x, (float)y, 300);
}
static bool spawn(qa_q1_game *g, uint32_t rune, qa_vec3 origin, qa_error *error) {
    q1_actor *e;
    if (!current(g, error) || !q1_create(g, "rogue_rune", Q1_SOURCE_ROGUE_RUNE,
        (qa_actor_id){0}, &e, error)) return false;
    qa_actor_id actor = e->id;
    char word[32];
    if (!qa_format_ecmascript_number((float)(rune), word, error) ||
        !qa_strings_intern_cstr(qa_session_strings(g->services.session), word, &e->state.rogue_rune, error) ||
        !entity_current(g, actor, e, error)) return false;
    e->source_movement_flags = 256;
    e->physics.solid = QA_PHYSICS_TRIGGER;
    e->physics.motion = QA_PHYSICS_TOSS;
    unsigned model = (rune & 1) ? 1 : (rune & 2) ? 2 : (rune & 4) ? 3 : 4;
    snprintf(word, sizeof(word), "progs/end%u.mdl", model);
    qa_body_state body;
    if (!q1_model(g, e, word, error) || !entity_current(g, actor, e, error) ||
        !qa_world_body_read(g->services.world, actor, &body, error)) return false;
    body.origin = origin;
    body.velocity = velocity(g);
    body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    return qa_world_body_write(g->services.world, actor, &body, error) &&
        entity_current(g, actor, e, error) && q1_schedule(g, e, 120, Q1_THINK_SOURCE_ROGUE_RUNE_RESPAWN, error) &&
        q1_link(g, e, error) && entity_current(g, actor, e, error);
}
static bool message(qa_q1_game *g, qa_actor_id actor, const char *key,
    const q1_rogue_rune_player *row, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
        .provider = g->options.provider, .actor = actor, .time_ns = g->time_ns};
    return qa_strings_intern_cstr(qa_session_strings(g->services.session), key, &event.text, error) &&
        qa_builtin_emit(&g->services, &event, error) && player_current(g, actor, row, error);
}
bool q1_source_rogue_rune_touch(qa_q1_game *g, q1_actor *e, qa_actor_id actor, qa_error *error) {
    qa_actor_id item = e->id;
    bool found;
    if (!entity_current(g, item, e, error) ||
        !g->source_rogue_tag.player(g->source_rogue_tag.context, actor, &found, error) ||
        !entity_current(g, item, e, error)) return false;
    if (!found) return true;
    qa_combat_state combat;
    if (!qa_combat_read(g->services.combat, actor, &combat, error) ||
        !entity_current(g, item, e, error)) return false;
    if (combat.health <= 0) return true;
    q1_rogue_rune_player *row = state(g, actor, error);
    if (!row) return false;
    if (row->rune) {
        if (row->notice < g->time && !message(g, actor, "$qc_already_have_rune", row, error)) return false;
        row->notice = g->time + 5;
        return true;
    }
    double value;
    if (!q1_source_number_read(qa_strings_text(qa_session_strings(g->services.session), e->state.rogue_rune),
        &value, error)) return false;
    uint32_t rune = value == 1 ? 1 : value == 2 ? 2 : value == 4 ? 4 : value == 8 ? 8 : 0;
    if (!rune) return fail(error, item, "Rogue rune lost its authored source number");
    row->rune |= rune;
    return q1_sound(g, actor, "weapons/pkup.wav", 0, 1, error) && player_current(g, actor, row, error) &&
        message(g, actor, (rune & 1) ? "$qc_rune_resistance" : (rune & 2) ? "$qc_rune_strength" :
            (rune & 4) ? "$qc_rune_haste" : "$qc_rune_regeneration", row, error) &&
        entity_current(g, item, e, error) && q1_remove(g, e, error);
}
bool q1_source_rogue_rune_think(qa_q1_game *g, q1_actor *e, q1_think_kind kind, qa_error *error) {
    if (!current(g, error)) return false;
    if (kind == Q1_THINK_SOURCE_ROGUE_RUNE_SPAWN) {
        if (e->kind != Q1_SOURCE_ROGUE_RUNE_TIMER || !q1_remove(g, e, error) || !current(g, error)) return false;
        for (uint32_t rune = 1; rune <= 8; rune <<= 1) {
            qa_vec3 origin;
            if (!next_spawn(g, &origin, error) || !spawn(g, rune, origin, error)) return false;
        }
        return true;
    }
    qa_actor_id actor = e->id;
    qa_vec3 origin;
    qa_body_state body;
    if (!entity_current(g, actor, e, error) || !next_spawn(g, &origin, error) ||
        !entity_current(g, actor, e, error) || !qa_world_body_read(g->services.world, actor, &body, error)) return false;
    body.origin = origin;
    body.velocity = velocity(g);
    return qa_world_body_write(g->services.world, actor, &body, error) &&
        entity_current(g, actor, e, error) && q1_link(g, e, error) && entity_current(g, actor, e, error) &&
        q1_schedule(g, e, 120, Q1_THINK_SOURCE_ROGUE_RUNE_RESPAWN, error);
}
static bool frame(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!current(g, error)) return false;
    q1_actor *owner = world(g, error);
    qa_string_id name;
    float cfg, deathmatch;
    if (!owner || !g->services.cvar ||
        !qa_strings_intern_cstr(qa_session_strings(g->services.session), "deathmatch", &name, error) ||
        !g->services.cvar(q1_cvar_context(g), name, &deathmatch, error) || !current(g, error)) return false;
    if (deathmatch != 0) {
        if (!qa_strings_intern_cstr(qa_session_strings(g->services.session), "gamecfg", &name, error) ||
            !g->services.cvar(q1_cvar_context(g), name, &cfg, error) || !current(g, error) ||
            world(g, error) != owner) return false;
        uint32_t integer = (uint32_t)qa_number_to_i32(cfg);
        double started = 0;
        if ((integer & 1) &&
            (!q1_source_number_read(qa_strings_text(qa_session_strings(g->services.session), owner->rogue_runes_spawned),
                &started, error))) return false;
        if ((integer & 1) && started == 0) {
            if (!qa_strings_intern_cstr(qa_session_strings(g->services.session), "1", &owner->rogue_runes_spawned, error) ||
                !current(g, error) || world(g, error) != owner) return false;
            q1_actor *timer;
            if (!q1_create(g, "rogue_rune_spawner", Q1_SOURCE_ROGUE_RUNE_TIMER,
                (qa_actor_id){0}, &timer, error) || !current(g, error) ||
                !q1_schedule(g, timer, .1, Q1_THINK_SOURCE_ROGUE_RUNE_SPAWN, error)) return false;
        }
    }
    q1_rogue_rune_player *row = state(g, actor, error);
    if (!row) return false;
    if (!(row->rune & 8) || row->regeneration >= g->time) return true;
    qa_combat_state health;
    if (!qa_combat_read(g->services.combat, actor, &health, error) || !player_current(g, actor, row, error)) return false;
    if (health.health >= 100) return true;
    if (!q1_sound(g, actor, "runes/end4.wav", 0, 1, error) || !player_current(g, actor, row, error) ||
        !qa_combat_read(g->services.combat, actor, &health, error) || !player_current(g, actor, row, error) ||
        !qa_combat_set_health(g->services.combat, actor, fminf(100, health.health + 5), error) ||
        !player_current(g, actor, row, error)) return false;
    row->regeneration = g->time + 1;
    return true;
}
bool qa_q1_source_rogue_runes_frame(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    bool okay = frame(g, actor, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_source_rogue_runes_drop(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_rogue_rune_player *row = state(g, actor, error);
    bool okay = row != NULL;
    if (okay && qa_world_body_storage_serial(g->services.world, actor)) {
        qa_body_state body;
        okay = qa_world_body_read(g->services.world, actor, &body, error) && player_current(g, actor, row, error);
        for (uint32_t rune = 1; okay && rune <= 8; rune <<= 1)
            if (row->rune & rune) okay = spawn(g, rune, body.origin, error) && player_current(g, actor, row, error);
        if (okay) row->rune = 0;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
static bool noise(qa_q1_game *g, qa_actor_id actor, q1_rogue_rune_player *row,
    unsigned rune, qa_error *error) {
    char name[24];
    snprintf(name, sizeof(name), "runes/end%u.wav", rune);
    return q1_sound(g, actor, name, 0, 1, error) && player_current(g, actor, row, error);
}
static bool adjust(qa_q1_game *g, qa_actor_id actor, float *amount,
    unsigned kind, qa_error *error) {
    if (!amount) return fail(error, actor, "Rogue rune calculation has no actual output");
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_rogue_rune_player *row = state(g, actor, error);
    bool okay = row != NULL;
    float value = *amount;
    if (okay && (row->rune & (kind == 0 ? 2u : kind == 1 ? 1u : 4u))) {
        if (kind == 0) value = (float)(float)((double)value * 2);
        else {
            double *stamp = kind == 1 ? &row->earth_noise : &row->hell_noise;
            if (*stamp < g->time) {
                okay = noise(g, actor, row, kind == 1 ? 1 : 3, error);
                if (okay) *stamp = g->time + 1;
            }
            if (okay) value = (float)(kind == 1 ? (float)((double)value / 2) :
                (float)((float)((double)value * 2) / 3));
        }
    }
    if (okay) *amount = value;
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_source_rogue_runes_damage(qa_q1_game *g, qa_actor_id actor, float *amount, qa_error *error) {
    return adjust(g, actor, amount, 0, error);
}
bool qa_q1_source_rogue_runes_resistance(qa_q1_game *g, qa_actor_id actor, float *amount, qa_error *error) {
    return adjust(g, actor, amount, 1, error);
}
bool qa_q1_source_rogue_runes_attack_delay(qa_q1_game *g, qa_actor_id actor, float *amount, qa_error *error) {
    return adjust(g, actor, amount, 2, error);
}
bool qa_q1_source_rogue_runes_before_fire(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_rogue_rune_player *row = state(g, actor, error);
    bool okay = row != NULL;
    if (okay && (row->rune & 2) && row->black_noise < g->time) {
        okay = noise(g, actor, row, 2, error);
        if (okay) row->black_noise = g->time + 1;
    }
    qa_q1_game_operation_end(&operation);
    return okay;
}
