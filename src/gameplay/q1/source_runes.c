#include "maps/internal.h"

static const char *const rune_names[QA_Q1_RUNE_COUNT] = {
    "resistance", "strength", "haste", "regeneration"};
static const char *const rune_classes[QA_Q1_RUNE_COUNT] = {
    "item_rune_resistance", "item_rune_strength", "item_rune_haste", "item_rune_regeneration"};
static const char *const rune_models[QA_Q1_RUNE_COUNT] = {
    "progs/end1.mdl", "progs/end2.mdl", "progs/end3.mdl", "progs/end4.mdl"};

static bool fail(qa_error *error, qa_actor_id actor, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "%s", message);
    return false;
}
static bool live(qa_q1_game *game, qa_error *error) {
    if (!game || game->destroy_pending || game->continuation_pending ||
        game->options.program != QA_Q1_CTF)
        return fail(error, (qa_actor_id){0}, "CTF rune requires its actual native source");
    return true;
}
static bool source_current(qa_q1_game *game, qa_error *error) {
    if (!live(game, error)) return false;
    if (!game->source_runes.current)
        return fail(error, (qa_actor_id){0}, "CTF rune has no current composition service");
    return game->source_runes.current(game->source_runes.context, game, error) &&
        live(game, error);
}
static bool actor_current(qa_q1_game *game, qa_actor_id actor, const q1_actor *expected,
    q1_entity_kind kind, qa_error *error) {
    if (!source_current(game, error)) return false;
    q1_actor *entity = q1_entity(game, actor);
    return (entity == expected && entity && entity->native && entity->kind == kind) ||
        fail(error, actor, "CTF rune source actor changed during a callback");
}
static q1_actor *world(qa_q1_game *game, qa_error *error) {
    if (!live(game, error)) return NULL;
    qa_actor_id actor = game->maps ? game->maps->world_actor : (qa_actor_id){0};
    q1_actor *entity = q1_entity(game, actor);
    if (!entity || !entity->native || !q1_classnamed(game, actor, "worldspawn")) {
        fail(error, actor, "CTF rune cursor lost its actual source world");
        return NULL;
    }
    return entity;
}
bool qa_q1_source_runes_configure(qa_q1_game *game,
    const qa_q1_source_runes_services *services, qa_error *error) {
    if (!live(game, error)) return false;
    if (game->observation_depth || !services || !services->current || !services->touch)
        return fail(error, (qa_actor_id){0}, "CTF runes require genuine composition callbacks");
    game->source_runes = *services;
    return true;
}
bool qa_q1_source_rune_read(const qa_q1_game *game, qa_actor_id actor,
    qa_q1_source_rune *out, qa_error *error) {
    const q1_actor *entity = q1_entity_const(game, actor);
    if (!out || !game || game->destroy_pending || game->continuation_pending ||
        game->options.program != QA_Q1_CTF || !entity || !entity->native ||
        entity->kind != Q1_SOURCE_CTF_RUNE)
        return fail(error, actor, "CTF rune observation requires its genuine native entity");
    qa_strings *strings = qa_session_strings(game->services.session);
    qa_bytes word = qa_strings_text(strings, entity->state.source_rune.rune);
    for (unsigned i = 0; i < QA_Q1_RUNE_COUNT; ++i)
        if (word.size == strlen(rune_names[i]) && !memcmp(word.data, rune_names[i], word.size)) {
            *out = (qa_q1_source_rune)i;
            return true;
        }
    return fail(error, actor, "CTF rune lost its actual source kind word");
}

/* The source Map enumerates the real deathmatch entities in creation order.
 * A retired or differently classified previous reference starts at the first. */
