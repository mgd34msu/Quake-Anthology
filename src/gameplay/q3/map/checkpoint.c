#include "internal.h"

static bool string_valid(const qa_q3_game *game, qa_string_id value) {
    return !value ||
           qa_strings_cstr(qa_session_strings(game->options.services.session), value) != NULL;
}

static bool reference_valid(const qa_q3_game *game, qa_actor_id value,
                            qa_error *error) {
    if (!value.registry)
        return !value.generation && !value.slot;
    qa_saved_actor_id saved;
    return qa_actors_save_reference(qa_session_actors(game->options.services.session),
                                    value, &saved, error);
}

static bool item_registry_valid(const qa_q3_game *game, uint64_t registered) {
    size_t count;
    (void)qa_q3_items(game->options.product, &count);
    return count <= 64 && (count == 64 || (registered >> count) == 0);
}

static bool state_valid(const qa_q3_game *game, const qa_q3_map_actor_state *state,
                        qa_error *error) {
    if (!state || !state->active || state->kind < QA_Q3_MAP_POINT ||
        state->kind > QA_Q3_MAP_MOVER_PLAT_TRIGGER ||
        state->think < QA_Q3_MAP_THINK_NONE ||
        state->think > QA_Q3_MAP_THINK_MOVER_TRAIN_RESUME ||
        !qa_actors_get(qa_session_actors(game->options.services.session), state->actor) ||
        !qa_vec_finite(state->origin) || !qa_vec_finite(state->angles) ||
        !qa_vec_finite(state->direction) || !qa_vec_finite(state->launch_velocity) ||
        !qa_vec_finite(state->first) || !qa_vec_finite(state->second) ||
        !qa_vec_finite(state->color) ||
        !qa_vec_finite(state->bounds.mins) || !qa_vec_finite(state->bounds.maxs) ||
        !isfinite(state->speed) || !isfinite(state->wait) || !isfinite(state->random) ||
        !isfinite(state->delay) || !isfinite(state->roll) || !isfinite(state->light) ||
        !isfinite(state->alpha))
        return q3_map_fail(error, "invalid Q3 authored actor checkpoint state");
    if (state->damageable && state->kind != QA_Q3_MAP_MOVER_DOOR &&
        state->kind != QA_Q3_MAP_MOVER_BUTTON)
        return q3_map_fail(error, "invalid Q3 authored damage admission state");
    const qa_string_id strings[] = {state->classname, state->model,      state->model2,
                                    state->targetname,
                                    state->target,    state->message,    state->team,
                                    state->noise,     state->shader_old, state->shader_new,
                                    state->item.target};
    for (size_t i = 0; i < sizeof(strings) / sizeof(*strings); ++i)
        if (!string_valid(game, strings[i]))
            return q3_map_fail(error, "invalid Q3 authored checkpoint string");
    if (state->team_slave && !state->team_master.registry)
        return q3_map_fail(error, "Q3 authored team slave has no master");
    if (state->kind == QA_Q3_MAP_ITEM) {
        size_t item_count;
        (void)qa_q3_items(game->options.product, &item_count);
        if (!state->item.item_index || state->item.item_index >= item_count ||
            !isfinite(state->item.wait_seconds) ||
            !isfinite(state->item.random_seconds) ||
            !qa_vec_finite(state->item.origin) || !qa_vec_finite(state->item.velocity) ||
            state->item.target != state->target)
            return q3_map_fail(error, "invalid Q3 authored item checkpoint state");
    }
    return reference_valid(game, state->actor, error) &&
           reference_valid(game, state->activator, error) &&
           reference_valid(game, state->enemy, error) &&
           reference_valid(game, state->team_master, error) &&
           reference_valid(game, state->team_next, error) &&
           reference_valid(game, state->parent, error) &&
           reference_valid(game, state->path_next, error);
}

static bool save_reference(const qa_q3_game *game, qa_actor_id actor,
                           qa_saved_actor_id *out, qa_error *error) {
    *out = (qa_saved_actor_id){0};
    return !actor.registry ||
           qa_actors_save_reference(qa_session_actors(game->options.services.session),
                                    actor, out, error);
}

