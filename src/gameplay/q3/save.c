#include "map/internal.h"
#include "qa/game_q3_save.h"
#include "qa/source_save.h"
#include "qa/persistence_gameplay.h"

typedef struct q3_saved_leases {
    uint64_t weapons, holdables, observation;
    uint32_t selections;
    bool inventory;
} q3_saved_leases;

#define FIELD(kind, value) do { if (!qa_source_save_##kind(io, &(value))) return false; } while (0)
#define ENUM(value, maximum) do { \
    uint32_t encoded = (uint32_t)(value); \
    FIELD(u32, encoded); \
    if (encoded > (uint32_t)(maximum)) return save_fail(io, "invalid Q3 save enum"); \
    if (io->direction == QA_SOURCE_SAVE_READ) (value) = encoded; \
} while (0)

static bool save_fail(qa_source_save_io *io, const char *message)
{
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    return false;
}

static bool trajectory(qa_source_save_io *io, qa_trajectory *p)
{
    ENUM(p->type, QA_TRAJECTORY_GRAVITY);
    FIELD(i32, p->time_ms); FIELD(i32, p->duration_ms);
    FIELD(vec3, p->base); FIELD(vec3, p->delta);
    return true;
}

static bool item_spawn(qa_source_save_io *io, qa_q3_item_spawn *p)
{
    FIELD(u32, p->item_index); FIELD(i32, p->count); FIELD(i32, p->team_restriction);
    FIELD(f32, p->wait_seconds); FIELD(f32, p->random_seconds);
    FIELD(bool, p->dropped); FIELD(bool, p->suspended);
    FIELD(vec3, p->origin); FIELD(vec3, p->velocity); FIELD(string, p->target);
    return true;
}

static bool player(qa_source_save_io *io, qa_q3_player_state *p)
{
    FIELD(u32, p->selections); FIELD(u32, p->flags); FIELD(u32, p->event_sequence);
    FIELD(u32, p->spawn_count);
    ENUM(p->weapon, QA_Q3_WEAPON_COUNT - 1); ENUM(p->requested_weapon, QA_Q3_WEAPON_COUNT - 1);
    ENUM(p->weapon_phase, QA_Q3_FIRING); ENUM(p->external_slot, QA_Q3_SLOT_RESUME_REQUESTED);
    FIELD(i32, p->weapon_time_ms); FIELD(i32, p->max_health); FIELD(i32, p->handicap);
    for (size_t i = 0; i < QA_Q3_POWERUP_COUNT; ++i) FIELD(i32, p->powerups[i]);
    for (size_t i = 0; i < QA_Q3_WEAPON_COUNT; ++i) FIELD(i32, p->ammo_time_ms[i]);
    for (size_t i = 0; i < QA_Q3_WEAPON_COUNT; ++i) FIELD(string, p->ammo_regeneration_items[i]);
    ENUM(p->persistent, QA_Q3_POWERUP_COUNT - 1); ENUM(p->holdable, QA_Q3_H_INVULNERABILITY);
    FIELD(i32, p->invulnerability_until); FIELD(i32, p->respawn_after);
    FIELD(i32, p->time_residual); FIELD(i32, p->air_out_time);
    FIELD(i32, p->drowning_damage); FIELD(i32, p->pain_after); FIELD(i32, p->reward_until);
    FIELD(i32, p->accuracy_shots); FIELD(i32, p->accuracy_hits); FIELD(i32, p->rail_streak);
    FIELD(i32, p->impressive_count); FIELD(i32, p->denied_rewards); FIELD(i32, p->player_events);
    FIELD(i32, p->deaths); FIELD(i32, p->excellent_count); FIELD(i32, p->gauntlet_frag_count);
    FIELD(i32, p->last_kill_ms); FIELD(i32, p->dead_yaw);
    FIELD(i32, p->legs_animation); FIELD(i32, p->torso_animation);
    FIELD(i32, p->legs_timer_ms); FIELD(i32, p->torso_timer_ms);
    FIELD(i32, p->delta_yaw_word); FIELD(i32, p->ground_entity_number);
    FIELD(i32, p->delta_pitch_word); FIELD(i32, p->delta_roll_word);
    FIELD(i32, p->teleport_lock_ms); FIELD(u64, p->teleport_revision);
    FIELD(i32, p->damage_event); FIELD(i32, p->damage_count);
    FIELD(i32, p->damage_pitch); FIELD(i32, p->damage_yaw); FIELD(i32, p->last_command_ms);
    FIELD(i32, p->fly_sound_after); FIELD(i32, p->jumppad_entity);
    FIELD(i32, p->jumppad_frame); FIELD(i32, p->pmove_frame_count);
    for (size_t i = 0; i < 3; ++i) FIELD(i32, p->last_command_angles[i]);
    FIELD(f32, p->damage_blood); FIELD(f32, p->damage_armor); FIELD(f32, p->damage_knockback);
    FIELD(vec3, p->damage_from); FIELD(string, p->loop_sound);
    FIELD(f32, p->fractional_weapon_ms); FIELD(f32, p->view_height);
    FIELD(vec3, p->view_angles); FIELD(vec3, p->grapple_point);
    FIELD(vec3, p->cutscene.origin); FIELD(vec3, p->cutscene.angles);
    FIELD(vec3, p->cutscene.view_offset); FIELD(bool, p->cutscene.active);
    FIELD(actor, p->hook); FIELD(actor, p->attached_mine);
    FIELD(actor, p->persistent_item); FIELD(actor, p->portal);
    FIELD(bool, p->spectator); FIELD(bool, p->dead); FIELD(bool, p->gibbed);
    FIELD(bool, p->respawned); FIELD(bool, p->use_item_held); FIELD(bool, p->fire_held);
    FIELD(bool, p->grapple_pull); FIELD(bool, p->damage_from_world); FIELD(bool, p->noclip);
    FIELD(bool, p->invulnerability_expanded); FIELD(bool, p->death_cleanup_done);
    FIELD(bool, p->gauntlet_contact); FIELD(bool, p->no_target);
    return true;
}

