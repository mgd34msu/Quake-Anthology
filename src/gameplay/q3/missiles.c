#include "internal.h"

typedef struct radius_context {
    qa_q3_game *game;
    qa_actor_id attacker;
    bool accuracy;
} radius_context;
static bool radius_prepare(void *context, qa_damage_request *request, bool *allowed,
                           qa_error *error) {
    radius_context *call = context;
    request->amount = truncf(request->amount);
    request->knockback = request->amount;
    if (call->game->options.hooks.combat_provider)
        request->attack.combat_provider = call->game->options.hooks.combat_provider(
            call->game->options.hooks.context, request->target, request->attack.combat_provider);
    *allowed = request->amount >= 0;
    if (!*allowed)
        return true;
    if (!qa_attack_next(&call->game->attack_sequence, &request->attack, error))
        return false;
    if (q3_accuracy(call->game, request->target, call->attacker))
        call->accuracy = true;
    return true;
}
bool q3_radius(qa_q3_game *game, qa_actor_id inflictor, qa_actor_id attacker, qa_q3_weapon weapon,
               int32_t method, qa_vec3 origin, float damage, float radius, qa_actor_id ignore,
               bool *accuracy, qa_error *error) {
    radius = fmaxf(radius, 1);
    qa_vec3 extent = qa_v3(radius, radius, radius);
    q3_snapshot_frame *frame = q3_bounds_snapshot(
        game, (qa_bounds){qa_vec_sub(origin, extent), qa_vec_add(origin, extent)},
        QA_COLLISION_BOTH, error);
    if (!frame)
        return false;
    radius_context context = {game, attacker, false};
    qa_builtin_radius attack = {
        .origin = origin,
        .radius = radius,
        .damage = damage,
        .distance_scale = damage / radius,
        .self_scale = 1,
        .knockback_scale = 1,
        .direction_z_bias = 24,
        .distance = QA_RADIUS_BOUNDS,
        .ignore = ignore,
        .check_visibility = true,
        .trace = qa_collision_default_policy(QA_COLLISION_Q3),
        .has_candidates = true,
        .candidates = frame->snapshot.ids,
        .candidate_count = frame->snapshot.count,
        .context = &context,
        .prepare = radius_prepare,
        .attack = {.time_ns = (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000),
                   .attacker = attacker,
                   .inflictor = inflictor,
                   .projectile = inflictor,
                   .weapon_provider = game->options.owner,
                   .combat_provider = game->options.owner,
                   .weapon = qa_q3_weapon_item(game, weapon, false),
                   .powerup_owner = game->options.owner,
                   .powerup_applied = true,
                   .cause = {.kind = QA_CAUSE_Q3, .source.q3 = {method, 1}}}};
    attack.trace.contents_mask = 1;
    bool ok = qa_builtin_radius_damage(&game->options.services, &attack, NULL, error);
    frame->active = false;
    if (accuracy)
        *accuracy = context.accuracy;
    return ok;
}
static qa_vec3 nail_direction(qa_vec3 value) {
    float length = (float)sqrt((double)qa_vec_dot(value, value));
    return length == 0 ? value : qa_vec_scale(value, q3_source_float_divide(1.0f, length));
}

