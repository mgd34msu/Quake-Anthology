#include "internal.h"

bool q3_map_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}

static void runtime_free(q3_map_runtime *map) {
    if (!map)
        return;
    free(map->actors);
    free(map);
}

static bool runtime_create(qa_q3_game *game, const qa_q3_map_options *options,
                           q3_map_runtime **out, qa_error *error) {
    if (!game || !options || !options->targets || !options->event || !out)
        return q3_map_fail(error, "invalid Q3 authored map options");
    q3_map_runtime *map = calloc(1, sizeof(*map));
    if (!map) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 map runtime");
        return false;
    }
    map->actors = calloc(game->capacity, sizeof(*map->actors));
    if (!map->actors) {
        runtime_free(map);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 map actor state");
        return false;
    }
    map->options = *options;
    map->capacity = game->capacity;
    size_t item_count;
    (void)qa_q3_items(game->options.product, &item_count);
    if (item_count > 64) {
        runtime_free(map);
        return q3_map_fail(error, "Q3 item registry exceeds its native bitset");
    }
    static const char *const defaults[] = {"weapon_machinegun", "weapon_gauntlet"};
    for (size_t i = 0; i < sizeof(defaults) / sizeof(*defaults); ++i) {
        uint32_t item;
        if (!qa_q3_find_item(game->options.product, defaults[i], &item)) {
            runtime_free(map);
            return q3_map_fail(error, "Q3 default item is absent from its product catalog");
        }
        map->registered_items |= UINT64_C(1) << item;
    }
    if (game->options.product == QA_Q3_TEAM_ARENA && game->options.rules.game_type == 7) {
        static const char *const cubes[] = {"item_redcube", "item_bluecube"};
        for (size_t i = 0; i < sizeof(cubes) / sizeof(*cubes); ++i) {
            uint32_t item;
            if (!qa_q3_find_item(game->options.product, cubes[i], &item)) {
                runtime_free(map);
                return q3_map_fail(error, "Q3 Harvester item is absent from its product catalog");
            }
            map->registered_items |= UINT64_C(1) << item;
        }
    }
    *out = map;
    return true;
}

static bool level_state_idle(qa_q3_game *game, bool require_empty, qa_error *error) {
    if (!qa_q3_destroy_ready(game) ||
        (require_empty &&
         qa_actors_count(qa_session_actors(game->options.services.session)) != 0))
        return q3_map_fail(error, "Q3 map transition requires a retired safe world");
    for (q3_snapshot_frame *frame = game->snapshot_frames; frame; frame = frame->next)
        if (frame->active)
            return q3_map_fail(error, "Q3 map transition has an active spatial snapshot");
    return true;
}

static bool provider_state_empty(const qa_q3_game *game) {
    for (uint32_t i = 0; i < game->capacity; ++i)
        if (game->actors[i].kind || game->kamikaze_cooldowns[i].actor.registry ||
            game->player_binding_tokens[i])
            return false;
    return true;
}

static void level_state_reset(qa_q3_game *game, const qa_q3_map_options *options) {
    game->rng = options->random_seed;
    game->death_animation = 0;
    game->body_queue_index = 0;
    game->ranking_hit = (qa_q3_ranking_hit){0};
    memset(game->body_queue, 0, sizeof(game->body_queue));
    memset(game->actors, 0, game->capacity * sizeof(*game->actors));
    memset(game->kamikaze_cooldowns, 0,
           game->capacity * sizeof(*game->kamikaze_cooldowns));
    memset(game->player_binding_tokens, 0,
           game->capacity * sizeof(*game->player_binding_tokens));
    game->previous_ms = options->start_time_ms;
    game->now_ms = options->start_time_ms;
    game->physics.gravity = 800;
    for (q3_snapshot_frame *frame = game->snapshot_frames; frame; frame = frame->next)
        frame->snapshot.count = 0;
}