static bool missile(qa_source_save_io *io, qa_q3_projectile_state *p)
{
    ENUM(p->weapon, QA_Q3_WEAPON_COUNT - 1);
    if (!trajectory(io, &p->trajectory)) return false;
    FIELD(actor, p->owner); FIELD(actor, p->pass); FIELD(actor, p->attached); FIELD(actor, p->trigger);
    FIELD(string, p->team); FIELD(vec3, p->normal); FIELD(vec3, p->damage_point);
    FIELD(f32, p->damage); FIELD(f32, p->splash); FIELD(f32, p->radius);
    FIELD(i32, p->method); FIELD(i32, p->splash_method); FIELD(i32, p->think_at); FIELD(i32, p->event_at);
    FIELD(u32, p->flags); FIELD(string, p->loop_sound);
    ENUM(p->phase, Q3_MISSILE_PROX_PLAYER); FIELD(bool, p->left_owner);
    return true;
}

static bool mover(qa_source_save_io *io, qa_q3_mover_definition *p)
{
    ENUM(p->state.kind, QA_Q3_MOVER_PROXIMITY_MINE);
    if (!trajectory(io, &p->state.position) || !trajectory(io, &p->state.angular)) return false;
    FIELD(i32, p->state.delta_yaw_word); FIELD(i32, p->state.ground_entity_number);
    FIELD(actor, p->state.team_next); FIELD(actor, p->state.proximity_pusher);
    FIELD(vec3, p->state.proximity_direction); FIELD(bool, p->state.stop); FIELD(bool, p->state.team_slave);
    FIELD(vec3, p->first); FIELD(vec3, p->second); FIELD(i32, p->state_index);
    FIELD(i32, p->wait_ms); FIELD(i32, p->damage); FIELD(i32, p->next_think_ms);
    FIELD(actor, p->team_leader); FIELD(actor, p->activator);
    FIELD(string, p->target); FIELD(string, p->loop_sound);
    FIELD(bool, p->crusher); FIELD(bool, p->map_controlled);
    return true;
}

