#include "internal.h"
#include <errno.h>
#include <limits.h>

bool q1_map_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
bool qa_q1_game_maps_bind(qa_q1_game *g, const qa_q1_map_options *options, qa_error *error) {
    if (!g || !options || g->maps || !options->targets || !options->level ||
        !options->server_flags || !options->static_model || !options->ambient ||
        !options->lightstyle || !options->set_skill || !options->secret_found ||
        !g->services.actor_traits || !g->services.physics || !g->services.physics->services.read ||
        !g->services.physics->services.write || !g->services.motion_changed ||
        (!!options->path_read != !!options->path_change))
        return q1_map_fail(error, "invalid Q1 map service binding");
    g->maps = calloc(1, sizeof(*g->maps));
    if (!g->maps) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 map runtime");
        return false;
    }
    g->maps->options = *options;
    g->maps->lightning_end = -1;
    return true;
}
static bool target_read(void *context, qa_actor_id actor, qa_authored_target *out) {
    return qa_q1_game_authored_target(context, actor, out);
}
static bool target_use(void *context, qa_actor_id actor, qa_actor_id other, qa_actor_id activator,
                       qa_error *error) {
    return qa_q1_game_use_from(context, actor, other, activator, error);
}
static bool target_set_target(void *context, qa_actor_id actor, qa_string_id value,
                              qa_error *error) {
    q1_actor *entity = q1_entity(context, actor);
    if (!entity || !entity->native)
        return q1_map_fail(error, "Q1 target field owner is unavailable");
    entity->target = value;
    return true;
}
static bool target_set_targetname(void *context, qa_actor_id actor, qa_string_id value,
                                  qa_error *error) {
    q1_actor *entity = q1_entity(context, actor);
    if (!entity || !entity->native)
        return q1_map_fail(error, "Q1 targetname field owner is unavailable");
    entity->targetname = value;
    return true;
}
static bool target_field(void *context, qa_actor_id actor, const char *key, qa_target_field *out) {
    qa_q1_game *g = context;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity)
        return false;
    static const struct {
        const char *name;
        size_t offset;
    } strings[] = {{"classname", offsetof(q1_actor, classname)},
                   {"targetname", offsetof(q1_actor, targetname)},
                   {"target", offsetof(q1_actor, target)},
                   {"killtarget", offsetof(q1_actor, killtarget)},
                   {"message", offsetof(q1_actor, message)}},
      numbers[] = {{"speed", offsetof(q1_actor, speed)},
                   {"wait", offsetof(q1_actor, wait)},
                   {"delay", offsetof(q1_actor, delay)}};
    for (size_t i = 0; i < sizeof(strings) / sizeof(*strings); ++i)
        if (!strcmp(key, strings[i].name)) {
            const qa_string_id *value = (const void *)((const char *)entity + strings[i].offset);
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = *value};
            return true;
        }
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); ++i)
        if (!strcmp(key, numbers[i].name)) {
            const float *value = (const void *)((const char *)entity + numbers[i].offset);
            *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = *value};
            return true;
        }
    if (!strcmp(key, "spawnflags")) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = entity->spawnflags};
        return true;
    }
    if (entity->map && !strcmp(key, "style")) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = entity->map->style};
        return true;
    }
    if (entity->map && !strcmp(key, "event")) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = entity->map->event};
        return true;
    }
    if (entity->map && !strcmp(key, "mangle")) {
        *out =
            (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR, .value.vector = entity->map->mangle};
        return true;
    }
    if (entity->map && entity->map->has_view_offset && !strcmp(key, "view_ofs")) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR,
                                 .value.vector = entity->map->view_offset};
        return true;
    }
    if (entity->kind == Q1_MONSTER && !strcmp(key, "wetsuit_time")) {
        *out = (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER,
                                 .value.number = entity->state.monster.follow_until};
        return true;
    }
    return false;
}
bool q1_map_bind_target(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!g->maps || !entity->native)
        return true;
    qa_target_binding binding = {.actor = entity->id,
                                 .context = g,
                                 .source = g->options.quakeworld ? QA_CLOCK_QUAKEWORLD
                                                                 : QA_CLOCK_NETQUAKE,
                                 .read = target_read,
                                 .use = target_use,
                                 .field = target_field,
                                 .set_target = target_set_target,
                                 .set_targetname = target_set_targetname};
    return qa_targets_bind(g->maps->options.targets, &binding, error);
}
qa_string_id qa_q1_game_map_name(const qa_q1_game *g) {
    return g && g->maps ? g->maps->options.current_map : QA_STRING_NONE;
}
uint32_t qa_q1_game_campaign_flags(const qa_q1_game *g) {
    return g && g->maps ? *g->maps->options.server_flags : 0;
}
q1_map_state *q1_map_allocate(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->map)
        return entity->map;
    if (!g->maps) {
        q1_map_fail(error, "Q1 authored map services are not bound");
        return NULL;
    }
    q1_map_state *state = g->maps->spare;
    if (state) {
        g->maps->spare = state->pool_next;
        q1_map_state *allocated = state->allocated_next;
        *state = (q1_map_state){.allocated_next = allocated};
    } else {
        state = calloc(1, sizeof(*state));
        if (!state) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 map actor");
            return NULL;
        }
        state->allocated_next = g->maps->allocated;
        g->maps->allocated = state;
    }
    entity->map = state;
    return state;
}
void q1_map_actor_released(qa_q1_game *g, q1_actor *entity) {
    if (g->maps && entity->native)
        qa_targets_unbind_context(g->maps->options.targets, entity->id, g);
    if (!entity->map)
        return;
    entity->map->pool_next = g->maps->retired;
    g->maps->retired = entity->map;
    entity->map = NULL;
}
void q1_map_frame_begin(qa_q1_game *g) {
    if (!g->maps)
        return;
    while (g->maps->retired) {
        q1_map_state *state = g->maps->retired;
        g->maps->retired = state->pool_next;
        state->pool_next = g->maps->spare;
        g->maps->spare = state;
    }
}
void q1_map_destroy(qa_q1_game *g) {
    if (!g->maps)
        return;
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *entity = g->actors[i];
        if (entity && entity->active && entity->native) {
            qa_targets_unbind_context(g->maps->options.targets, entity->id, g);
            entity->map = NULL;
        }
    }
    q1_map_state *state = g->maps->allocated;
    while (state) {
        q1_map_state *next = state->allocated_next;
        free(state);
        state = next;
    }
    q1_door_group *group = g->maps->door_groups;
    while (group) {
        q1_door_group *next = group->next;
        free(group->members);
        free(group);
        group = next;
    }
    free(g->maps);
    g->maps = NULL;
}
bool q1_map_clone(qa_q1_game *g, const q1_actor *source, q1_actor *destination, qa_error *error) {
    if (!source->map)
        return true;
    q1_map_state *copy = q1_map_allocate(g, destination, error);
    if (!copy)
        return false;
    q1_map_state *allocated = copy->allocated_next;
    *copy = *source->map;
    copy->allocated_next = allocated;
    copy->pool_next = NULL;
    if (copy->action == Q1_MAP_DELAYED_USE)
        copy->pending.delayed.source = destination->id;
    return true;
}
bool q1_map_collision(const q1_actor *entity, qa_actor_collision *collision) {
    if (!entity->map || !entity->map->has_inline_model || entity->physics.solid != QA_PHYSICS_BRUSH)
        return false;
    collision->inline_model = true;
    collision->model = entity->map->inline_model;
    return true;
}
bool q1_map_schedule(qa_q1_game *g, q1_actor *entity, double delay, q1_map_action action,
                     qa_error *error) {
    if (!isfinite(delay))
        return q1_map_fail(error, "invalid Q1 map think delay");
    if (entity->physics.motion == QA_PHYSICS_PUSH) {
        double deadline = (double)entity->physics.local_time_ns + delay * 1000000000.0;
        if (!isfinite(deadline) || deadline >= (double)INT64_MAX || deadline <= (double)INT64_MIN)
            return q1_map_fail(error, "Q1 local map deadline overflow");
        entity->physics.next_think_ns = (int64_t)deadline;
        entity->think = Q1_THINK_MAP;
        entity->next_think = (double)entity->physics.next_think_ns / 1000000000.0;
    } else if (!q1_schedule(g, entity, delay, Q1_THINK_MAP, error))
        return false;
    entity->map->action = action;
    return true;
}
void q1_map_cancel(qa_q1_game *g, q1_actor *entity) {
    qa_scheduler_cancel(qa_session_scheduler(g->services.session), entity->id);
    entity->think = Q1_THINK_NONE;
    entity->next_think = -1;
    entity->physics.next_think_ns = -1;
    if (entity->map)
        entity->map->action = Q1_MAP_IDLE;
}
bool q1_map_damageable(qa_q1_game *g, q1_actor *entity, bool enabled, qa_error *error) {
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &traits, error))
        return false;
    traits.can_take_damage = enabled;
    return qa_combat_set_traits(g->services.combat, entity->id, &traits, error);
}
bool q1_map_player(qa_q1_game *g, qa_actor_id actor) {
    qa_builtin_actor_traits traits;
    return q1_alive(g, actor) && g->services.actor_traits &&
           g->services.actor_traits(g->services.context, actor, &traits) && traits.player;
}
bool q1_map_targets(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    return qa_targets_use(g->maps->options.targets, entity->id, activator, g->time_ns, error);
}
bool q1_map_ambient(qa_q1_game *g, qa_vec3 origin, const char *sound, float volume,
                    qa_error *error) {
    qa_string_id resource;
    return qa_builtin_resource(&g->services, sound, &resource, error) &&
           g->maps->options.ambient(g->maps->options.context, origin, resource, volume, 3, error);
}
bool q1_map_lightstyle(qa_q1_game *g, q1_actor *entity, const char *pattern, qa_error *error) {
    qa_string_id resource;
    return qa_builtin_resource(&g->services, pattern, &resource, error) &&
           g->maps->options.lightstyle(g->maps->options.context, entity->map->style, resource,
                                       error);
}