bool qa_q3_maps_bind(qa_q3_game *game, const qa_q3_map_options *options, qa_error *error) {
    if (!game || game->source_restored || game->map || !provider_state_empty(game))
        return q3_map_fail(error, "invalid Q3 authored map binding");
    if (!level_state_idle(game, false, error))
        return false;
    q3_map_runtime *map;
    if (!runtime_create(game, options, &map, error))
        return false;
    game->map = map;
    level_state_reset(game, options);
    return true;
}

bool qa_q3_maps_reset(qa_q3_game *game, const qa_q3_map_options *options,
                      qa_error *error) {
    if (!game || game->source_restored || !game->map)
        return q3_map_fail(error, "invalid Q3 authored map reset");
    if (!level_state_idle(game, true, error))
        return false;
    q3_map_runtime *map;
    if (!runtime_create(game, options, &map, error))
        return false;
    q3_map_destroy(game);
    game->map = map;
    level_state_reset(game, options);
    return true;
}

bool q3_map_register_item(qa_q3_game *game, uint32_t item, qa_error *error) {
    size_t count;
    if (!game || !game->map)
        return q3_map_fail(error, "missing Q3 map item registry");
    (void)qa_q3_items(game->options.product, &count);
    if (!item || item >= count || item >= 64)
        return q3_map_fail(error, "invalid Q3 registered item index");
    game->map->registered_items |= UINT64_C(1) << item;
    return true;
}

qa_q3_map_actor_state *q3_map_get(qa_q3_game *game, qa_actor_id actor) {
    if (!game || !game->map || actor.slot >= game->map->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return NULL;
    qa_q3_map_actor_state *state = &game->map->actors[actor.slot];
    return state->active && qa_actor_id_equal(state->actor, actor) ? state : NULL;
}

const qa_q3_map_actor_state *q3_map_const(const qa_q3_game *game, qa_actor_id actor) {
    if (!game || !game->map || actor.slot >= game->map->capacity ||
        !qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return NULL;
    const qa_q3_map_actor_state *state = &game->map->actors[actor.slot];
    return state->active && qa_actor_id_equal(state->actor, actor) ? state : NULL;
}

bool q3_map_text(const qa_q3_game *game, qa_string_id text) {
    return text && qa_strings_text(qa_session_strings(game->options.services.session), text).size;
}

const char *q3_map_cstr(const qa_q3_game *game, qa_string_id text) {
    return qa_strings_cstr(qa_session_strings(game->options.services.session), text);
}

bool q3_map_intern_cstr(qa_q3_game *game, const char *text, qa_string_id *out,
                        qa_error *error) {
    return qa_strings_intern_cstr(qa_session_strings(game->options.services.session),
                                  text ? text : "", out, error);
}

bool q3_map_emit(qa_q3_game *game, const qa_q3_map_event *event, qa_error *error) {
    if (!game || !game->map || !event)
        return q3_map_fail(error, "invalid Q3 map event");
    return game->map->options.event(game->map->options.context, event, error);
}

void q3_map_warn(qa_q3_game *game, qa_actor_id actor, const char *message) {
    if (game && game->map && game->map->options.diagnostic)
        game->map->options.diagnostic(game->map->options.context, actor, message);
}

bool q3_map_schedule(qa_q3_game *game, qa_q3_map_actor_state *state, int32_t delay,
                     qa_q3_map_think think) {
    if (!game || !state || !state->active)
        return false;
    state->due_ms = q3_add_time(game->now_ms, delay);
    state->think = think;
    return true;
}

static bool target_read(void *context, qa_actor_id actor, qa_authored_target *out) {
    qa_q3_game *game = context;
    const qa_q3_map_actor_state *state = q3_map_const(game, actor);
    if (!state || !out)
        return false;
    *out = (qa_authored_target){.classname = state->classname,
                                .targetname = state->targetname,
                                .target = state->target,
                                .message = state->message,
                                .shader_old = state->shader_old,
                                .shader_new = state->shader_new,
                                .delay_seconds = state->delay,
                                .wait_seconds = state->wait};
    return true;
}

static bool target_use(void *context, qa_actor_id actor, qa_actor_id other,
                       qa_actor_id activator, qa_error *error) {
    return qa_q3_map_use(context, actor, other, activator, error);
}

static bool target_set_targetname(void *context, qa_actor_id actor, qa_string_id value,
                                  qa_error *error) {
    qa_q3_game *game = context;
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state)
        return q3_map_fail(error, "missing Q3 authored targetname owner");
    qa_bytes text = qa_strings_text(qa_session_strings(game->options.services.session), value);
    qa_string_id folded = 0;
    if (text.size && !q3_map_intern_fold(game, text, &folded, error))
        return false;
    state->targetname = folded;
    return true;
}

static bool target_set_target(void *context, qa_actor_id actor, qa_string_id value,
                              qa_error *error) {
    qa_q3_game *game = context;
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state)
        return q3_map_fail(error, "missing Q3 authored target owner");
    qa_bytes text = qa_strings_text(qa_session_strings(game->options.services.session), value);
    qa_string_id folded = 0;
    if (text.size && !q3_map_intern_fold(game, text, &folded, error))
        return false;
    state->target = folded;
    if (state->kind == QA_Q3_MAP_ITEM)
        state->item.target = folded;
    return true;
}