static bool actor_state(qa_source_save_io *io, qa_q3_actor_state *p)
{
    FIELD(actor, p->actor); ENUM(p->kind, Q3_ACTOR_KAMIKAZE_TIMER);
    FIELD(f32, p->alpha);
    switch (p->kind) {
    case Q3_ACTOR_PLAYER: return player(io, &p->state.player);
    case Q3_ACTOR_MISSILE: return missile(io, &p->state.missile);
    case Q3_ACTOR_MOVER: return mover(io, &p->state.mover);
    case Q3_ACTOR_ITEM:
        if (!item_spawn(io, &p->state.item.spawn) || !trajectory(io, &p->state.item.trajectory)) return false;
        FIELD(i32, p->state.item.respawn_at); FIELD(i32, p->state.item.expire_at);
        FIELD(i32, p->state.item.ground_entity_number); FIELD(f32, p->state.item.bounce);
        FIELD(bool, p->state.item.hidden); FIELD(bool, p->state.item.on_ground);
        return true;
    case Q3_ACTOR_PROX_TRIGGER:
        FIELD(actor, p->state.trigger.parent); return true;
    case Q3_ACTOR_KAMIKAZE:
    case Q3_ACTOR_KAMIKAZE_TIMER:
        FIELD(actor, p->state.kamikaze.attacker); FIELD(i32, p->state.kamikaze.start);
        FIELD(i32, p->state.kamikaze.next); FIELD(i32, p->state.kamikaze.elapsed);
        FIELD(vec3, p->state.kamikaze.angles); return true;
    case Q3_ACTOR_PORTAL:
        FIELD(actor, p->state.portal.destination); FIELD(actor, p->state.portal.owner);
        FIELD(i32, p->state.portal.expire_at); FIELD(i32, p->state.portal.activate_at);
        FIELD(vec3, p->state.portal.angles); FIELD(vec3, p->state.portal.fallback);
        FIELD(bool, p->state.portal.source); return true;
    case Q3_ACTOR_CORPSE:
        FIELD(actor, p->state.corpse.player);
        if (!trajectory(io, &p->state.corpse.trajectory)) return false;
        FIELD(i32, p->state.corpse.animation); FIELD(i32, p->state.corpse.timestamp);
        FIELD(i32, p->state.corpse.next_sink); FIELD(u32, p->state.corpse.flags); return true;
    case Q3_ACTOR_NONE: break;
    }
    return save_fail(io, "empty Q3 actor in continuation");
}

static bool rules(qa_source_save_io *io, qa_q3_rules *p)
{
    FIELD(i32, p->game_type); FIELD(i32, p->proximity_timeout_ms); FIELD(i32, p->force_respawn_seconds);
    FIELD(f32, p->quad_factor); FIELD(f32, p->knockback);
    FIELD(f32, p->weapon_respawn_seconds); FIELD(f32, p->team_weapon_respawn_seconds);
    FIELD(bool, p->friendly_fire); FIELD(bool, p->blood); FIELD(bool, p->intermission);
    return true;
}

static void *allocate(qa_source_save_io *io, size_t count, size_t size)
{
    if (count > SIZE_MAX / size || io->offset > io->input.size ||
        count > io->input.size - io->offset) {
        save_fail(io, "Q3 continuation allocation exceeds input");
        return NULL;
    }
    void *out = calloc(count, size);
    if (!out)
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating Q3 continuation");
    return out;
}

static bool configstring_text(qa_source_save_io *io, char **text)
{
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE && *text ? strlen(*text) : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (!length) return save_fail(io, "empty Q3 stored configstring slot");
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, *text, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset)
        return save_fail(io, "Q3 configstring exceeds continuation input");
    char *copy = malloc(length + 1);
    if (!copy) {
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating Q3 stored configstring");
        return false;
    }
    if (!qa_source_save_bytes(io, copy, length) || memchr(copy, 0, length)) {
        free(copy);
        return save_fail(io, "invalid Q3 stored configstring text");
    }
    copy[length] = 0;
    *text = copy;
    return true;
}

