#include "internal.h"

static bool addon(const qa_q1_game *g) {
    return g->options.program >= QA_Q1_DOPA && g->options.program <= QA_Q1_MG3;
}
qa_vec3 q1_map_direction(qa_vec3 angles) {
    if (angles.x == 0 && angles.z == 0 && angles.y == -1)
        return qa_v3(0, 0, 1);
    if (angles.x == 0 && angles.z == 0 && angles.y == -2)
        return qa_v3(0, 0, -1);
    qa_vec3 forward;
    qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
    return forward;
}
bool q1_map_trigger_init(qa_q1_game *g, q1_actor *entity, bool zero_direction, qa_error *error) {
    qa_actor_id id = entity->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    q1_map_state *state = entity->map;
    bool authored =
        addon(g) && (state->has_movedir || qa_vec_dot(state->movedir, state->movedir) != 0);
    if (!authored)
        state->movedir = zero_direction && body.angles.x == 0 && body.angles.y == 0 && body.angles.z == 0
                             ? qa_v3(0, 0, 0)
                             : q1_map_direction(body.angles);
    body.angles = qa_v3(0, 0, 0);
    entity->model = QA_STRING_NONE;
    entity->physics.solid = QA_PHYSICS_TRIGGER;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    return qa_world_body_write(g->services.world, id, &body, error);
}
static bool remove_addon_trigger(qa_q1_game *g, q1_actor *entity) {
    if (!addon(g))
        return false;
    q1_map_kind kind = entity->map->kind;
    bool filtered = kind == Q1_MAP_MULTI || kind == Q1_MAP_TELEPORT || kind == Q1_MAP_RELAY ||
                    kind == Q1_MAP_REGISTERED || kind == Q1_MAP_SETSKILL ||
                    kind == Q1_MAP_MONSTERJUMP;
    if (!filtered)
        return false;
    bool inhibit_coop = kind == Q1_MAP_MULTI || kind == Q1_MAP_RELAY;
    if (g->options.coop ? inhibit_coop && (entity->spawnflags & 131072u)
                        : (entity->spawnflags & 32768u))
        return true;
    return g->options.program == QA_Q1_MG3 &&
           (entity->spawnflags & (262144u << qa_q1_mg3_rune_count(*g->maps->options.server_flags)));
}
static bool multi_enable(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_state *state = entity->map;
    if (entity->wait == 0)
        entity->wait = .2f;
    if (!q1_map_trigger_init(g, entity, true, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    state = entity->map;
    state->dormant = false;
    state->use_enabled = true;
    if (addon(g) ? entity->max_health != 0 : entity->max_health > 0) {
        if (entity->spawnflags & 1u)
            return q1_map_fail(error, "Q1 trigger combines health and notouch");
        if (!q1_map_damageable(g, entity, true, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        entity->physics.solid = QA_PHYSICS_BOX;
    } else
        state->touch_enabled = !(entity->spawnflags & 1u);
    return q1_link(g, entity, error);
}
bool q1_map_trigger_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (q1_map_is_addon_field(g, entity->map->kind))
        return q1_map_addon_field_spawn(g, entity, error);
    q1_map_state *state = entity->map;
    if (remove_addon_trigger(g, entity))
        return q1_remove(g, entity, error);
    switch (state->kind) {
    case Q1_MAP_MULTI: {
        bool secret = q1_classnamed(g, entity->id, "trigger_secret");
        if (secret || q1_classnamed(g, entity->id, "trigger_once"))
            entity->wait = -1;
        if (secret) {
            if (g->maps->total_secrets == UINT32_MAX)
                return q1_map_fail(error, "Q1 secret count overflow");
            ++g->maps->total_secrets;
            if (!q1_map_text(g, entity->message) &&
                (g->options.program != QA_Q1_MG3 || !(entity->spawnflags & 128u)) &&
                !qa_builtin_resource(&g->services, "$qc_found_secret", &entity->message, error))
                return false;
            if (!state->sounds)
                state->sounds = 1;
        }
        if (addon(g) && (entity->spawnflags & 2u)) {
            entity->spawnflags &= ~2u;
            entity->model = QA_STRING_NONE;
            state->dormant = true;
            state->use_enabled = true;
            return true;
        }
        return multi_enable(g, entity, error);
    }
    case Q1_MAP_COUNTER:
        entity->model = QA_STRING_NONE;
        if (entity->count == 0)
            entity->count = 2;
        state->use_enabled = true;
        return true;
    case Q1_MAP_RELAY:
        state->use_enabled = true;
        return true;
    case Q1_MAP_TELEPORT: {
        if (!q1_map_text(g, entity->target))
            return q1_map_fail(error, "Q1 teleport has no target");
        if (!q1_map_trigger_init(g, entity, true, error))
            return false;
        state->touch_enabled = state->use_enabled = true;
        if (!(entity->spawnflags & 2u)) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
                !q1_map_ambient(g,
                                qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f),
                                "ambience/hum1.wav", .5f, error))
                return false;
        }
        return !q1_alive(g, entity->id) || q1_link(g, entity, error);
    }
    case Q1_MAP_CHANGELEVEL:
        if (addon(g) && q1_classnamed(g, entity->id, "hub_trigger_changelevel") &&
            (*g->maps->options.server_flags & 31) != 31)
            return q1_remove(g, entity, error);
        if (!q1_map_text(g, state->map))
            return q1_map_fail(error, "Q1 changelevel has no map");
        if (addon(g) && ((!g->options.coop && (entity->spawnflags & 32768u)) ||
            (g->options.program == QA_Q1_MG3 &&
             (entity->spawnflags &
              (262144u << qa_q1_mg3_rune_count(*g->maps->options.server_flags))))))
            return q1_remove(g, entity, error);
        break;
    case Q1_MAP_MONSTERJUMP: {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        if (body.angles.y == 0) {
            body.angles = qa_v3(0, 360, 0);
            if (!qa_world_body_write(g->services.world, entity->id, &body, error))
                return false;
        }
        if (entity->speed == 0)
            entity->speed = 200;
        break;
    }
    case Q1_MAP_PUSH:
        if (entity->speed == 0)
            entity->speed = 1000;
        break;
    case Q1_MAP_HURT:
        if (entity->damage == 0)
            entity->damage = 5;
        break;
    case Q1_MAP_SETSKILL:
    case Q1_MAP_REGISTERED:
        break;
    default:
        return q1_map_fail(error, "unknown Q1 trigger class");
    }
    state->touch_enabled = true;
    bool zero_direction = state->kind == Q1_MAP_PUSH || state->kind == Q1_MAP_HURT || addon(g);
    return q1_map_trigger_init(g, entity, zero_direction, error) && q1_link(g, entity, error);
}
bool q1_map_grounded(qa_q1_game *g, q1_actor *entity, qa_actor_id other) {
    if (g->options.program != QA_Q1_MG3 || !(entity->spawnflags & 64u))
        return true;
    const q1_actor *native = q1_entity_const(g, other);
    if (native) return (native->physics.flags & QA_PHYSICS_ONGROUND) != 0;
    qa_builtin_actor_traits traits;
    return g->services.actor_traits && g->services.actor_traits(g->services.context, other, &traits) && traits.grounded;
}
bool q1_map_multi_fire(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_state *state = entity->map;
    if (entity->next_think > g->time)
        return true;
    bool secret = q1_classnamed(g, entity->id, "trigger_secret");
    if (secret) {
        if (!q1_map_player(g, activator))
            return true;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        if (g->maps->found_secrets == UINT32_MAX)
            return q1_map_fail(error, "Q1 found-secret count overflow");
        ++g->maps->found_secrets;
        if (!g->maps->options.secret_found(g->maps->options.context, entity->id, activator,
                                           g->maps->total_secrets, g->maps->found_secrets, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
        if (g->options.edition == QA_Q1_RERELEASE) {
            qa_builtin_event event = {.kind = QA_BUILTIN_ACHIEVEMENT,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = activator,
                                      .time_ns = g->time_ns};
            if (!qa_builtin_resource(&g->services, "ACH_FIND_SECRET", &event.text, error) ||
                !qa_builtin_emit(&g->services, &event, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
    }
    entity->activator = q1_ref_from(g, activator);
    if (!q1_map_damageable(g, entity, false, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    state = entity->map;
    const char *sound = state->sounds == 1   ? "misc/secret.wav"
                        : state->sounds == 2 ? "misc/talk.wav"
                        : state->sounds == 3 ? "misc/trigger1.wav"
                                             : NULL;
    if (sound && !q1_sound(g, entity->id, sound, 0, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!q1_map_targets(g, entity, activator, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (entity->wait > 0)
        return q1_map_schedule(g, entity, entity->wait, Q1_MAP_REARM, error);
    state->touch_enabled = false;
    return q1_map_schedule(g, entity, .1, Q1_MAP_REMOVE, error);
}
bool q1_map_trigger_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_actor_id activator,
                        qa_error *error) {
    q1_map_state *state = entity->map;
    switch (state->kind) {
    case Q1_MAP_MULTI:
        if (state->dormant)
            return multi_enable(g, entity, error);
        return !q1_map_grounded(g, entity, other) || q1_map_multi_fire(g, entity, activator, error);
    case Q1_MAP_RELAY:
        return q1_map_targets(g, entity, activator, error);
    case Q1_MAP_TELEPORT:
        if (!g->host.force_retouch)
            return q1_map_fail(error, "Q1 teleport needs source retouch service");
        if (!g->host.force_retouch(g->host.context, 2, error))
            return false;
        return !q1_alive(g, entity->id) || q1_map_schedule(g, entity, .2, Q1_MAP_IDLE, error);
    case Q1_MAP_COUNTER:
        --entity->count;
        if (entity->count < 0)
            return true;
        if (!(entity->spawnflags & 1u)) {
            const char *message = entity->count >= 4   ? "$qc_more_go"
                                  : entity->count == 3 ? "$qc_three_more"
                                  : entity->count == 2 ? "$qc_two_more"
                                  : entity->count == 1 ? "$qc_one_more"
                                                       : "$qc_sequence_completed";
            if (!q1_message(g, activator, message, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
        if (entity->count != 0)
            return true;
        if (!q1_map_targets(g, entity, activator, error))
            return false;
        return !q1_alive(g, entity->id) || q1_map_schedule(g, entity, .1, Q1_MAP_REMOVE, error);
    default:
        return true;
    }
}
static bool changed(qa_q1_game *g, qa_actor_id actor, const qa_body_state *body,
                    qa_builtin_motion_reason reason, bool view, uint64_t hold, qa_error *error) {
    if (!g->services.motion_changed)
        return q1_map_fail(error, "Q1 map motion needs selected movement service");
    qa_builtin_motion_change motion = {.reason = reason,
                                       .body = *body,
                                       .view_angles = body->angles,
                                       .force_view_angles = view,
                                       .hold_ns = hold};
    return g->services.motion_changed(g->services.context, actor, &motion, error);
}
static bool teleport(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (q1_map_text(g, entity->targetname) && entity->next_think < g->time)
        return true;
    bool player = q1_map_player(g, other);
    if ((entity->spawnflags & 1u) && !player)
        return true;
    qa_physics_properties physics;
    if (q1_health(g, other) <= 0 ||
        (!player && (!g->services.physics->services.read(g->services.physics->services.context,
                                                         other, &physics) ||
                     physics.solid != QA_PHYSICS_BOX)))
        return true;
    qa_actor_id target;
    if (!qa_targets_first(g->maps->options.targets, entity->target, &target))
        return q1_map_fail(error, "Q1 teleport target is missing");
    qa_body_state body, destination;
    if (!qa_world_body_read(g->services.world, other, &body, error))
        return false;
    if (!q1_map_targets(g, entity, other, error))
        return false;
    if (!q1_alive(g, other) || !q1_alive(g, target))
        return true;
    if (!qa_q1_spawn_teleport_fog(g, body.origin, NULL, error))
        return false;
    if (!q1_alive(g, other) || !q1_alive(g, target))
        return true;
    if (!qa_world_body_read(g->services.world, target, &destination, error))
        return false;
    const q1_actor *native = q1_entity_const(g, target);
    qa_vec3 angles = native && native->map ? native->map->mangle : destination.angles;
    qa_vec3 forward = q1_map_direction(angles);
    if (!qa_q1_spawn_teleport_fog(g, qa_vec_add(destination.origin, qa_vec_scale(forward, 32)),
                                  NULL, error))
        return false;
    if (!q1_alive(g, other))
        return true;
    if (!qa_q1_spawn_teledeath(g, destination.origin, other, NULL, error))
        return false;
    if (!q1_alive(g, other))
        return true;
    body.origin = destination.origin;
    body.angles = angles;
    body.ground = (qa_actor_reference){0};
    if (player)
        body.velocity = qa_vec_scale(forward, 300);
    if (!qa_world_body_write(g->services.world, other, &body, error) ||
        !qa_world_link(g->services.world, other, NULL, error))
        return false;
    return !q1_alive(g, other) || changed(g, other, &body, QA_BUILTIN_MOTION_TELEPORT, player,
                                          player ? UINT64_C(700000000) : 0, error);
}
static bool cvar(qa_q1_game *g, const char *name, float *value, qa_error *error) {
    *value = 0;
    if (!g->services.cvar)
        return true;
    qa_string_id key;
    if (!qa_builtin_resource(&g->services, name, &key, error) ||
        !g->services.cvar(q1_cvar_context(g), key, value, error))
        return false;
    return isfinite(*value) || q1_map_fail(error, "nonfinite Q1 map cvar");
}
static bool damage(qa_q1_game *g, q1_actor *entity, qa_actor_id other, float amount,
                   const char *cause, qa_error *error) {
    qa_string_id type;
    return qa_builtin_resource(&g->services, cause, &type, error) &&
           q1_damage_typed(g, other, entity->id, entity->id, amount, QA_Q1_WEAPON_COUNT,
                           QA_Q1_ARMOR_NORMAL, type, error);
}
static bool changelevel(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (!q1_map_player(g, other))
        return true;
    float no_exit, same_level;
    if (!cvar(g, "noexit", &no_exit, error) || !cvar(g, "samelevel", &same_level, error))
        return false;
    const qa_q1_map_options *options = &g->maps->options;
    const char *current =
        qa_strings_cstr(qa_session_strings(g->services.session), options->current_map);
    if (no_exit == 1 || (no_exit == 2 && (!current || strcmp(current, "start"))))
        return damage(g, entity, other, 50000, "exit", error);
    if (!qa_q1_level_touch(options->level, entity->id, other, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (options->player_exited && !options->player_exited(options->context, other, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!q1_map_targets(g, entity, other, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if ((entity->spawnflags & 1u) && !g->options.deathmatch &&
        (!addon(g) || !q1_map_text(g, entity->map->endtext)))
        return qa_q1_level_travel(
            options->level, same_level != 0 ? options->current_map : entity->map->map, other, error);
    entity->map->touch_enabled = false;
    entity->activator = q1_ref_from(g, other);
    return q1_map_schedule(g, entity, .1, Q1_MAP_BEGIN_LEVEL, error);
}
static bool path(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    qa_actor_id corner = entity->id;
    if (g->options.program == QA_Q1_MG3)
        return q1_map_path_touch(g, entity, other, error);
    if (g->options.program == QA_Q1_HIPNOTIC)
        return q1_map_hip_path_touch(g, entity, other, error);
    if (g->options.program == QA_Q1_ROGUE) {
        bool handled;
        if (!qa_q1_game_rogue_path_touch(g, corner, other, &handled, error))
            return false;
        entity = q1_entity(g, corner);
        if (handled || !entity || !entity->map || !q1_alive(g, other))
            return true;
    }
    if (g->maps->options.path_touch) {
        bool handled = false;
        if (!g->maps->options.path_touch(g->maps->options.context, corner, other, &handled, error))
            return false;
        entity = q1_entity(g, corner);
        if (handled || !entity || !entity->map || !q1_alive(g, other))
            return true;
    }
    q1_actor *follower = q1_entity(g, other);
    if (!follower || follower->kind != Q1_MONSTER ||
        follower->state.monster.path != entity->targetname ||
        q1_ref_present(follower->state.monster.enemy))
        return true;
    qa_actor_id next = {0};
    (void)qa_targets_first(g->maps->options.targets, entity->target, &next);
    entity = q1_entity(g, corner);
    follower = q1_entity(g, other);
    if (!entity || !entity->map || !follower || follower->kind != Q1_MONSTER)
        return true;
    follower->state.monster.path = next.registry ? entity->target : QA_STRING_NONE;
    follower->physics.goal = q1_ref_from(g, next);
    if (!next.registry) {
        follower->state.monster.pause_until = g->time + 999999;
        return !follower->state.monster.path_end ||
               q1_monster_play(g, follower, follower->state.monster.species->stand, error);
    }
    qa_body_state from, to;
    if (!qa_world_body_read(g->services.world, other, &from, error) ||
        !qa_world_body_read(g->services.world, next, &to, error))
        return false;
    follower = q1_entity(g, other);
    if (!follower || follower->kind != Q1_MONSTER)
        return true;
    qa_vec3 delta = qa_vec_sub(to.origin, from.origin);
    float yaw = atan2f(delta.y, delta.x) * (180.0f / 3.14159265358979323846f);
    follower->physics.ideal_yaw = yaw < 0 ? yaw + 360 : yaw;
    return true;
}
bool q1_map_trigger_touch(qa_q1_game *g, q1_actor *entity, const qa_touch_contact *contact,
                          qa_error *error) {
    qa_actor_id other = contact->other;
    if (!q1_alive(g, other))
        return true;
    if (q1_map_is_addon_field(g, entity->map->kind))
        return q1_map_addon_field_touch(g, entity, other, error);
    q1_map_state *state = entity->map;
    qa_body_state body;
    switch (state->kind) {
    case Q1_MAP_MULTI:
        if (!q1_map_player(g, other) || !q1_map_grounded(g, entity, other))
            return true;
        if (!qa_world_body_read(g->services.world, other, &body, error))
            return false;
        if (qa_vec_dot(q1_map_direction(body.angles), state->movedir) < 0)
            return true;
        return q1_map_multi_fire(g, entity, other, error);
    case Q1_MAP_TELEPORT:
        return teleport(g, entity, other, error);
    case Q1_MAP_CHANGELEVEL:
        return changelevel(g, entity, other, error);
    case Q1_MAP_PATH:
        return path(g, entity, other, error);
    case Q1_MAP_FOLLOW:
        return q1_map_follow_touch(g, entity, other, error);
    case Q1_MAP_HURT:
        if (entity->physics.solid != QA_PHYSICS_TRIGGER || !q1_damageable(g, other))
            return true;
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        if (!damage(g, entity, other, entity->damage, "trigger", error))
            return false;
        return !q1_alive(g, entity->id) || q1_map_schedule(g, entity, 1, Q1_MAP_REARM, error);
    case Q1_MAP_PUSH:
        if (q1_health(g, other) <= 0 && !q1_classnamed(g, other, "grenade"))
            return true;
        if (!qa_world_body_read(g->services.world, other, &body, error))
            return false;
        body.velocity = qa_vec_scale(state->movedir, entity->speed * 10);
        if (!qa_world_body_write(g->services.world, other, &body, error) ||
            !changed(g, other, &body, QA_BUILTIN_MOTION_LAUNCH, false, 0, error))
            return false;
        return !q1_alive(g, entity->id) || !(entity->spawnflags & 1u) ||
               q1_remove(g, entity, error);
    case Q1_MAP_SETSKILL: {
        if (!q1_map_player(g, other))
            return true;
        const char *text =
            qa_strings_cstr(qa_session_strings(g->services.session), entity->message);
        char *end;
        double value = text ? strtod(text, &end) : 0;
        if (text) {
            while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
                ++end;
            if (*end || isnan(value))
                return true;
        }
        int32_t skill = value <= 0 ? 0 : value >= 3 ? 3 : (int32_t)floor(value);
        return g->maps->options.set_skill(g->maps->options.context, skill, error);
    }
    case Q1_MAP_REGISTERED:
        if (!q1_map_player(g, other) || state->cooldown > g->time)
            return true;
        state->cooldown = g->time + 2;
        if (g->maps->options.registered) {
            entity->message = QA_STRING_NONE;
            if (!q1_map_targets(g, entity, other, error))
                return false;
            return !q1_alive(g, entity->id) || q1_remove(g, entity, error);
        }
        if (q1_map_text(g, entity->message)) {
            if (!q1_message(
                    g, other,
                    qa_strings_cstr(qa_session_strings(g->services.session), entity->message),
                    error))
                return false;
            if (q1_alive(g, other))
                return q1_sound(g, other, "misc/talk.wav", 4, 1, error);
        }
        return true;
    case Q1_MAP_MONSTERJUMP: {
        qa_builtin_actor_traits traits;
        qa_physics *physics = g->services.physics;
        qa_physics_properties props;
        if (!g->services.actor_traits(g->services.context, other, &traits) || !traits.monster ||
            !physics->services.read(physics->services.context, other, &props) ||
            (props.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)))
            return true;
        if (!qa_world_body_read(g->services.world, other, &body, error))
            return false;
        body.velocity.x = state->movedir.x * entity->speed;
        body.velocity.y = state->movedir.y * entity->speed;
        if (props.flags & QA_PHYSICS_ONGROUND) {
            body.velocity.z = state->height != 0 ? state->height : 200;
            props.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
        }
        body.ground = (qa_actor_reference){0};
        if (!qa_world_body_write(g->services.world, other, &body, error) ||
            !physics->services.write(physics->services.context, other, &props, error))
            return false;
        return !q1_alive(g, other) ||
               changed(g, other, &body, QA_BUILTIN_MOTION_LAUNCH, false, 0, error);
    }
    default:
        return true;
    }
}