static bool target_field(void *context, qa_actor_id actor, const char *key,
                         qa_target_field *out) {
    qa_q3_game *game = context;
    const qa_q3_map_actor_state *state = q3_map_const(game, actor);
    if (!state || !key || !out)
        return false;
    static const struct {
        const char *name;
        size_t offset;
    } texts[] = {{"classname", offsetof(qa_q3_map_actor_state, classname)},
                 {"targetname", offsetof(qa_q3_map_actor_state, targetname)},
                 {"target", offsetof(qa_q3_map_actor_state, target)},
                 {"message", offsetof(qa_q3_map_actor_state, message)},
                 {"team", offsetof(qa_q3_map_actor_state, team)},
                 {"model", offsetof(qa_q3_map_actor_state, model)},
                 {"model2", offsetof(qa_q3_map_actor_state, model2)}},
      numbers[] = {{"speed", offsetof(qa_q3_map_actor_state, speed)},
                   {"wait", offsetof(qa_q3_map_actor_state, wait)},
                   {"random", offsetof(qa_q3_map_actor_state, random)},
                   {"roll", offsetof(qa_q3_map_actor_state, roll)}};
    for (size_t i = 0; i < sizeof(texts) / sizeof(*texts); ++i)
        if (!strcmp(key, texts[i].name)) {
            const qa_string_id *value = (const void *)((const char *)state + texts[i].offset);
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = *value};
            return true;
        }
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); ++i)
        if (!strcmp(key, numbers[i].name)) {
            const float *value = (const void *)((const char *)state + numbers[i].offset);
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = *value};
            return true;
        }
    if (!strcmp(key, "spawnflags")) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = state->spawnflags};
        return true;
    }
    if (!strcmp(key, "count") || !strcmp(key, "health") || !strcmp(key, "dmg")) {
        int32_t value = !strcmp(key, "count") ? state->count
                        : !strcmp(key, "health") ? state->health
                                                  : state->damage;
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = value};
        return true;
    }
    if (!strcmp(key, "origin") || !strcmp(key, "angles") || !strcmp(key, "movedir")) {
        qa_vec3 value = !strcmp(key, "origin") ? state->origin
                        : !strcmp(key, "angles") ? state->angles
                                                  : state->direction;
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR, .value.vector = value};
        return true;
    }
    return false;
}

bool q3_map_target_binding(qa_q3_game *game, qa_actor_id actor, qa_target_binding *out) {
    if (!out || !q3_map_get(game, actor))
        return false;
    *out = (qa_target_binding){.actor = actor,
                                 .source = QA_CLOCK_Q3,
                                 .context = game,
                                 .read = target_read,
                                 .use = target_use,
                                 .set_targetname = target_set_targetname,
                                 .set_target = target_set_target,
                                 .field = target_field};
    return true;
}
bool q3_map_bind_target(qa_q3_game *game, qa_q3_map_actor_state *state, qa_error *error) {
    qa_target_binding binding;
    return q3_map_target_binding(game, state->actor, &binding) &&
           qa_targets_bind(game->map->options.targets, &binding, error);
}