bool q3_launch(qa_q3_game *game, qa_actor_id owner, qa_q3_weapon weapon, qa_vec3 start,
               qa_vec3 direction, qa_vec3 right, qa_vec3 up, float factor, qa_actor_id *out,
               qa_error *error) {
    float speed = 0, damage = 0, splash = 0, radius = 0;
    int32_t duration = 10000, method = 0, splash_method = 0;
    bool gravity = false;
    switch (weapon) {
    case QA_Q3_W_GRENADE:
        speed = 700;
        duration = 2500;
        gravity = true;
        damage = 100;
        splash = 100;
        radius = 150;
        method = 4;
        splash_method = 5;
        break;
    case QA_Q3_W_ROCKET:
        speed = 900;
        duration = 15000;
        damage = 100;
        splash = 100;
        radius = 120;
        method = 6;
        splash_method = 7;
        break;
    case QA_Q3_W_PLASMA:
        speed = 2000;
        damage = 20;
        splash = 15;
        radius = 20;
        method = 8;
        splash_method = 9;
        break;
    case QA_Q3_W_BFG:
        speed = 2000;
        damage = 100;
        splash = 100;
        radius = 120;
        method = 12;
        splash_method = 13;
        break;
    case QA_Q3_W_GRAPPLE:
        speed = 800;
        duration = 10000;
        method = game->options.product == QA_Q3_ARENA ? 23 : 28;
        break;
    case QA_Q3_W_NAIL:
        damage = 20;
        method = 23;
        break;
    case QA_Q3_W_PROX:
        speed = 700;
        duration = 3000;
        gravity = true;
        splash = 100;
        radius = 150;
        method = splash_method = 25;
        break;
    default:
        return q3_fail(error, "Q3 weapon has no projectile");
    }
    if (weapon != QA_Q3_W_NAIL)
        direction = qa_vec_normalize(direction);
    qa_vec3 velocity = qa_vec_scale(direction, speed);
    if (weapon == QA_Q3_W_NAIL) {
        float angle = q3_source_float_multiply(
            q3_source_float_multiply(q3_random(game), Q3_PI), 2.0f);
        float vertical = q3_source_float_multiply(
            q3_source_float_multiply(
                q3_source_float_multiply((float)sin((double)angle), q3_crandom(game)), 500.0f),
            16.0f);
        float horizontal = q3_source_float_multiply(
            q3_source_float_multiply(
                q3_source_float_multiply((float)cos((double)angle), q3_crandom(game)), 500.0f),
            16.0f);
        qa_vec3 end = qa_vec_add(
            qa_vec_add(qa_vec_add(start, qa_vec_scale(direction, 131072.0f)),
                       qa_vec_scale(right, horizontal)),
            qa_vec_scale(up, vertical));
        float nail_speed = q3_source_float_add(555.0f,
            q3_source_float_multiply(q3_random(game), 1800.0f));
        velocity = qa_vec_scale(nail_direction(qa_vec_sub(end, start)), nail_speed);
    }
    velocity = qa_physics_q3_snap(velocity);
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .owner = owner,
                                    .role = QA_COLLISION_SOLID};
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .body = {.origin = start, .velocity = velocity},
                              .collision = &collision};
    qa_actor_id actor;
    if (!qa_builtin_spawn_actor(&game->options.services, &spawn, &actor, error))
        return false;
    q3_actor *entry = &game->actors[actor.slot];
    *entry = (q3_actor){
        .actor = actor,
        .kind = Q3_ACTOR_MISSILE,
        .alpha = 1,
        .state.missile = {
            .weapon = weapon,
            .owner = owner,
            .pass = owner,
            .damage = truncf(damage * factor),
            .splash = truncf(splash * factor),
            .radius = radius,
            .method = method,
            .splash_method = splash_method,
            .think_at = q3_add_time(game->now_ms, duration),
            .phase = Q3_MISSILE_FLIGHT,
            .flags = weapon == QA_Q3_W_GRENADE ? 0x20u : 0,
            .trajectory = {.type = gravity ? QA_TRAJECTORY_GRAVITY : QA_TRAJECTORY_LINEAR,
                           .time_ms = weapon == QA_Q3_W_NAIL ? game->now_ms
                                                             : q3_add_time(game->now_ms, -50),
                           .base = start,
                           .delta = velocity}}};
    qa_combat_state owner_state;
    qa_error ignored = {0};
    bool described = qa_combat_read(game->options.services.combat, owner, &owner_state, &ignored);
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE) {
        if (out)
            *out = (qa_actor_id){0};
        return true;
    }
    if (described)
        entry->state.missile.team = owner_state.team;
    if (weapon == QA_Q3_W_GRAPPLE) {
        q3_actor *player = q3_actor_get(game, owner);
        if (player && player->kind == Q3_ACTOR_PLAYER)
            player->state.player.hook = actor;
    }
    qa_builtin_projectile_role role = weapon == QA_Q3_W_GRENADE || weapon == QA_Q3_W_PROX
                                          ? QA_BUILTIN_GRENADE
                                      : weapon == QA_Q3_W_ROCKET  ? QA_BUILTIN_ROCKET
                                      : weapon == QA_Q3_W_PLASMA  ? QA_BUILTIN_PLASMA
                                      : weapon == QA_Q3_W_GRAPPLE ? QA_BUILTIN_GRAPPLE
                                      : weapon == QA_Q3_W_NAIL    ? QA_BUILTIN_NAIL
                                                                  : QA_BUILTIN_ENERGY;
    qa_builtin_weapon_launch launch = {.projectile = actor,
                                       .shooter = owner,
                                       .weapon = game->weapon_items[weapon],
                                       .provider = game->options.owner,
                                       .role = role,
                                       .time_ns =
                                           (uint64_t)(uint32_t)game->now_ms * UINT64_C(1000000),
                                       .body = spawn.body};
    bool changed = false;
    if (!qa_builtin_launch_projectile(&game->options.services, &launch, &changed, error))
        return q3_rollback_spawn(game, actor, error);
    entry = q3_actor_get(game, actor);
    if (!entry) {
        if (out)
            *out = (qa_actor_id){0};
        return true;
    }
    if (changed && entry) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return q3_rollback_spawn(game, actor, error);
        entry->state.missile.trajectory.base = body.origin;
        entry->state.missile.trajectory.delta = body.velocity;
        entry->state.missile.trajectory.time_ms = game->now_ms;
    }
    if (out)
        *out = actor;
    return true;
}
static bool set_origin(qa_q3_game *game, qa_actor_id actor, qa_vec3 origin, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    body.origin = origin;
    body.velocity = qa_v3(0, 0, 0);
    entry->state.missile.trajectory =
        (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = origin};
    return qa_world_body_write(game->options.services.world, actor, &body, error);
}
static bool impact_event(qa_q3_game *game, qa_actor_id actor, qa_actor_id target, qa_vec3 normal,
                         int32_t flags, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    qa_combat_state state;
    qa_error ignored = {0};
    bool player = q3_is_player(game, target);
    bool flesh = player &&
                 qa_combat_read(game->options.services.combat, target, &state, &ignored) &&
                 state.can_take_damage;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    uint8_t parameter = 0;
    (void)qa_normal_byte(normal, &parameter);
    return q3_event(game, actor, target, QA_BUILTIN_IMPACT, flesh ? 50 : (flags & 0x1000 ? 52 : 51),
                    parameter, body.origin, qa_v3((float)entry->state.missile.weapon, 0, 0), normal,
                    error);
}
bool q3_missile_explode(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    q3_missile missile = entry->state.missile;
    qa_vec3 origin;
    if (!qa_trajectory_position(&missile.trajectory, game->now_ms, 800, &origin, error))
        return false;
    origin = qa_physics_q3_snap(origin);
    if (!set_origin(game, actor, origin, error) ||
        !impact_event(game, actor, (qa_actor_id){0}, qa_v3(0, 0, 1), 0, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.missile.phase = Q3_MISSILE_EVENT;
    entry->state.missile.event_at = game->now_ms;
    entry->state.missile.loop_sound = 0;
    if (!qa_world_set_collision(game->options.services.world, actor, NULL, error))
        return false;
    bool accuracy = false;
    if (missile.splash &&
        !q3_radius(game, actor, missile.owner, missile.weapon, missile.splash_method, origin,
                   missile.splash, missile.radius, actor, &accuracy, error))
        return false;
    if (accuracy)
        q3_credit_accuracy(game, missile.owner);
    if (q3_actor_get(game, missile.trigger) &&
        !qa_session_release(game->options.services.session, missile.trigger, error))
        return false;
    return !q3_actor_get(game, actor) ||
           qa_world_link(game->options.services.world, actor, NULL, error);
}
static bool attach_hook(qa_q3_game *game, qa_actor_id actor, const qa_trace_result *trace,
                        qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_actor_id owner = entry->state.missile.owner;
    q3_actor *player = q3_actor_get(game, owner);
    if (!player || player->kind != Q3_ACTOR_PLAYER)
        return qa_session_release(game->options.services.session, actor, error);
    qa_vec3 origin = trace->end;
    if (q3_is_player(game, trace->actor)) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, trace->actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        origin = qa_vec_add(body.origin,
                            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        entry->state.missile.attached = trace->actor;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    origin = qa_physics_q3_snap_towards(origin, entry->state.missile.trajectory.base);
    if (!set_origin(game, actor, origin, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.missile.phase = Q3_MISSILE_HOOK;
    entry->state.missile.think_at = q3_add_time(game->now_ms, 50);
    player = q3_actor_get(game, owner);
    if (player && player->kind == Q3_ACTOR_PLAYER) {
        player->state.player.grapple_pull = true;
        player->state.player.grapple_point = origin;
    }
    if (!impact_event(game, actor, trace->actor, trace->contact_plane.normal, trace->surface_flags,
                      error))
        return false;
    return !q3_actor_get(game, actor) ||
           qa_world_link(game->options.services.world, actor, NULL, error);
}
static bool stick_mine(qa_q3_game *game, qa_actor_id actor, const qa_trace_result *trace,
                       qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_combat_state state;
    qa_error ignored = {0};
    bool living_player = q3_is_player(game, trace->actor) &&
                         qa_combat_read(game->options.services.combat, trace->actor, &state,
                                        &ignored) && state.health > 0;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    q3_actor *target = q3_actor_get(game, trace->actor);
    if (living_player) {
        q3_actor *prior = NULL;
        for (uint32_t i = 0; i < game->capacity; ++i) {
            q3_actor *candidate = &game->actors[i];
            if (candidate->kind == Q3_ACTOR_MISSILE &&
                candidate->state.missile.phase == Q3_MISSILE_PROX_PLAYER &&
                qa_actor_id_equal(candidate->state.missile.attached, trace->actor) &&
                q3_actor_get(game, candidate->actor)) {
                prior = candidate;
                break;
            }
        }
        if (prior && prior->kind == Q3_ACTOR_MISSILE) {
            prior->state.missile.splash += entry->state.missile.splash;
            prior->state.missile.radius *= 1.5f;
            return qa_session_release(game->options.services.session, actor, error);
        }
        bool invulnerable = false;
        if (target && target->kind == Q3_ACTOR_PLAYER) {
            target->state.player.attached_mine = actor;
            target->state.player.flags |= 2u;
            invulnerable = target->state.player.invulnerability_until > game->now_ms;
        }
        entry->state.missile.attached = trace->actor;
        entry->state.missile.phase = Q3_MISSILE_PROX_PLAYER;
        entry->state.missile.flags |= 0x80u;
        entry->state.missile.think_at = q3_add_time(game->now_ms, invulnerable ? 2000 : 10000);
        if (!qa_world_set_collision(game->options.services.world, actor, NULL, error))
            return false;
    } else {
        qa_vec3 origin =
            qa_physics_q3_snap_towards(trace->end, entry->state.missile.trajectory.base);
        if (!set_origin(game, actor, origin, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        entry->state.missile.normal = trace->contact_plane.normal;
        entry->state.missile.attached = trace->actor;
        entry->state.missile.phase = Q3_MISSILE_PROX_ARMING;
        entry->state.missile.think_at = q3_add_time(game->now_ms, 2000);
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        body.bounds = (qa_bounds){qa_v3(-4, -4, -4), qa_v3(4, 4, 4)};
        if (!qa_world_body_write(game->options.services.world, actor, &body, error) ||
            !qa_world_link(game->options.services.world, actor, NULL, error))
            return false;
    }
    return q3_event(game, actor, trace->actor, QA_BUILTIN_IMPACT, 66, trace->surface_flags,
                    trace->end, qa_v3(0, 0, 0), trace->contact_plane.normal, error);
}
static bool missile_impact(qa_q3_game *game, qa_actor_id actor, const qa_trace_result *trace,
                           qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    q3_missile missile = entry->state.missile;
    qa_combat_state target;
    qa_error ignored = {0};
    bool damageable =
        trace->hit == QA_TRACE_HIT_ACTOR &&
        qa_combat_read(game->options.services.combat, trace->actor, &target, &ignored) &&
        target.can_take_damage;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    if (!damageable && (missile.flags & 0x30u)) {
        bool stopped;
        if (!qa_physics_q3_bounce(&game->physics, actor, &entry->state.missile.trajectory, trace,
                                  game->previous_ms, game->now_ms, (missile.flags & 0x20u) != 0,
                                  &stopped, error))
            return false;
        return q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_IMPACT, 44, 0, trace->end,
                        qa_v3(0, 0, 0), trace->contact_plane.normal, error);
    }
    q3_actor *victim = q3_actor_get(game, trace->actor);
    if (damageable && missile.weapon != QA_Q3_W_PROX && victim && victim->kind == Q3_ACTOR_PLAYER &&
        victim->state.player.invulnerability_until > game->now_ms &&
        game->options.product == QA_Q3_TEAM_ARENA) {
        qa_vec3 point, normal;
        bool hit;
        if (!q3_invulnerability(game, trace->actor, missile.trajectory.delta,
                                missile.trajectory.base, &point, &normal, &hit, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        if (hit) {
            qa_trace_result reflected = *trace;
            reflected.contact_plane.normal = normal;
            reflected.plane.normal = normal;
            bool stopped;
            if (!qa_physics_q3_bounce(&game->physics, actor, &entry->state.missile.trajectory,
                                      &reflected, game->previous_ms, game->now_ms, false, &stopped,
                                      error))
                return false;
        }
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        entry->state.missile.pass = trace->actor;
        return true;
    }
    bool direct_accuracy = false;
    if (damageable && missile.damage) {
        direct_accuracy = q3_accuracy(game, trace->actor, missile.owner);
        if (direct_accuracy)
            q3_credit_accuracy(game, missile.owner);
        qa_vec3 velocity;
        if (!qa_trajectory_velocity(&missile.trajectory, game->now_ms, 800, &velocity, error))
            return false;
        if (qa_vec_length(velocity) == 0)
            velocity.z = 1;
        if (!q3_damage(game, trace->actor, missile.owner, actor, missile.weapon, missile.method, 0,
                       missile.damage, velocity, missile.damage_point, false, NULL, error))
            return false;
        if (!q3_actor_get(game, actor))
            return true;
    }
    if (missile.weapon == QA_Q3_W_GRAPPLE)
        return attach_hook(game, actor, trace, error);
    if (missile.weapon == QA_Q3_W_PROX)
        return stick_mine(game, actor, trace, error);
    if (!impact_event(game, actor, trace->actor, trace->contact_plane.normal, trace->surface_flags,
                      error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    entry->state.missile.phase = Q3_MISSILE_EVENT;
    entry->state.missile.event_at = game->now_ms;
    qa_vec3 origin = qa_physics_q3_snap_towards(trace->end, missile.trajectory.base);
    if (!set_origin(game, actor, origin, error) ||
        !qa_world_set_collision(game->options.services.world, actor, NULL, error))
        return false;
    bool accuracy = false;
    if (missile.splash &&
        !q3_radius(game, actor, missile.owner, missile.weapon, missile.splash_method, origin,
                   missile.splash, missile.radius, trace->actor, &accuracy, error))
        return false;
    if (accuracy && !direct_accuracy)
        q3_credit_accuracy(game, missile.owner);
    return !q3_actor_get(game, actor) ||
           qa_world_link(game->options.services.world, actor, NULL, error);
}
static bool activate_mine(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    qa_combat_state combat = {.health = 1, .mass = 1, .can_take_damage = true};
    if (!qa_combat_create_actor(game->options.services.combat, actor, &combat, error))
        return false;
    float radius = entry->state.missile.radius;
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = Q3_CONTENTS_TRIGGER,
                                    .role = QA_COLLISION_TRIGGER};
    qa_builtin_spawn spawn = {
        .owner = game->options.owner,
        .body = {.origin = body.origin,
                 .bounds = {qa_v3(-radius, -radius, -radius), qa_v3(radius, radius, radius)}},
        .collision = &collision,
        .link = true};
    qa_actor_id trigger;
    if (!qa_builtin_spawn_actor(&game->options.services, &spawn, &trigger, error))
        return false;
    game->actors[trigger.slot] =
        (q3_actor){.actor = trigger, .kind = Q3_ACTOR_PROX_TRIGGER, .alpha = 1,
                   .state.trigger = {actor}};
    entry = q3_actor_get(game, actor);
    if (!entry)
        return qa_session_release(game->options.services.session, trigger, error);
    entry->state.missile.trigger = trigger;
    entry->state.missile.phase = Q3_MISSILE_PROX_ARMED;
    if (!qa_builtin_resource(&game->options.services, "sound/weapons/proxmine/wstbtick.wav",
                             &entry->state.missile.loop_sound, error))
        return false;
    entry->state.missile.think_at =
        q3_add_time(game->now_ms, game->options.rules.proximity_timeout_ms);
    qa_actor_collision mine_collision = {.family = QA_COLLISION_Q3,
                                         .shape = QA_SHAPE_BOX,
                                         .contents = Q3_CONTENTS_BODY,
                                         .role = QA_COLLISION_SOLID};
    return qa_world_set_collision(game->options.services.world, actor, &mine_collision, error) &&
           qa_world_link(game->options.services.world, actor, NULL, error);
}
bool q3_missile_trigger(qa_q3_game *game, qa_actor_id actor, qa_actor_id player, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_PROX_ARMED)
        return true;
    if (!q3_is_player(game, player))
        return true;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_PROX_ARMED)
        return true;
    qa_body_state mine_body, body;
    qa_combat_state combat;
    if (!qa_world_body_read(game->options.services.world, actor, &mine_body, error) ||
        !qa_world_body_read(game->options.services.world, player, &body, error) ||
        !qa_combat_read(game->options.services.combat, player, &combat, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_PROX_ARMED)
        return true;
    if (qa_vec_length(qa_vec_sub(body.origin, mine_body.origin)) > entry->state.missile.radius ||
        (game->options.rules.game_type >= 3 && entry->state.missile.team == combat.team))
        return true;
    qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q3);
    policy.contents_mask = 1;
    bool visible;
    if (!qa_builtin_can_damage(&game->options.services, mine_body.origin, player, (qa_actor_id){0},
                               policy, false, &visible, error))
        return false;
    if (!visible)
        return true;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_PROX_ARMED)
        return true;
    qa_actor_id trigger = entry->state.missile.trigger;
    entry->state.missile.phase = Q3_MISSILE_PROX_TRIGGERED;
    entry->state.missile.think_at = q3_add_time(game->now_ms, 500);
    entry->state.missile.loop_sound = 0;
    if (!q3_event(game, actor, player, QA_BUILTIN_IMPACT, 67, 0, mine_body.origin, qa_v3(0, 0, 0),
                  qa_v3(0, 0, 0), error))
        return false;
    return !q3_actor_get(game, trigger) ||
           qa_session_release(game->options.services.session, trigger, error);
}
bool q3_missile_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    if (entry->state.missile.phase == Q3_MISSILE_FLIGHT) {
        bool changed = false;
        if (!qa_builtin_step_projectile(&game->options.services, actor,
                                        (uint64_t)(uint32_t)game->previous_ms * UINT64_C(1000000),
                                        &changed, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        if (changed) {
            qa_body_state body;
            if (!qa_world_body_read(game->options.services.world, actor, &body, error))
                return false;
            entry->state.missile.trajectory.base = body.origin;
            entry->state.missile.trajectory.delta = body.velocity;
            entry->state.missile.trajectory.time_ms = game->previous_ms;
        }
    }
    q3_missile missile = entry->state.missile;
    if (missile.phase == Q3_MISSILE_EVENT)
        return q3_sub_time(game->now_ms, missile.event_at) > 300
                   ? qa_session_release(game->options.services.session, actor, error)
                   : true;
    if (missile.phase == Q3_MISSILE_HOOK) {
        if (game->now_ms < missile.think_at)
            return true;
        q3_actor *owner = q3_actor_get(game, missile.owner);
        if (!owner || owner->kind != Q3_ACTOR_PLAYER)
            return qa_session_release(game->options.services.session, actor, error);
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        if (missile.attached.registry) {
            qa_body_state target;
            if (!qa_actors_get(qa_session_actors(game->options.services.session), missile.attached))
                return qa_session_release(game->options.services.session, actor, error);
            if (!qa_world_body_read(game->options.services.world, missile.attached, &target, error))
                return false;
            qa_vec3 center =
                qa_vec_add(target.origin,
                           qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
            body.origin = qa_physics_q3_snap_towards(center, body.origin);
            if (!set_origin(game, actor, body.origin, error))
                return false;
        }
        owner = q3_actor_get(game, missile.owner);
        entry = q3_actor_get(game, actor);
        if (!entry)
            return true;
        if (!owner || owner->kind != Q3_ACTOR_PLAYER)
            return qa_session_release(game->options.services.session, actor, error);
        owner->state.player.grapple_point = body.origin;
        entry->state.missile.think_at = q3_add_time(game->now_ms, 50);
        return qa_world_link(game->options.services.world, actor, NULL, error);
    }
    if (missile.phase != Q3_MISSILE_FLIGHT) {
        if (game->now_ms < missile.think_at)
            return true;
        if (missile.phase == Q3_MISSILE_PROX_ARMING)
            return activate_mine(game, actor, error);
        if (missile.phase == Q3_MISSILE_PROX_PLAYER) {
            q3_actor *player = q3_actor_get(game, missile.attached);
            if (!qa_actors_get(qa_session_actors(game->options.services.session), missile.attached))
                return qa_session_release(game->options.services.session, actor, error);
            if (player && player->kind == Q3_ACTOR_PLAYER)
                player->state.player.flags &= ~2u;
            qa_body_state body;
            if (!qa_world_body_read(game->options.services.world, missile.attached, &body, error))
                return false;
            if (player && player->kind == Q3_ACTOR_PLAYER &&
                player->state.player.invulnerability_until > game->now_ms) {
                if (!q3_damage(game, missile.attached, missile.owner, missile.owner, QA_Q3_W_PROX,
                               27, 4, 1000, qa_v3(0, 0, 0), missile.damage_point, false, NULL,
                               error))
                    return false;
                player = q3_actor_get(game, missile.attached);
                if (player && player->kind == Q3_ACTOR_PLAYER)
                    player->state.player.invulnerability_until = 0;
                if (!q3_event(game, missile.attached, actor, QA_BUILTIN_EXPLOSION, 72, 0,
                              body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error))
                    return false;
                return !q3_actor_get(game, actor) ||
                       qa_session_release(game->options.services.session, actor, error);
            }
            if (!set_origin(game, actor, body.origin, error))
                return false;
        }
        return q3_missile_explode(game, actor, error);
    }
    qa_trace_result trace;
    if (!qa_physics_q3_missile_move(&game->physics, actor, &entry->state.missile.trajectory,
                                    game->now_ms, missile.pass, &trace, error))
        return false;
    if (trace.fraction < 1) {
        if (trace.surface_flags & Q3_SURF_NOIMPACT)
            return qa_session_release(game->options.services.session, actor, error);
        if (!missile_impact(game, actor, &trace, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->state.missile.phase != Q3_MISSILE_FLIGHT)
        return true;
    if (missile.weapon == QA_Q3_W_PROX && !entry->state.missile.left_owner) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        qa_trace_result overlap;
        if (!q3_trace(game, body.origin, body.origin, (qa_actor_id){0}, Q3_MASK_SHOT, &overlap,
                      error))
            return false;
        if (!overlap.start_solid || !qa_actor_id_equal(overlap.actor, missile.owner)) {
            entry->state.missile.left_owner = true;
            entry->state.missile.pass = (qa_actor_id){0};
        }
    }
    if (game->now_ms >= entry->state.missile.think_at) {
        if (missile.weapon == QA_Q3_W_GRAPPLE)
            return qa_session_release(game->options.services.session, actor, error);
        return q3_missile_explode(game, actor, error);
    }
    return true;
}
