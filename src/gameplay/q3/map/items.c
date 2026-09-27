#include "internal.h"

static bool hide_item(qa_q3_game *game, qa_actor_id actor, int32_t expire_at,
                      qa_error *error) {
    return qa_q3_item_availability(game, actor, false, 0, expire_at, error);
}

bool q3_map_spawn_item(qa_q3_game *game, const qa_q3_map_fields *fields,
                       qa_q3_map_actor_state *state, uint32_t item_index,
                       qa_error *error) {
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    if (!state || item_index == 0 || item_index >= count)
        return q3_map_fail(error, "invalid Q3 authored item");
    double wait, random;
    if (!q3_map_number(fields, "wait", 0, &wait, error) ||
        !q3_map_number(fields, "random", 0, &random, error))
        return false;
    state->wait = (float)wait;
    state->random = (float)random;
    state->kind = QA_Q3_MAP_ITEM;
    state->item = (qa_q3_item_spawn){.item_index = item_index,
                                     .count = state->count,
                                     .team_restriction =
                                         items[item_index].kind == QA_Q3_ITEM_PERSISTENT
                                             ? (int32_t)state->spawnflags
                                             : 0,
                                     .wait_seconds = state->wait,
                                     .random_seconds = state->random,
                                     .suspended = (state->spawnflags & 1u) != 0,
                                     .origin = state->origin,
                                     .target = state->target};
    if (!q3_map_allocate(game, state, NULL, false, error))
        return false;
    qa_q3_map_actor_state *stored = q3_map_get(game, state->actor);
    if (!stored)
        return q3_map_fail(error, "missing Q3 authored item state");
    q3_map_schedule(game, stored, 200, QA_Q3_MAP_THINK_ITEM_FINISH);
    *state = *stored;
    return true;
}

static bool finish_item(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_error *error) {
    bool available = !state->team_slave && !q3_map_text(game, state->targetname);
    size_t item_count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &item_count);
    if (state->item.item_index == 0 || state->item.item_index >= item_count)
        return q3_map_fail(error, "invalid Q3 authored item definition");
    bool delayed_powerup = available &&
                           items[state->item.item_index].kind == QA_Q3_ITEM_POWERUP;
    qa_actor_id actor = state->actor;
    state->think = QA_Q3_MAP_THINK_NONE;
    state->due_ms = 0;
    bool placed;
    if (!q3_item_bind_existing(game, actor, &state->item,
                               available && !delayed_powerup, false, &placed, error))
        return q3_rollback_spawn(game, actor, error);
    if (!placed) {
        q3_map_warn(game, actor, "Q3 authored item spawned inside solid geometry");
        return qa_session_release(game->options.services.session, actor, error);
    }
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    state->item_bound = true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    state->origin = body.origin;
    state->bounds = body.bounds;
    q3_actor *entry = q3_actor_get(game, actor);
    state->linked = entry && entry->kind == Q3_ACTOR_ITEM && !entry->state.item.hidden;
    if (delayed_powerup) {
        float seconds = q3_source_float_add(
            45.0f, q3_source_float_multiply(q3_crandom(game), 15.0f));
        state->due_ms = q3_map_source_schedule(game->now_ms, seconds);
        state->think = QA_Q3_MAP_THINK_ITEM_RESPAWN;
    }
    return true;
}

static bool team_member(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_q3_map_actor_state **out, qa_error *error) {
    if (!q3_map_text(game, state->team)) {
        *out = state;
        return true;
    }
    qa_q3_map_actor_state *master = q3_map_get(
        game, state->team_master.registry ? state->team_master : state->actor);
    if (!master)
        return q3_map_fail(error, "Q3 item team has no live master");
    size_t count = 0;
    qa_q3_map_actor_state *cursor = master;
    while (cursor) {
        if (cursor->kind != QA_Q3_MAP_ITEM || cursor->team != master->team)
            return q3_map_fail(error, "broken Q3 item team chain");
        if (++count > game->map->capacity)
            return q3_map_fail(error, "cyclic Q3 item team chain");
        if (!cursor->team_next.registry)
            cursor = NULL;
        else if (!(cursor = q3_map_get(game, cursor->team_next)))
            return q3_map_fail(error, "broken Q3 item team chain");
    }
    size_t choice = (size_t)(q3_rand(game) % (uint32_t)count);
    cursor = master;
    while (choice--) {
        cursor = q3_map_get(game, cursor->team_next);
        if (!cursor)
            return q3_map_fail(error, "broken Q3 item team selection");
    }
    *out = cursor;
    return true;
}