bool q3_map_use_targets(qa_q3_game *game, qa_q3_map_actor_state *state,
                        qa_actor_id activator, qa_error *error) {
    return qa_targets_use(game->map->options.targets, state->actor, activator,
                          (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000), error);
}

qa_vec3 q3_map_direction(qa_vec3 angles) {
    if (angles.x == 0 && angles.z == 0 && angles.y == -1)
        return qa_v3(0, 0, 1);
    if (angles.x == 0 && angles.z == 0 && angles.y == -2)
        return qa_v3(0, 0, -1);
    qa_vec3 forward;
    q3_source_angle_vectors(angles, &forward, NULL, NULL);
    return forward;
}

bool q3_map_is_player(qa_q3_game *game, qa_actor_id actor) { return q3_is_player(game, actor); }

bool q3_map_player_launchable(qa_q3_game *game, qa_actor_id actor, q3_actor **native) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (native)
        *native = entry && entry->kind == Q3_ACTOR_PLAYER ? entry : NULL;
    if (entry && entry->kind == Q3_ACTOR_PLAYER) {
        const qa_q3_player_state *player = &entry->state.player;
        return !player->dead && !player->spectator && !player->noclip &&
               !player->powerups[QA_Q3_P_FLIGHT];
    }
    qa_builtin_actor_traits traits = {0};
    if (!game || !game->options.services.actor_traits ||
        !game->options.services.actor_traits(game->options.services.context, actor, &traits) ||
        !traits.player || traits.spectator)
        return false;
    qa_combat_state combat;
    qa_error ignored = {0};
    return qa_combat_read(game->options.services.combat, actor, &combat, &ignored) &&
           combat.health > 0;
}

int32_t q3_map_team(qa_q3_game *game, qa_actor_id actor) {
    return game->options.hooks.source_team
               ? game->options.hooks.source_team(game->options.hooks.context, actor)
               : 0;
}

bool q3_map_set_velocity(qa_q3_game *game, qa_actor_id actor, qa_vec3 velocity,
                         qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor))
        return true;
    body.velocity = velocity;
    if (!qa_world_body_write(game->options.services.world, actor, &body, error))
        return false;
    if (!qa_actors_get(qa_session_actors(game->options.services.session), actor) ||
        !game->options.services.motion_changed)
        return true;
    qa_builtin_motion_change change = {
        .reason = QA_BUILTIN_MOTION_LAUNCH, .body = body};
    return game->options.services.motion_changed(game->options.services.context, actor, &change,
                                                  error);
}

bool q3_map_target_pose(qa_q3_game *game, qa_actor_id actor, qa_vec3 *origin,
                        qa_vec3 *angles, qa_error *error) {
    qa_targets *targets = game && game->map ? game->map->options.targets : NULL;
    if (!targets)
        return q3_map_fail(error, "missing Q3 authored target router");
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    *origin = body.origin;
    *angles = body.angles;
    qa_target_field field;
    if (qa_targets_field(targets, actor, "origin", &field) &&
        field.kind == QA_TARGET_FIELD_VECTOR)
        *origin = field.value.vector;
    if (qa_targets_field(targets, actor, "angles", &field) &&
        field.kind == QA_TARGET_FIELD_VECTOR)
        *angles = field.value.vector;
    return true;
}

bool q3_map_pick(qa_q3_game *game, qa_string_id target, qa_actor_id *out, qa_error *error) {
    if (!q3_map_text(game, target)) {
        *out = (qa_actor_id){0};
        return true;
    }
    qa_actor_id first;
    if (!qa_targets_first(game->map->options.targets, target, &first)) {
        *out = (qa_actor_id){0};
        q3_map_warn(game, (qa_actor_id){0}, "Q3 target name has no live destination");
        return true;
    }
    if (!qa_targets_pick(game->map->options.targets, target, q3_rand(game), 32, out))
        return q3_map_fail(error, "Q3 target selection changed without a callback");
    return true;
}