static bool capture_state(const qa_q3_game *game, const qa_q3_map_actor_state *state,
                          qa_q3_map_actor_checkpoint *out, qa_error *error) {
    if (!state_valid(game, state, error))
        return false;
    *out = (qa_q3_map_actor_checkpoint){.state = *state};
    if (!save_reference(game, state->actor, &out->actor, error) ||
        !save_reference(game, state->activator, &out->activator, error) ||
        !save_reference(game, state->enemy, &out->enemy, error) ||
        !save_reference(game, state->team_master, &out->team_master, error) ||
        !save_reference(game, state->team_next, &out->team_next, error) ||
        !save_reference(game, state->parent, &out->parent, error) ||
        !save_reference(game, state->path_next, &out->path_next, error))
        return false;
    out->state.actor = (qa_actor_id){0};
    out->state.activator = (qa_actor_id){0};
    out->state.enemy = (qa_actor_id){0};
    out->state.team_master = (qa_actor_id){0};
    out->state.team_next = (qa_actor_id){0};
    out->state.parent = (qa_actor_id){0};
    out->state.path_next = (qa_actor_id){0};
    return true;
}

static bool restore_reference(const qa_q3_game *game, qa_saved_actor_id saved,
                              qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    if (!saved.generation && !saved.slot)
        return true;
    const qa_actor_record *record = qa_actors_resolve_saved(
        qa_session_actors(game->options.services.session), saved);
    if (!record)
        return qa_actors_reference_saved(
            qa_session_actors(game->options.services.session), saved, true, out, error);
    *out = record->id;
    return true;
}

static bool native_state_valid(qa_q3_game *game, const qa_q3_map_actor_state *state,
                               qa_error *error) {
    q3_actor *native = q3_actor_get(game, state->actor);
    if (state->kind == QA_Q3_MAP_ITEM) {
        bool valid = state->item_bound ? native && native->kind == Q3_ACTOR_ITEM
                                       : native == NULL;
        return valid ? true
                     : q3_map_fail(error, "Q3 authored item native state is inconsistent");
    }
    if (state->kind >= QA_Q3_MAP_MOVER_DOOR &&
        state->kind <= QA_Q3_MAP_MOVER_PENDULUM)
        return native && native->kind == Q3_ACTOR_MOVER
                   ? true
                   : q3_map_fail(error, "Q3 authored mover lost its native state");
    return true;
}

static bool sync_native_state(qa_q3_game *game, qa_q3_map_actor_state *state,
                              qa_error *error) {
    if (state->kind >= QA_Q3_MAP_MOVER_DOOR &&
        state->kind <= QA_Q3_MAP_MOVER_PENDULUM &&
        !q3_map_mover_sync_state(game, state, error))
        return false;
    return !state->damageable || q3_map_mover_sync_admission(game, state, error);
}

static bool clear_admission(qa_q3_game *game,
                            const qa_q3_map_actor_state *state,
                            qa_error *error) {
    return !state->damageable ||
           qa_combat_set_admission(game->options.services.combat, state->actor,
                                   NULL, error);
}

static bool restore_state(const qa_q3_game *game,
                          const qa_q3_map_actor_checkpoint *saved,
                          qa_q3_map_actor_state *out, qa_error *error) {
    *out = saved->state;
    if (!restore_reference(game, saved->actor, &out->actor, error) ||
        !restore_reference(game, saved->activator, &out->activator, error) ||
        !restore_reference(game, saved->enemy, &out->enemy, error) ||
        !restore_reference(game, saved->team_master, &out->team_master, error) ||
        !restore_reference(game, saved->team_next, &out->team_next, error) ||
        !restore_reference(game, saved->parent, &out->parent, error) ||
        !restore_reference(game, saved->path_next, &out->path_next, error))
        return false;
    return state_valid(game, out, error);
}

bool qa_q3_map_actor_capture(const qa_q3_game *game, qa_actor_id actor,
                             qa_q3_map_actor_state *out) {
    const qa_q3_map_actor_state *state = q3_map_const(game, actor);
    if (!state || !out)
        return false;
    *out = *state;
    return true;
}

