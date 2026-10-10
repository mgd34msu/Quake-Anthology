#include "internal.h"
#include "qa/game_type.h"
#include "source_postgame.h"

bool qa_q3_source_memory_allocate(qa_q3_game *game, uint32_t size, uint32_t *out,
                                  qa_error *error) {
    if (!game || !out || size > INT32_MAX || game->source_restored ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "G_Alloc requires a nonnegative source int size");
    uint32_t point = game->memory.allocated_bytes, aligned = (size + 31u) & ~31u;
    int32_t debug = 0;
    ++game->observation_depth;
    bool ok = !game->options.hooks.memory_debug_integer ||
        game->options.hooks.memory_debug_integer(game->options.hooks.context, &debug, error);
    if (ok && debug && game->options.hooks.console_print) {
        uint32_t left_bits = QA_Q3_SOURCE_MEMORY_BYTES - point - aligned;
        int32_t left; memcpy(&left, &left_bits, sizeof(left));
        char text[96];
        snprintf(text, sizeof(text), "G_Alloc of %u bytes (%i left)\n", size, left);
        ok = game->options.hooks.console_print(game->options.hooks.context, text, error);
    }
    --game->observation_depth;
    if (!ok) return false;
    if (point != game->memory.allocated_bytes)
        return q3_fail(error, "Q3 GAME memory changed during its allocation report");
    if (size > QA_Q3_SOURCE_MEMORY_BYTES - point) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "G_Alloc: failed on allocation of %u bytes\n", size);
        return false;
    }
    game->memory.allocated_bytes = point + aligned;
    *out = point;
    return true;
}
bool qa_q3_source_memory_rewind(qa_q3_game *game, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth)
        return q3_fail(error, "Q3 GAME memory rewind requires its idle source owner");
    game->memory.allocated_bytes = 0;
    return true;
}
bool qa_q3_source_memory_allocated(const qa_q3_game *game, uint32_t *out, qa_error *error) {
    if (!game || !out) return q3_fail(error, "Q3 GAME memory read needs its source owner");
    *out = game->memory.allocated_bytes;
    return true;
}
bool qa_q3_source_memory_status(qa_q3_game *game, qa_error *error) {
    if (!game || game->source_restored || !game->options.hooks.console_print ||
        game->observation_depth == SIZE_MAX)
        return q3_fail(error, "Q3 memory status requires its real console print service");
    char text[96];
    snprintf(text, sizeof(text), "Game memory status: %u out of %u bytes allocated\n",
             game->memory.allocated_bytes, QA_Q3_SOURCE_MEMORY_BYTES);
    ++game->observation_depth;
    bool ok = game->options.hooks.console_print(game->options.hooks.context, text, error);
    --game->observation_depth;
    return ok;
}
bool qa_q3_source_memory_read(const qa_q3_game *game, uint32_t offset, void *out,
                              uint32_t size, qa_error *error) {
    if (!game || (size && !out) || offset > QA_Q3_SOURCE_MEMORY_BYTES ||
        size > QA_Q3_SOURCE_MEMORY_BYTES - offset)
        return q3_fail(error, "Q3 source allocation exceeds its real GAME pool");
    if (size) memcpy(out, game->memory.pool + offset, size);
    return true;
}
bool qa_q3_source_memory_write(qa_q3_game *game, uint32_t offset, const void *bytes,
                               uint32_t size, qa_error *error) {
    if (!game || game->source_restored || (size && !bytes) || offset > QA_Q3_SOURCE_MEMORY_BYTES ||
        size > QA_Q3_SOURCE_MEMORY_BYTES - offset)
        return q3_fail(error, "Q3 source allocation write exceeds its real GAME pool");
    if (size) memmove(game->memory.pool + offset, bytes, size);
    return true;
}
bool qa_q3_source_memory_span(qa_q3_game *game, uint32_t offset, uint32_t size,
                             uint8_t **out, qa_error *error) {
    if (!game || !out || offset > QA_Q3_SOURCE_MEMORY_BYTES ||
        size > QA_Q3_SOURCE_MEMORY_BYTES - offset)
        return q3_fail(error, "Q3 source byte span exceeds its real mutable GAME pool");
    *out = game->memory.pool + offset;
    return true;
}
bool q3_rollback_spawn(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    qa_error original = {0}, cleanup = {0};
    bool preserve = error && error->code != QA_OK;
    if (preserve)
        original = *error;
    if (game && qa_actors_get(qa_session_actors(game->options.services.session), actor))
        (void)qa_session_release(game->options.services.session, actor, &cleanup);
    if (error) {
        if (preserve)
            *error = original;
        else if (cleanup.code != QA_OK)
            *error = cleanup;
    }
    return false;
}
qa_builtin_snapshot_frame *q3_bounds_snapshot(qa_q3_game *game, qa_bounds bounds,
                                       qa_collision_role role, qa_error *error) {
    if (!game) {
        q3_fail(error, "missing Q3 spatial query provider");
        return NULL;
    }
    qa_builtin_snapshot_frame *frame = qa_builtin_snapshot_acquire(
        &game->snapshot_frames, game->capacity, error);
    if (!frame) return NULL;
    bool overflow = false;
    if (!qa_world_query(game->options.services.world, bounds, role, frame->snapshot.ids,
                        frame->snapshot.capacity, &frame->snapshot.count, &overflow, error)) {
        qa_builtin_snapshot_release(frame);
        return NULL;
    }
    if (overflow) {
        qa_builtin_snapshot_release(frame);
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Q3 spatial query exceeded the actor registry capacity");
        return NULL;
    }
    return frame;
}
int32_t q3_add_time(int32_t a, int32_t b) {
    uint32_t bits = (uint32_t)a + (uint32_t)b;
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
int32_t q3_sub_time(int32_t a, int32_t b) {
    uint32_t bits = (uint32_t)a - (uint32_t)b;
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
q3_actor *q3_actor_storage(qa_q3_game *game, qa_actor_id actor) {
    if (!game || actor.slot >= game->capacity) return NULL;
    uint32_t slot = game->source_numbers[actor.slot];
    if (slot < QA_Q3_SOURCE_CLIENTS &&
        qa_actor_id_equal(game->source_entities[slot].actor, actor))
        return &game->client_actors[slot];
    return &game->actors[actor.slot];
}
q3_actor *q3_actor_get(qa_q3_game *game, qa_actor_id actor) {
    if (!game || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return NULL;
    q3_actor *entry = q3_actor_storage(game, actor);
    return entry->kind && qa_actor_id_equal(entry->actor, actor) ? entry : NULL;
}
const q3_actor *q3_actor_at_const(const qa_q3_game *game, uint32_t host_slot) {
    if (!game || host_slot >= game->capacity) return NULL;
    uint32_t slot = game->source_numbers[host_slot];
    if (slot < QA_Q3_SOURCE_CLIENTS &&
        game->source_entities[slot].actor.registry &&
        game->source_entities[slot].actor.slot == host_slot)
        return &game->client_actors[slot];
    return &game->actors[host_slot];
}
q3_actor *q3_actor_at(qa_q3_game *game, uint32_t host_slot) {
    return (q3_actor *)q3_actor_at_const(game, host_slot);
}
const q3_actor *q3_actor_const(const qa_q3_game *game, qa_actor_id actor) {
    if (!game || actor.slot >= game->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return NULL;
    uint32_t slot = game->source_numbers[actor.slot];
    const q3_actor *entry = slot < QA_Q3_SOURCE_CLIENTS &&
        qa_actor_id_equal(game->source_entities[slot].actor, actor)
        ? &game->client_actors[slot] : &game->actors[actor.slot];
    return entry->kind && qa_actor_id_equal(entry->actor, actor) ? entry : NULL;
}
uint32_t q3_rand(qa_q3_game *game) {
    game->rng = game->rng * UINT32_C(69069) + 1u;
    return game->rng & UINT32_C(0x7fff);
}
float q3_random(qa_q3_game *game) {
    return ((float)q3_rand(game) / 32767.0f);
}
float q3_crandom(qa_q3_game *game) {
    return (2.0f * (q3_random(game) + -0.5f));
}
bool qa_q3_game_random(qa_q3_game *game, float *out, qa_error *error) {
    if (!game || !out || game->source_restored)
        return q3_fail(error, "Q3 random needs its actual active source owner");
    *out = q3_random(game);
    return true;
}
bool qa_q3_game_rand(qa_q3_game *game, uint32_t *out, qa_error *error) {
    if (!game || !out || game->source_restored)
        return q3_fail(error, "Q3 rand needs its actual active source owner");
    *out = q3_rand(game);
    return true;
}

qa_q3_rules qa_q3_default_rules(void) {
    return (qa_q3_rules){.proximity_timeout_ms = 20000,
                         .force_respawn_seconds = 20,
                         .quad_factor = 3,
                         .knockback = 1000,
                         .weapon_respawn_seconds = 5,
                         .team_weapon_respawn_seconds = 30,
                         .gravity = 800,
                         .blood = true};
}
static bool valid_rules(const qa_q3_rules *rules) {
    return rules && rules->game_type >= 0 && rules->proximity_timeout_ms >= 0 &&
           rules->force_respawn_seconds >= 0 && isfinite(rules->quad_factor) &&
           rules->quad_factor >= 0 && isfinite(rules->knockback) && rules->knockback >= 0 &&
           isfinite(rules->weapon_respawn_seconds) &&
           fabsf(rules->weapon_respawn_seconds) <= 2147483 &&
           isfinite(rules->team_weapon_respawn_seconds) &&
           fabsf(rules->team_weapon_respawn_seconds) <= 2147483 && isfinite(rules->gravity);
}
static bool valid_source_rules(const qa_q3_rules *rules) {
    return rules && isfinite(rules->quad_factor) && isfinite(rules->knockback) &&
           isfinite(rules->weapon_respawn_seconds) &&
           isfinite(rules->team_weapon_respawn_seconds) && isfinite(rules->gravity);
}
static bool physics_read(void *context, qa_actor_id actor, qa_physics_properties *out) {
    qa_q3_game *game = context;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return false;
    *out = qa_physics_properties_default(QA_GAME_Q3);
    out->clip_mask = Q3_MASK_SHOT;
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER)
        out->flags |= QA_PHYSICS_PLAYER;
    else if (entry && entry->kind == Q3_ACTOR_MISSILE)
        out->motion = QA_PHYSICS_FLY_MISSILE;
    else if (entry && entry->kind == Q3_ACTOR_VICTORY_MODEL)
        out->clip_mask = UINT32_C(0x10001);
    else
        out->clip_mask = 1;
    return true;
}
const char *qa_q3_weapon_identity_name(qa_q3_weapon weapon) {
    static const char *const names[QA_Q3_WEAPON_COUNT] = {
        NULL, "gauntlet", "machinegun", "shotgun", "grenadelauncher", "rocketlauncher",
        "lightning", "railgun", "plasmagun", "bfg", "grapple", "nailgun",
        "proxlauncher", "chaingun"};
    return weapon > QA_Q3_W_NONE && weapon < QA_Q3_WEAPON_COUNT ? names[weapon] : NULL;
}
bool qa_q3_create(const qa_q3_options *options, qa_q3_game **out, qa_error *error) {
    if (!options || !out || !options->owner || options->product < QA_Q3_ARENA ||
        options->product > QA_Q3_TEAM_ARENA || !options->services.pickups ||
        !options->max_clients || options->max_clients > 64 ||
        !valid_rules(&options->rules) || !qa_builtin_services_validate(&options->services, error))
        return q3_fail(error, "invalid Q3 game options");
    qa_q3_game *game = calloc(1, sizeof(*game));
    if (!game) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 provider");
        return false;
    }
    game->options = *options;
    game->capacity = qa_actors_capacity(qa_session_actors(options->services.session));
    game->actors = calloc(game->capacity, sizeof(*game->actors));
    game->kamikaze_cooldowns = calloc(game->capacity, sizeof(*game->kamikaze_cooldowns));
    game->player_binding_tokens = calloc(game->capacity, sizeof(*game->player_binding_tokens));
    game->item_observations = calloc(game->capacity, sizeof(*game->item_observations));
    game->inventory_owners = calloc(game->capacity, sizeof(*game->inventory_owners));
    game->source_numbers = malloc(game->capacity * sizeof(*game->source_numbers));
    if (!game->actors || !game->kamikaze_cooldowns || !game->player_binding_tokens || !game->item_observations || !game->inventory_owners || !game->source_numbers) {
        free(game->actors);
        free(game->kamikaze_cooldowns);
        free(game->player_binding_tokens);
        free(game->item_observations);
        free(game->inventory_owners);
        free(game->source_numbers);
        free(game);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 actor extensions");
        return false;
    }
    game->rng = options->random_seed;
    if (!q3_wire_create(game, error)) goto fail;
    q3_source_state_reset(game);
    if (!qa_builtin_resource(&options->services, "noclass", &game->source_noclass, error) ||
        !qa_builtin_resource(&options->services, "freed", &game->source_freed, error)) goto fail;
    game->physics = (qa_physics){.world = options->services.world,
                                 .gravity = 800,
                                 .max_velocity = 2000,
                                 .stop_speed = 100,
                                 .services = {.context = game, .read = physics_read}};
    size_t push_capacity = game->capacity < QA_PHYSICS_SOURCE_PUSH_LIMIT
        ? QA_PHYSICS_SOURCE_PUSH_LIMIT : game->capacity;
    if (!qa_physics_prepare_push_frames(&game->physics, QA_PHYSICS_DEFAULT_PUSH_FRAMES,
                                        push_capacity, push_capacity, error)) goto fail;
    for (int i = 1; i < QA_Q3_WEAPON_COUNT; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "q3:weapon/%s", qa_q3_weapon_identity_name((qa_q3_weapon)i));
        if (!qa_builtin_resource(&options->services, name, &game->weapon_items[i], error))
            goto fail;
        if (i != QA_Q3_W_GAUNTLET && i != QA_Q3_W_GRAPPLE) {
            snprintf(name, sizeof(name), "q3:ammo/%s", qa_q3_weapon_identity_name((qa_q3_weapon)i));
            if (!qa_builtin_resource(&options->services, name, &game->ammo_items[i], error))
                goto fail;
        }
    }
    if (!q3_item_register(game, error))
        goto fail;
    *out = game;
    return true;
fail:
    (void)qa_physics_dispose(&game->physics, NULL);
    q3_wire_destroy(game);
    free(game->source_numbers);
    free(game->kamikaze_cooldowns);
    free(game->player_binding_tokens);
    free(game->item_observations);
    free(game->inventory_owners);
    free(game->actors);
    free(game);
    return false;
}
bool qa_q3_destroy_ready(const qa_q3_game *game) {
    if (!game)
        return true;
    if (game->observation_depth || game->player_binding_count || game->physics.push_transaction ||
        game->physics.push_frames.active || !q3_source_origins_idle(game) ||
        !qa_session_safe(game->options.services.session) ||
        !qa_world_idle(game->options.services.world) ||
        !qa_combat_idle(game->options.services.combat))
        return false;
    return true;
}
bool qa_q3_destroy(qa_q3_game *game, qa_error *error) {
    if (!game)
        return true;
    if (!qa_q3_destroy_ready(game))
        return q3_fail(error, "Q3 provider destruction requires a safe point");
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q3_inventory_owner *owner = &game->inventory_owners[i];
        if (qa_inventory_lease_current(game->options.services.inventory, owner->weapons) &&
            !qa_inventory_close_items(game->options.services.inventory, owner->weapons, error))
            return false;
        if (qa_inventory_lease_current(game->options.services.inventory, owner->holdables) &&
            !qa_inventory_close_items(game->options.services.inventory, owner->holdables, error))
            return false;
    }
    if (!qa_physics_dispose(&game->physics, error)) return false;
    q3_map_destroy(game);
    q3_configstrings_clear(game);
    q3_wire_destroy(game);
    free(game->source_numbers);
    for(uint32_t i=0;i<game->capacity;++i)
        if(game->item_observations[i].serial)
            qa_pickups_observation_close(game->options.services.pickups,game->item_observations[i],NULL);
    qa_builtin_snapshot_pool_free(&game->snapshot_frames);
    free(game->kamikaze_cooldowns);
    free(game->player_binding_tokens);
    free(game->item_observations);
    free(game->inventory_owners);
    free(game->actors);
    free(game);
    return true;
}
bool qa_q3_rules_read(const qa_q3_game *game, qa_q3_rules *out, qa_error *error) {
    if (!game || !out)
        return q3_fail(error, "Q3 rules query requires actual game and output");
    *out = game->options.rules;
    return true;
}
bool qa_q3_source_clock(const qa_q3_game *game, int32_t *out, qa_error *error) {
    if (!game || !out)
        return q3_fail(error, "Q3 source clock query requires its actual game and output");
    *out = game->now_ms;
    return true;
}
bool qa_q3_set_rules(qa_q3_game *game, const qa_q3_rules *rules, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth || !valid_rules(rules))
        return q3_fail(error, "invalid Q3 game rules");
    game->options.rules = *rules;
    return true;
}
bool qa_q3_set_source_rules(qa_q3_game *game, const qa_q3_rules *rules, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth ||
        !valid_source_rules(rules))
        return q3_fail(error, "invalid Q3 source game rules");
    game->options.rules = *rules;
    return true;
}
int32_t q3_entity_number(const qa_q3_game *game, qa_actor_id actor) {
    if (actor.slot < game->capacity &&
        qa_actors_get(qa_session_actors(game->options.services.session), actor)) {
        uint32_t slot = game->source_numbers[actor.slot];
        if (slot < QA_Q3_SOURCE_ENTITIES &&
            qa_actor_id_equal(game->source_entities[slot].actor, actor))
            return (int32_t)slot;
    }
    if (game->options.hooks.entity_number)
        return game->options.hooks.entity_number(game->options.hooks.context, actor);
    return QA_Q3_SOURCE_WORLD;
}
static bool sound_report(qa_q3_game *game, qa_actor_id actor, qa_string_id resource,
                         qa_vec3 origin, int32_t channel, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q3,
                              .provider = game->options.owner,
                              .actor = actor,
                              .time_ns = (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000),
                              .resource = resource,
                              .origin = origin,
                              .volume = 1,
                              .attenuation = 1,
                              .channel = channel};
    return qa_builtin_emit(&game->options.services, &event, error);
}
bool q3_sound_report(qa_q3_game *game, qa_actor_id actor, const char *path, int32_t channel,
                     qa_error *error) {
    qa_string_id resource;
    qa_body_state body;
    return qa_builtin_resource(&game->options.services, path, &resource, error) &&
        q3_source_body_read(game, actor, &body, error) &&
        sound_report(game, actor, resource, body.origin, channel, error);
}
bool q3_sound(qa_q3_game *game, qa_actor_id actor, const char *path, int32_t channel,
              qa_error *error) {
    qa_string_id resource;
    if (!qa_builtin_resource(&game->options.services, path, &resource, error))
        return false;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    uint32_t source_slot;
    if (qa_q3_source_actor_slot(game, actor, &source_slot, NULL)) {
        int32_t sound_index;
        qa_actor_id temporary;
        if (!qa_q3_sound_index(game, path, &sound_index, error) ||
            !q3_wire_temp_entity(game, body.origin, 45, &temporary, error)) return false;
        qa_q3_entity *source = q3_wire_temporary(game, temporary);
        if (!source) return q3_fail(error, "Q3 sound lost its actual temporary entity");
        source->eventParm = sound_index;
    }
    return sound_report(game, actor, resource, body.origin, channel, error);
}
qa_item_id qa_q3_weapon_item(const qa_q3_game *game, qa_q3_weapon weapon, bool ammo) {
    if (!game || weapon <= QA_Q3_W_NONE || weapon >= QA_Q3_WEAPON_COUNT)
        return 0;
    return ammo ? game->ammo_items[weapon] : game->weapon_items[weapon];
}
bool q3_event(qa_q3_game *game, qa_actor_id actor, qa_actor_id other, qa_builtin_event_kind kind,
              int32_t code, int32_t parameter, qa_vec3 origin, qa_vec3 end, qa_vec3 normal,
              qa_error *error) {
    qa_builtin_event event = {.kind = kind,
                              .family = QA_GAME_Q3,
                              .provider = game->options.owner,
                              .actor = actor,
                              .other = other,
                              .time_ns = (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000),
                              .origin = origin,
                              .end = end,
                              .direction = normal,
                              .code = code,
                              .count = parameter};
    return qa_builtin_emit(&game->options.services, &event, error);
}
bool q3_player_event(qa_q3_game *game, qa_actor_id actor, int32_t code, int32_t parameter,
                     qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER) {
        qa_q3_player_state *player = &entry->state.player;
        if (code == 23) {
            player->has_last_fire = true;
            player->last_fire_ms = game->now_ms;
        }
        uint32_t slot = player->event_sequence & 1u;
        player->events[slot] = code;
        player->event_parameters[slot] = parameter;
        ++player->event_sequence;
        uint32_t source_slot;
        if (qa_q3_native_client_slot(game, actor, &source_slot, NULL)) {
            qa_q3_player *copied = q3_client_follow_player(game, source_slot);
            if (copied) {
                copied->events[slot] = code;
                copied->eventParms[slot] = parameter;
                memcpy(&copied->eventSequence, &player->event_sequence, sizeof(copied->eventSequence));
            }
        }
    }
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error))
        return false;
    return q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_ANIMATION, code, parameter,
                    body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