static bool checkpoint(qa_source_save_io *io, qa_q3_game *game, qa_q3_checkpoint *p)
{
    FIELD(u32, p->version); FIELD(u32, p->random_state); FIELD(u32, p->death_animation);
    FIELD(u32, p->body_queue_index); ENUM(p->product, QA_Q3_TEAM_ARENA);
    if (p->version != 6) return save_fail(io, "unsupported Q3 typed continuation");
    FIELD(u32, p->max_clients);
    if (!p->max_clients || p->max_clients > 64) return save_fail(io, "invalid Q3 source client capacity");
    if (!rules(io, &p->rules)) return false;
    FIELD(i32, p->previous_ms); FIELD(i32, p->now_ms); FIELD(u64, p->attack_sequence);
    FIELD(i32, p->ranking_hit.frame); FIELD(i32, p->ranking_hit.self);
    FIELD(i32, p->ranking_hit.attacker); FIELD(i32, p->ranking_hit.method); FIELD(bool, p->ranking_hit.valid);
    for (size_t i = 0; i < 8; ++i) FIELD(actor, p->body_queue[i]);
    if (!qa_source_save_count(io, &p->actor_count, game->capacity)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->actor_count) {
        p->actors = allocate(io, p->actor_count, sizeof(*p->actors));
        if (!p->actors) return false;
    }
    for (size_t i = 0; i < p->actor_count; ++i) if (!actor_state(io, &p->actors[i])) return false;
    if (!qa_source_save_count(io, &p->cooldown_count, game->capacity)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->cooldown_count) {
        p->kamikaze_cooldowns = allocate(io, p->cooldown_count, sizeof(*p->kamikaze_cooldowns));
        if (!p->kamikaze_cooldowns) return false;
    }
    for (size_t i = 0; i < p->cooldown_count; ++i) {
        FIELD(actor, p->kamikaze_cooldowns[i].actor);
        FIELD(i32, p->kamikaze_cooldowns[i].damage_after);
        FIELD(i32, p->kamikaze_cooldowns[i].shock_after);
    }
    if (!qa_source_save_count(io, &p->configstring_count, QA_Q3_NATIVE_CONFIGSTRINGS)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->configstring_count) {
        p->configstrings = allocate(io, p->configstring_count, sizeof(*p->configstrings));
        if (!p->configstrings) return false;
    }
    for (size_t i = 0; i < p->configstring_count; ++i) {
        FIELD(u32, p->configstrings[i].index);
        if (p->configstrings[i].index >= QA_Q3_NATIVE_CONFIGSTRINGS ||
            (i && p->configstrings[i].index <= p->configstrings[i - 1].index))
            return save_fail(io, "invalid Q3 stored configstring order");
        if (!configstring_text(io, &p->configstrings[i].text)) return false;
    }
    return true;
}

static bool saved_actor(qa_source_save_io *io, qa_saved_actor_id *p)
{
    FIELD(u64, p->generation); FIELD(u32, p->slot);
    return true;
}