bool qa_q3_map_actor_restore(qa_q3_game *game, const qa_q3_map_actor_state *state,
                             qa_error *error) {
    if (!game || !game->map || !state || state->actor.slot >= game->map->capacity ||
        !state_valid(game, state, error) || !native_state_valid(game, state, error))
        return q3_map_fail(error, "invalid Q3 authored actor restore");
    qa_q3_map_actor_state prior = game->map->actors[state->actor.slot];
    if (prior.active && !qa_actor_id_equal(prior.actor, state->actor))
        return q3_map_fail(error, "Q3 authored actor restore slot is occupied");
    if (prior.active && !clear_admission(game, &prior, error))
        return false;
    if (prior.active)
        qa_targets_unbind_context(game->map->options.targets, prior.actor, game);
    game->map->actors[state->actor.slot] = *state;
    if (q3_map_bind_target(game, &game->map->actors[state->actor.slot], error) &&
        sync_native_state(game, &game->map->actors[state->actor.slot], error))
        return true;
    qa_error ignored = {0};
    qa_targets_unbind_context(game->map->options.targets, state->actor, game);
    game->map->actors[state->actor.slot] = prior;
    if (prior.active) {
        (void)q3_map_bind_target(game, &game->map->actors[state->actor.slot], &ignored);
        (void)sync_native_state(game, &game->map->actors[state->actor.slot], &ignored);
    }
    return false;
}

void qa_q3_map_checkpoint_free(qa_q3_map_checkpoint *checkpoint) {
    if (!checkpoint)
        return;
    free(checkpoint->actors);
    *checkpoint = (qa_q3_map_checkpoint){0};
}

bool qa_q3_map_checkpoint_capture(const qa_q3_game *game,
                                  qa_q3_map_checkpoint *out, qa_error *error) {
    if (!game || !game->map || !out ||
        !qa_session_safe(game->options.services.session) ||
        !qa_combat_idle(game->options.services.combat))
        return q3_map_fail(error, "Q3 authored checkpoint requires a session safe point");
    qa_q3_map_checkpoint saved = {.version = 3,
                                  .registered_items = game->map->registered_items,
                                  .motd = game->map->options.motd,
                                  .random_seed = game->map->options.random_seed,
                                  .start_time_ms = game->map->options.start_time_ms,
                                  .restarted = game->map->options.restarted,
                                  .warmup = game->map->options.warmup,
                                  .gravity = game->physics.gravity,
                                  .world_spawned = game->map->world_spawned,
                                  .post_spawned = game->map->post_spawned,
                                  .locations_linked = game->map->locations_linked};
    for (uint32_t i = 0; i < game->map->capacity; ++i)
        if (game->map->actors[i].active)
            ++saved.actor_count;
    if (saved.actor_count) {
        saved.actors = calloc(saved.actor_count, sizeof(*saved.actors));
        if (!saved.actors) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 authored checkpoint");
            return false;
        }
    }
    size_t at = 0;
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        const qa_q3_map_actor_state *state = &game->map->actors[i];
        if (state->active && !capture_state(game, state, &saved.actors[at++], error)) {
            qa_q3_map_checkpoint_free(&saved);
            return false;
        }
    }
    *out = saved;
    return true;
}

