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
static bool actor_valid(const qa_q3_game *game, const q3_actor *actor, qa_error *error) {
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor->actor))
        return q3_fail(error, "stale Q3 checkpoint actor");
    switch (actor->kind) {
    case Q3_ACTOR_PLAYER: {
        const qa_q3_player_state *p = &actor->state.player;
        if (!q3_player_state_valid(p) || !string_valid(game, p->loop_sound))
            return false;
        for (unsigned i = 0; i < QA_Q3_WEAPON_COUNT; ++i)
            if (!string_valid(game, p->ammo_regeneration_items[i]))
                return false;
        return reference_valid(game, p->hook, error) &&
               reference_valid(game, p->attached_mine, error) &&
               reference_valid(game, p->persistent_item, error) &&
               reference_valid(game, p->portal, error);
    }
    case Q3_ACTOR_MISSILE: {
        const q3_missile *m = &actor->state.missile;
        return m->weapon > QA_Q3_W_NONE && m->weapon < QA_Q3_WEAPON_COUNT &&
               m->phase >= Q3_MISSILE_FLIGHT && m->phase <= Q3_MISSILE_PROX_PLAYER &&
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
        return m->state_index >= 0 && m->state_index <= 3 && qa_vec_finite(m->first) &&
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
    *checkpoint = (qa_q3_checkpoint){0};
}
bool qa_q3_checkpoint_capture(const qa_q3_game *game, qa_q3_checkpoint *out, qa_error *error) {
    if (!game || !out || game->observation_depth || !qa_session_safe(game->options.services.session) ||
        !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 checkpoint requires a session safe point");
    qa_q3_checkpoint saved = {.version = 3,
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
    *out = saved;
    return true;
}
bool qa_q3_checkpoint_restore(qa_q3_game *game, const qa_q3_checkpoint *saved, qa_error *error) {
    if (!game || !saved || game->observation_depth || !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) ||
        !qa_combat_idle(game->options.services.combat) || saved->version != 3 ||
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
    q3_actor *actors = calloc(game->capacity, sizeof(*actors));
    qa_pickup_lease *observations=NULL;
    q3_kamikaze_cooldown *cooldowns = calloc(game->capacity, sizeof(*cooldowns));
    if (!actors || !cooldowns) {
        free(actors);
        free(cooldowns);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 restore candidate");
        return false;
    }
    for (size_t i = 0; i < saved->actor_count; ++i) {
        const q3_actor *actor = &saved->actors[i];
        if (actor->actor.slot >= game->capacity || actors[actor->actor.slot].kind ||
            !actor_valid(game, actor, error))
            goto invalid;
        actors[actor->actor.slot] = *actor;
    }
    for (size_t i = 0; i < saved->cooldown_count; ++i) {
        const q3_kamikaze_cooldown *cooldown = &saved->kamikaze_cooldowns[i];
        if (cooldown->actor.slot >= game->capacity ||
            !qa_actors_get(qa_session_actors(game->options.services.session), cooldown->actor) ||
            cooldowns[cooldown->actor.slot].actor.registry)
            goto invalid;
        cooldowns[cooldown->actor.slot] = *cooldown;
    }
    for (size_t i = 0; i < 8; ++i) {
        qa_actor_id body = saved->body_queue[i];
        if (!reference_valid(game, body, error))
            goto invalid;
        if (qa_actors_get(qa_session_actors(game->options.services.session), body) &&
            (body.slot >= game->capacity || actors[body.slot].kind != Q3_ACTOR_CORPSE ||
             !qa_actor_id_equal(actors[body.slot].actor, body)))
            goto invalid;
    }
    if(!q3_item_observations_prepare(game,actors,&observations,error)) goto invalid;
    if (!qa_q3_set_rules(game, &saved->rules, error))
        goto invalid;
    q3_item_observations_commit(game,observations);observations=NULL;
    free(game->actors);
    free(game->kamikaze_cooldowns);
    game->actors = actors;
    game->kamikaze_cooldowns = cooldowns;
    game->rng = saved->random_state;
    game->death_animation = saved->death_animation;
    game->body_queue_index = saved->body_queue_index;
    memcpy(game->body_queue, saved->body_queue, sizeof(game->body_queue));
    game->previous_ms = saved->previous_ms;
    game->now_ms = saved->now_ms;
    game->attack_sequence = saved->attack_sequence;
    game->ranking_hit = (qa_q3_ranking_hit){.frame = saved->ranking_hit.frame,
                                           .self = saved->ranking_hit.self,
                                           .attacker = saved->ranking_hit.attacker,
                                           .method = saved->ranking_hit.method,
                                           .valid = saved->ranking_hit.valid};
    return true;
invalid:
    q3_item_observations_abort(game,observations);
    free(actors);
    free(cooldowns);
    return q3_fail(error, "invalid Q3 checkpoint state or references");
}
bool qa_q3_projectile_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_projectile_state *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE || !out)
        return false;
    *out = entry->state.missile;
    return true;
}
bool qa_q3_projectile_steer(qa_q3_game *game, qa_actor_id actor, qa_vec3 velocity,
                            qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_FLIGHT || !qa_vec_finite(velocity))
        return q3_fail(error, "invalid Q3 projectile continuation");
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry->state.missile.trajectory.base = body.origin;
    entry->state.missile.trajectory.time_ms = game->now_ms;
    entry->state.missile.trajectory.delta = velocity;
    body.velocity = velocity;
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