static bool map_actor(qa_source_save_io *io, qa_q3_map_actor_checkpoint *p)
{
    if (!saved_actor(io, &p->actor) || !saved_actor(io, &p->activator) ||
        !saved_actor(io, &p->enemy) || !saved_actor(io, &p->team_master) ||
        !saved_actor(io, &p->team_next) || !saved_actor(io, &p->parent) ||
        !saved_actor(io, &p->path_next)) return false;
    qa_q3_map_actor_state *s = &p->state;
    ENUM(s->kind, QA_Q3_MAP_MOVER_PLAT_TRIGGER); ENUM(s->think, QA_Q3_MAP_THINK_MOVER_TRAIN_RESUME);
    FIELD(string, s->classname); FIELD(string, s->model); FIELD(string, s->model2);
    FIELD(string, s->targetname); FIELD(string, s->target); FIELD(string, s->message); FIELD(string, s->team);
    FIELD(string, s->noise); FIELD(string, s->shader_old); FIELD(string, s->shader_new);
    FIELD(vec3, s->origin); FIELD(vec3, s->angles); FIELD(vec3, s->direction); FIELD(vec3, s->launch_velocity);
    FIELD(vec3, s->first); FIELD(vec3, s->second); FIELD(vec3, s->color);
    FIELD(vec3, s->bounds.mins); FIELD(vec3, s->bounds.maxs);
    if (!item_spawn(io, &s->item)) return false;
    FIELD(u32, s->spawnflags); FIELD(u32, s->inline_model); FIELD(u32, s->ordinal);
    FIELD(i32, s->count); FIELD(i32, s->health); FIELD(i32, s->damage); FIELD(i32, s->due_ms);
    FIELD(i32, s->cooldown_ms); FIELD(i32, s->sound_frame); FIELD(i32, s->sound_random);
    FIELD(f32, s->speed); FIELD(f32, s->wait); FIELD(f32, s->random);
    FIELD(f32, s->delay); FIELD(f32, s->roll); FIELD(f32, s->light);
    FIELD(f32, s->alpha);
    FIELD(bool, s->active); FIELD(bool, s->linked); FIELD(bool, s->has_inline_model);
    FIELD(bool, s->touchable); FIELD(bool, s->usable); FIELD(bool, s->team_slave);
    FIELD(bool, s->item_bound); FIELD(bool, s->damageable); FIELD(bool, s->has_delay);
    FIELD(bool, s->sound_looping); FIELD(bool, s->has_color); FIELD(bool, s->has_light);
    FIELD(bool, s->no_bots); FIELD(bool, s->no_humans);
    return true;
}

static bool map_checkpoint(qa_source_save_io *io, qa_q3_game *game, qa_q3_map_checkpoint *p)
{
    FIELD(u32, p->version);
    if (p->version != 4) return save_fail(io, "unsupported Q3 authored continuation");
    FIELD(i32, p->loaded_game_type);
    if (p->loaded_game_type < -1) return save_fail(io, "invalid Q3 loaded game type");
    FIELD(u64, p->registered_items); FIELD(f32, p->gravity);
    FIELD(string, p->motd); FIELD(u32, p->random_seed); FIELD(i32, p->start_time_ms);
    FIELD(i32, p->restarted); FIELD(bool, p->warmup);
    FIELD(bool, p->world_spawned); FIELD(bool, p->post_spawned); FIELD(bool, p->locations_linked);
    if (p->loaded_game_type >= 0 && (!p->world_spawned || !p->post_spawned))
        return save_fail(io, "Q3 loaded game type lacks completed authored admission");
    if (!qa_source_save_count(io, &p->actor_count, game->capacity)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ && p->actor_count) {
        p->actors = allocate(io, p->actor_count, sizeof(*p->actors));
        if (!p->actors) return false;
    }
    for (size_t i = 0; i < p->actor_count; ++i) if (!map_actor(io, &p->actors[i])) return false;
    return true;
}

static bool private_leases(qa_source_save_io *io, qa_q3_game *game,
                            const qa_q3_checkpoint *native, q3_saved_leases *leases)
{
    for (size_t i = 0; i < native->actor_count; ++i) {
        const q3_actor *actor = &native->actors[i];
        if (actor->actor.slot >= game->capacity)
            return save_fail(io, "Q3 private lease actor is outside the candidate");
        q3_saved_leases *saved = &leases[actor->actor.slot];
        if (io->direction == QA_SOURCE_SAVE_WRITE) {
            const q3_inventory_owner *owner = &game->inventory_owners[actor->actor.slot];
            const qa_pickup_lease *observation = &game->item_observations[actor->actor.slot];
            *saved = (q3_saved_leases){.inventory = owner->actor.registry != 0,
                .selections = owner->selections, .weapons = owner->weapons.serial,
                .holdables = owner->holdables.serial, .observation = observation->serial};
        }
        FIELD(bool, saved->inventory); FIELD(u32, saved->selections);
        FIELD(u64, saved->weapons); FIELD(u64, saved->holdables); FIELD(u64, saved->observation);
        if ((!saved->inventory && (saved->selections || saved->weapons || saved->holdables)) ||
            (saved->inventory && (actor->kind != Q3_ACTOR_PLAYER ||
                (saved->selections & ~actor->state.player.selections))) ||
            (saved->weapons && !(saved->selections & QA_Q3_ARSENAL)) ||
            (saved->holdables && !(saved->selections & QA_Q3_EQUIPMENT)) ||
            (saved->weapons && saved->weapons == saved->holdables) ||
            ((actor->kind == Q3_ACTOR_ITEM) != (saved->observation != 0)))
            return save_fail(io, "Q3 private leases differ from native continuation roles");
    }
    return true;
}