static bool next_spawn(qa_q1_game *game, qa_actor_id *out, qa_error *error) {
    q1_actor *owner = world(game, error);
    if (!owner || !source_current(game, error) || world(game, error) != owner) return false;
    const q1_actor *previous = q1_entity_const(game, owner->ctf_rune_spawn);
    const qa_actor_registry *actors = qa_session_actors(game->services.session);
    const qa_actor_record *before = previous && previous->native &&
        q1_classnamed(game, previous->id, "info_player_deathmatch") ?
        qa_actors_get(actors, previous->id) : NULL;
    bool has_previous = before != NULL;
    if (before && (before->owner != game->options.provider || !before->has_source))
        return fail(error, previous->id, "CTF rune cursor lost its source creation ordinal");
    const qa_actor_record *first = NULL, *next = NULL;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q1_actor *spot = game->actors[i];
        if (!spot || !spot->active || !spot->native || !q1_alive(game, spot->id) ||
            !q1_classnamed(game, spot->id, "info_player_deathmatch")) continue;
        const qa_actor_record *record = qa_actors_get(actors, spot->id);
        if (!record || record->owner != game->options.provider || !record->has_source)
            return fail(error, spot->id, "CTF rune spawn lost its genuine native identity");
        if (!first || record->source_slot < first->source_slot) first = record;
        if (has_previous && record->source_slot > before->source_slot &&
            (!next || record->source_slot < next->source_slot)) next = record;
    }
    const qa_actor_record *selected = next ? next : first;
    if (!selected) return fail(error, (qa_actor_id){0}, "CTF has no info_player_deathmatch to spawn a rune");
    owner->ctf_rune_spawn = selected->id;
    *out = selected->id;
    return true;
}
static bool drop(qa_q1_game *game, qa_q1_source_rune rune, qa_vec3 origin,
    qa_actor_id *out, qa_error *error) {
    if ((unsigned)rune >= QA_Q1_RUNE_COUNT || !out || !source_current(game, error))
        return (!error || error->code == QA_OK) ?
            fail(error, (qa_actor_id){0}, "Invalid CTF rune source creation") : false;
    q1_actor *entity;
    if (!q1_create(game, rune_classes[rune], Q1_SOURCE_CTF_RUNE, (qa_actor_id){0},
        &entity, error)) return false;
    qa_actor_id actor = entity->id;
    if (!actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error) ||
        !q1_model(game, entity, rune_models[rune], error) ||
        !actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error)) return false;
    entity->physics.solid = QA_PHYSICS_TRIGGER;
    entity->physics.motion = QA_PHYSICS_TOSS;
    entity->state.source_rune.movement_flags |= UINT32_C(256);
    if (!qa_strings_intern_cstr(qa_session_strings(game->services.session), rune_names[rune],
        &entity->state.source_rune.rune, error) ||
        !actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error)) return false;
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, actor, &body, error) ||
        !actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error)) return false;
    body.origin = qa_vec_add(origin, qa_v3(0, 0, -24));
    /* Preserve the two genuine source RNG draws and their ordering. */
    double x = -500 + (double)q1_random(game) * 1000;
    double y = -500 + (double)q1_random(game) * 1000;
    body.velocity = qa_v3((float)x, (float)y, 400);
    body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
    if (!qa_world_body_write(game->services.world, actor, &body, error) ||
        !actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error) ||
        !q1_link(game, entity, error) ||
        !actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error) ||
        !q1_schedule(game, entity, 120, Q1_THINK_SOURCE_CTF_RUNE_RESPAWN, error) ||
        !actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error)) return false;
    *out = actor;
    return true;
}
bool qa_q1_source_rune_drop(qa_q1_game *game, qa_q1_source_rune rune, qa_vec3 origin,
    qa_actor_id *out, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    bool okay = drop(game, rune, origin, out, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_source_rune_collect(qa_q1_game *game, qa_actor_id actor, qa_error *error) {
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(game, &operation, error)) return false;
    qa_q1_source_rune rune;
    q1_actor *entity = q1_entity(game, actor);
    bool okay = qa_q1_source_rune_read(game, actor, &rune, error) &&
        actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error) &&
        q1_remove(game, entity, error) && source_current(game, error);
    qa_q1_game_operation_end(&operation);
    return okay;
}

bool q1_source_runes_start(qa_q1_game *game, qa_error *error) {
    if (game->options.program != QA_Q1_CTF || !game->options.deathmatch) return true;
    q1_actor *owner = world(game, error);
    if (!owner) return false;
    qa_strings *strings = qa_session_strings(game->services.session);
    qa_bytes map = qa_strings_text(strings, game->maps->options.current_map);
    if (map.size == 5 && !memcmp(map.data, "start", 5)) return true;
    double started;
    qa_bytes word = owner->ctf_runes_spawned ? qa_strings_text(strings, owner->ctf_runes_spawned) : (qa_bytes){0};
    if (!q1_source_number_read(word, &started, error)) return false;
    if (started != 0) return true;
    qa_string_id one;
    if (!qa_strings_intern_cstr(strings, "1", &one, error) || world(game, error) != owner) return false;
    owner->ctf_runes_spawned = one;
    q1_actor *timer;
    if (!q1_create(game, "ctf_rune_spawn", Q1_SOURCE_CTF_RUNE_TIMER, (qa_actor_id){0},
        &timer, error)) return false;
    if (!live(game, error) || q1_entity(game, timer->id) != timer || !timer->native ||
        timer->kind != Q1_SOURCE_CTF_RUNE_TIMER)
        return fail(error, timer->id, "CTF rune timer retired during its source constructor");
    return q1_schedule(game, timer, .1, Q1_THINK_SOURCE_CTF_RUNE_SPAWN, error);
}
bool q1_source_rune_think(qa_q1_game *game, q1_actor *entity, q1_think_kind think,
    qa_error *error) {
    qa_actor_id actor = entity->id;
    q1_entity_kind kind = think == Q1_THINK_SOURCE_CTF_RUNE_SPAWN ?
        Q1_SOURCE_CTF_RUNE_TIMER : Q1_SOURCE_CTF_RUNE;
    if (!actor_current(game, actor, entity, kind, error)) return false;
    qa_q1_source_rune first = QA_Q1_RUNE_RESISTANCE;
    unsigned count = QA_Q1_RUNE_COUNT;
    if (think == Q1_THINK_SOURCE_CTF_RUNE_RESPAWN) {
        if (!qa_q1_source_rune_read(game, actor, &first, error)) return false;
        count = 1;
    } else {
        double rotations = (double)q1_random(game) * 10;
        for (; rotations > 0; --rotations) {
            qa_actor_id spot;
            if (!next_spawn(game, &spot, error) || !actor_current(game, actor, entity, kind, error))
                return false;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        qa_actor_id spot, item;
        qa_body_state body;
        if (!next_spawn(game, &spot, error) ||
            !qa_world_body_read(game->services.world, spot, &body, error) ||
            !actor_current(game, actor, entity, kind, error) || !q1_alive(game, spot) ||
            !drop(game, (qa_q1_source_rune)(first + i), body.origin, &item, error) ||
            !actor_current(game, actor, entity, kind, error)) return false;
    }
    return q1_remove(game, entity, error) && source_current(game, error);
}
bool q1_source_rune_touch(qa_q1_game *game, q1_actor *entity, qa_actor_id player,
    qa_error *error) {
    qa_actor_id actor = entity->id;
    return actor_current(game, actor, entity, Q1_SOURCE_CTF_RUNE, error) &&
        game->source_runes.touch(game->source_runes.context, actor, player, error) &&
        source_current(game, error);
}