static bool allocate_actor(qa_q3_game *game, qa_q3_map_actor_state *source,
                           const qa_actor_collision *collision, bool link, bool authored,
                           qa_error *error) {
    qa_builtin_spawn spawn = {
        .owner = game->options.owner,
        .definition = source->classname,
        .has_source = authored,
        .source_slot = source->ordinal,
        .body = {.origin = source->origin, .angles = source->angles, .bounds = source->bounds},
        .collision = collision,
        .link = link};
    qa_actor_id actor;
    if (!qa_builtin_spawn_actor(&game->options.services, &spawn, &actor, error))
        return false;
    if (actor.slot >= game->map->capacity) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q3 map actor exceeds runtime capacity");
        return q3_rollback_spawn(game, actor, error);
    }
    source->actor = actor;
    source->active = true;
    source->alpha = 1;
    source->linked = link;
    game->map->actors[actor.slot] = *source;
    qa_q3_map_actor_state *state = &game->map->actors[actor.slot];
    if (!q3_map_bind_target(game, state, error))
        return q3_rollback_spawn(game, actor, error);
    *source = *state;
    return true;
}

bool q3_map_allocate(qa_q3_game *game, qa_q3_map_actor_state *source,
                     const qa_actor_collision *collision, bool link, qa_error *error) {
    return allocate_actor(game, source, collision, link, true, error);
}

bool q3_map_allocate_generated(qa_q3_game *game, qa_q3_map_actor_state *source,
                               const qa_actor_collision *collision, bool link,
                               qa_error *error) {
    return allocate_actor(game, source, collision, link, false, error);
}

static bool map_use(qa_q3_game *game, qa_actor_id actor, qa_actor_id other,
                      qa_actor_id activator, qa_error *error) {
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state)
        return true;
    if (state->kind == QA_Q3_MAP_ITEM)
        return q3_map_item_use(game, state, error);
    if (state->kind >= QA_Q3_MAP_TARGET_GIVE &&
        state->kind <= QA_Q3_MAP_TARGET_POSITION)
        return q3_map_target_use(game, state, other, activator, error);
    if (state->kind >= QA_Q3_MAP_TRIGGER_MULTIPLE && state->kind <= QA_Q3_MAP_TIMER)
        return q3_map_trigger_use(game, state, other, activator, error);
    if (state->kind >= QA_Q3_MAP_MOVER_DOOR &&
        state->kind <= QA_Q3_MAP_MOVER_PENDULUM)
        return state->usable ? qa_q3_use_mover(game, state->actor, activator, error) : true;
    return q3_map_misc_use(game, state, other, activator, error);
}
bool qa_q3_map_use(qa_q3_game *game, qa_actor_id actor, qa_actor_id other,
                   qa_actor_id activator, qa_error *error) {
    if (!game || game->source_restored || game->observation_depth == SIZE_MAX)
        return q3_map_fail(error, "invalid Q3 authored use boundary");
    ++game->observation_depth;
    bool okay = map_use(game, actor, other, activator, error);
    --game->observation_depth;
    return okay;
}

bool q3_map_frame_begin(qa_q3_game *game, qa_error *error) {
    (void)game;
    (void)error;
    return true;
}