static bool respawn_item(qa_q3_game *game, qa_q3_map_actor_state *state,
                         qa_error *error) {
    qa_q3_map_actor_state *selected;
    state->think = QA_Q3_MAP_THINK_NONE;
    state->due_ms = 0;
    if (!team_member(game, state, &selected, error))
        return false;
    if (!selected->item_bound)
        return q3_map_fail(error, "Q3 item team member has not finished spawning");
    q3_actor *entry = q3_actor_get(game, selected->actor);
    if (!entry || entry->kind != Q3_ACTOR_ITEM)
        return q3_map_fail(error, "missing Q3 item team member lifecycle");
    int32_t expire_at = entry->state.item.expire_at;
    qa_actor_id actor = selected->actor;
    if (!qa_q3_item_availability(game, actor, true, 0, expire_at, error))
        return false;
    selected = q3_map_get(game, actor);
    if (!selected)
        return true;
    selected->linked = true;
    size_t count;
    const qa_q3_item *items = qa_q3_items(game->options.product, &count);
    uint32_t index = selected->item.item_index;
    if (index == 0 || index >= count)
        return q3_map_fail(error, "invalid Q3 item team member definition");
    const qa_q3_item *item = &items[index];
    int32_t channel = selected->speed ? 3 : 0;
    if (item->kind == QA_Q3_ITEM_POWERUP &&
        !q3_sound(game, actor, "sound/items/poweruprespawn.wav", channel, error))
        return false;
    if (item->kind == QA_Q3_ITEM_HOLDABLE && item->tag == QA_Q3_H_KAMIKAZE &&
        !q3_sound(game, actor, "sound/items/kamikazerespawn.wav", channel, error))
        return false;
    if (!q3_map_get(game, actor) || !q3_actor_get(game, actor))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    return !q3_map_get(game, actor) || !q3_actor_get(game, actor) ||
           q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_ITEM, 40, 0, body.origin,
                    qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}

bool q3_map_item_think(qa_q3_game *game, qa_q3_map_actor_state *state,
                       qa_error *error) {
    if (!state || state->kind != QA_Q3_MAP_ITEM)
        return q3_map_fail(error, "invalid Q3 authored item continuation");
    switch (state->think) {
    case QA_Q3_MAP_THINK_ITEM_FINISH:
        return finish_item(game, state, error);
    case QA_Q3_MAP_THINK_ITEM_RESPAWN:
        return respawn_item(game, state, error);
    default:
        return true;
    }
}

bool q3_map_item_use(qa_q3_game *game, qa_q3_map_actor_state *state,
                     qa_error *error) {
    if (!state || state->kind != QA_Q3_MAP_ITEM || !state->item_bound)
        return true;
    state->think = QA_Q3_MAP_THINK_ITEM_RESPAWN;
    state->due_ms = game->now_ms;
    return respawn_item(game, state, error);
}

bool q3_map_item_picked(qa_q3_game *game, qa_actor_id actor, int32_t respawn_at,
                        int32_t expire_at, bool *handled, qa_error *error) {
    if (handled)
        *handled = false;
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state || state->kind != QA_Q3_MAP_ITEM || !q3_map_text(game, state->team))
        return true;
    if (handled)
        *handled = true;
    if (!hide_item(game, actor, expire_at, error))
        return false;
    state = q3_map_get(game, actor);
    if (!state)
        return true;
    state->linked = false;
    if (!respawn_at)
        return true;
    qa_q3_map_actor_state *master = q3_map_get(
        game, state->team_master.registry ? state->team_master : state->actor);
    if (!master)
        return q3_map_fail(error, "Q3 item team lost its master during pickup");
    master->due_ms = respawn_at;
    master->think = QA_Q3_MAP_THINK_ITEM_RESPAWN;
    return true;
}