static bool bindings_current(qa_q3_game *game, qa_error *error)
{
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q3_actor *actor = q3_actor_const(game, game->actors[i].actor);
        const q3_inventory_owner *owner = &game->inventory_owners[i];
        const qa_pickup_lease *observation = &game->item_observations[i];
        if (owner->actor.registry || owner->weapons.serial || owner->holdables.serial) {
            if (!actor || actor->kind != Q3_ACTOR_PLAYER || owner->game != game ||
                !qa_actor_id_equal(owner->actor, actor->actor) ||
                (owner->selections & ~actor->state.player.selections) ||
                (owner->weapons.serial && (!(owner->selections & QA_Q3_ARSENAL) ||
                    !qa_actor_id_equal(owner->weapons.actor, actor->actor) ||
                    !qa_inventory_lease_current(game->options.services.inventory, owner->weapons))) ||
                (owner->holdables.serial && (!(owner->selections & QA_Q3_EQUIPMENT) ||
                    !qa_actor_id_equal(owner->holdables.actor, actor->actor) ||
                    !qa_inventory_lease_current(game->options.services.inventory, owner->holdables))))
                return q3_fail(error, "Q3 native inventory private lease is not current");
        }
        if (observation->serial || (actor && actor->kind == Q3_ACTOR_ITEM)) {
            if (!actor || actor->kind != Q3_ACTOR_ITEM || !observation->serial ||
                !qa_actor_id_equal(observation->actor, actor->actor) ||
                !qa_pickups_observation_current(game->options.services.pickups, *observation))
                return q3_fail(error, "Q3 native item private observation is not current");
        }
    }
    if (game->map)
        for (uint32_t i = 0; i < game->map->capacity; ++i) {
            qa_q3_map_actor_state *state = &game->map->actors[i];
            if (!state->active) continue;
            qa_target_binding expected_target, actual_target;
            if (!q3_map_target_binding(game, state->actor, &expected_target) ||
                !qa_persistence_targets_binding(game->map->options.targets, state->actor, &actual_target) ||
                actual_target.source != expected_target.source ||
                actual_target.context != expected_target.context ||
                actual_target.read != expected_target.read || actual_target.use != expected_target.use ||
                actual_target.field != expected_target.field ||
                actual_target.set_targetname != expected_target.set_targetname ||
                actual_target.set_target != expected_target.set_target ||
                actual_target.set_delay != expected_target.set_delay)
                return q3_fail(error, "Q3 authored target binding is not current");
            if (!state->damageable) continue;
            qa_combat_admission expected, actual;
            if (!qa_q3_game_damage_admission(game, state->actor, &expected, error) ||
                !qa_persistence_combat_admission(game->options.services.combat, state->actor, &actual) ||
                actual.context != expected.context || actual.admit != expected.admit)
                return q3_fail(error, "Q3 authored mover damage admission is not current");
        }
    return true;
}

static bool continuation(qa_source_save_io *io, qa_q3_game *game,
                          qa_q3_checkpoint *native, qa_q3_map_checkpoint *map,
                          q3_saved_leases *leases)
{
    uint8_t signature[8] = {'Q', 'A', 'Q', '3', 'S', 'A', 'V', 'E'};
    static const uint8_t expected[8] = {'Q', 'A', 'Q', '3', 'S', 'A', 'V', 'E'};
    if (!qa_source_save_bytes(io, signature, sizeof(signature)) ||
        memcmp(signature, expected, sizeof(signature))) return save_fail(io, "invalid Q3 save signature");
    uint32_t version = 6;
    FIELD(u32, version);
    if (version != 6) return save_fail(io, "unsupported Q3 save version");
    if (!checkpoint(io, game, native)) return false;
    bool has_map = game->map != NULL;
    FIELD(bool, has_map);
    if (has_map != (game->map != NULL)) return save_fail(io, "Q3 authored map owner differs");
    return (!has_map || map_checkpoint(io, game, map)) && private_leases(io, game, native, leases);
}