bool q3_map_actor_frame(qa_q3_game *game, qa_actor_id actor, bool *handled,
                        qa_error *error) {
    if (handled)
        *handled = false;
    qa_q3_map_actor_state *state = q3_map_get(game, actor);
    if (!state)
        return true;
    q3_actor *native = q3_actor_get(game, actor);
    if (handled)
        *handled = !native;
    if (state->think == QA_Q3_MAP_THINK_NONE || state->due_ms <= 0 ||
        q3_sub_time(game->now_ms, state->due_ms) < 0)
        return true;
    state->due_ms = 0;
    if (state->kind == QA_Q3_MAP_ITEM)
        return q3_map_item_think(game, state, error);
    if (state->kind >= QA_Q3_MAP_TARGET_GIVE &&
        state->kind <= QA_Q3_MAP_TARGET_POSITION)
        return q3_map_target_think(game, state, error);
    if (state->kind >= QA_Q3_MAP_TRIGGER_MULTIPLE && state->kind <= QA_Q3_MAP_TIMER)
        return q3_map_trigger_think(game, state, error);
    if (state->kind >= QA_Q3_MAP_MOVER_DOOR)
        return q3_map_mover_think(game, state, error);
    return q3_map_misc_think(game, state, error);
}

bool q3_map_touch(qa_q3_game *game, const qa_touch_contact *contact, bool *handled,
                  qa_error *error) {
    if (handled)
        *handled = false;
    qa_q3_map_actor_state *state = q3_map_get(game, contact->self);
    if (!state || state->kind == QA_Q3_MAP_ITEM)
        return true;
    if (handled)
        *handled = state->touchable;
    if (!state->touchable)
        return true;
    return state->kind >= QA_Q3_MAP_MOVER_DOOR
               ? q3_map_mover_touch(game, state, contact, error)
               : q3_map_trigger_touch(game, state, contact, error);
}

void q3_map_actor_released(qa_q3_game *game, qa_actor_record record) {
    if (!game || !game->map || record.id.slot >= game->map->capacity)
        return;
    qa_q3_map_actor_state *state = &game->map->actors[record.id.slot];
    if (!state->active || !qa_actor_id_equal(state->actor, record.id))
        return;
    qa_q3_map_actor_state retired = *state;
    qa_actor_id successor = retired.team_next;
    bool promote = !retired.team_slave && successor.registry;
    if (retired.damageable) {
        qa_error ignored = {0};
        (void)qa_combat_set_admission(game->options.services.combat,
                                      retired.actor, NULL, &ignored);
    }
    qa_targets_unbind_context(game->map->options.targets, record.id, game);
    *state = (qa_q3_map_actor_state){0};
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        qa_q3_map_actor_state *other = &game->map->actors[i];
        if (!other->active)
            continue;
        if (qa_actor_id_equal(other->activator, record.id))
            other->activator = (qa_actor_id){0};
        if (qa_actor_id_equal(other->enemy, record.id))
            other->enemy = (qa_actor_id){0};
        if (qa_actor_id_equal(other->team_next, record.id))
            other->team_next = successor;
        if (qa_actor_id_equal(other->team_master, record.id)) {
            other->team_master = promote ? successor : (qa_actor_id){0};
            other->team_slave = promote && !qa_actor_id_equal(other->actor, successor);
        }
        if (promote && qa_actor_id_equal(other->actor, successor)) {
            other->team_master = successor;
            other->team_slave = false;
        }
        if (qa_actor_id_equal(other->parent, record.id)) {
            qa_actor_id child = other->actor;
            other->parent = (qa_actor_id){0};
            qa_error ignored = {0};
            (void)qa_session_release(game->options.services.session, child, &ignored);
            continue;
        }
        if (qa_actor_id_equal(other->path_next, record.id))
            other->path_next = (qa_actor_id){0};
        if (other->kind >= QA_Q3_MAP_MOVER_DOOR &&
            other->kind <= QA_Q3_MAP_MOVER_PENDULUM) {
            qa_error ignored = {0};
            (void)q3_map_mover_sync_state(game, other, &ignored);
        }
    }
}

