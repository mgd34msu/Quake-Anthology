#include "internal.h"
#include "qa/game_type.h"

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
    qa_builtin_snapshot_frame *frame = q3_bounds_snapshot(
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
                   .inventory_provider = game->options.owner,
                   .movement_provider = game->options.owner,
                   .weapon = qa_q3_weapon_item(game, weapon, false),
                   .powerup_owner = game->options.owner,
                   .powerup_applied = true,
                   .cause = {.kind = QA_CAUSE_Q3, .source.q3 = {method, 1}}}};
    if (game->options.hooks.attack_providers &&
        !game->options.hooks.attack_providers(game->options.hooks.context, attacker,
            attack.attack.weapon, &attack.attack.inventory_provider,
            &attack.attack.movement_provider, error)) {
        qa_builtin_snapshot_release(frame);
        return false;
    }
    attack.trace.contents_mask = 1;
    bool ok = qa_builtin_radius_damage(&game->options.services, &attack, NULL, error);
    qa_builtin_snapshot_release(frame);
    if (accuracy)
        *accuracy = context.accuracy;
    return ok;
}
static qa_vec3 nail_direction(qa_vec3 value) {
    float length = (float)sqrt((double)qa_vec_dot(value, value));
    return length == 0 ? value : qa_vec_scale(value, (1.0f / length));
}