bool q3_add_event(qa_q3_game *game, qa_actor_id actor, int32_t code, int32_t parameter,
                   qa_error *error) {
    uint32_t slot;
    bool physical = qa_q3_source_actor_slot(game, actor, &slot, NULL);
    if (physical) {
        if (!q3_wire_add_event(game, actor, code, parameter, error)) return false;
    } else {
        q3_actor *entry = q3_actor_get(game, actor);
        if (entry && entry->kind == Q3_ACTOR_PLAYER && code) {
            qa_q3_player_state *player = &entry->state.player;
            player->external_event = code | (((player->external_event & 0x300) + 0x100) & 0x300);
            player->external_event_parameter = parameter;
            player->external_event_time = game->now_ms;
        }
    }
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor)) return true;
    qa_body_state body;
    if (!q3_source_body_read(game, actor, &body, error)) return false;
    if (!physical && game->options.hooks.source_participant_event &&
        !game->options.hooks.source_participant_event(game->options.hooks.context,
            actor, code, parameter, body.origin, game->now_ms, error)) return false;
    return !q3_actor_get(game, actor) ||
           q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_ANIMATION, code, parameter,
                    body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
bool q3_trace(qa_q3_game *game, qa_vec3 start, qa_vec3 end, qa_actor_id pass, uint32_t mask,
              qa_trace_result *trace, qa_error *error) {
    qa_trace_query query = {.start = start,
                            .end = end,
                            .shape = {.kind = QA_SHAPE_POINT},
                            .pass_actor = pass,
                            .policy = qa_collision_default_policy(QA_GAME_Q3)};
    query.policy.contents_mask = qa_collision_contents_mask(mask, QA_GAME_Q3);
    return qa_world_trace(game->options.services.world, &query, trace, error);
}
bool q3_accuracy(qa_q3_game *game, qa_actor_id target, qa_actor_id attacker) {
    if (qa_actor_id_equal(target, attacker))
        return false;
    bool ap = q3_is_player(game, attacker), tp = q3_is_player(game, target);
    if (!ap || !tp)
        return false;
    qa_combat_state ac, tc;
    qa_error ignored = {0};
    if (!qa_combat_read(game->options.services.combat, attacker, &ac, &ignored) ||
        !qa_combat_read(game->options.services.combat, target, &tc, &ignored))
        return false;
    if (!tc.can_take_damage || tc.health <= 0 ||
        !qa_actors_get(qa_session_actors(game->options.services.session), attacker) ||
        !qa_actors_get(qa_session_actors(game->options.services.session), target)) return false;
    if (!qa_game_type_has_allies(game->options.rules.game_type)) return true;
    uint32_t attacker_client, target_client;
    bool native_attacker = q3_source_client_pointer(game, attacker, &attacker_client);
    bool native_target = q3_source_client_pointer(game, target, &target_client);
    if (native_attacker && native_target)
        return game->clients[attacker_client].rule.session.team != game->clients[target_client].rule.session.team;
    if (!native_attacker && !native_target) return ac.team != tc.team;
    char native_team[32];
    snprintf(native_team, sizeof(native_team), "q3-team:%i",
        game->clients[native_attacker ? attacker_client : target_client].rule.session.team);
    const char *foreign_team = qa_strings_cstr(qa_session_strings(game->options.services.session),
                                                native_attacker ? tc.team : ac.team);
    return !foreign_team || strcmp(native_team, foreign_team) != 0;
}
bool q3_is_player(qa_q3_game *game, qa_actor_id actor) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER)
        return true;
    qa_builtin_actor_traits traits;
    return game->options.services.actor_traits &&
           game->options.services.actor_traits(game->options.services.context, actor, &traits) &&
           traits.player;
}
bool qa_q3_actor_traits(const qa_q3_game *game, qa_actor_id actor, qa_builtin_actor_traits *out) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || !out)
        return false;
    *out = (qa_builtin_actor_traits){.gib_health = -40};
    if (entry->kind == Q3_ACTOR_PLAYER) {
        out->player = true;
        out->has_life = (entry->state.player.selections & QA_Q3_CHARACTER) != 0;
        out->birth_epoch = out->has_life ? entry->state.player.spawn_count : 0;
        out->dead = out->has_life && entry->state.player.dead;
        out->spectator = entry->state.player.spectator;
        out->no_target = entry->state.player.no_target;
        qa_builtin_player_control control;
        if (q3_player_control(game, actor, &control, NULL)) {
            out->view_height = *control.view_height;
            out->grounded = control.ground->hit != QA_TRACE_HIT_NONE;
        }
        out->max_health = (float)entry->state.player.max_health;
        out->invisible = entry->state.player.powerups[QA_Q3_P_INVIS] != 0;
    } else if (entry->kind == Q3_ACTOR_MISSILE)
        out->owner = entry->state.missile.owner;
    else if (entry->kind == Q3_ACTOR_PORTAL)
        out->owner = entry->state.portal.owner;
    else if (entry->kind == Q3_ACTOR_CORPSE)
        out->owner = entry->state.corpse.player;
    return true;
}
void q3_credit_accuracy(qa_q3_game *game, qa_actor_id actor) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (entry && entry->kind == Q3_ACTOR_PLAYER)
        entry->state.player.accuracy_hits = q3_add_time(entry->state.player.accuracy_hits, 1);
}
bool q3_combat_describe(void *context, const qa_damage_request *request,
                        const qa_combat_state *target, const qa_combat_state *attacker,
                        qa_combat_context *out, qa_error *error) {
    (void)error;
    qa_q3_game *game = context;
    q3_actor *t = q3_actor_get(game, request->target),
             *a = q3_actor_get(game, request->attack.attacker);
    qa_q3_player_state *tp = t && t->kind == Q3_ACTOR_PLAYER ? &t->state.player : NULL;
    qa_q3_player_state *ap = a && a->kind == Q3_ACTOR_PLAYER ? &a->state.player : NULL;
    int32_t method = request->attack.cause.kind == QA_CAUSE_Q3
                         ? request->attack.cause.source.q3.means_of_death
                         : 0;
    *out = (qa_combat_context){
        .armor = {.alive = target->health > 0},
        .game.q3 = {
            .player = q3_is_player(game, request->target),
            .attacker_player = q3_is_player(game, request->attack.attacker),
            .attacker_guard = ap && ap->persistent == QA_Q3_P_GUARD,
            .attacker_max_health = ap ? ap->max_health : 100,
            .intermission = game->options.rules.intermission,
            .noclip = tp && tp->noclip,
            .missionpack_invulnerability = tp && tp->invulnerability_until > game->now_ms,
            .no_knockback = target->no_knockback,
            .friendly_fire = !qa_game_type_has_allies(game->options.rules.game_type) || game->options.rules.friendly_fire,
            .battlesuit = tp && tp->powerups[QA_Q3_P_BATTLESUIT] != 0,
            .falling = method == 19,
            .juiced = method == 27,
            .proximity_protected = qa_game_type_has_allies(game->options.rules.game_type) && method == 25 && attacker &&
                                   target->team && target->team == attacker->team,
            .missionpack = game->options.product == QA_Q3_TEAM_ARENA,
            .knockback_scale = game->options.rules.knockback}};
    return true;
}
bool qa_q3_timed_invulnerability(const qa_q3_game *game, qa_actor_id actor) {
    const q3_actor *entry = q3_actor_const(game, actor);
    return game && game->options.product == QA_Q3_TEAM_ARENA && entry &&
           entry->kind == Q3_ACTOR_PLAYER &&
           entry->state.player.invulnerability_until > game->now_ms;
}
bool qa_q3_damage_allowed(const qa_q3_game *game, const qa_damage_request *request) {
    if (!game || !request)
        return false;
    if (!qa_q3_timed_invulnerability(game, request->target))
        return true;
    return request->attack.cause.kind == QA_CAUSE_Q3 &&
           request->attack.cause.source.q3.means_of_death == 27;
}
bool q3_damage(qa_q3_game *game, qa_actor_id target, qa_actor_id attacker, qa_actor_id inflictor,
               qa_q3_weapon weapon, int32_t method, uint32_t flags, float amount, qa_vec3 direction,
               qa_vec3 point, bool radius, qa_damage_outcome *result, qa_error *error) {
    if (!attacker.registry)
        attacker = game->source_entities[QA_Q3_SOURCE_WORLD].actor;
    if (!inflictor.registry)
        inflictor = game->source_entities[QA_Q3_SOURCE_WORLD].actor;
    qa_actor_owner policy = game->options.owner;
    if (game->options.hooks.combat_provider)
        policy = game->options.hooks.combat_provider(game->options.hooks.context, target, policy);
    qa_damage_request request = {
        .target = target,
        .amount = amount,
        .knockback = amount,
        .direction = direction,
        .point = point,
        .radius = radius,
        .attack = {.time_ns = (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000),
                   .attacker = attacker,
                   .inflictor = inflictor,
                   .projectile = inflictor,
                   .weapon = qa_q3_weapon_item(game, weapon, false),
                   .weapon_provider = game->options.owner,
                   .combat_provider = policy,
                   .inventory_provider = game->options.owner,
                   .movement_provider = game->options.owner,
                   .powerup_owner = game->options.owner,
                   .powerup_applied = true,
                   .cause = {.kind = QA_CAUSE_Q3, .source.q3 = {method, flags}}}};
    if (game->options.hooks.attack_providers &&
        !game->options.hooks.attack_providers(game->options.hooks.context, attacker,
            request.attack.weapon, &request.attack.inventory_provider,
            &request.attack.movement_provider, error)) return false;
    if (!qa_attack_next(&game->attack_sequence, &request.attack, error))
        return false;
    qa_damage_outcome local = {0};
    qa_damage_outcome *out = result ? result : &local;
    bool ok = qa_combat_apply(game->options.services.combat, &request, out, error);
    if (!result)
        qa_damage_outcome_free(&local);
    return ok;
}
float q3_damage_factor(qa_q3_game *game, const qa_q3_player_state *player) {
    float factor = player->powerups[QA_Q3_P_QUAD] ? game->options.rules.quad_factor : 1;
    if (game->options.product == QA_Q3_TEAM_ARENA && player->persistent == QA_Q3_P_DOUBLER)
        factor *= 2;
    return factor;
}
bool qa_q3_frame(qa_q3_game *game, int32_t previous, int32_t now, qa_error *error) {
    if (!game || game->source_restored)
        return q3_fail(error, "Q3 source restoration is pending or unavailable");
    game->previous_ms = previous;
    game->now_ms = now;
    memset(game->source_actor_ran, 0, sizeof(game->source_actor_ran));
    return q3_map_frame_begin(game, error);
}
static bool actor_frame(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    bool handled = false;
    if (!q3_map_actor_frame(game, actor, &handled, error))
        return false;
    if (handled)
        return true;
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return q3_postgame_think_override(game, actor, &handled, error);
    uint32_t native_slot;
    if (entry->kind == Q3_ACTOR_PLAYER &&
        !qa_q3_native_client_slot(game, actor, &native_slot, NULL)) {
        qa_q3_player_motion motion;
        if (!q3_player_motion_read(game, actor, &motion, error)) return false;
        if (motion.jumppad_frame != motion.pmove_frame_count)
            q3_player_jumppad_write(game, actor, (qa_actor_id){0}, 0);
    }
    switch (entry->kind) {
    case Q3_ACTOR_PODIUM: case Q3_ACTOR_VICTORY_MODEL:
        return q3_postgame_step(game, actor, error);
    case Q3_ACTOR_MISSILE:
        return q3_missile_step(game, actor, error);
    case Q3_ACTOR_ITEM:
        return q3_item_step(game, actor, error);
    case Q3_ACTOR_MOVER: {
        qa_q3_mover_services adapter = qa_q3_mover_adapter(game);
        qa_physics_result result;
        return qa_physics_q3_run_mover(&game->physics, &adapter, actor, game->previous_ms,
                                       game->now_ms, &result, error);
    }
    case Q3_ACTOR_KAMIKAZE:
        return q3_kamikaze_step(game, actor, error);
    case Q3_ACTOR_PORTAL:
        return q3_portal_step(game, actor, error);
    case Q3_ACTOR_CORPSE:
        return q3_corpse_step(game, actor, error);
    case Q3_ACTOR_OBELISK:
        return q3_obelisk_step(game, actor, error);
    case Q3_ACTOR_TEMPORARY:
        return q3_sub_time(game->now_ms, entry->state.temporary.event_time_ms) <= 300 ||
               qa_session_release(game->options.services.session, actor, error);
    case Q3_ACTOR_KAMIKAZE_TIMER:
        if (!q3_postgame_think_override(game, actor, &handled, error)) return false;
        if (handled) return true;
        if (game->now_ms >= entry->state.kamikaze.next) {
            qa_actor_id source = entry->state.kamikaze.attacker;
            q3_actor *owner = q3_actor_get(game, source);
            qa_actor_id attacker =
                owner && owner->kind == Q3_ACTOR_CORPSE ? owner->state.corpse.player : source;
            qa_actor_id origin =
                qa_actors_get(qa_session_actors(game->options.services.session), source) ? source
                                                                                         : actor;
            if (!q3_start_kamikaze(game, origin, attacker, false, error))
                return false;
            return !q3_actor_get(game, actor) ||
                   qa_session_release(game->options.services.session, actor, error);
        }
        return true;
    default:
        return q3_postgame_think_override(game, actor, &handled, error);
    }
}
bool qa_q3_actor_frame(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 actor frame boundary");
    ++game->observation_depth;
    bool okay = actor_frame(game, actor, error);
    --game->observation_depth;
    return okay;
}
void qa_q3_actor_released(qa_q3_game *game, qa_actor_record record) {
    if (!game || record.id.slot >= game->capacity)
        return;
    if(qa_actor_id_equal(game->item_observations[record.id.slot].actor,record.id)) {
        qa_pickups_observation_close(game->options.services.pickups,game->item_observations[record.id.slot],NULL);
        game->item_observations[record.id.slot]=(qa_pickup_lease){0};
    }
    q3_map_actor_released(game, record);
    if (qa_actor_id_equal(game->kamikaze_cooldowns[record.id.slot].actor, record.id))
        game->kamikaze_cooldowns[record.id.slot] = (q3_kamikaze_cooldown){0};
    q3_inventory_owner *inventory = &game->inventory_owners[record.id.slot];
    if (qa_actor_id_equal(inventory->actor, record.id)) {
        if (qa_inventory_lease_current(game->options.services.inventory, inventory->weapons))
            qa_inventory_close_items(game->options.services.inventory, inventory->weapons, NULL);
        if (qa_inventory_lease_current(game->options.services.inventory, inventory->holdables))
            qa_inventory_close_items(game->options.services.inventory, inventory->holdables, NULL);
        *inventory = (q3_inventory_owner){0};
    }
    q3_actor *entry = q3_actor_storage(game, record.id);
    if (qa_actor_id_equal(entry->actor, record.id)) {
        q3_actor retired = *entry;
        if (game->source_numbers[record.id.slot] >= QA_Q3_SOURCE_CLIENTS)
            memset(entry, 0, sizeof(*entry));
        if (retired.kind == Q3_ACTOR_MISSILE) {
            q3_missile missile = retired.state.missile;
            q3_actor *owner = q3_actor_get(game, missile.owner);
            if (owner && owner->kind == Q3_ACTOR_PLAYER &&
                qa_actor_id_equal(owner->state.player.hook, record.id)) {
                owner->state.player.hook = (qa_actor_id){0};
                owner->state.player.grapple_pull = false;
                owner->state.player.selected_pm_flags &= ~0x800u;
            }
            q3_actor *attached = q3_actor_get(game, missile.attached);
            if (attached && attached->kind == Q3_ACTOR_PLAYER &&
                qa_actor_id_equal(attached->state.player.attached_mine, record.id)) {
                attached->state.player.attached_mine = (qa_actor_id){0};
                attached->state.player.flags &= ~2u;
            }
            if (q3_actor_get(game, missile.trigger)) {
                qa_error ignored = {0};
                (void)qa_session_release(game->options.services.session, missile.trigger, &ignored);
            }
        }
    }
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q3_actor *other = &game->actors[i];
        if (other->kind != Q3_ACTOR_MISSILE)
            continue;
        q3_missile *missile = &other->state.missile;
        if (qa_actor_id_equal(missile->attached, record.id) ||
            (missile->weapon == QA_Q3_W_GRAPPLE && qa_actor_id_equal(missile->owner, record.id))) {
            qa_actor_id release = other->actor;
            qa_error ignored = {0};
            (void)qa_session_release(game->options.services.session, release, &ignored);
        } else if (qa_actor_id_equal(missile->trigger, record.id))
            missile->trigger = (qa_actor_id){0};
    }
    q3_source_actor_released(game, record.id);
}
static bool component_begin(void *context, qa_session *session, const qa_source_frame *frame,
                            qa_error *error) {
    (void)session;
    qa_q3_game *game = context;
    if (game->source_restored)
        return q3_fail(error, "Q3 source restoration is pending");
    game->previous_ms = game->now_ms;
    game->now_ms = (int32_t)(uint32_t)(frame->time_ns / UINT64_C(1000000));
    memset(game->source_actor_ran, 0, sizeof(game->source_actor_ran));
    if (game->options.hooks.source_settings_update &&
        !game->options.hooks.source_settings_update(game->options.hooks.context, frame, error))
        return false;
    return q3_map_frame_begin(game, error);
}
static bool component_actor(void *context, qa_session *session, qa_actor_id actor,
                            const qa_source_frame *frame, qa_error *error) {
    (void)session;
    qa_q3_game *game = context;
    uint32_t slot;
    return qa_q3_source_actor_slot(game, actor, &slot, NULL)
        ? qa_q3_source_run_actor(game, actor, frame, error)
        : qa_q3_actor_frame(game, actor, error);
}
bool qa_q3_source_run_actor(qa_q3_game *game, qa_actor_id actor,
                            const qa_source_frame *frame, qa_error *error) {
    uint32_t slot;
    qa_source_frame active;
    if (!game || !frame || frame->provider != game->options.owner || frame->kind != QA_RULESET_Q3 ||
        frame->phase != QA_ENTITY_PHYSICS ||
        !qa_session_active_frame(game->options.services.session, frame->provider, &active) ||
        active.number != frame->number || active.time_ns != frame->time_ns ||
        !qa_q3_source_actor_slot(game, actor, &slot, error))
        return q3_fail(error, "Q3 actor dispatch needs its genuine native source frame");
    if (!game->source_entities[slot].in_use || game->source_actor_ran[slot]) return true;
    game->source_actor_ran[slot] = true;
    q3_wire_event_status event_status;
    if (!q3_wire_expire_events(game, actor, &event_status, error)) return false;
    if (event_status != Q3_WIRE_EVENT_ACTIVE) return true;
    if (slot < QA_Q3_SOURCE_CLIENTS && game->options.hooks.source_client_run &&
        !game->options.hooks.source_client_run(game->options.hooks.context, actor, frame, error))
        return false;
    if (!qa_q3_source_actor_slot(game, actor, &slot, NULL)) return true;
    return qa_q3_actor_frame(game, actor, error);
}
static bool component_end(void *context, qa_session *session, const qa_source_frame *frame,
                           qa_error *error) {
    qa_q3_game *game = context;
    if (!frame || session != game->options.services.session ||
        frame->provider != game->options.owner || frame->kind != QA_RULESET_Q3 ||
        frame->phase != QA_CLIENT_END_FRAME || game->source_restored)
        return q3_fail(error, "Q3 frame tail requires its actual source END phase");
    if (game->options.hooks.source_client_end)
        for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
            qa_actor_id actor = game->source_entities[slot].actor;
            if (!game->source_entities[slot].in_use || !actor.registry ||
                !qa_actors_get(qa_session_actors(session), actor)) continue;
            if (!game->options.hooks.source_client_end(
                    game->options.hooks.context, actor, frame, error)) return false;
        }
    return !game->options.hooks.source_end_frame || game->options.hooks.source_end_frame(
        game->options.hooks.context, frame, error);
}
static void component_release(void *context, qa_session *session, qa_actor_record actor) {
    (void)session;
    qa_q3_actor_released(context, actor);
}
static bool component_command_actor(void *context, qa_session *session, qa_actor_id actor) {
    qa_q3_game *game = context;
    uint32_t slot;
    return game && !game->source_restored && session == game->options.services.session &&
        qa_q3_native_client_slot(game, actor, &slot, NULL);
}
qa_component qa_q3_component(qa_q3_game *game) {
    return (qa_component){.owner = game->options.owner,
                          .clock = qa_clock_defaults(QA_RULESET_Q3),
                          .state = game,
                          .begin_frame = component_begin,
                          .actor_frame = component_actor,
                          .end_frame = component_end,
                          .command_actor = component_command_actor,
                          .actor_released = component_release};
}

bool qa_q3_combat_policy(qa_q3_game *game, qa_combat_policy *out, qa_error *error) {
    if (!game || !out)
        return q3_fail(error, "missing Q3 provider or combat policy output");
    *out = (qa_combat_policy){.provider = game->options.owner,
                              .family = QA_GAME_Q3,
                              .context = game,
                              .describe = q3_combat_describe};
    return true;
}