void q3_map_mover_presentation(const qa_q3_game *game, qa_actor_id actor,
                               const char **model, const char **secondary,
                               uint32_t *constant_light) {
    const qa_q3_map_actor_state *state = q3_map_const(game, actor);
    if (model)
        *model = state ? q3_map_cstr(game, state->model) : NULL;
    if (secondary)
        *secondary = state ? q3_map_cstr(game, state->model2) : NULL;
    if (constant_light)
        *constant_light = 0;
    if (!state || (!state->has_light && !state->has_color) || !constant_light)
        return;
    int32_t red = q3_map_float_to_int(fminf(255, state->color.x * 255.0f));
    int32_t green = q3_map_float_to_int(fminf(255, state->color.y * 255.0f));
    int32_t blue = q3_map_float_to_int(fminf(255, state->color.z * 255.0f));
    int32_t intensity = q3_map_float_to_int(fminf(255, state->light / 4.0f));
    *constant_light = (uint32_t)red | ((uint32_t)green << 8) |
                      ((uint32_t)blue << 16) | ((uint32_t)intensity << 24);
}

void q3_map_destroy(qa_q3_game *game) {
    if (!game || !game->map)
        return;
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        qa_q3_map_actor_state *state = &game->map->actors[i];
        if (state->active) {
            if (state->damageable) {
                qa_error ignored = {0};
                (void)qa_combat_set_admission(game->options.services.combat,
                                              state->actor, NULL, &ignored);
            }
            qa_targets_unbind_context(game->map->options.targets, state->actor, game);
        }
    }
    runtime_free(game->map);
    game->map = NULL;
}

bool qa_q3_map_spawnpoint_next(const qa_q3_game *game, uint32_t *cursor,
                               qa_q3_map_spawnpoint *out) {
    if (!game || !game->map || !cursor || !out)
        return false;
    while (*cursor < game->map->capacity) {
        const qa_q3_map_actor_state *state = &game->map->actors[(*cursor)++];
        if (!state->active || state->kind != QA_Q3_MAP_POINT)
            continue;
        const char *name = q3_map_cstr(game, state->classname);
        if (!name)
            continue;
        qa_q3_spawnpoint_kind kind;
        if (!strcmp(name, "info_player_start"))
            kind = QA_Q3_SPAWN_START;
        else if (!strcmp(name, "info_player_deathmatch"))
            kind = QA_Q3_SPAWN_DEATHMATCH;
        else if (!strcmp(name, "info_player_intermission"))
            kind = QA_Q3_SPAWN_INTERMISSION;
        else if (!strcmp(name, "team_CTF_redplayer"))
            kind = QA_Q3_SPAWN_RED_PLAYER;
        else if (!strcmp(name, "team_CTF_blueplayer"))
            kind = QA_Q3_SPAWN_BLUE_PLAYER;
        else if (!strcmp(name, "team_CTF_redspawn"))
            kind = QA_Q3_SPAWN_RED;
        else if (!strcmp(name, "team_CTF_bluespawn"))
            kind = QA_Q3_SPAWN_BLUE;
        else
            continue;
        *out = (qa_q3_map_spawnpoint){.actor = state->actor,
                                      .kind = kind,
                                      .origin = state->origin,
                                      .angles = state->angles,
                                      .flags = state->spawnflags,
                                      .ordinal = state->ordinal,
                                      .no_bots = state->no_bots,
                                      .no_humans = state->no_humans};
        return true;
    }
    return false;
}

bool qa_q3_map_nearest_location(const qa_q3_game *game, qa_vec3 origin,
                                qa_actor_id *actor, qa_string_id *message) {
    if (!game || !game->map || !qa_vec_finite(origin) || !actor || !message)
        return false;
    float nearest = INFINITY;
    uint32_t best_ordinal = 0;
    qa_actor_id found = {0};
    qa_string_id text = 0;
    for (uint32_t i = 0; i < game->map->capacity; ++i) {
        const qa_q3_map_actor_state *state = &game->map->actors[i];
        if (!state->active || state->kind != QA_Q3_MAP_TARGET_LOCATION)
            continue;
        float distance = qa_vec_length(qa_vec_sub(state->origin, origin));
        if (distance < nearest || (distance == nearest && state->ordinal > best_ordinal)) {
            nearest = distance;
            best_ordinal = state->ordinal;
            found = state->actor;
            text = state->message;
        }
    }
    *actor = found;
    *message = text;
    return found.registry != 0;
}