static bool fields(qa_q1_game *g, q1_actor *entity, const qa_q1_map_fields *source,
                   qa_error *error) {
    if (!source)
        return true;
    const float numbers[] = {source->height,       source->lip,        source->width,
                             source->length,       source->pause_time, source->volume,
                             source->duration,     source->distance,   source->next_think_seconds,
                             source->counter_value};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); ++i)
        if (!isfinite(numbers[i]))
            return q1_map_fail(error, "nonfinite Q1 authored field");
    if (!qa_vec_finite(source->mangle) || !qa_vec_finite(source->movedir) ||
        (source->has_view_offset && !qa_vec_finite(source->view_offset)))
        return q1_map_fail(error, "invalid Q1 authored direction");
    q1_map_state *state = entity->map;
    const char *input[] = {
        source->model,   source->map,    source->noise,   source->noise1,
        source->noise2,  source->noise3, source->endtext, source->intermissiontext,
        source->netname, source->event};
    qa_string_id *output[] = {
        &state->original_model, &state->map,      &state->noise[0], &state->noise[1],
        &state->noise[2],       &state->noise[3], &state->endtext,  &state->intermissiontext,
        &state->netname,        &state->event};
    for (size_t i = 0; i < sizeof(input) / sizeof(*input); ++i)
        if (input[i] && input[i][0] &&
            !qa_builtin_resource(&g->services, input[i], output[i], error))
            return false;
    entity->model = state->original_model;
    state->mangle = source->mangle;
    state->view_offset = source->view_offset;
    state->has_view_offset = source->has_view_offset;
    state->height = source->height;
    state->lip = source->lip;
    state->has_movedir = source->has_movedir;
    state->movedir = source->movedir;
    state->width = source->width;
    state->length = source->length;
    state->pause_time = source->pause_time;
    state->volume = source->volume;
    state->duration = source->duration;
    state->distance = source->distance;
    state->initial_think = source->next_think_seconds;
    state->sounds = source->sounds;
    state->style = source->style;
    state->color_map = source->color_map;
    state->impulse = source->impulse;
    state->counter_value = source->counter_value;
    state->particle_color = source->particle_color;
    if (source->model && source->model[0] == '*') {
        const char *number = source->model + 1;
        char *end;
        errno = 0;
        unsigned long model = strtoul(number, &end, 10);
        if (*number < '0' || *number > '9' || *end || errno || model > UINT32_MAX)
            return q1_map_fail(error, "invalid Q1 inline model name");
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !qa_collision_model_bounds(qa_world_geometry(g->services.world), (uint32_t)model,
                                       &body.bounds, error) ||
            !qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
        state->has_inline_model = true;
        state->inline_model = (uint32_t)model;
    }
    return true;
}
static q1_map_kind classify(const char *name) {
    static const struct {
        const char *name;
        q1_map_kind kind;
    } classes[] = {{"worldspawn", Q1_MAP_WORLD},
                   {"func_wall", Q1_MAP_WALL},
                   {"func_door", Q1_MAP_DOOR},
                   {"func_button", Q1_MAP_BUTTON},
                   {"func_door_secret", Q1_MAP_SECRET_DOOR},
                   {"func_plat", Q1_MAP_PLAT},
                   {"func_train", Q1_MAP_TRAIN},
                   {"func_train2", Q1_MAP_TRAIN2},
                   {"func_bobbingwater", Q1_MAP_BOBBING_WATER},
                   {"func_pushable", Q1_MAP_PUSHABLE},
                   {"misc_teleporttrain", Q1_MAP_TRAIN},
                   {"func_episodegate", Q1_MAP_GATE},
                   {"func_bossgate", Q1_MAP_GATE},
                   {"func_illusionary", Q1_MAP_STATIC},
                   {"item_sigil", Q1_MAP_SIGIL},
                   {"trap_spikeshooter", Q1_MAP_SHOOTER},
                   {"trap_shooter", Q1_MAP_SHOOTER},
                   {"misc_fireball", Q1_MAP_FIREBALL_SOURCE},
                   {"air_bubbles", Q1_MAP_BUBBLES},
                   {"light_globe", Q1_MAP_STATIC},
                   {"light_torch_small_walltorch", Q1_MAP_STATIC},
                   {"light_flame_large_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_yellow", Q1_MAP_STATIC},
                   {"light_flame_small_white", Q1_MAP_STATIC},
                   {"ambient_suck_wind", Q1_MAP_AMBIENT},
                   {"ambient_flouro_buzz", Q1_MAP_AMBIENT},
                   {"ambient_drip", Q1_MAP_AMBIENT},
                   {"ambient_thunder", Q1_MAP_AMBIENT},
                   {"ambient_light_buzz", Q1_MAP_AMBIENT},
                   {"ambient_swamp1", Q1_MAP_AMBIENT},
                   {"ambient_swamp2", Q1_MAP_AMBIENT},
                   {"viewthing", Q1_MAP_VIEW},
                   {"misc_noisemaker", Q1_MAP_NOISE},
                   {"event_lightning", Q1_MAP_LIGHTNING},
                   {"play_sound", Q1_MAP_SOUND},
                   {"play_sound_triggered", Q1_MAP_SOUND},
                   {"random_thunder", Q1_MAP_SOUND},
                   {"random_thunder_triggered", Q1_MAP_SOUND},
                   {"ambient_humming", Q1_MAP_HIP_AMBIENT},
                   {"ambient_rushing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_running_water", Q1_MAP_HIP_AMBIENT},
                   {"ambient_fan_blowing", Q1_MAP_HIP_AMBIENT},
                   {"ambient_waterfall", Q1_MAP_HIP_AMBIENT},
                   {"ambient_riftpower", Q1_MAP_HIP_AMBIENT},
                   {"info_command", Q1_MAP_COMMAND},
                   {"effect_teleport", Q1_MAP_TELEPORT_EFFECT},
                   {"func_exploder", Q1_MAP_EXPLODER},
                   {"func_multi_exploder", Q1_MAP_EXPLODER},
                   {"func_rubble", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble1", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble2", Q1_MAP_RUBBLE_SOURCE},
                   {"func_rubble3", Q1_MAP_RUBBLE_SOURCE},
                   {"func_earthquake", Q1_MAP_EARTHQUAKE},
                   {"func_particlefield", Q1_MAP_PARTICLE_FIELD},
                   {"func_togglewall", Q1_MAP_TOGGLE_WALL},
                   {"wallsprite", Q1_MAP_WALL_SPRITE},
                   {"misc_sacrifice", Q1_MAP_SACRIFICE},
                   {"trigger_multiple", Q1_MAP_MULTI},
                   {"trigger_once", Q1_MAP_MULTI},
                   {"trigger_secret", Q1_MAP_MULTI},
                   {"trigger_counter", Q1_MAP_COUNTER},
                   {"trigger_relay", Q1_MAP_RELAY},
                   {"trigger_teleport", Q1_MAP_TELEPORT},
                   {"info_teleport_destination", Q1_MAP_DESTINATION},
                   {"trigger_hurt", Q1_MAP_HURT},
                   {"trigger_push", Q1_MAP_PUSH},
                   {"trigger_changelevel", Q1_MAP_CHANGELEVEL},
                   {"trigger_setskill", Q1_MAP_SETSKILL},
                   {"trigger_onlyregistered", Q1_MAP_REGISTERED},
                   {"trigger_monsterjump", Q1_MAP_MONSTERJUMP},
                   {"path_corner", Q1_MAP_PATH},
                   {"path_follow", Q1_MAP_FOLLOW},
                   {"path_follow2", Q1_MAP_FOLLOW},
                   {"target_cancelpause", Q1_MAP_CANCEL_PAUSE},
                   {"target_switchpath", Q1_MAP_SWITCH_PATH},
                   {"info_player_start", Q1_MAP_POINT},
                   {"info_player_start2", Q1_MAP_POINT},
                   {"info_player_coop", Q1_MAP_POINT},
                   {"info_player_deathmatch", Q1_MAP_POINT},
                   {"info_intermission", Q1_MAP_POINT},
                   {"info_notnull", Q1_MAP_POINT},
                   {"testplayerstart", Q1_MAP_POINT},
                   {"light", Q1_MAP_LIGHT},
                   {"light_fluoro", Q1_MAP_LIGHT},
                   {"light_fluorospark", Q1_MAP_LIGHT},
                   {"misc_explobox", Q1_MAP_BARREL},
                   {"misc_explobox2", Q1_MAP_BARREL}};
    for (size_t i = 0; i < sizeof(classes) / sizeof(*classes); ++i)
        if (!strcmp(name, classes[i].name))
            return classes[i].kind;
    return Q1_MAP_FIELDS;
}
bool q1_map_spawn(qa_q1_game *g, q1_actor *entity, const qa_q1_spawn *spawn, bool *handled,
                  qa_error *error) {
    q1_map_kind kind = classify(spawn->classname);
    if (g->options.program != QA_Q1_MG3 &&
        (kind == Q1_MAP_CANCEL_PAUSE || kind == Q1_MAP_SWITCH_PATH))
        kind = Q1_MAP_FIELDS;
    if (g->options.program != QA_Q1_HIPNOTIC &&
        (kind == Q1_MAP_FOLLOW || kind == Q1_MAP_TRAIN2 || kind == Q1_MAP_BOBBING_WATER ||
         kind == Q1_MAP_PUSHABLE))
        kind = Q1_MAP_FIELDS;
    *handled = kind != Q1_MAP_FIELDS;
    if (!*handled && !spawn->map_fields)
        return true;
    q1_map_state *state = q1_map_allocate(g, entity, error);
    if (!state || !fields(g, entity, spawn->map_fields, error))
        return false;
    state->kind = kind;
    if (!*handled)
        return true;
    entity->kind = Q1_MAP;
    if (kind == Q1_MAP_BOBBING_WATER || kind == Q1_MAP_PUSHABLE)
        return q1_map_hip_brush_spawn(g, entity, error);
    if (kind == Q1_MAP_SACRIFICE)
        return q1_map_sacrifice_spawn(g, entity, error);
    if (q1_map_is_mover(kind))
        return q1_map_mover_spawn(g, entity, error);
    if (kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_spawn(g, entity, error);
    if (kind >= Q1_MAP_SOUND)
        return q1_map_hip_misc_spawn(g, entity, error);
    if (kind >= Q1_MAP_GATE)
        return q1_map_special_spawn(g, entity, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    switch (kind) {
    case Q1_MAP_WORLD: {
        g->maps->world_actor = entity->id;
        static const char *const styles[] = {"m",
                                             "mmnmmommommnonmmonqnmmo",
                                             "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba",
                                             "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",
                                             "mamamamamama",
                                             "jklmnopqrstuvwxyzyxwvutsrqponmlkj",
                                             "nmonqnmomnmomomno",
                                             "mmmaaaabcdefgmmmmaaaammmaamm",
                                             "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa",
                                             "aaaaaaaazzzzzzzz",
                                             "mmamammmmammamamaaamammma",
                                             "abcdefghijklmnopqrrqponmlkjihgfedcba"};
        g->options.world_type = spawn->map_fields ? spawn->map_fields->world_type : 0;
        for (size_t i = 0; i < sizeof(styles) / sizeof(*styles); ++i) {
            qa_string_id pattern;
            if (!qa_builtin_resource(&g->services, styles[i], &pattern, error) ||
                !g->maps->options.lightstyle(g->maps->options.context, (int32_t)i, pattern, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        return true;
    }
    case Q1_MAP_WALL:
        if (!state->has_inline_model)
            return q1_map_fail(error, "Q1 wall has no brush model");
        entity->physics.motion = QA_PHYSICS_PUSH;
        entity->physics.solid = QA_PHYSICS_BRUSH;
        body.angles = qa_v3(0, 0, 0);
        state->use_enabled = true;
        break;
    case Q1_MAP_POINT:
        break;
    case Q1_MAP_DESTINATION:
        if (!q1_map_text(g, entity->targetname))
            return q1_map_fail(error, "Q1 teleport destination has no targetname");
        state->mangle = body.angles;
        body.angles = qa_v3(0, 0, 0);
        body.origin.z += 27;
        break;
    case Q1_MAP_PATH:
        if (!q1_map_text(g, entity->targetname))
            return q1_map_fail(error, "Q1 path corner has no targetname");
        if (g->options.program == QA_Q1_MG3 && entity->wait < 0)
            entity->wait = 999999;
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        state->touch_enabled = true;
        body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        break;
    case Q1_MAP_FOLLOW:
        entity->physics.solid = QA_PHYSICS_TRIGGER;
        state->touch_enabled = true;
        if (!strcmp(spawn->classname, "path_follow2"))
            body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        else {
            entity->physics.motion = QA_PHYSICS_STATIONARY;
            entity->model = QA_STRING_NONE;
        }
        break;
    case Q1_MAP_CANCEL_PAUSE:
    case Q1_MAP_SWITCH_PATH:
        if (!q1_map_text(g, entity->target) || !q1_map_text(g, entity->targetname) ||
            (kind == Q1_MAP_SWITCH_PATH && !q1_map_text(g, state->netname)))
            return q1_map_fail(error, "Q1 path control has missing authored target fields");
        state->use_enabled = true;
        break;
    case Q1_MAP_LIGHT:
        if (q1_classnamed(g, entity->id, "light") && !q1_map_text(g, entity->targetname))
            return q1_remove(g, entity, error);
        if (state->style >= 32 && !q1_classnamed(g, entity->id, "light_fluorospark")) {
            state->use_enabled = true;
            if (!q1_map_lightstyle(g, entity, entity->spawnflags & 1 ? "a" : "m", error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        if (q1_classnamed(g, entity->id, "light_fluoro") ||
            q1_classnamed(g, entity->id, "light_fluorospark"))
            if (!q1_map_ambient(g, body.origin,
                                q1_classnamed(g, entity->id, "light_fluoro")
                                    ? "ambience/fl_hum1.wav"
                                    : "ambience/buzz1.wav",
                                .5f, error))
                return false;
        break;
    case Q1_MAP_BARREL: {
        bool small = q1_classnamed(g, entity->id, "misc_explobox2");
        if (!q1_model(g, entity, small ? "maps/b_exbox2.bsp" : "maps/b_explob.bsp", error) ||
            !qa_combat_set_health(g->services.combat, entity->id, 20, error) ||
            !q1_map_damageable(g, entity, true, error))
            return false;
        entity->physics.solid = QA_PHYSICS_BOX;
        entity->aimed_damage = true;
        body.bounds = (qa_bounds){{0, 0, 0}, {32, 32, small ? 32 : 64}};
        body.origin.z += 2;
        qa_trace_query query = {.start = body.origin,
                                .end = qa_vec_add(body.origin, qa_v3(0, 0, -256)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = entity->id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        if (trace.fraction < 1 && !trace.all_solid) {
            if (body.origin.z - trace.end.z > 250)
                return q1_remove(g, entity, error);
            body.origin = trace.end;
            body.ground = trace.actor;
            entity->physics.flags |= QA_PHYSICS_ONGROUND;
        }
        break;
    }
    default:
        return q1_map_trigger_spawn(g, entity, error);
    }
    return !q1_alive(g, entity->id) ||
           (qa_world_body_write(g->services.world, entity->id, &body, error) &&
            q1_link(g, entity, error));
}

bool q1_map_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_actor_id activator,
                qa_error *error) {
    if (!entity->map || !entity->map->use_enabled)
        return true;
    if (entity->map->kind == Q1_MAP_CANCEL_PAUSE || entity->map->kind == Q1_MAP_SWITCH_PATH)
        return q1_map_path_use(g, entity, error);
    if (entity->map->kind == Q1_MAP_SACRIFICE) {
        entity->activator = activator;
        return q1_map_sacrifice_gib(g, entity, error);
    }
    if (q1_map_is_mover(entity->map->kind))
        return q1_map_mover_use(g, entity, activator, error);
    if (entity->map->kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_use(g, entity, other, error);
    if (entity->map->kind >= Q1_MAP_SOUND)
        return q1_map_hip_misc_use(g, entity, activator, error);
    if (entity->map->kind >= Q1_MAP_GATE)
        return q1_map_special_use(g, entity, activator, error);
    if (entity->map->kind == Q1_MAP_WALL) {
        entity->frame = 1 - entity->frame;
        return true;
    }
    if (entity->map->kind == Q1_MAP_LIGHT) {
        entity->spawnflags ^= 1;
        return q1_map_lightstyle(g, entity, entity->spawnflags & 1 ? "a" : "m", error);
    }
    return q1_map_trigger_use(g, entity, other, activator, error);
}
bool q1_map_touch(qa_q1_game *g, q1_actor *entity, const qa_touch_contact *contact,
                  qa_error *error) {
    if (entity->map && entity->map->kind == Q1_MAP_PUSHABLE_PROXY)
        return q1_map_pushable_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind >= Q1_MAP_PARTICLE_FIELD)
        return q1_map_hip_particles_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && q1_map_is_mover(entity->map->kind))
        return q1_map_mover_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind >= Q1_MAP_SOUND)
        return q1_map_hip_misc_touch(g, entity, contact->other, error);
    if (entity->map && entity->map->touch_enabled && entity->map->kind >= Q1_MAP_GATE)
        return q1_map_special_touch(g, entity, contact->other, error);
    return !entity->map || !entity->map->touch_enabled ||
           q1_map_trigger_touch(g, entity, contact, error);
}
bool q1_map_blocked(qa_q1_game *g, q1_actor *entity, qa_actor_id obstacle, qa_error *error) {
    return !entity->map || !q1_map_is_mover(entity->map->kind) ||
           q1_map_mover_blocked(g, entity, obstacle, error);
}
bool q1_map_reaction(qa_q1_game *g, q1_actor *entity, const qa_damage_outcome *outcome,
                     qa_error *error) {
    if (q1_map_is_mover(entity->map->kind))
        return q1_map_mover_reaction(g, entity, outcome, error);
    if (outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    if (entity->map->kind == Q1_MAP_SACRIFICE)
        return q1_map_sacrifice_gib(g, entity, error);
    if (entity->map->kind == Q1_MAP_MULTI)
        return !q1_map_grounded(g, entity, outcome->request.attack.attacker) ||
               q1_map_multi_fire(g, entity, outcome->request.attack.attacker, error);
    if (entity->map->kind != Q1_MAP_BARREL)
        return true;
    entity->activator = outcome->request.attack.attacker;
    return qa_builtin_resource(&g->services, "explo_box", &entity->classname, error) &&
           q1_map_damageable(g, entity, false, error) &&
           q1_map_schedule(g, entity, .3, Q1_MAP_BARREL_EXPLODE, error);
}
bool q1_map_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_state *state = entity->map;
    q1_map_action action = state->action;
    state->action = Q1_MAP_IDLE;
    if (action == Q1_MAP_BOB_WATER)
        return q1_map_bob_water(g, entity, error);
    if (action == Q1_MAP_SACRIFICE_ANIMATE || action == Q1_MAP_SACRIFICE_FLOAT)
        return q1_map_sacrifice_think(g, entity, action, error);
    if (action >= Q1_MAP_MOVE_DONE && action <= Q1_MAP_TRAIN_WAIT)
        return q1_map_mover_think(g, entity, action, error);
    if (action >= Q1_MAP_SOUND_REPEAT)
        return q1_map_hip_misc_think(g, entity, action, error);
    if (action >= Q1_MAP_LIGHTNING_FIRE)
        return q1_map_boss_think(g, entity, action, error);
    if (action >= Q1_MAP_SIGIL_PLACE)
        return q1_map_special_think(g, entity, action, error);
    switch (action) {
    case Q1_MAP_IDLE:
        return true;
    case Q1_MAP_REMOVE:
        return q1_remove(g, entity, error);
    case Q1_MAP_REARM:
        if (state->kind == Q1_MAP_HURT)
            entity->physics.solid = QA_PHYSICS_TRIGGER;
        else if (entity->max_health > 0) {
            if (!qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error) ||
                !q1_map_damageable(g, entity, true, error))
                return false;
            entity->physics.solid = QA_PHYSICS_BOX;
        }
        return q1_link(g, entity, error);
    case Q1_MAP_DELAYED_USE: {
        qa_target_use use = state->pending.delayed;
        use.time_ns = g->time_ns;
        if (!qa_targets_use_now(g->maps->options.targets, &use, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    case Q1_MAP_BEGIN_LEVEL:
        return qa_q1_level_begin(g->maps->options.level, state->map, entity->activator, g->time,
                                 error);
    case Q1_MAP_PENDING_LEVEL:
        if (!qa_q1_level_begin_pending(g->maps->options.level, g->time, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    case Q1_MAP_FINALE_TIMER:
        if (!qa_q1_campaign_source_timer(g->maps->options.campaign_source, state->pending.finale,
                                         error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    case Q1_MAP_BARREL_EXPLODE: {
        if (!q1_radius(g, entity->id, entity->activator, 160, (qa_actor_id){0}, QA_Q1_WEAPON_COUNT,
                       error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !q1_sound(g, entity->id, "weapons/r_exp3.wav", 0, 1, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (!q1_effect(g, QA_BUILTIN_EXPLOSION, entity->id,
                       qa_vec_add(body.origin, qa_v3(0, 0, 32)), 0, 0, error))
            return false;
        return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
    }
    default:
        return q1_map_fail(error, "unknown Q1 map continuation");
    }
}

bool q1_map_timer(qa_q1_game *g, const char *name, q1_actor **out, qa_error *error) {
    if (!g || !g->maps)
        return q1_map_fail(error, "Q1 map services are not bound");
    q1_actor *entity;
    if (!q1_create(g, name, Q1_MAP, (qa_actor_id){0}, &entity, error))
        return false;
    if (!q1_map_allocate(g, entity, error)) {
        (void)q1_remove(g, entity, NULL);
        return false;
    }
    entity->map->kind = Q1_MAP_DELAY;
    *out = entity;
    return true;
}
bool qa_q1_game_map_defer_targets(qa_q1_game *g, const qa_target_use *use, qa_error *error) {
    if (!use || !isfinite(use->fields.delay_seconds))
        return q1_map_fail(error, "invalid Q1 delayed target use");
    q1_actor *entity;
    if (!q1_map_timer(g, "DelayedUse", &entity, error))
        return false;
    entity->map->pending.delayed = *use;
    entity->map->pending.delayed.source = entity->id;
    entity->map->pending.delayed.fields.classname = entity->classname;
    entity->map->pending.delayed.fields.delay_seconds = 0;
    entity->map->pending.delayed.live_fields = false;
    entity->activator = use->activator;
    entity->target = use->fields.target;
    entity->killtarget = use->fields.killtarget;
    entity->message = use->fields.message;
    if (q1_map_schedule(g, entity, use->fields.delay_seconds, Q1_MAP_DELAYED_USE, error))
        return true;
    (void)q1_remove(g, entity, NULL);
    return false;
}
bool qa_q1_game_map_defer_level(qa_q1_game *g, double delay, qa_error *error) {
    q1_actor *entity;
    if (!q1_map_timer(g, "nextlevel", &entity, error))
        return false;
    if (q1_map_schedule(g, entity, delay, Q1_MAP_PENDING_LEVEL, error))
        return true;
    (void)q1_remove(g, entity, NULL);
    return false;
}
bool qa_q1_game_map_defer_finale(qa_q1_game *g, qa_q1_campaign_timer kind, double delay,
                                 qa_error *error) {
    if (!g || !g->maps || !g->maps->options.campaign_source || kind < QA_Q1_CAMPAIGN_CHECK_FINALE ||
        kind > QA_Q1_CAMPAIGN_FINISH_FINALE)
        return q1_map_fail(error, "invalid Q1 campaign timer");
    q1_actor *entity;
    if (!q1_map_timer(g, "finale_timer", &entity, error))
        return false;
    entity->map->pending.finale = kind;
    if (q1_map_schedule(g, entity, delay, Q1_MAP_FINALE_TIMER, error))
        return true;
    (void)q1_remove(g, entity, NULL);
    return false;
}
qa_string_id qa_q1_game_map_text(const qa_q1_game *g, qa_actor_id actor, qa_q1_campaign_text kind) {
    const q1_actor *entity = q1_entity_const(g, actor);
    if (!entity || !entity->map)
        return QA_STRING_NONE;
    return kind == QA_Q1_CAMPAIGN_ENDTEXT             ? entity->map->endtext
           : kind == QA_Q1_CAMPAIGN_INTERMISSION_TEXT ? entity->map->intermissiontext
                                                      : QA_STRING_NONE;
}
bool qa_q1_game_map_set_text(qa_q1_game *g, qa_actor_id actor, qa_q1_campaign_text kind,
                             qa_string_id text, qa_error *error) {
    q1_actor *entity = g ? q1_entity(g, actor) : NULL;
    if (!entity || !entity->map ||
        (text && !qa_strings_cstr(qa_session_strings(g->services.session), text)))
        return q1_map_fail(error, "invalid Q1 map text target");
    switch (kind) {
    case QA_Q1_CAMPAIGN_ENDTEXT:
        entity->map->endtext = text;
        return true;
    case QA_Q1_CAMPAIGN_INTERMISSION_TEXT:
        entity->map->intermissiontext = text;
        return true;
    }
    return q1_map_fail(error, "unknown Q1 map text field");
}
void qa_q1_game_map_secrets(const qa_q1_game *g, uint32_t *total, uint32_t *found) {
    if (total)
        *total = g && g->maps ? g->maps->total_secrets : 0;
    if (found)
        *found = g && g->maps ? g->maps->found_secrets : 0;
}