static bool checkpoint_restore(qa_q3_game *game,
                                const qa_q3_map_checkpoint *saved,
                                bool reconnect, qa_error *error) {
    if (!game || !game->map || !saved || saved->version != 3 ||
        (saved->motd && !qa_strings_cstr(qa_session_strings(game->options.services.session), saved->motd)) ||
        !item_registry_valid(game, saved->registered_items) ||
        !isfinite(saved->gravity) ||
        saved->actor_count > game->map->capacity ||
        (saved->actor_count && !saved->actors) ||
        !qa_session_safe(game->options.services.session) ||
        !qa_combat_idle(game->options.services.combat))
        return q3_map_fail(error, "invalid Q3 authored checkpoint restore");
    qa_q3_map_actor_state *candidate = calloc(game->map->capacity, sizeof(*candidate));
    if (!candidate) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 authored restore candidate");
        return false;
    }
    for (size_t i = 0; i < saved->actor_count; ++i) {
        qa_q3_map_actor_state state;
        if (!restore_state(game, &saved->actors[i], &state, error) ||
            state.actor.slot >= game->map->capacity ||
            !native_state_valid(game, &state, error) ||
            candidate[state.actor.slot].active) {
            free(candidate);
            return q3_map_fail(error, "invalid or duplicate Q3 authored checkpoint actor");
        }
        candidate[state.actor.slot] = state;
    }
    if (!reconnect) {
        for (uint32_t i = 0; i < game->map->capacity; ++i) {
            qa_q3_map_actor_state *state = &candidate[i];
            if (state->active && state->kind >= QA_Q3_MAP_MOVER_DOOR &&
                state->kind <= QA_Q3_MAP_MOVER_PENDULUM &&
                !q3_map_mover_sync_state(game, state, error)) {
                free(candidate);
                return false;
            }
        }
        free(game->map->actors);
        game->map->actors = candidate;
        game->map->registered_items = saved->registered_items;
        game->map->options.motd = saved->motd;
        game->map->options.random_seed = saved->random_seed;
        game->map->options.start_time_ms = saved->start_time_ms;
        game->map->options.restarted = saved->restarted;
        game->map->options.warmup = saved->warmup;
        game->physics.gravity = saved->gravity;
        game->map->world_spawned = saved->world_spawned;
        game->map->post_spawned = saved->post_spawned;
        game->map->locations_linked = saved->locations_linked;
        return true;
    }
    qa_q3_map_actor_state *prior = game->map->actors;
    uint32_t cleared = 0;
    for (; cleared < game->map->capacity; ++cleared) {
        if (!prior[cleared].active || clear_admission(game, &prior[cleared], error))
            continue;
        for (uint32_t j = 0; j < cleared; ++j) {
            if (!prior[j].active || !prior[j].damageable)
                continue;
            qa_error ignored = {0};
            (void)q3_map_mover_sync_admission(game, &prior[j], &ignored);
        }
        free(candidate);
        return false;
    }
    for (uint32_t i = 0; i < game->map->capacity; ++i)
        if (prior[i].active)
            qa_targets_unbind_context(game->map->options.targets, prior[i].actor, game);
    game->map->actors = candidate;
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        if (!candidate[i].active)
            continue;
        if (!q3_map_bind_target(game, &candidate[i], error)) {
            for (uint32_t j = 0; j < game->map->capacity; ++j)
                if (candidate[j].active)
                    qa_targets_unbind_context(game->map->options.targets,
                                              candidate[j].actor, game);
            game->map->actors = prior;
            for (uint32_t j = 0; j < game->map->capacity; ++j) {
                if (!prior[j].active)
                    continue;
                qa_error ignored = {0};
                (void)q3_map_bind_target(game, &prior[j], &ignored);
                (void)sync_native_state(game, &prior[j], &ignored);
            }
            free(candidate);
            return false;
        }
    }
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        if (!candidate[i].active || sync_native_state(game, &candidate[i], error))
            continue;
        for (uint32_t j = 0; j < i; ++j) {
            qa_error ignored = {0};
            if (candidate[j].active)
                (void)clear_admission(game, &candidate[j], &ignored);
        }
        for (uint32_t j = 0; j < game->map->capacity; ++j)
            if (candidate[j].active)
                qa_targets_unbind_context(game->map->options.targets,
                                          candidate[j].actor, game);
        game->map->actors = prior;
        for (uint32_t j = 0; j < game->map->capacity; ++j) {
            if (!prior[j].active)
                continue;
            qa_error ignored = {0};
            (void)q3_map_bind_target(game, &prior[j], &ignored);
            (void)sync_native_state(game, &prior[j], &ignored);
        }
        free(candidate);
        return false;
    }
    free(prior);
    game->map->registered_items = saved->registered_items;
    game->map->options.motd = saved->motd;
    game->map->options.random_seed = saved->random_seed;
    game->map->options.start_time_ms = saved->start_time_ms;
    game->map->options.restarted = saved->restarted;
    game->map->options.warmup = saved->warmup;
    game->physics.gravity = saved->gravity;
    game->map->world_spawned = saved->world_spawned;
    game->map->post_spawned = saved->post_spawned;
    game->map->locations_linked = saved->locations_linked;
    return true;
}
bool qa_q3_map_checkpoint_restore(qa_q3_game *game,
                                  const qa_q3_map_checkpoint *saved, qa_error *error) {
    return checkpoint_restore(game, saved, true, error);
}
bool q3_map_checkpoint_restore_source(qa_q3_game *game,
                                      const qa_q3_map_checkpoint *saved, qa_error *error) {
    return checkpoint_restore(game, saved, false, error);
}