bool qa_q3_game_capture(qa_q3_game *game, qa_buffer *out, qa_error *error)
{
    if (!game || !out || game->source_restored || !qa_world_idle(game->options.services.world))
        return q3_fail(error, "Q3 portable capture requires an idle world");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->player_binding_tokens[i])
            return q3_fail(error, "Q3 portable capture conflicts with player admission");
    q3_saved_leases *leases = calloc(game->capacity, sizeof(*leases));
    if (!leases) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 private lease capture");
        return false;
    }
    qa_q3_checkpoint native = {0};
    qa_q3_map_checkpoint map = {0};
    qa_source_save_io io = {0};
    bool okay = bindings_current(game, error) && qa_q3_checkpoint_capture(game, &native, error) &&
        (!game->map || qa_q3_map_checkpoint_capture(game, &map, error)) &&
        qa_source_save_writer(&io, game->options.services.session, error) &&
        continuation(&io, game, &native, &map, leases) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    qa_q3_checkpoint_free(&native); qa_q3_map_checkpoint_free(&map);
    free(leases);
    return okay;
}

bool qa_q3_game_restore(qa_q3_game *game, qa_bytes input, qa_error *error)
{
    if (!game || game->source_restored || game->observation_depth || !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) || !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 portable restore requires an idle candidate");
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->actors[i].kind != Q3_ACTOR_NONE || game->player_binding_tokens[i] ||
            game->inventory_owners[i].actor.registry || game->item_observations[i].serial ||
            (game->map && game->map->actors[i].active))
            return q3_fail(error, "Q3 portable restore requires an empty native candidate");
    q3_saved_leases *leases = calloc(game->capacity, sizeof(*leases));
    if (!leases) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 private lease restore");
        return false;
    }
    qa_q3_checkpoint native = {0};
    qa_q3_map_checkpoint map = {0};
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, game->options.services.session, input, error) &&
        continuation(&io, game, &native, &map, leases) && qa_source_save_finish(&io, NULL) &&
        q3_checkpoint_restore_source(game, &native, error);
    if (okay) {
        game->source_restored = true;
        okay = !game->map || q3_map_checkpoint_restore_source(game, &map, error);
    }
    if (okay) {
        for (size_t i = 0; i < native.actor_count; ++i) {
            qa_actor_id actor = native.actors[i].actor;
            const q3_saved_leases *saved = &leases[actor.slot];
            if (saved->inventory)
                game->inventory_owners[actor.slot] = (q3_inventory_owner){.game = game,
                    .actor = actor, .selections = saved->selections,
                    .weapons = {.actor = actor, .serial = saved->weapons},
                    .holdables = {.actor = actor, .serial = saved->holdables}};
            if (saved->observation)
                game->item_observations[actor.slot] = (qa_pickup_lease){.actor = actor,
                    .serial = saved->observation};
        }
    }
    qa_source_save_dispose(&io);
    qa_q3_checkpoint_free(&native); qa_q3_map_checkpoint_free(&map);
    free(leases);
    return okay;
}

bool qa_q3_game_reconnect(qa_q3_game *game, qa_error *error)
{
    if (!game || game->observation_depth || !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) || !qa_combat_idle(game->options.services.combat))
        return q3_fail(error, "Q3 reconnect requires an idle restored candidate");
    if (!bindings_current(game, error)) return false;
    game->source_restored = false;
    return true;
}

bool qa_q3_game_target_binding(qa_q3_game *game, qa_actor_id actor,
                               qa_target_binding *out, qa_error *error)
{
    if (!game || !out)
        return q3_fail(error, "Q3 target resolution requires a source and output");
    return q3_map_target_binding(game, actor, out);
}

#undef FIELD
#undef ENUM