bool q3_launch(qa_q3_game *game, qa_actor_id owner, qa_q3_weapon weapon, qa_vec3 start,
               qa_vec3 direction, qa_vec3 right, qa_vec3 up, float factor, qa_actor_id *out,
               qa_error *error) {
    float speed = 0, damage = 0, splash = 0, radius = 0;
    const char *classname = NULL;
    int32_t duration = 10000, method = 0, splash_method = 0;
    bool gravity = false;
    switch (weapon) {
    case QA_Q3_W_GRENADE:
        classname = "grenade";
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
        classname = "rocket";
        speed = 900;
        duration = 15000;
        damage = 100;
        splash = 100;
        radius = 120;
        method = 6;
        splash_method = 7;
        break;
    case QA_Q3_W_PLASMA:
        classname = "plasma";
        speed = 2000;
        damage = 20;
        splash = 15;
        radius = 20;
        method = 8;
        splash_method = 9;
        break;
    case QA_Q3_W_BFG:
        classname = "bfg";
        speed = 2000;
        damage = 100;
        splash = 100;
        radius = 120;
        method = 12;
        splash_method = 13;
        break;
    case QA_Q3_W_GRAPPLE:
        classname = "hook";
        speed = 800;
        duration = 10000;
        method = game->options.product == QA_Q3_ARENA ? 23 : 28;
        break;
    case QA_Q3_W_NAIL:
        classname = "nail";
        damage = 20;
        method = 23;
        break;
    case QA_Q3_W_PROX:
        classname = "prox mine";
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
    qa_vec3 velocity = weapon == QA_Q3_W_NAIL ? qa_v3(0, 0, 0)
                                             : qa_vec_scale(direction, speed);
    velocity = qa_physics_q3_snap(velocity);
    const qa_actor_record *owner_record = qa_actors_get(qa_session_actors(game->options.services.session), owner);
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .owner = owner_record && owner_record->owner == game->options.owner && owner_record->has_source ?
                                        qa_actor_reference_source(owner_record->owner, owner_record->source_slot) : qa_actor_reference_lifetime(owner),
                                    .role = QA_COLLISION_SOLID};
    qa_string_id definition;
    if (!qa_builtin_resource(&game->options.services, classname, &definition, error)) return false;
    qa_builtin_spawn spawn = {.owner = game->options.owner,
                              .definition = definition,
                              .body = {.origin = start, .velocity = velocity},
                              .collision = &collision};
    qa_actor_id actor;
    if (!q3_spawn_actor(game, &spawn, &actor, error))
        return false;
    uint32_t source_slot = game->source_numbers[actor.slot];
    game->source_entities[source_slot].server_flags = 0x80u;
    game->source_entities[source_slot].owner_number = q3_entity_number(game, owner);
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
                           .time_ms = q3_add_time(game->now_ms, -50),
                           .base = start,
                           .delta = velocity}}};
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_rollback_spawn(game, actor, error);
    source->type = 3;
    q3_postgame_native_think_assigned(game, actor);
    if (weapon == QA_Q3_W_GRAPPLE)
        source->other_entity = q3_entity_number(game, owner);
    if (weapon == QA_Q3_W_NAIL) {
        float angle = ((q3_random(game) * Q3_PI) * 2.0f);
        float vertical = ((((float)sin((double)angle) * q3_crandom(game)) * 500.0f) * 16.0f);
        float horizontal = ((((float)cos((double)angle) * q3_crandom(game)) * 500.0f) * 16.0f);
        qa_vec3 end = qa_vec_add(
            qa_vec_add(qa_vec_add(start, qa_vec_scale(direction, 131072.0f)),
                       qa_vec_scale(right, horizontal)),
            qa_vec_scale(up, vertical));
        float nail_speed = (555.0f + (q3_random(game) * 1800.0f));
        velocity = qa_physics_q3_snap(
            qa_vec_scale(nail_direction(qa_vec_sub(end, start)), nail_speed));
        entry->state.missile.trajectory.time_ms = game->now_ms;
        entry->state.missile.trajectory.delta = velocity;
        spawn.body.velocity = velocity;
        if (!qa_world_body_write(game->options.services.world, actor, &spawn.body, error))
            return q3_rollback_spawn(game, actor, error);
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE) {
        if (out)
            *out = (qa_actor_id){0};
        return true;
    }
    if (weapon == QA_Q3_W_PROX) {
        int32_t team;
        uint32_t owner_slot;
        if (qa_q3_native_client_slot(game, owner, &owner_slot, NULL)) {
            if (!game->options.hooks.source_team) {
                q3_fail(error, "Q3 proximity launch has no source session team owner");
                return q3_rollback_spawn(game, actor, error);
            }
            team = game->options.hooks.source_team(game->options.hooks.context, owner);
        } else {
            qa_combat_state combat;
            if (!qa_combat_read(game->options.services.combat, owner, &combat, error))
                return q3_rollback_spawn(game, actor, error);
            team = (int32_t)combat.team;
        }
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE) {
            if (out) *out = (qa_actor_id){0};
            return true;
        }
        entry->state.missile.team = (qa_team_id)team;
    }
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
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE) {
            if (out) *out = (qa_actor_id){0};
            return true;
        }
        entry->state.missile.trajectory.base = body.origin;
        entry->state.missile.trajectory.delta = body.velocity;
        entry->state.missile.trajectory.time_ms = game->now_ms;
    }
    if (!q3_wire_entity_ready(game, actor, error))
        return q3_rollback_spawn(game, actor, error);
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
    int32_t event = flesh ? 50 : (flags & 0x1000 ? 52 : 51);
    if (!q3_wire_add_event(game, actor, event, parameter, error))
        return false;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return true;
    if (flesh)
        source->other_entity = q3_entity_number(game, target);
    return q3_event(game, actor, target, QA_BUILTIN_IMPACT, event,
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
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_fail(error, "Q3 missile explosion lost its native event row");
    source->type = 0;
    source->free_after_event = true;
    source->loop_sound = 0;
    if (!qa_world_set_collision(game->options.services.world, actor, NULL, error))
        return false;
    bool accuracy = false;
    if (missile.splash != 0 &&
        !q3_radius(game, actor, missile.owner, missile.weapon, missile.splash_method, origin,
                   missile.splash, missile.radius, actor, &accuracy, error))
        return false;
    if (accuracy)
        q3_credit_accuracy(game, missile.owner);
    if (q3_actor_get(game, missile.trigger) &&
        !qa_session_release(game->options.services.session, missile.trigger, error))
        return false;
    return !q3_actor_get(game, actor) ||
           qa_q3_wire_link(game, actor, NULL, error);
}
static bool attach_hook(qa_q3_game *game, qa_actor_id actor, const qa_trace_result *trace,
                        qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_actor_id impact;
    if (!q3_spawn_actor(game, &(qa_builtin_spawn){.owner = game->options.owner,
            .definition = game->source_noclass}, &impact, error))
        return false;
    uint32_t impact_slot;
    if (!qa_q3_source_actor_slot(game, impact, &impact_slot, error))
        return q3_rollback_spawn(game, impact, error);
    game->actors[impact.slot] = (q3_actor){.actor = impact, .kind = Q3_ACTOR_TEMPORARY,
        .alpha = 1, .state.temporary.entity.number = (int32_t)impact_slot};
    q3_wire_entity_source *impact_source = q3_wire_entity(game, impact);
    if (!impact_source)
        return q3_rollback_spawn(game, impact, error);
    impact_source->free_after_event = true;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return qa_session_release(game->options.services.session, impact, error);
    qa_actor_id owner = entry->state.missile.owner;
    q3_actor *player = q3_actor_get(game, owner);
    if (!player || player->kind != Q3_ACTOR_PLAYER)
        return qa_session_release(game->options.services.session, actor, error) &&
               qa_session_release(game->options.services.session, impact, error);
    qa_vec3 origin = trace->end;
    qa_combat_state target_combat;
    qa_error ignored = {0};
    bool flesh = q3_is_player(game, trace->actor) &&
        qa_combat_read(game->options.services.combat, trace->actor, &target_combat, &ignored) &&
        target_combat.can_take_damage;
    uint8_t parameter = 0;
    (void)qa_normal_byte(trace->contact_plane.normal, &parameter);
    if (!q3_wire_add_event(game, impact, flesh ? 50 : 51, parameter, error))
        return q3_rollback_spawn(game, impact, error);
    qa_q3_entity *event = q3_wire_temporary(game, impact);
    if (!event)
        return true;
    if (flesh)
        event->otherEntityNum = q3_entity_number(game, trace->actor);
    if (flesh) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, trace->actor, &body, error))
            return q3_rollback_spawn(game, impact, error);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return qa_session_release(game->options.services.session, impact, error);
        origin = qa_vec_add(body.origin,
                            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        entry->state.missile.attached = trace->actor;
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return qa_session_release(game->options.services.session, impact, error);
    origin = qa_physics_q3_snap_towards(origin, entry->state.missile.trajectory.base);
    if (!set_origin(game, actor, origin, error))
        return q3_rollback_spawn(game, impact, error);
    entry = q3_actor_get(game, actor);
    if (!entry)
        return qa_session_release(game->options.services.session, impact, error);
    entry->state.missile.phase = Q3_MISSILE_HOOK;
    q3_postgame_native_think_assigned(game, actor);
    entry->state.missile.think_at = q3_add_time(game->now_ms, 50);
    player = q3_actor_get(game, owner);
    if (player && player->kind == Q3_ACTOR_PLAYER) {
        player->state.player.grapple_pull = true;
        player->state.player.selected_pm_flags |= 0x800u;
        player->state.player.grapple_point = origin;
    }
    event = q3_wire_temporary(game, impact);
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!event || !source) {
        q3_fail(error, "Q3 grapple impact lost its source hook or event row");
        return q3_rollback_spawn(game, impact, error);
    }
    source->type = 9;
    event->eType = 0;
    event->pos = (qa_q3_trajectory){.base = {origin.x, origin.y, origin.z}};
    if (!qa_world_body_write(game->options.services.world, impact,
                              &(qa_body_state){.origin = origin}, error))
        return q3_rollback_spawn(game, impact, error);
    event = q3_wire_temporary(game, impact);
    if (!event)
        return true;
    if (q3_actor_get(game, actor) && !qa_q3_wire_link(game, actor, NULL, error))
        return q3_rollback_spawn(game, impact, error);
    if (!q3_actor_get(game, impact))
        return true;
    if (!qa_q3_wire_link(game, impact, NULL, error) ||
        !q3_wire_entity_ready(game, impact, error))
        return q3_rollback_spawn(game, impact, error);
    return !q3_actor_get(game, actor) ||
        q3_event(game, actor, trace->actor, QA_BUILTIN_IMPACT, flesh ? 50 : 51, parameter,
                  origin, qa_v3(QA_Q3_W_GRAPPLE, 0, 0), trace->contact_plane.normal, error);
}
static bool stick_mine(qa_q3_game *game, qa_actor_id actor, const qa_trace_result *trace,
                       qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    if (entry->state.missile.trajectory.type != QA_TRAJECTORY_GRAVITY)
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
        if (entry->state.missile.flags & 0x80u)
            return true;
        if (!q3_wire_add_event(game, actor, 66, 0, error))
            return false;
        uint32_t target_slot;
        q3_wire_entity_source *target_source = qa_q3_native_client_slot(
            game, trace->actor, &target_slot, NULL) ? q3_wire_entity(game, trace->actor) : NULL;
        bool ticking = target_source ? (target_source->flags & 2) != 0
            : target && target->kind == Q3_ACTOR_PLAYER && (target->state.player.flags & 2u);
        if (ticking) {
            q3_actor *prior = target && target->kind == Q3_ACTOR_PLAYER
                ? q3_actor_get(game, target->state.player.attached_mine) : NULL;
            if (!prior || prior->kind != Q3_ACTOR_MISSILE ||
                prior->state.missile.phase != Q3_MISSILE_PROX_PLAYER ||
                !qa_actor_id_equal(prior->state.missile.attached, trace->actor))
                return q3_fail(error, "Q3 ticking player lost its genuine proximity activator");
            prior->state.missile.splash += entry->state.missile.splash;
            prior->state.missile.radius *= 1.5f;
            entry->state.missile.phase = Q3_MISSILE_PROX_DISCARD;
            q3_postgame_native_think_assigned(game, actor);
            entry->state.missile.think_at = game->now_ms;
            return true;
        }
        bool invulnerable = false;
        if (target && target->kind == Q3_ACTOR_PLAYER) {
            target->state.player.attached_mine = actor;
            target->state.player.flags |= 2u;
            invulnerable = target->state.player.invulnerability_until > game->now_ms;
        }
        entry->state.missile.attached = trace->actor;
        entry->state.missile.phase = Q3_MISSILE_PROX_PLAYER;
        q3_postgame_native_think_assigned(game, actor);
        entry->state.missile.flags |= 0x80u;
        entry->state.missile.trajectory.type = QA_TRAJECTORY_LINEAR;
        entry->state.missile.trajectory.delta = qa_v3(0, 0, 0);
        uint32_t source_slot;
        if (!qa_q3_source_actor_slot(game, actor, &source_slot, error)) return false;
        game->source_entities[source_slot].server_flags |= 1u;
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
        q3_postgame_native_think_assigned(game, actor);
        entry->state.missile.think_at = q3_add_time(game->now_ms, 2000);
        q3_wire_entity_source *source = q3_wire_entity(game, actor);
        if (!source)
            return q3_fail(error, "Q3 stuck proximity mine lost its source angles");
        qa_vec3 normal = trace->contact_plane.normal;
        float yaw = normal.x == 0 ? (normal.y > 0 ? 90 : normal.y < 0 ? 270 : 0)
            : (((float)atan2((double)normal.y, (double)normal.x) * 180) / Q3_PI);
        if (yaw < 0) yaw = (yaw + 360);
        float pitch = normal.x == 0 && normal.y == 0 ? (normal.z > 0 ? 90 : 270)
            : (((float)atan2((double)normal.z,
                (double)(float)sqrt((double)((normal.x * normal.x) + (normal.y * normal.y)))) * 180) / Q3_PI);
        if (pitch < 0) pitch = (pitch + 360);
        source->authored_angles = qa_v3((-pitch + 90), yaw, 0);
        if (!q3_wire_add_event(game, actor, 66, trace->surface_flags, error))
            return false;
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        body.bounds = (qa_bounds){qa_v3(-4, -4, -4), qa_v3(4, 4, 4)};
        if (!qa_world_body_write(game->options.services.world, actor, &body, error) ||
            !qa_q3_wire_link(game, actor, NULL, error))
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
        return q3_wire_add_event(game, actor, 44, 0, error) &&
               q3_event(game, actor, (qa_actor_id){0}, QA_BUILTIN_IMPACT, 44, 0, trace->end,
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
    if (damageable && missile.damage != 0) {
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
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_fail(error, "Q3 missile impact lost its native event row");
    source->type = 0;
    source->free_after_event = true;
    qa_vec3 origin = qa_physics_q3_snap_towards(trace->end, missile.trajectory.base);
    if (!set_origin(game, actor, origin, error) ||
        !qa_world_set_collision(game->options.services.world, actor, NULL, error))
        return false;
    bool accuracy = false;
    if (missile.splash != 0 &&
        !q3_radius(game, actor, missile.owner, missile.weapon, missile.splash_method, origin,
                   missile.splash, missile.radius, trace->actor, &accuracy, error))
        return false;
    if (accuracy && !direct_accuracy)
        q3_credit_accuracy(game, missile.owner);
    return !q3_actor_get(game, actor) ||
           qa_q3_wire_link(game, actor, NULL, error);
}
static bool activate_mine(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry)
        return true;
    qa_vec3 origin = entry->state.missile.trajectory.base;
    entry->state.missile.phase = Q3_MISSILE_PROX_ARMED;
    q3_postgame_native_think_assigned(game, actor);
    entry->state.missile.think_at =
        q3_add_time(game->now_ms, game->options.rules.proximity_timeout_ms);
    qa_combat_state combat = {.health = 1, .mass = 1, .can_take_damage = true};
    if (!qa_combat_set_health(game->options.services.combat, actor, combat.health, error) ||
        !qa_combat_set_traits(game->options.services.combat, actor, &combat, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    int32_t sound_index;
    if (!qa_q3_sound_index(game, "sound/weapons/proxmine/wstbtick.wav", &sound_index, error))
        return false;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return true;
    source->loop_sound = sound_index;
    qa_string_id resource;
    if (!qa_builtin_resource(&game->options.services, "sound/weapons/proxmine/wstbtick.wav",
                             &resource, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    entry->state.missile.loop_sound = resource;
    float radius = entry->state.missile.radius;
    qa_actor_collision collision = {.family = QA_COLLISION_Q3,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = Q3_CONTENTS_TRIGGER,
                                    .role = QA_COLLISION_TRIGGER};
    qa_builtin_spawn spawn = {
        .owner = game->options.owner,
        .body = {.origin = origin,
                 .bounds = {qa_v3(-radius, -radius, -radius), qa_v3(radius, radius, radius)}},
        .collision = &collision};
    if (!qa_builtin_resource(&game->options.services, "proxmine_trigger",
                              &spawn.definition, error)) return false;
    qa_actor_id trigger;
    if (!q3_spawn_actor(game, &spawn, &trigger, error))
        return false;
    game->actors[trigger.slot] =
        (q3_actor){.actor = trigger, .kind = Q3_ACTOR_PROX_TRIGGER, .alpha = 1,
                   .state.trigger = {actor}};
    q3_wire_entity_source *trigger_source = q3_wire_entity(game, trigger);
    if (!trigger_source)
        return q3_rollback_spawn(game, trigger, error);
    trigger_source->position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY,
                                               .base = origin};
    if (!qa_q3_wire_link(game, trigger, NULL, error) ||
        !q3_wire_entity_ready(game, trigger, error))
        return q3_rollback_spawn(game, trigger, error);
    entry = q3_actor_get(game, actor);
    if (!entry)
        return qa_session_release(game->options.services.session, trigger, error);
    entry->state.missile.trigger = trigger;
    return true;
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
    qa_actor_id trigger = entry->state.missile.trigger;
    q3_wire_entity_source *trigger_source = q3_wire_entity(game, trigger);
    if (!trigger_source || !q3_actor_get(game, trigger))
        return q3_fail(error, "Q3 armed proximity mine lost its actual trigger");
    qa_vec3 origin = trigger_source->position.base;
    uint32_t player_slot;
    int32_t team = (int32_t)combat.team;
    if (qa_q3_native_client_slot(game, player, &player_slot, NULL)) {
        qa_q3_entity source_player;
        qa_q3_wire_visibility visibility;
        if (!qa_q3_wire_entity_read(game, player_slot, &source_player, &visibility, error))
            return false;
        body.origin = qa_v3(source_player.pos.base[0], source_player.pos.base[1],
                             source_player.pos.base[2]);
        if (!game->options.hooks.source_team)
            return q3_fail(error, "Q3 proximity trigger has no source session team owner");
        team = game->options.hooks.source_team(game->options.hooks.context, player);
    }
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_PROX_ARMED)
        return true;
    if (qa_vec_length(qa_vec_sub(body.origin, origin)) > entry->state.missile.radius ||
        (qa_game_type_has_allies(game->options.rules.game_type) && entry->state.missile.team == (qa_team_id)team))
        return true;
    qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q3);
    policy.contents_mask = 1;
    bool visible;
    if (!qa_builtin_can_damage(&game->options.services, origin, player, (qa_actor_id){0},
                               policy, false, &visible, error))
        return false;
    if (!visible)
        return true;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE ||
        entry->state.missile.phase != Q3_MISSILE_PROX_ARMED)
        return true;
    entry->state.missile.phase = Q3_MISSILE_PROX_TRIGGERED;
    q3_postgame_native_think_assigned(game, actor);
    entry->state.missile.think_at = q3_add_time(game->now_ms, 500);
    entry->state.missile.loop_sound = 0;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_fail(error, "Q3 triggered proximity mine lost its source loop sound");
    source->loop_sound = 0;
    if (!q3_wire_add_event(game, actor, 67, 0, error))
        return false;
    if (q3_actor_get(game, trigger) &&
        !qa_session_release(game->options.services.session, trigger, error))
        return false;
    return !q3_actor_get(game, actor) ||
        q3_event(game, actor, player, QA_BUILTIN_IMPACT, 67, 0, mine_body.origin,
                  qa_v3(0, 0, 0), qa_v3(0, 0, 0), error);
}
static bool missile_move(qa_q3_game *game, qa_actor_id actor,
                         const qa_trajectory *trajectory, qa_actor_id pass,
                         qa_trace_result *out, qa_error *error) {
    qa_vec3 destination, velocity;
    qa_body_state body;
    if (!qa_trajectory_position(trajectory, game->now_ms, 800, &destination, error) ||
        !qa_trajectory_velocity(trajectory, game->now_ms, 800, &velocity, error) ||
        !qa_world_body_read(game->options.services.world, actor, &body, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    qa_trace_query query = {.start = body.origin, .end = destination,
        .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds}, .pass_actor = pass,
        .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    query.policy.contents_mask = Q3_MASK_SHOT;
    qa_trace_result trace;
    if (!qa_world_trace(game->options.services.world, &query, &trace, error))
        return false;
    if (!q3_actor_get(game, actor))
        return true;
    if (trace.start_solid || trace.all_solid) {
        query.end = query.start;
        if (!qa_world_trace(game->options.services.world, &query, &trace, error))
            return false;
        trace.fraction = 0;
    } else {
        body.origin = trace.end;
        body.velocity = velocity;
        if (!qa_world_body_write(game->options.services.world, actor, &body, error))
            return false;
    }
    if (!q3_actor_get(game, actor))
        return true;
    if (!qa_q3_wire_link(game, actor, NULL, error))
        return false;
    *out = trace;
    return true;
}
static bool missile_think(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    q3_missile missile = entry->state.missile;
    if (missile.phase == Q3_MISSILE_EVENT)
        return q3_sub_time(game->now_ms, missile.event_at) > 300
                   ? qa_session_release(game->options.services.session, actor, error)
                   : true;
    bool replaced;
    if (!q3_postgame_think_override(game, actor, &replaced, error)) return false;
    if (replaced) return true;
    if (missile.phase == Q3_MISSILE_HOOK) {
        if (!missile.think_at || game->now_ms < missile.think_at)
            return true;
        entry->state.missile.think_at = 0;
        q3_actor *owner = q3_actor_get(game, missile.owner);
        if (!owner || owner->kind != Q3_ACTOR_PLAYER)
            return qa_session_release(game->options.services.session, actor, error);
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        if (missile.attached.registry) {
            qa_body_state target;
            if (!qa_actors_get(qa_session_actors(game->options.services.session), missile.attached))
                return qa_session_release(game->options.services.session, actor, error);
            if (!qa_world_body_read(game->options.services.world, missile.attached, &target, error))
                return false;
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_MISSILE)
                return true;
            if (!qa_actors_get(qa_session_actors(game->options.services.session), missile.attached))
                return qa_session_release(game->options.services.session, actor, error);
            qa_vec3 center =
                qa_vec_add(target.origin,
                           qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
            body.origin = qa_physics_q3_snap_towards(center, body.origin);
            if (!set_origin(game, actor, body.origin, error))
                return false;
        }
        owner = q3_actor_get(game, missile.owner);
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        if (!owner || owner->kind != Q3_ACTOR_PLAYER)
            return qa_session_release(game->options.services.session, actor, error);
        owner->state.player.grapple_point = body.origin;
        return true;
    }
    if (missile.phase != Q3_MISSILE_FLIGHT) {
        if (!missile.think_at || game->now_ms < missile.think_at)
            return true;
        entry->state.missile.think_at = 0;
        if (missile.phase == Q3_MISSILE_PROX_DISCARD)
            return qa_session_release(game->options.services.session, actor, error);
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
            entry = q3_actor_get(game, actor);
            if (!entry || entry->kind != Q3_ACTOR_MISSILE)
                return true;
            if (!qa_actors_get(qa_session_actors(game->options.services.session), missile.attached))
                return qa_session_release(game->options.services.session, actor, error);
            player = q3_actor_get(game, missile.attached);
            if (player && player->kind == Q3_ACTOR_PLAYER &&
                player->state.player.invulnerability_until > game->now_ms) {
                if (!q3_damage(game, missile.attached, missile.owner, missile.owner, QA_Q3_W_PROX,
                               27, 4, 1000, qa_v3(0, 0, 0), missile.damage_point, false, NULL,
                               error))
                    return false;
                entry = q3_actor_get(game, actor);
                if (!entry || entry->kind != Q3_ACTOR_MISSILE)
                    return true;
                player = q3_actor_get(game, missile.attached);
                if (player && player->kind == Q3_ACTOR_PLAYER)
                    player->state.player.invulnerability_until = 0;
                if (!qa_actors_get(qa_session_actors(game->options.services.session), missile.attached))
                    return qa_session_release(game->options.services.session, actor, error);
                qa_actor_id temporary;
                if (!q3_wire_temp_entity(game, body.origin, 72, &temporary, error) ||
                    !q3_event(game, missile.attached, actor, QA_BUILTIN_EXPLOSION, 72, 0,
                              body.origin, qa_v3(0, 0, 0), qa_v3(0, 0, 0), error))
                    return false;
                return true;
            }
            uint32_t player_slot;
            if (qa_q3_native_client_slot(game, missile.attached, &player_slot, NULL)) {
                qa_q3_entity source_player;
                qa_q3_wire_visibility visibility;
                if (!qa_q3_wire_entity_read(game, player_slot, &source_player, &visibility, error))
                    return false;
                body.origin = qa_v3(source_player.pos.base[0], source_player.pos.base[1],
                                     source_player.pos.base[2]);
            }
            if (!q3_actor_get(game, actor))
                return true;
            if (!set_origin(game, actor, body.origin, error))
                return false;
            if (!q3_actor_get(game, actor)) return true;
            uint32_t source_slot;
            if (!qa_q3_source_actor_slot(game, actor, &source_slot, error)) return false;
            game->source_entities[source_slot].server_flags &= ~1u;
        }
        return q3_missile_explode(game, actor, error);
    }
    if (!missile.think_at || game->now_ms < missile.think_at)
        return true;
    entry->state.missile.think_at = 0;
    return missile.weapon == QA_Q3_W_GRAPPLE
        ? qa_session_release(game->options.services.session, actor, error)
        : q3_missile_explode(game, actor, error);
}
bool q3_missile_step(qa_q3_game *game, qa_actor_id actor, qa_error *error) {
    q3_actor *entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    q3_wire_entity_source *source = q3_wire_entity(game, actor);
    if (!source)
        return q3_fail(error, "Q3 projectile step lost its actual source entity");
    if (source->type != 3 || source->free_after_event)
        return missile_think(game, actor, error);
    bool changed = false;
    if (!qa_builtin_step_projectile(&game->options.services, actor,
                                    (uint64_t)(uint32_t)game->previous_ms * UINT64_C(1000000),
                                    &changed, error))
        return false;
    entry = q3_actor_get(game, actor);
    source = q3_wire_entity(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE || !source)
        return true;
    if (source->type != 3 || source->free_after_event)
        return missile_think(game, actor, error);
    if (changed) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        entry->state.missile.trajectory.base = body.origin;
        entry->state.missile.trajectory.delta = body.velocity;
        entry->state.missile.trajectory.time_ms = game->previous_ms;
    }
    q3_missile missile = entry->state.missile;
    qa_trace_result trace;
    if (!missile_move(game, actor, &missile.trajectory, missile.pass, &trace, error))
        return false;
    entry = q3_actor_get(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE)
        return true;
    if (trace.fraction < 1) {
        if (trace.surface_flags & Q3_SURF_NOIMPACT)
            return qa_session_release(game->options.services.session, actor, error);
        if (!missile_impact(game, actor, &trace, error))
            return false;
    }
    entry = q3_actor_get(game, actor);
    source = q3_wire_entity(game, actor);
    if (!entry || entry->kind != Q3_ACTOR_MISSILE || !source ||
        source->type != 3 || source->free_after_event)
        return true;
    if (missile.weapon == QA_Q3_W_PROX && !entry->state.missile.left_owner) {
        qa_body_state body;
        if (!qa_world_body_read(game->options.services.world, actor, &body, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        qa_trace_result overlap;
        qa_trace_query query = {.start = body.origin, .end = body.origin,
            .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
            .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
        query.policy.contents_mask = Q3_MASK_SHOT;
        if (!qa_world_trace(game->options.services.world, &query, &overlap, error))
            return false;
        entry = q3_actor_get(game, actor);
        if (!entry || entry->kind != Q3_ACTOR_MISSILE)
            return true;
        if ((!overlap.start_solid && !overlap.all_solid) ||
            overlap.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(overlap.actor, missile.owner)) {
            entry->state.missile.left_owner = true;
            entry->state.missile.pass = (qa_actor_id){0};
        }
    }
    return missile_think(game, actor, error);
}
