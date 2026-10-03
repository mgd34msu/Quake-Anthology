#include "internal.h"

static bool reference_valid(const qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!actor.registry)
        return (!actor.generation && !actor.slot) ||
               q3_fail(error, "malformed null Q3 actor reference");
    qa_saved_actor_id saved;
    return qa_actors_save_reference(qa_session_actors(game->options.services.session), actor,
                                    &saved, error);
}
static bool string_valid(const qa_q3_game *game, qa_string_id id) {
    return !id || qa_strings_cstr(qa_session_strings(game->options.services.session), id) != NULL;
}
static bool trajectory_valid(const qa_trajectory *trajectory, qa_error *error) {
    qa_vec3 position;
    return qa_vec_finite(trajectory->base) && qa_vec_finite(trajectory->delta) &&
           qa_trajectory_position(trajectory, trajectory->time_ms, 800, &position, error);
}
static bool player_references_valid(const qa_q3_game *game,
                                     const qa_q3_player_state *p, qa_error *error) {
    if (!string_valid(game, p->loop_sound)) return false;
    for (unsigned i = 0; i < QA_Q3_WEAPON_COUNT; ++i)
        if (!string_valid(game, p->ammo_regeneration_items[i])) return false;
    return reference_valid(game, p->hook, error) &&
           reference_valid(game, p->attached_mine, error) &&
           reference_valid(game, p->persistent_item, error) &&
           reference_valid(game, p->portal, error);
}
static bool enemy_reference_valid(const qa_q3_game *game, const q3_actor *actor, qa_error *error) {
    return reference_valid(game, actor->enemy, error) &&
        (actor->enemy_source_present ? actor->enemy_source_slot < QA_Q3_SOURCE_NONE :
                                      actor->enemy_source_slot == 0);
}
static bool actor_valid(const qa_q3_game *game, const q3_actor *actor, qa_error *error) {
    if (!isfinite(actor->alpha) || !enemy_reference_valid(game, actor, error) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor->actor))
        return q3_fail(error, "stale Q3 checkpoint actor");
    switch (actor->kind) {
    case Q3_ACTOR_PLAYER: {
        const qa_q3_player_state *p = &actor->state.player;
        return q3_player_state_valid(p) && player_references_valid(game, p, error);
    }
    case Q3_ACTOR_MISSILE: {
        const q3_missile *m = &actor->state.missile;
        return m->weapon > QA_Q3_W_NONE && m->weapon < QA_Q3_WEAPON_COUNT &&
               m->phase >= Q3_MISSILE_FLIGHT && m->phase <= Q3_MISSILE_PROX_DISCARD &&
               trajectory_valid(&m->trajectory, error) && qa_vec_finite(m->normal) &&
               qa_vec_finite(m->damage_point) && isfinite(m->damage) && m->damage >= 0 &&
               isfinite(m->splash) && m->splash >= 0 && isfinite(m->radius) && m->radius >= 0 &&
               string_valid(game, m->team) && string_valid(game, m->loop_sound) &&
               reference_valid(game, m->owner, error) && reference_valid(game, m->pass, error) &&
               reference_valid(game, m->attached, error) &&
               reference_valid(game, m->trigger, error);
    }
    case Q3_ACTOR_ITEM: {
        const q3_item_state *item = &actor->state.item;
        size_t count;
        qa_q3_items(game->options.product, &count);
        return item->spawn.item_index > 0 && item->spawn.item_index < count &&
               isfinite(item->bounce) && item->bounce >= 0 && qa_vec_finite(item->spawn.origin) &&
               qa_vec_finite(item->spawn.velocity) && isfinite(item->spawn.wait_seconds) &&
               isfinite(item->spawn.random_seconds) &&
               fabsf(item->spawn.wait_seconds) + fabsf(item->spawn.random_seconds) <= 2147483 &&
               string_valid(game, item->spawn.target) && trajectory_valid(&item->trajectory, error);
    }
    case Q3_ACTOR_MOVER: {
        const qa_q3_mover_definition *m = &actor->state.mover;
        return m->state_index >= 0 && m->state_index <= 3 && isfinite(m->wait_ms) &&
               m->blocked >= QA_Q3_MOVER_BLOCKED_NONE && m->blocked <= QA_Q3_MOVER_BLOCKED_DOOR &&
               qa_vec_finite(m->first) &&
               qa_vec_finite(m->second) && m->state.kind >= QA_Q3_MOVER_IGNORE &&
               m->state.kind <= QA_Q3_MOVER_PROXIMITY_MINE &&
               trajectory_valid(&m->state.position, error) &&
               trajectory_valid(&m->state.angular, error) &&
               qa_vec_finite(m->state.proximity_direction) && string_valid(game, m->target) &&
               string_valid(game, m->loop_sound) &&
               reference_valid(game, m->team_leader, error) &&
               reference_valid(game, m->activator, error) &&
               reference_valid(game, m->state.team_next, error) &&
               reference_valid(game, m->state.proximity_pusher, error);
    }
    case Q3_ACTOR_PROX_TRIGGER:
        return reference_valid(game, actor->state.trigger.parent, error);
    case Q3_ACTOR_KAMIKAZE:
    case Q3_ACTOR_KAMIKAZE_TIMER:
        return actor->state.kamikaze.elapsed >= 0 && actor->state.kamikaze.elapsed <= 2000 &&
               qa_vec_finite(actor->state.kamikaze.angles) &&
               reference_valid(game, actor->state.kamikaze.attacker, error);
    case Q3_ACTOR_PORTAL:
        return qa_vec_finite(actor->state.portal.angles) &&
               qa_vec_finite(actor->state.portal.fallback) &&
               reference_valid(game, actor->state.portal.destination, error) &&
               reference_valid(game, actor->state.portal.owner, error);
    case Q3_ACTOR_CORPSE:
        return trajectory_valid(&actor->state.corpse.trajectory, error) &&
               reference_valid(game, actor->state.corpse.player, error);
    case Q3_ACTOR_OBELISK:
        return actor->state.obelisk.think >= QA_Q3_OBELISK_NONE &&
               actor->state.obelisk.think <= QA_Q3_OBELISK_RESPAWN &&
               reference_valid(game, actor->state.obelisk.model, error);
    case Q3_ACTOR_TEMPORARY: {
        const qa_q3_entity *entity = &actor->state.temporary.entity;
        if (entity->number < (int32_t)QA_Q3_SOURCE_CLIENTS ||
            entity->number >= (int32_t)QA_Q3_SOURCE_WORLD ||
            entity->eType < 13 || entity->eType > 1036 ||
            entity->clientNum < 0 || entity->clientNum >= (int32_t)QA_Q3_NATIVE_CLIENTS)
            return false;
        for (size_t i = 0; i < 3; ++i)
            if (!isfinite(entity->pos.base[i]) || !isfinite(entity->pos.delta[i]) ||
                !isfinite(entity->apos.base[i]) || !isfinite(entity->apos.delta[i]) ||
                !isfinite(entity->origin[i]) || !isfinite(entity->origin2[i]) ||
                !isfinite(entity->angles[i]) || !isfinite(entity->angles2[i])) return false;
        return true;
    }
    case Q3_ACTOR_PODIUM: case Q3_ACTOR_VICTORY_MODEL: {
        const qa_q3_entity *s = &actor->state.postgame.entity;
        if (s->number < (int32_t)QA_Q3_SOURCE_CLIENTS || s->number >= (int32_t)QA_Q3_SOURCE_WORLD ||
            s->eType != (actor->kind == Q3_ACTOR_PODIUM ? 0 : 1) ||
            s->pos.type < 0 || s->pos.type > QA_TRAJECTORY_GRAVITY ||
            s->apos.type < 0 || s->apos.type > QA_TRAJECTORY_GRAVITY ||
            s->clientNum < 0 || s->clientNum >= (int32_t)QA_Q3_SOURCE_CLIENTS ||
            !isfinite(actor->state.postgame.physics_bounce)) return false;
        for (size_t i = 0; i < 3; ++i)
            if (!isfinite(s->pos.base[i]) || !isfinite(s->pos.delta[i]) ||
                !isfinite(s->apos.base[i]) || !isfinite(s->apos.delta[i]) ||
                !isfinite(s->origin[i]) || !isfinite(s->origin2[i]) ||
                !isfinite(s->angles[i]) || !isfinite(s->angles2[i])) return false;
        return true;
    }
    case Q3_ACTOR_NONE:
        break;
    }
    return false;
}
void qa_q3_checkpoint_free(qa_q3_checkpoint *checkpoint) {
    if (!checkpoint)
        return;
    free(checkpoint->actors);
    free(checkpoint->kamikaze_cooldowns);
    if (checkpoint->configstrings)
        for (size_t i = 0; i < checkpoint->configstring_count; ++i)
            free(checkpoint->configstrings[i].text);
    free(checkpoint->configstrings);
    qa_buffer_free(&checkpoint->wire_state);
    *checkpoint = (qa_q3_checkpoint){0};
}
bool qa_q3_checkpoint_capture(const qa_q3_game *game, qa_q3_checkpoint *out, qa_error *error) {
    if (!game || !out || game->observation_depth || !q3_source_origins_idle(game) ||
        !qa_session_safe(game->options.services.session) ||
        !q3_client_counts_valid(&game->client_counts, game->options.max_clients) ||
        !q3_level_state_valid(game, &game->team_state, &game->match_state, error) ||
        game->memory.allocated_bytes > QA_Q3_SOURCE_MEMORY_BYTES ||
        game->memory.allocated_bytes % 32 ||
        !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 checkpoint requires a session safe point");
    qa_q3_checkpoint saved = {.memory = game->memory,
                              .max_clients = game->options.max_clients,
                              .source_count = game->source_count,
                              .new_session = game->new_session,
                              .fry_sound_index = game->fry_sound_index,
                              .portal_sequence = game->portal_sequence,
                              .last_team_location_time = game->last_team_location_time,
                              .client_counts = game->client_counts,
                              .team_state = game->team_state,
                              .match_state = game->match_state,
                              .random_state = game->rng,
                              .death_animation = game->death_animation,
                              .body_queue_index = game->body_queue_index,
                              .product = game->options.product,
                              .rules = game->options.rules,
                              .previous_ms = game->previous_ms,
                              .now_ms = game->now_ms,
                              .attack_sequence = game->attack_sequence,
                              .ranking_hit = {.frame = game->ranking_hit.frame,
                                              .self = game->ranking_hit.self,
                                              .attacker = game->ranking_hit.attacker,
                                              .method = game->ranking_hit.method,
                                              .valid = game->ranking_hit.valid}};
    memcpy(saved.body_queue, game->body_queue, sizeof(saved.body_queue));
    memcpy(saved.podium_players, game->podium_players, sizeof(saved.podium_players));
    memcpy(saved.source_entities, game->source_entities, sizeof(saved.source_entities));
    memcpy(saved.clients, game->clients, sizeof(saved.clients));
    memcpy(saved.source_clients, game->client_actors, sizeof(saved.source_clients));
    for (size_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i)
        if (!qa_vec_finite(saved.clients[i].old_origin) ||
            (saved.clients[i].has_followed_player &&
             !q3_followed_player_saved_valid(game, &saved.clients[i].followed_player)))
            return q3_fail(error, "Q3 client checkpoint has invalid retained source PS");
    uint16_t *source_numbers = NULL;
    if (!q3_source_prepare((qa_q3_game *)game, &saved, &source_numbers, error)) return false;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q3_actor *actor = q3_actor_const(game, game->actors[i].actor);
        if (!actor) continue;
        uint32_t source_slot = source_numbers[actor->actor.slot];
        if (source_slot == UINT16_MAX && actor->kind == Q3_ACTOR_PLAYER &&
            actor->state.player.selections && actor_valid(game, actor, error)) continue;
        if (source_slot < QA_Q3_SOURCE_CLIENTS || source_slot >= QA_Q3_SOURCE_WORLD ||
            !actor_valid(game, actor, error)) {
            free(source_numbers);
            return q3_fail(error, "Q3 capture has invalid actual source actor state");
        }
        const qa_q3_source_binding *binding = &saved.source_entities[source_slot];
        bool model = actor->kind == Q3_ACTOR_VICTORY_MODEL;
        if ((model ? binding->client_slot < 0 ||
             saved.source_entities[binding->client_slot].client_slot != binding->client_slot
             : binding->client_slot != -1) ||
            ((model || actor->kind == Q3_ACTOR_PODIUM) &&
             actor->state.postgame.entity.number != (int32_t)source_slot)) {
            free(source_numbers);
            return q3_fail(error, "Q3 capture has invalid borrowed source client identity");
        }
    }
    free(source_numbers);
    for (size_t i = 0; i < 3; ++i) {
        uint32_t podium = saved.podium_players[i];
        if (podium != QA_Q3_SOURCE_NONE &&
            (podium < QA_Q3_SOURCE_CLIENTS || podium >= saved.source_count))
            return q3_fail(error, "Q3 capture has an invalid retained podium source slot");
    }
    for (uint32_t i = 0; i < game->capacity; ++i) {
        if (q3_actor_const(game, game->actors[i].actor))
            ++saved.actor_count;
        if (game->kamikaze_cooldowns[i].actor.registry &&
            qa_actors_get(qa_session_actors(game->options.services.session),
                          game->kamikaze_cooldowns[i].actor))
            ++saved.cooldown_count;
    }
    if (saved.actor_count)
        saved.actors = malloc(saved.actor_count * sizeof(*saved.actors));
    if (saved.cooldown_count)
        saved.kamikaze_cooldowns = malloc(saved.cooldown_count * sizeof(*saved.kamikaze_cooldowns));
    if ((saved.actor_count && !saved.actors) ||
        (saved.cooldown_count && !saved.kamikaze_cooldowns)) {
        qa_q3_checkpoint_free(&saved);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 checkpoint");
        return false;
    }
    size_t actors = 0, cooldowns = 0;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        if (q3_actor_const(game, game->actors[i].actor))
            saved.actors[actors++] = game->actors[i];
        if (game->kamikaze_cooldowns[i].actor.registry &&
            qa_actors_get(qa_session_actors(game->options.services.session),
                          game->kamikaze_cooldowns[i].actor))
            saved.kamikaze_cooldowns[cooldowns++] = game->kamikaze_cooldowns[i];
    }
    if (!q3_configstrings_capture(game, &saved, error) ||
        !q3_shader_remaps_capture(game, &saved.shader_remaps, error) ||
        !q3_wire_capture(game, &saved.wire_state, error)) {
        qa_q3_checkpoint_free(&saved);
        return false;
    }
    *out = saved;
    return true;
}
static bool checkpoint_restore(qa_q3_game *game, const qa_q3_checkpoint *saved,
                                bool reconnect, qa_error *error) {
    if (!game || !saved || game->observation_depth || !q3_source_origins_idle(game) ||
        !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) ||
        !qa_combat_idle(game->options.services.combat) || saved->memory.allocated_bytes > QA_Q3_SOURCE_MEMORY_BYTES ||
        saved->memory.allocated_bytes % 32 ||
        saved->fry_sound_index < 0 || saved->fry_sound_index > 255 ||
        !saved->max_clients || saved->max_clients > 64 ||
        !q3_client_counts_valid(&saved->client_counts, saved->max_clients) ||
        !q3_level_state_valid(game, &saved->team_state, &saved->match_state, error) ||
        (saved->ranking_hit.valid &&
         (saved->ranking_hit.self < 0 || saved->ranking_hit.attacker < 0)) ||
        saved->product != game->options.product || saved->death_animation >= 3 ||
        saved->body_queue_index >= 8 || saved->actor_count > game->capacity ||
        saved->cooldown_count > game->capacity || (saved->actor_count && !saved->actors) ||
        (saved->cooldown_count && !saved->kamikaze_cooldowns))
        return q3_fail(error, "invalid Q3 checkpoint restore");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->player_binding_tokens[i])
            return q3_fail(error, "Q3 checkpoint restore conflicts with player admission");
    for (size_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i)
        if (!qa_vec_finite(saved->clients[i].old_origin) ||
            (saved->clients[i].has_followed_player &&
             !q3_followed_player_saved_valid(game, &saved->clients[i].followed_player)))
            return q3_fail(error, "Q3 client restore has invalid retained source PS");
    char **configstrings = NULL;
    uint16_t *source_numbers = NULL;
    q3_wire_state *wire = NULL;
    qa_q3_shader_remap_state shader_remaps;
    if (!q3_source_prepare(game, saved, &source_numbers, error)) return false;
    if (!q3_configstrings_prepare(saved, &configstrings, error))
        goto invalid_strings;
    if (!q3_shader_remaps_prepare(&saved->shader_remaps, &shader_remaps, error) ||
        !q3_wire_prepare(game, (qa_bytes){saved->wire_state.data, saved->wire_state.size}, &wire, error) ||
        !q3_wire_validate_saved(game, wire, saved, error)) {
        q3_configstrings_discard(configstrings);
        q3_wire_discard(wire);
        goto invalid_strings;
    }
    q3_actor *actors = calloc(game->capacity, sizeof(*actors));
    qa_pickup_lease *observations=NULL;
    q3_kamikaze_cooldown *cooldowns = calloc(game->capacity, sizeof(*cooldowns));
    if (!actors || !cooldowns) {
        free(actors);
        free(cooldowns);
        q3_configstrings_discard(configstrings);
        free(source_numbers);
        q3_wire_discard(wire);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 restore candidate");
        return false;
    }
    for (size_t i = 0; i < saved->actor_count; ++i) {
        const q3_actor *actor = &saved->actors[i];
        if (actor->actor.slot >= game->capacity || actors[actor->actor.slot].kind ||
            !actor_valid(game, actor, error))
            goto invalid;
        actors[actor->actor.slot] = *actor;
        if (source_numbers[actor->actor.slot] == UINT16_MAX &&
            actor->kind == Q3_ACTOR_PLAYER && actor->state.player.selections) continue;
        if (source_numbers[actor->actor.slot] < QA_Q3_SOURCE_CLIENTS ||
            source_numbers[actor->actor.slot] >= QA_Q3_SOURCE_WORLD) goto invalid;
        if (actor->kind == Q3_ACTOR_TEMPORARY &&
            actor->state.temporary.entity.number != source_numbers[actor->actor.slot]) goto invalid;
        const qa_q3_source_binding *binding = &saved->source_entities[source_numbers[actor->actor.slot]];
        if (actor->kind == Q3_ACTOR_PODIUM || actor->kind == Q3_ACTOR_VICTORY_MODEL) {
            if (actor->state.postgame.entity.number != source_numbers[actor->actor.slot] ||
                (actor->kind == Q3_ACTOR_PODIUM ? binding->client_slot != -1 :
                 binding->client_slot < 0)) goto invalid;
            if (actor->kind == Q3_ACTOR_VICTORY_MODEL &&
                saved->source_entities[binding->client_slot].client_slot != binding->client_slot)
                goto invalid;
        } else if (binding->client_slot != -1) goto invalid;
    }
    for (uint32_t i = 0; i < QA_Q3_NATIVE_CLIENTS; ++i)
        if (!enemy_reference_valid(game, &saved->source_clients[i], error) ||
            !player_references_valid(game, &saved->source_clients[i].state.player, error))
            goto invalid;
    for (size_t i = 0; i < saved->cooldown_count; ++i) {
        const q3_kamikaze_cooldown *cooldown = &saved->kamikaze_cooldowns[i];
        if (cooldown->actor.slot >= game->capacity ||
            !qa_actors_get(qa_session_actors(game->options.services.session), cooldown->actor) ||
            cooldowns[cooldown->actor.slot].actor.registry)
            goto invalid;
        cooldowns[cooldown->actor.slot] = *cooldown;
    }
    for (size_t i = 0; i < 3; ++i) {
        uint32_t podium = saved->podium_players[i];
        if (podium != QA_Q3_SOURCE_NONE &&
            (podium < QA_Q3_SOURCE_CLIENTS || podium >= saved->source_count)) goto invalid;
    }
    for (size_t i = 0; i < 8; ++i) {
        qa_actor_id body = saved->body_queue[i];
        if (!reference_valid(game, body, error))
            goto invalid;
        if (qa_actors_get(qa_session_actors(game->options.services.session), body) &&
            (body.slot >= game->capacity || actors[body.slot].kind != Q3_ACTOR_CORPSE ||
             !qa_actor_id_equal(actors[body.slot].actor, body) ||
             source_numbers[body.slot] < QA_Q3_SOURCE_CLIENTS ||
             source_numbers[body.slot] >= saved->source_count ||
             !saved->source_entities[source_numbers[body.slot]].never_free))
            goto invalid;
        if (saved->source_entities[QA_Q3_SOURCE_WORLD].actor.registry &&
            !qa_actors_get(qa_session_actors(game->options.services.session), body)) goto invalid;
        for (size_t j = 0; body.registry && j < i; ++j)
            if (qa_actor_id_equal(body, saved->body_queue[j])) goto invalid;
    }
    if(reconnect && !q3_item_observations_prepare(game,actors,&observations,error)) goto invalid;
    if (!qa_q3_set_source_rules(game, &saved->rules, error))
        goto invalid;
    if (reconnect) {
        q3_item_observations_commit(game,observations);observations=NULL;
    }
    free(game->actors);
    free(game->kamikaze_cooldowns);
    game->actors = actors;
    game->kamikaze_cooldowns = cooldowns;
    game->rng = saved->random_state;
    game->memory = saved->memory;
    game->options.max_clients = saved->max_clients;
    q3_source_commit(game, saved, source_numbers);
    game->new_session = saved->new_session;
    game->fry_sound_index = saved->fry_sound_index;
    game->portal_sequence = saved->portal_sequence;
    game->last_team_location_time = saved->last_team_location_time;
    game->client_counts = saved->client_counts;
    game->team_state = saved->team_state;
    game->match_state = saved->match_state;
    q3_shader_remaps_commit(game, &shader_remaps);
    q3_wire_commit(game, wire);
    game->death_animation = saved->death_animation;
    game->body_queue_index = saved->body_queue_index;
    memcpy(game->body_queue, saved->body_queue, sizeof(game->body_queue));
    memcpy(game->podium_players, saved->podium_players, sizeof(game->podium_players));
    game->previous_ms = saved->previous_ms;
    game->now_ms = saved->now_ms;
    game->attack_sequence = saved->attack_sequence;
    game->ranking_hit = (qa_q3_ranking_hit){.frame = saved->ranking_hit.frame,
                                           .self = saved->ranking_hit.self,
                                           .attacker = saved->ranking_hit.attacker,
                                           .method = saved->ranking_hit.method,
                                           .valid = saved->ranking_hit.valid};
    q3_configstrings_commit(game, configstrings);
    return true;
invalid:
    q3_item_observations_abort(game,observations);
    free(actors);
    free(cooldowns);
    q3_configstrings_discard(configstrings);
    q3_wire_discard(wire);
invalid_strings:
    free(source_numbers);
    return q3_fail(error, "invalid Q3 checkpoint state or references");
}
bool qa_q3_checkpoint_restore(qa_q3_game *game, const qa_q3_checkpoint *saved, qa_error *error) {
    return checkpoint_restore(game, saved, true, error);
}
bool q3_checkpoint_restore_source(qa_q3_game *game, const qa_q3_checkpoint *saved,
                                  qa_error *error) {
    return checkpoint_restore(game, saved, false, error);
}
bool qa_q3_projectile_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_projectile_state *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE || !out)
        return false;
    *out = entry->state.missile;
    return true;
}
static bool projectile_steer(qa_q3_game *game, qa_actor_id actor, qa_vec3 velocity,
                             qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_FLIGHT || !qa_vec_finite(velocity))
        return q3_fail(error, "invalid Q3 projectile continuation");
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_FLIGHT)
        return true;
    entry->state.missile.trajectory.base = body.origin;
    entry->state.missile.trajectory.time_ms = game->now_ms;
    entry->state.missile.trajectory.delta = velocity;
    body.velocity = velocity;
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
bool qa_q3_projectile_steer(qa_q3_game *game, qa_actor_id actor, qa_vec3 velocity,
                            qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 projectile steering boundary");
    ++game->observation_depth;
    bool okay = projectile_steer(game, actor, velocity, error);
    --game->observation_depth;
    return okay;
}
