#include "internal.h"

static bool live(qa_q2_game *g, qa_actor_id id) { return q2_actor_live(g, id); }
bool q2_target_damageable(qa_q2_game *g, qa_actor_id id) {
    qa_combat_state state;
    qa_error ignored;
    return live(g, id) && qa_combat_read(g->services.combat, id, &state, &ignored) &&
           state.can_take_damage;
}
bool q2_target_creature(qa_q2_game *g, qa_actor_id id, bool *player) {
    bool ignored_player;
    if (player == NULL)
        player = &ignored_player;
    *player = false;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL &&
        g->services.actor_traits(g->services.context, id, &traits)) {
        *player = traits.player;
        return traits.player || traits.monster;
    }
    qa_physics_properties properties;
    if (g->services.physics != NULL &&
        g->services.physics->services.read(g->services.physics->services.context, id,
                                           &properties)) {
        *player = (properties.flags & QA_PHYSICS_PLAYER) != 0;
        if (*player || (properties.flags & QA_PHYSICS_MONSTER) != 0)
            return true;
    }
    qa_actor_collision collision;
    if (!qa_world_get_collision(g->services.world, id, &collision))
        return false;
    *player = ((uint32_t)collision.contents & Q2_PLAYER_CONTENTS) != 0;
    return collision.monster || *player;
}
bool q2_projectile_event(qa_q2_game *g, qa_actor_id id, qa_builtin_event_kind kind,
                         const char *path, int code, qa_vec3 origin, qa_vec3 end, qa_error *e) {
    qa_builtin_event event = {.kind = kind,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = id,
                              .time_ns = g->now_ns,
                              .origin = origin,
                              .end = end,
                              .volume = 1,
                              .attenuation = 1,
                              .code = code};
    if (kind == QA_BUILTIN_SOUND || kind == QA_BUILTIN_STOP_SOUND)
        event.channel = code;
    if (path != NULL && !qa_builtin_resource(&g->services, path, &event.resource, e))
        return false;
    if (kind == QA_BUILTIN_ANIMATION && path != NULL && id.slot < g->capacity &&
        g->actors[id.slot] != NULL && qa_actor_id_equal(g->actors[id.slot]->id, id) &&
        g->actors[id.slot]->projectile.kind != Q2_PROJECTILE_NONE) {
        g->actors[id.slot]->projectile.model = event.resource;
        g->actors[id.slot]->projectile.visible = path[0] != '\0';
    }
    return qa_builtin_emit(&g->services, &event, e);
}
qa_attack q2_projectile_attack(qa_q2_game *g, qa_actor_id id, const q2_projectile *p, int mod,
                               uint32_t flags) {
    qa_attack attack = p->attack;
    attack.sequence = 0;
    attack.time_ns = g->now_ns;
    attack.inflictor = id;
    attack.projectile = id;
    qa_q2_edition edition = p->kind == Q2_LMCTF_HOOK ? QA_Q2_CLASSIC : g->options.edition;
    attack.cause = qa_q2_damage_cause(edition, g->options.product, mod, flags);
    if (p->kind == Q2_GREEN_BOLT)
        attack.weapon = 0;
    else if (p->kind == Q2_BOLT || p->kind == Q2_BLUE_BOLT)
        attack.weapon = mod == 10  ? g->items[QA_Q2_HYPERBLASTER]
                        : mod == 1 ? g->items[QA_Q2_BLASTER]
                                   : 0;
    return attack;
}
bool q2_projectile_loop(qa_q2_game *g, q2_actor *a, const char *path, bool stop_previous,
                        qa_error *e) {
    qa_string_id resource = 0;
    if (*path != 0 && !qa_builtin_resource(&g->services, path, &resource, e))
        return false;
    if (resource == a->projectile.loop_sound)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_STOP_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .time_ns = g->now_ns,
                              .origin = body.origin,
                              .channel = 0,
                              .volume = 1,
                              .attenuation = 1,
                              .resource = a->projectile.loop_sound};
    a->projectile.loop_sound = resource;
    if (stop_previous && event.resource != 0 && !qa_builtin_emit(&g->services, &event, e))
        return false;
    if (resource == 0 || !q2_actor_live(g, a->id) || a->projectile.loop_sound != resource)
        return true;
    event.kind = QA_BUILTIN_SOUND;
    event.resource = resource;
    event.flags = 1;
    return qa_builtin_emit(&g->services, &event, e);
}
bool q2_projectile_noise(qa_q2_game *g, const q2_projectile *p, qa_vec3 origin, qa_error *e) {
    return !live(g, p->owner) || q2_noise_for_actor(g, p->owner, origin, true, e);
}
static bool check_dodge(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, float speed,
                        qa_error *e) {
    qa_q2_game *g = c->game;
    bool player = false;
    q2_target_creature(g, c->actor->id, &player);
    if (c->rerelease || !player)
        return true;
    if (g->options.skill == 0 && q2_random(g) > 0.25f)
        return true;
    qa_trace_query query = {.start = start,
                            .end = qa_vec_add(start, qa_vec_scale(direction, 8192)),
                            .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = Q2_SHOT_MASK;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    if (trace.hit != QA_TRACE_HIT_ACTOR)
        return true;
    bool target_player = false;
    if (!q2_target_creature(g, trace.actor, &target_player) || target_player)
        return true;
    qa_combat_state combat;
    qa_error ignored = {0};
    if (!qa_combat_read(g->services.combat, trace.actor, &combat, &ignored) || combat.health <= 0)
        return true;
    qa_body_state target, owner;
    if (!qa_world_body_read(g->services.world, trace.actor, &target, e) ||
        !qa_world_body_read(g->services.world, c->actor->id, &owner, e))
        return false;
    qa_vec3 forward;
    qa_builtin_angle_vectors(target.angles, &forward, NULL, NULL);
    if (qa_vec_dot(qa_vec_normalize(qa_vec_sub(owner.origin, target.origin)), forward) <= 0.3f)
        return true;
    float eta = (qa_vec_length(qa_vec_sub(trace.end, start)) - target.bounds.maxs.x) / speed;
    return q2_monster_dodge(g, trace.actor, c->actor->id, eta, &trace, false, e);
}
bool q2_projectile_radius(qa_q2_game *g, qa_actor_id id, const q2_projectile *p, qa_vec3 origin,
                          qa_actor_id ignore, float damage, float range, int mod, uint32_t flags,
                          qa_error *e) {
    qa_builtin_radius request = {.attack = q2_projectile_attack(g, id, p, mod, flags | 1u),
                                 .origin = origin,
                                 .radius = range,
                                 .damage = damage,
                                 .distance_scale = 0.5f,
                                 .self_scale = 0.5f,
                                 .knockback_scale = 1,
                                 .ignore = ignore,
                                 .distance = QA_RADIUS_CENTER,
                                 .visibility_pass = id,
                                 .check_visibility = true,
                                 .trace = qa_collision_default_policy(QA_COLLISION_Q2),
                                 .context = g,
                                 .prepare = q2_prepare_radius_damage};
    request.trace.contents_mask = 1;
    size_t count;
    return q2_radius_damage(g, &request, &count, e);
}
static bool grenade_explode(qa_q2_game *g, qa_actor_id id, qa_actor_id direct, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (a == NULL)
        return false;
    q2_projectile p = a->projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !q2_projectile_noise(g, &p, body.origin, e))
        return false;
    if (!live(g, id))
        return true;
    if (q2_target_damageable(g, direct)) {
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, direct, &target, e))
            return false;
        qa_vec3 center = qa_vec_add(
            target.origin, qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
        float points = truncf(p.damage - 0.5f * qa_vec_length(qa_vec_sub(body.origin, center)));
        qa_attack attack = q2_projectile_attack(g, id, &p, p.direct_mod, 1);
        if (!q2_damage(g, &attack, direct, points, points, qa_vec_sub(target.origin, body.origin),
                       body.origin, qa_v3(0, 0, 0), true, e))
            return false;
        if (!live(g, id))
            return true;
    }
    if (!q2_projectile_radius(g, id, &p, body.origin, direct, p.damage, p.radius, p.splash_mod, 0,
                              e))
        return false;
    if (!live(g, id))
        return true;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_point_contents water;
    if (!qa_world_point_contents(g->services.world, &query, &water, e))
        return false;
    bool wet = ((uint32_t)water.contents & Q2_WATER_MASK) != 0;
    const char *effect = body.ground.registry == 0
                             ? (wet ? "q2:rocket-explosion-water" : "q2:rocket-explosion")
                             : (wet ? "q2:grenade-explosion-water" : "q2:grenade-explosion");
    return q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, effect, 0,
                               qa_vec_add(body.origin, qa_vec_scale(body.velocity, -0.02f)),
                               qa_v3(0, 0, 0), e) &&
           (!live(g, id) || qa_session_release(g->services.session, id, e));
}
static bool bfg_target(qa_q2_game *g, qa_actor_id id) {
    bool player = false;
    if (q2_target_creature(g, id, &player))
        return true;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, id, &traits);
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), traits.classname);
    return (name != NULL && strcmp(name, "misc_explobox") == 0) ||
           (g->options.edition == QA_Q2_RERELEASE && traits.damageable_target);
}
static bool bfg_effect_run(qa_q2_game *g, qa_actor_id id, const q2_projectile *p, qa_vec3 origin,
                           const qa_builtin_actor_snapshot *targets, qa_error *e) {
    for (size_t i = 0; i < targets->count; ++i) {
        if (!live(g, id))
            return true;
        qa_actor_id target = targets->ids[i];
        if (qa_actor_id_equal(target, id) || qa_actor_id_equal(target, p->owner) ||
            !q2_target_damageable(g, target))
            continue;
        if (g->options.edition == QA_Q2_RERELEASE && !bfg_target(g, target))
            continue;
        qa_body_state body, blast;
        if (!qa_world_body_read(g->services.world, target, &body, e) ||
            !qa_world_body_read(g->services.world, id, &blast, e))
            return false;
        origin = blast.origin;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        float distance = qa_vec_length(qa_vec_sub(origin, center));
        bool visible;
        qa_trace_policy policy = qa_collision_default_policy(QA_COLLISION_Q2);
        policy.contents_mask = 1;
        if (!qa_builtin_can_damage(&g->services, origin, target, id, policy, false, &visible, e))
            return false;
        if (!visible)
            continue;
        if (live(g, p->owner)) {
            qa_body_state owner;
            qa_error read_error = {0};
            if (qa_world_body_read(g->services.world, p->owner, &owner, &read_error)) {
                if (!qa_builtin_can_damage(&g->services, owner.origin, target, p->owner, policy,
                                           false, &visible, e))
                    return false;
                if (!visible)
                    continue;
            } else if (read_error.code != QA_ERROR_NOT_FOUND) {
                if (e != NULL)
                    *e = read_error;
                return false;
            }
        }
        if (g->options.edition == QA_Q2_RERELEASE && g->hooks.can_target != NULL &&
            !g->hooks.can_target(g->hooks.context, p->owner, target))
            continue;
        float damage = truncf(p->damage * (1 - sqrtf(distance / p->radius)));
        qa_attack attack = q2_projectile_attack(g, id, p, 14, 4);
        if (g->options.edition == QA_Q2_CLASSIC &&
            !q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, "q2:bfg-explosion", 0, body.origin,
                                 qa_v3(0, 0, 0), e))
            return false;
        if (!live(g, id))
            return true;
        if (!live(g, target))
            continue;
        if (!qa_world_body_read(g->services.world, id, &blast, e))
            return false;
        if (!q2_damage(g, &attack, target, damage, 0, blast.velocity,
                       g->options.edition == QA_Q2_CLASSIC ? body.origin : center, qa_v3(0, 0, 0),
                       false, e))
            return false;
        if (!live(g, id))
            return true;
        if (g->options.edition == QA_Q2_RERELEASE) {
            if (!qa_world_body_read(g->services.world, id, &blast, e) ||
                !q2_projectile_event(g, id, QA_BUILTIN_BEAM, "q2:bfg-zap", 0, blast.origin, center,
                                     e))
                return false;
        }
    }
    return true;
}
static bool bfg_effect(qa_q2_game *g, qa_actor_id id, const q2_projectile *p, qa_vec3 origin,
                       qa_error *e) {
    q2_trace_frame *scratch = q2_nearby(g, origin, p->radius, e);
    if (scratch == NULL)
        return false;
    bool ok = bfg_effect_run(g, id, p, origin, &scratch->snapshot, e);
    scratch->active = false;
    return ok;
}
static bool bfg_fly_run(qa_q2_game *g, qa_actor_id id, const q2_projectile *p, qa_vec3 origin,
                        const qa_builtin_actor_snapshot *targets, qa_actor_id *excluded,
                        qa_error *e) {
    for (size_t i = 0; i < targets->count; ++i) {
        if (!live(g, id))
            return true;
        qa_actor_id target = targets->ids[i];
        if (qa_actor_id_equal(target, id) || qa_actor_id_equal(target, p->owner) ||
            !q2_target_damageable(g, target))
            continue;
        if (!bfg_target(g, target))
            continue;
        if (g->hooks.can_target != NULL && g->options.edition == QA_Q2_RERELEASE &&
            !g->hooks.can_target(g->hooks.context, p->owner, target))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, target, &body, e))
            return false;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(center, origin));
        qa_trace_query query = {.start = origin,
                                .end = center,
                                .pass_actor = id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        qa_trace_result trace;
        if (g->options.edition == QA_Q2_RERELEASE) {
            query.policy.contents_mask = 3;
            query.pass_actor = (qa_actor_id){0};
            if (!qa_world_trace(g->services.world, &query, &trace, e))
                return false;
            if (trace.fraction < 1)
                continue;
        }
        query.pass_actor = id;
        query.end = qa_vec_add(origin, qa_vec_scale(direction, 2048));
        query.policy.contents_mask =
            UINT32_C(0x06000001) | (g->options.edition == QA_Q2_RERELEASE ? Q2_PLAYER_CONTENTS : 0);
        size_t count = 0, limit = g->options.edition == QA_Q2_RERELEASE ? 16 : g->capacity;
        for (;;) {
            if (!qa_world_trace_excluding(g->services.world, &query, excluded, count, &trace, e))
                return false;
            if (trace.fraction == 1)
                break;
            qa_actor_id hit = trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor : (qa_actor_id){0};
            qa_builtin_actor_traits hit_traits = {0};
            bool immune = g->services.actor_traits != NULL &&
                          g->services.actor_traits(g->services.context, hit, &hit_traits) &&
                          hit_traits.laser_immune;
            if (!qa_actor_id_equal(hit, p->owner) && !immune && q2_target_damageable(g, hit)) {
                qa_attack attack = q2_projectile_attack(g, id, p, 12, 4);
                if (!q2_damage(g, &attack, hit, g->options.deathmatch ? 5 : 10, 1, direction,
                               trace.end, qa_v3(0, 0, 0), false, e))
                    return false;
                if (!live(g, id))
                    return true;
                hit_traits = (qa_builtin_actor_traits){0};
                if (g->services.actor_traits != NULL)
                    g->services.actor_traits(g->services.context, hit, &hit_traits);
            }
            bool player;
            if (!q2_target_creature(g, hit, &player) &&
                !(g->options.edition == QA_Q2_RERELEASE && hit_traits.damageable_target)) {
                qa_builtin_event spark = {.kind = QA_BUILTIN_IMPACT,
                                          .family = QA_GAME_Q2,
                                          .provider = g->options.owner,
                                          .actor = id,
                                          .origin = trace.end,
                                          .direction = trace.contact_plane.normal,
                                          .count = 4,
                                          .code = (int)p->skin,
                                          .time_ns = g->now_ns};
                if (!qa_builtin_resource(&g->services, "q2:laser-sparks", &spark.resource, e) ||
                    !qa_builtin_emit(&g->services, &spark, e))
                    return false;
                break;
            }
            bool duplicate = false;
            for (size_t i = 0; i < count; ++i)
                duplicate |= qa_actor_id_equal(excluded[i], hit);
            if (duplicate || count >= limit)
                break;
            excluded[count++] = hit;
            if (g->options.edition == QA_Q2_CLASSIC) {
                query.start = trace.end;
                query.pass_actor = hit;
            }
        }
        if (!q2_projectile_event(g, id, QA_BUILTIN_BEAM, "q2:bfg-laser", 0, origin, trace.end, e))
            return false;
    }
    return true;
}
static bool bfg_fly(qa_q2_game *g, qa_actor_id id, const q2_projectile *p, qa_vec3 origin,
                    qa_error *e) {
    q2_trace_frame *scratch = q2_nearby(g, origin, 256, e);
    if (scratch == NULL)
        return false;
    bool result = bfg_fly_run(g, id, p, origin, &scratch->snapshot, scratch->snapshot.sort, e);
    scratch->active = false;
    return result;
}
static bool bfg_ambient(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, qa_error *e) {
    float theta = q2_random(g) * 6.283185307179586f;
    float phi = acosf(q2_random(g) * 2 - 1);
    qa_vec3 direction = qa_v3(sinf(phi) * cosf(theta), sinf(phi) * sinf(theta), cosf(phi));
    qa_trace_query query = {.start = origin,
                            .end = qa_vec_add(origin, qa_vec_scale(direction, 256)),
                            .pass_actor = id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = 25;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    if (trace.fraction == 1)
        return true;
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = id,
                              .time_ns = g->now_ns,
                              .origin = origin,
                              .end = trace.end,
                              .value = 0.3f};
    return qa_builtin_resource(&g->services, "q2:bfg-lightning", &event.resource, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
static bool tracker_daemon(qa_q2_game *g, const q2_projectile *p, qa_actor_id target, qa_error *e) {
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "pain daemon", &definition, e))
        return false;
    qa_builtin_spawn spawn = {.owner = g->options.owner, .definition = definition};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    uint64_t interval = g->options.edition == QA_Q2_RERELEASE ? 100 * Q2_MS : g->frame_ns;
    a->projectile = *p;
    a->projectile.kind = Q2_TRACKER_DAEMON;
    a->projectile.enemy = target;
    a->projectile.visible = false;
    a->projectile.model = 0;
    a->projectile.loop_sound = 0;
    a->projectile.effects = 0;
    a->projectile.dodgeable = false;
    a->projectile.damage = truncf(p->damage * (float)((double)interval / 1e9) / 0.5f);
    a->projectile.born_ns = g->now_ns;
    a->projectile.expire_ns = q2_deadline(g->now_ns, 500 * Q2_MS);
    a->projectile.next_ns =
        g->options.edition == QA_Q2_RERELEASE ? g->now_ns : q2_deadline(g->now_ns, interval);
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.motion = QA_PHYSICS_STATIONARY;
    a->physics.solid = QA_PHYSICS_NOT_SOLID;
    return true;
}
static bool tracker_touch(qa_q2_game *g, qa_actor_id id, const q2_projectile *p,
                          const qa_touch_contact *contact, const qa_body_state *body, qa_error *e) {
    if (q2_target_damageable(g, contact->other)) {
        qa_combat_state health;
        if (!qa_combat_read(g->services.combat, contact->other, &health, e))
            return false;
        bool player, is_creature = q2_target_creature(g, contact->other, &player),
                     living = is_creature && health.health > 0;
        qa_attack attack = q2_projectile_attack(g, id, p, 51, 260);
        if (!q2_damage(g, &attack, contact->other,
                       living        ? 0
                       : is_creature ? p->damage * 4
                                     : p->damage,
                       p->damage * 3, body->velocity, body->origin,
                       contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0), false, e))
            return false;
        if (living && live(g, contact->other)) {
            qa_body_state target;
            if (!qa_world_body_read(g->services.world, contact->other, &target, e))
                return false;
            q2_actor *native =
                contact->other.slot < g->capacity ? g->actors[contact->other.slot] : NULL;
            bool floating =
                native != NULL && qa_actor_id_equal(native->id, contact->other) &&
                native->physics_bound &&
                (native->physics.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)) != 0;
            if (!floating) {
                target.velocity.z += 140;
                if (!qa_world_body_write(g->services.world, contact->other, &target, e))
                    return false;
            }
            if (!tracker_daemon(g, p, contact->other, e))
                return false;
        }
    }
    if (!live(g, id))
        return true;
    return q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, "q2:tracker-explosion", 0, body->origin,
                               body->origin, e) &&
           qa_session_release(g->services.session, id, e);
}
static bool tracker_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_actor_id id = a->id;
    q2_projectile p = a->projectile;
    if (p.next_ns > g->now_ns)
        return true;
    qa_combat_state health;
    qa_body_state target, body;
    qa_error ignored;
    bool valid =
        live(g, p.enemy) && qa_combat_read(g->services.combat, p.enemy, &health, &ignored) &&
        health.health > 0 && qa_world_body_read(g->services.world, p.enemy, &target, &ignored);
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    if (!valid || (p.kind == Q2_TRACKER_DAEMON && g->now_ns > p.expire_ns)) {
        if (p.kind == Q2_TRACKER_DAEMON && live(g, p.enemy) && p.enemy.slot < g->capacity) {
            q2_actor *victim = g->actors[p.enemy.slot];
            bool player = false;
            q2_target_creature(g, p.enemy, &player);
            if (victim != NULL && qa_actor_id_equal(victim->id, p.enemy) && victim->physics_bound &&
                !player)
                victim->extra_effects &= ~UINT64_C(0x80000000);
        }
        if (p.kind == Q2_TRACKER &&
            !q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, "q2:tracker-explosion", 0,
                                 body.origin, body.origin, e))
            return false;
        return qa_session_release(g->services.session, id, e);
    }
    bool player;
    q2_target_creature(g, p.enemy, &player);
    qa_vec3 center = qa_vec_add(
        target.origin, qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), 0.5f));
    if (p.kind == Q2_TRACKER_DAEMON) {
        uint64_t interval = g->options.edition == QA_Q2_RERELEASE ? 100 * Q2_MS : g->frame_ns;
        qa_vec3 point = g->options.edition == QA_Q2_RERELEASE ? center : target.origin;
        qa_attack attack = q2_projectile_attack(g, id, &p, 51, 268);
        if (!q2_damage(g, &attack, p.enemy, p.damage, 0, qa_v3(0, 0, 0), point, qa_v3(0, 0, 1),
                       false, e))
            return false;
        if (!live(g, id))
            return true;
        if (live(g, p.enemy) && qa_combat_read(g->services.combat, p.enemy, &health, &ignored) &&
            health.health < 1) {
            qa_builtin_actor_traits traits = {0};
            q2_monster_traits(g, p.enemy, &traits);
            qa_attack gib_attack = q2_projectile_attack(g, id, &p, 51, 268);
            if (!q2_damage(g, &gib_attack, p.enemy,
                           traits.gib_health == 0 ? 500 : -traits.gib_health, 0, qa_v3(0, 0, 0),
                           point, qa_v3(0, 0, 1), false, e))
                return false;
        }
        if (!live(g, id))
            return true;
        if (player && live(g, p.enemy)) {
            uint64_t until = q2_deadline(g->now_ns, interval);
            if (!q2_player_tracker_pain(g, p.enemy, until, e) ||
                (g->hooks.tracker_pain != NULL &&
                 !g->hooks.tracker_pain(g->hooks.context, p.enemy, until, e)))
                return false;
        } else if (live(g, p.enemy) && p.enemy.slot < g->capacity) {
            q2_actor *victim = g->actors[p.enemy.slot];
            if (victim != NULL && qa_actor_id_equal(victim->id, p.enemy) && victim->physics_bound)
                victim->extra_effects |= UINT64_C(0x80000000);
        }
        a->projectile.next_ns = q2_deadline(g->now_ns, interval);
    } else {
        if (g->services.controls_trajectory != NULL &&
            g->services.controls_trajectory(g->services.context, id)) {
            a->projectile.next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            return true;
        }
        qa_builtin_actor_traits traits = {.view_height = 22};
        if (g->services.actor_traits != NULL)
            g->services.actor_traits(g->services.context, p.enemy, &traits);
        qa_bounds bounds = qa_bounds_translate(target.bounds, target.origin);
        qa_vec3 destination = player ? qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height))
                              : qa_vec_length(bounds.mins) == 0 || qa_vec_length(bounds.maxs) == 0
                                  ? target.origin
                                  : center;
        qa_vec3 direction = qa_vec_normalize(qa_vec_sub(destination, body.origin));
        a->projectile.movedir = direction;
        body.velocity = qa_vec_scale(direction, p.speed);
        body.angles =
            qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                  atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
        if (!qa_world_body_write(g->services.world, id, &body, e))
            return false;
        a->projectile.next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
    }
    return true;
}
bool qa_q2_physics_read(qa_q2_game *g, qa_actor_id id, qa_physics_properties *out) {
    if (g == NULL || out == NULL || id.slot >= g->capacity || g->actors[id.slot] == NULL ||
        !qa_actor_id_equal(g->actors[id.slot]->id, id) || !g->actors[id.slot]->physics_bound)
        return false;
    *out = g->actors[id.slot]->physics;
    return true;
}
bool qa_q2_physics_write(qa_q2_game *g, qa_actor_id id, const qa_physics_properties *properties,
                         qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (a == NULL || properties == NULL)
        return false;
    if (!a->physics_bound) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 does not own this actor's physics");
        return false;
    }
    a->physics = *properties;
    return true;
}
bool qa_q2_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    if (g == NULL || contact == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 touch contact");
        return false;
    }
    qa_actor_id id = contact->self;
    if (id.slot >= g->capacity || g->actors[id.slot] == NULL ||
        !qa_actor_id_equal(g->actors[id.slot]->id, id))
        return true;
    q2_projectile p = g->actors[id.slot]->projectile;
    if (p.kind == Q2_PROBOSCIS || p.kind == Q2_PROBOSCIS_SEGMENT)
        return q2_proboscis_touch(g, contact, e);
    if (p.kind == Q2_BFG_BALL && p.armed)
        return true;
    if (p.kind == Q2_LMCTF_PLASMA_SPREAD || p.kind == Q2_LMCTF_PLASMA_BOUNCE)
        return q2_lmctf_plasma_touch(g, contact, e);
    if (p.kind == Q2_CTF_HOOK || p.kind == Q2_LMCTF_HOOK)
        return q2_grapple_touch(g, contact, e);
    if (p.kind == Q2_PROX || p.kind == Q2_TESLA || p.kind == Q2_TRAP || p.kind == Q2_PROX_FIELD ||
        p.kind == Q2_TESLA_FIELD || p.kind == Q2_BAD_AREA)
        return q2_mine_touch(g, contact, e);
    if (p.kind == Q2_GIB || p.kind == Q2_DEBRIS || p.kind == Q2_TRAP_GIB ||
        p.kind == Q2_TRAP_ORBIT_GIB)
        return q2_gib_touch(g, contact, e);
    if (p.kind == Q2_PROJECTILE_NONE) {
        q2_actor *a = g->actors[id.slot];
        if (a->item != NULL && !q2_item_touch(g, contact, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (a->entity != NULL && !q2_entity_touch(g, contact, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (a->client != NULL && !q2_client_touch(g, contact, e))
            return false;
        return !q2_actor_live(g, id) || a->monster == NULL || q2_monster_touch(g, contact, e);
    }
    if (p.kind == Q2_NUKE) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            return false;
        return q2_projectile_event(g, id, QA_BUILTIN_SOUND,
                                   q2_random(g) > 0.5f ? "weapons/hgrenb1a.wav"
                                                       : "weapons/hgrenb2a.wav",
                                   2, body.origin, body.origin, e);
    }
    if (qa_actor_id_equal(contact->other, p.owner))
        return true;
    if (contact->has_surface && (contact->surface.flags & 4) != 0)
        return qa_session_release(g->services.session, id, e);
    if (p.kind == Q2_GREEN_BOLT)
        return q2_green_touch(g, g->actors[id.slot], contact, e);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    qa_vec3 normal = contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0);
    bool hurt = q2_target_damageable(g, contact->other);
    if (p.kind == Q2_TRACKER)
        return tracker_touch(g, id, &p, contact, &body, e);
    if (p.kind == Q2_GRENADE) {
        if (hurt)
            return grenade_explode(g, id, contact->other, e);
        const char *sound =
            p.hand ? (q2_random(g) > 0.5f ? "weapons/hgrenb1a.wav" : "weapons/hgrenb2a.wav")
                   : "weapons/grenlb1b.wav";
        return q2_projectile_event(g, id, QA_BUILTIN_SOUND, sound, 2, body.origin, qa_v3(0, 0, 0),
                                   e);
    }
    if (p.kind != Q2_FLECHETTE && !q2_projectile_noise(g, &p, body.origin, e))
        return false;
    if (!live(g, id))
        return true;
    if (p.kind == Q2_ION && !hurt)
        return true;
    if (hurt) {
        float damage = p.kind == Q2_BFG_BALL ? 200 : p.damage;
        qa_attack attack = q2_projectile_attack(
            g, id, &p, p.direct_mod,
            p.kind == Q2_FLECHETTE ? 128u
            : p.kind == Q2_BOLT || p.kind == Q2_BLUE_BOLT || p.kind == Q2_ION ||
                    (p.kind == Q2_BFG_BALL && g->options.edition == QA_Q2_RERELEASE)
                ? 4u
                : 0u);
        if (!q2_damage(g, &attack, contact->other, damage, p.kind == Q2_ION ? 1 : p.kick,
                       body.velocity, body.origin, normal, false, e))
            return false;
        if (!live(g, id))
            return true;
    }
    if (!hurt && (p.kind == Q2_ROCKET || p.kind == Q2_HEAT_ROCKET) && !g->options.deathmatch &&
        !g->options.cooperative && contact->has_surface && (contact->surface.flags & 120u) == 0) {
        int count = (int)(q2_random(g) * 5);
        for (int i = 0; i < count; ++i) {
            if (!q2_spawn_debris(g, id, e))
                return false;
            if (!live(g, id))
                return true;
        }
    }
    if (p.kind == Q2_BOLT || p.kind == Q2_BLUE_BOLT || p.kind == Q2_ION || p.kind == Q2_FLECHETTE) {
        if (!hurt && p.kind != Q2_ION &&
            !q2_projectile_event(g, id, QA_BUILTIN_IMPACT,
                                 p.kind == Q2_BOLT || p.kind == Q2_BLUE_BOLT ? "q2:blaster"
                                                                             : "q2:flechette",
                                 0, body.origin, normal, e))
            return false;
        return !live(g, id) || qa_session_release(g->services.session, id, e);
    }
    if (p.kind == Q2_BFG_BALL) {
        if (!q2_projectile_radius(g, id, &p, body.origin, contact->other, 200, 100, 13,
                                  g->options.edition == QA_Q2_RERELEASE ? 4u : 0u, e))
            return false;
        if (!live(g, id))
            return true;
        q2_actor *a = g->actors[id.slot];
        if (!q2_projectile_event(g, id, QA_BUILTIN_SOUND, "weapons/bfg__x1b.wav", 2, body.origin,
                                 body.origin, e))
            return false;
        if (!live(g, id))
            return true;
        if (!q2_projectile_loop(g, a, "", true, e))
            return false;
        if (!live(g, id))
            return true;
        a->projectile.armed = true;
        a->projectile.frame = 0;
        a->projectile.next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
        a->projectile.enemy = contact->other;
        a->projectile.loop_sound = 0;
        a->projectile.effects &= ~UINT64_C(8192);
        a->physics.motion = QA_PHYSICS_STATIONARY;
        a->physics.solid = QA_PHYSICS_NOT_SOLID;
        body.origin = qa_vec_add(body.origin,
                                 qa_vec_scale(body.velocity, -(float)((double)g->frame_ns / 1e9)));
        body.velocity = qa_v3(0, 0, 0);
        return qa_world_body_write(g->services.world, id, &body, e) &&
               qa_world_set_collision(g->services.world, id, NULL, e) &&
               qa_world_link(g->services.world, id, NULL, e) &&
               q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, "sprites/s_bfg3.sp2", 0,
                                   body.origin, body.angles, e) &&
               q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, "q2:bfg-bigexplosion", 0,
                                   body.origin, body.origin, e);
    }
    if (p.radius > 0 && p.radius_damage > 0 &&
        !q2_projectile_radius(g, id, &p, body.origin, contact->other, p.radius_damage, p.radius,
                              p.splash_mod, 0, e))
        return false;
    if (!live(g, id))
        return true;
    qa_vec3 origin = p.kind == Q2_PLASMA || g->options.edition == QA_Q2_CLASSIC
                         ? qa_vec_add(body.origin, qa_vec_scale(body.velocity, -0.02f))
                         : qa_vec_add(body.origin, normal);
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_point_contents content;
    if (!qa_world_point_contents(g->services.world, &query, &content, e))
        return false;
    const char *effect = p.kind == Q2_PLASMA                       ? "q2:plasma-explosion"
                         : ((uint32_t)content.contents & 56u) != 0 ? "q2:rocket-explosion-water"
                                                                   : "q2:rocket-explosion";
    return q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, effect, 0, origin, normal, e) &&
           (!live(g, id) || qa_session_release(g->services.session, id, e));
}
bool q2_tracker_target(q2_weapon_call *c, qa_vec3 start, qa_vec3 direction, qa_actor_id *out,
                       qa_error *e) {
    qa_q2_game *g = c->game;
    qa_trace_query query = {.start = start,
                            .end = qa_vec_add(start, qa_vec_scale(direction, 8192)),
                            .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask =
        c->rerelease ? (c->input.players_collide ? Q2_PROJECTILE_MASK
                                                 : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS)
                     : Q2_SHOT_MASK;
    bool lag = c->rerelease && g->hooks.lag_begin != NULL;
    if (lag && !g->hooks.lag_begin(g->hooks.context, c->actor->id, start, direction, e))
        return false;
    qa_trace_result trace;
    bool traced = qa_world_trace(g->services.world, &query, &trace, e);
    qa_error ignored;
    bool restored = !lag || g->hooks.lag_end(g->hooks.context, traced ? e : &ignored);
    if (!traced || !restored)
        return false;
    if (trace.hit != QA_TRACE_HIT_ACTOR ||
        (g->services.physics != NULL &&
         qa_actor_id_equal(trace.actor, g->services.physics->world_actor))) {
        query.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX,
                                       .bounds = {qa_v3(-16, -16, -16), qa_v3(16, 16, 16)}};
        if (!qa_world_trace(g->services.world, &query, &trace, e))
            return false;
    }
    *out = (qa_actor_id){0};
    if (trace.hit != QA_TRACE_HIT_ACTOR || !q2_target_damageable(g, trace.actor))
        return true;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, trace.actor, &traits);
    bool player;
    if (!q2_target_creature(g, trace.actor, &player) && !traits.damageable_target)
        return true;
    qa_combat_state health;
    if (!qa_combat_read(g->services.combat, trace.actor, &health, e))
        return false;
    if (health.health > 0)
        *out = trace.actor;
    return true;
}
static bool trajectory_changed(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_projectile *p = &a->projectile;
    if (p->kind != Q2_ION && p->kind != Q2_PLASMA && p->kind != Q2_FLECHETTE &&
        p->kind != Q2_TRACKER)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    p->speed = qa_vec_length(body.velocity);
    p->movedir = qa_vec_normalize(body.velocity);
    return true;
}
bool q2_launch_behavior(qa_q2_game *g, q2_actor *a, qa_builtin_projectile_role role, bool *changed,
                        qa_error *e) {
    if (changed != NULL)
        *changed = false;
    bool player;
    q2_target_creature(g, a->projectile.owner, &player);
    if (!player)
        return true;
    qa_builtin_weapon_launch launch = {.projectile = a->id,
                                       .shooter = a->projectile.owner,
                                       .weapon = a->projectile.attack.weapon,
                                       .provider = g->options.owner,
                                       .role = role,
                                       .time_ns = g->now_ns};
    bool updated = false;
    if (!qa_world_body_read(g->services.world, a->id, &launch.body, e) ||
        !qa_builtin_launch_projectile(&g->services, &launch, &updated, e))
        return false;
    if (changed != NULL)
        *changed = updated;
    if (!live(g, a->id))
        return true;
    return !updated || trajectory_changed(g, a, e);
}
bool q2_projectile_spawn(q2_weapon_call *c, q2_projectile_kind kind, qa_vec3 start,
                         qa_vec3 direction, float damage, float kick, float speed, float range,
                         float splash, float fuse, int direct_mod, int splash_mod, bool hand,
                         bool held, qa_error *e) {
    qa_q2_game *g = c->game;
    if (!live(g, c->actor->id))
        return true;
    q2_projectile_kind source_kind = kind;
    if (kind == Q2_GREEN_BOLT || kind == Q2_BLUE_BOLT)
        kind = Q2_BOLT;
    if (kind == Q2_HEAT_ROCKET)
        kind = Q2_ROCKET;
    qa_actor_id owner = c->has_attack_owner ? c->attack_owner : c->actor->id;
    if (kind == Q2_TRAP || kind == Q2_TESLA || kind == Q2_PROX)
        return q2_mine_spawn(c, kind, start, direction, damage, speed, range, splash, fuse, held,
                             e);
    bool player = false, monster = q2_target_creature(g, c->actor->id, &player) && !player;
    const char *name = kind == Q2_BOLT        ? "bolt"
                       : kind == Q2_ROCKET    ? "rocket"
                       : kind == Q2_BFG_BALL  ? "bfg blast"
                       : kind == Q2_ION       ? "ion"
                       : kind == Q2_PLASMA    ? "plasma"
                       : kind == Q2_FLECHETTE ? "flechette"
                       : kind == Q2_TRACKER   ? "tracker"
                       : hand                 ? (c->rerelease ? "hand_grenade" : "hgrenade")
                                              : "grenade";
    const char *model = kind == Q2_BOLT        ? "models/objects/laser/tris.md2"
                        : kind == Q2_ROCKET    ? "models/objects/rocket/tris.md2"
                        : kind == Q2_BFG_BALL  ? "sprites/s_bfg1.sp2"
                        : kind == Q2_ION       ? "models/objects/boomrang/tris.md2"
                        : kind == Q2_PLASMA    ? "sprites/s_photon.sp2"
                        : kind == Q2_FLECHETTE ? "models/proj/flechette/tris.md2"
                        : kind == Q2_TRACKER   ? "models/proj/disintegrator/tris.md2"
                        : hand ? (c->rerelease ? "models/objects/grenade3/tris.md2"
                                               : "models/objects/grenade2/tris.md2")
                               : (c->rerelease && !monster ? "models/objects/grenade4/tris.md2"
                                                           : "models/objects/grenade/tris.md2");
    if (source_kind == Q2_BLUE_BOLT)
        model = "models/objects/blaser/tris.md2";
    if (source_kind == Q2_GREEN_BOLT && !c->rerelease)
        model = "models/proj/laser2/tris.md2";
    if ((kind == Q2_BOLT && (!c->rerelease || source_kind == Q2_GREEN_BOLT)) || kind == Q2_ION ||
        kind == Q2_FLECHETTE)
        direction = qa_vec_normalize(direction);
    qa_vec3 dodge_start = start, dodge_direction = direction;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, name, &definition, e))
        return false;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = 2,
                                    .owner = owner,
                                    .role = QA_COLLISION_SOLID};
    qa_builtin_spawn spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .collision = hand && fuse <= 0 ? NULL : &collision,
        .body = {.origin = start,
                 .velocity = qa_vec_scale(direction, speed),
                 .angles = {-atan2f(direction.z, hypotf(direction.x, direction.y)) *
                                57.29577951308232f,
                            atan2f(direction.y, direction.x) * 57.29577951308232f, 0}},
        .link = false};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    if (c->spawned_projectile != NULL)
        *c->spawned_projectile = id;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    a->projectile = (q2_projectile){
        .kind = source_kind,
        .attack = q2_attack(c, direct_mod, 0),
        .owner = owner,
        .movedir = direction,
        .damage = damage,
        .kick = kick,
        .speed = speed,
        .radius = range,
        .radius_damage = splash,
        .gravity = c->input.gravity,
        .born_ns = c->now_ns,
        .expire_ns = q2_deadline(c->now_ns, q2_duration(fuse)),
        .next_ns = q2_deadline(c->now_ns, c->frame_ns),
        .direct_mod = direct_mod,
        .splash_mod = splash_mod,
        .hand = hand,
        .held = held,
        .scale = 1,
        .dodgeable = kind != Q2_BFG_BALL &&
                     (c->rerelease || kind == Q2_ION || kind == Q2_PLASMA || kind == Q2_FLECHETTE ||
                      kind == Q2_TRACKER || source_kind == Q2_GREEN_BOLT)};
    a->projectile.effects =
        kind == Q2_BOLT
            ? (direct_mod == 10 ? ((c->state->frame == 6 || c->state->frame == 9) ? 64u : 0u) : 8u)
        : kind == Q2_ROCKET   ? 16u
        : kind == Q2_GRENADE  ? 32u
        : kind == Q2_BFG_BALL ? (128u | 8192u)
        : kind == Q2_ION      ? UINT64_C(0x100000)
        : kind == Q2_PLASMA   ? UINT64_C(0x1002000)
        : kind == Q2_TRACKER  ? UINT64_C(0x04000000)
                              : 0;
    if (kind == Q2_ION || kind == Q2_FLECHETTE)
        a->projectile.render_flags = 8;
    if (c->has_projectile_effects)
        a->projectile.effects = c->projectile_effects;
    if (source_kind == Q2_GREEN_BOLT) {
        a->projectile.radius = 128;
        if (a->projectile.effects != 0)
            a->projectile.effects |= UINT64_C(0x4000000);
        if (c->rerelease) {
            a->projectile.skin = 2;
            a->projectile.scale = 2.5f;
        }
    }
    if (source_kind == Q2_HEAT_ROCKET) {
        a->projectile.turn_fraction = 0.075f;
        a->projectile.expire_ns = UINT64_MAX;
        a->projectile.next_ns = q2_deadline(c->now_ns, c->rerelease ? c->frame_ns : 100 * Q2_MS);
    }
    if (kind == Q2_GRENADE && c->rerelease && monster && !hand)
        a->projectile.effects |= UINT64_C(1) << 37;
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = c->rerelease;
    a->physics.motion = kind == Q2_GRENADE ? QA_PHYSICS_BOUNCE
                        : kind == Q2_ION   ? QA_PHYSICS_WALL_BOUNCE
                                           : QA_PHYSICS_FLY_MISSILE;
    a->physics.solid = hand && fuse <= 0 ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_BOX;
    a->physics.clip_mask =
        c->rerelease ? (c->input.players_collide ? Q2_PROJECTILE_MASK
                                                 : Q2_PROJECTILE_MASK & ~Q2_PLAYER_CONTENTS)
                     : Q2_SHOT_MASK;
    if (kind == Q2_TRACKER) {
        if (c->has_projectile_enemy)
            a->projectile.enemy = c->projectile_enemy;
        else if (!q2_tracker_target(c, start, direction, &a->projectile.enemy, e))
            return false;
        if (a->projectile.enemy.registry != 0) {
            a->projectile.next_ns = q2_deadline(c->now_ns, 100 * Q2_MS);
            a->projectile.expire_ns = UINT64_MAX;
        }
    }
    if (kind == Q2_GRENADE) {
        float horizontal = sqrtf(direction.x * direction.x + direction.y * direction.y);
        qa_vec3 angles = qa_v3(-atan2f(direction.z, horizontal) * 57.29577951308232f,
                               atan2f(direction.y, direction.x) * 57.29577951308232f, 0),
                r, u;
        qa_builtin_angle_vectors(angles, NULL, &r, &u);
        float up, side;
        if (c->has_grenade_impulse) {
            side = c->grenade_right;
            up = c->grenade_up;
        } else if (c->rerelease && !hand && !monster) {
            side = q2_crandom(g) * 10;
            up = 200 + q2_crandom(g) * 10;
        } else {
            up = 200 + q2_crandom(g) * 10;
            side = q2_crandom(g) * 10;
        }
        float gravity = c->has_grenade_impulse ? c->grenade_gravity : c->input.gravity;
        up *= c->rerelease ? gravity / 800 : 1;
        spawn.body.velocity =
            qa_vec_add(qa_vec_add(spawn.body.velocity, qa_vec_scale(u, up)), qa_vec_scale(r, side));
        if (!qa_world_body_write(g->services.world, id, &spawn.body, e))
            return false;
        if (c->rerelease && (hand || monster)) {
            a->physics.angular_velocity.x = q2_crandom(g) * 360;
            a->physics.angular_velocity.y = q2_crandom(g) * 360;
            a->physics.angular_velocity.z = q2_crandom(g) * 360;
        } else if (!c->rerelease)
            a->physics.angular_velocity = qa_v3(300, 300, 300);
        else {
            spawn.body.angles =
                qa_v3(-atan2f(spawn.body.velocity.z,
                              hypotf(spawn.body.velocity.x, spawn.body.velocity.y)) *
                          57.29577951308232f,
                      atan2f(spawn.body.velocity.y, spawn.body.velocity.x) * 57.29577951308232f, 0);
            if (!qa_world_body_write(g->services.world, id, &spawn.body, e))
                return false;
            a->projectile.armed = true;
            a->projectile.render_flags |= 1;
        }
    }
    if (!qa_builtin_resource(&g->services, model, &a->projectile.model, e))
        return false;
    if (hand) {
        if (!q2_projectile_loop(g, a, "weapons/hgrenc1b.wav", false, e))
            return false;
        if (!live(g, id))
            return true;
        if (fuse <= 0)
            return grenade_explode(g, id, (qa_actor_id){0}, e);
        if (!q2_sound(c, "weapons/hgrent1a.wav", 1, 1, e))
            return false;
        if (!live(g, id) || !live(g, c->actor->id))
            return true;
    }
    if (kind == Q2_BFG_BALL && !check_dodge(c, dodge_start, dodge_direction, speed, e))
        return false;
    if (!live(g, id) || !live(g, c->actor->id))
        return true;
    qa_builtin_projectile_role role = kind == Q2_ROCKET      ? QA_BUILTIN_ROCKET
                                      : kind == Q2_GRENADE   ? QA_BUILTIN_GRENADE
                                      : kind == Q2_PLASMA    ? QA_BUILTIN_PLASMA
                                      : kind == Q2_FLECHETTE ? QA_BUILTIN_NAIL
                                      : kind == Q2_BFG_BALL || kind == Q2_TRACKER
                                          ? QA_BUILTIN_ENERGY
                                          : QA_BUILTIN_BOLT;
    bool steered = false;
    if (source_kind != Q2_GREEN_BOLT && !(hand && fuse <= 0) &&
        !q2_launch_behavior(g, a, role, &steered, e))
        return false;
    if (!live(g, id))
        return true;
    if (!qa_world_body_read(g->services.world, id, &spawn.body, e))
        return false;
    start = spawn.body.origin;
    if (kind == Q2_BOLT && source_kind != Q2_GREEN_BOLT) {
        if (steered)
            direction = qa_vec_normalize(spawn.body.velocity);
    } else
        direction = a->projectile.movedir;
    if (!qa_world_link(g->services.world, id, NULL, e) ||
        !q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, model, 0, start, direction, e))
        return false;
    if (!live(g, id))
        return true;
    const char *loop = source_kind == Q2_GREEN_BOLT             ? NULL
                       : kind == Q2_BOLT || kind == Q2_ION      ? "misc/lasfly.wav"
                       : kind == Q2_ROCKET || kind == Q2_PLASMA ? "weapons/rockfly.wav"
                       : kind == Q2_BFG_BALL                    ? "weapons/bfg__l1a.wav"
                       : kind == Q2_TRACKER                     ? "weapons/disrupt.wav"
                                                                : NULL;
    if (loop != NULL && !q2_projectile_loop(g, a, loop, false, e))
        return false;
    qa_vec3 dodge =
        kind == Q2_ION || kind == Q2_FLECHETTE || kind == Q2_TRACKER || source_kind == Q2_GREEN_BOLT
            ? a->projectile.movedir
            : dodge_direction;
    if (kind != Q2_GRENADE && kind != Q2_BFG_BALL && !check_dodge(c, dodge_start, dodge, speed, e))
        return false;
    if (!live(g, id) || !live(g, c->actor->id))
        return true;
    if (kind == Q2_BOLT || kind == Q2_ION || kind == Q2_TRACKER ||
        (kind == Q2_FLECHETTE && c->rerelease)) {
        if (kind != Q2_BOLT || source_kind == Q2_GREEN_BOLT) {
            if (!qa_world_body_read(g->services.world, id, &spawn.body, e))
                return false;
            start = spawn.body.origin;
            direction = a->projectile.movedir;
        }
        qa_body_state owner;
        if (!qa_world_body_read(g->services.world, c->actor->id, &owner, e))
            return false;
        qa_trace_query query = {.start = owner.origin,
                                .end = start,
                                .pass_actor = id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = a->physics.clip_mask;
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, e))
            return false;
        if (trace.fraction < 1) {
            spawn.body.origin = c->rerelease ? qa_vec_add(trace.end, trace.contact_plane.normal)
                                             : qa_vec_add(start, qa_vec_scale(direction, -10));
            if (!qa_world_body_write(g->services.world, id, &spawn.body, e))
                return false;
            qa_touch_contact contact = {.self = id,
                                        .other = trace.hit == QA_TRACE_HIT_ACTOR ? trace.actor
                                                                                 : (qa_actor_id){0},
                                        .has_plane = c->rerelease && trace.contact,
                                        .plane = trace.contact_plane,
                                        .has_surface = c->rerelease && trace.has_surface,
                                        .surface = trace.surface};
            if (!qa_q2_touch(g, &contact, e))
                return false;
        }
    }
    return true;
}
bool q2_projectile_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (a->projectile.kind == Q2_PROBOSCIS || a->projectile.kind == Q2_PROBOSCIS_SEGMENT)
        return q2_proboscis_tick(g, a, e);
    qa_actor_id id = a->id;
    q2_projectile p = a->projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    if (p.kind == Q2_CTF_HOOK || p.kind == Q2_LMCTF_HOOK) {
        if (!q2_grapple_think(g, a, e))
            return false;
        if (!live(g, id))
            return true;
    }
    if (p.kind == Q2_GIB || p.kind == Q2_TRAP_ORBIT_GIB || p.kind == Q2_SPAWN_GROWTH) {
        if (!q2_gib_think(g, a, e))
            return false;
        if (!live(g, id) || p.kind == Q2_TRAP_ORBIT_GIB || p.kind == Q2_SPAWN_GROWTH)
            return true;
        p = a->projectile;
    }
    if (p.kind == Q2_NUKE) {
        if (!q2_nuke_think(g, a, e))
            return false;
        if (!live(g, id))
            return true;
        p = a->projectile;
    }
    if (p.kind == Q2_HEAT_ROCKET) {
        if (!q2_heat_rocket_think(g, a, e))
            return false;
        if (!live(g, id))
            return true;
    }
    if (p.kind == Q2_PROX || p.kind == Q2_TESLA || p.kind == Q2_TRAP || p.kind == Q2_PROX_FIELD ||
        p.kind == Q2_TESLA_FIELD || p.kind == Q2_BAD_AREA) {
        if (!q2_mine_think(g, a, e))
            return false;
        if (!live(g, id) || p.kind == Q2_PROX_FIELD || p.kind == Q2_TESLA_FIELD ||
            p.kind == Q2_BAD_AREA)
            return true;
        p = a->projectile;
    }
    if (p.kind == Q2_TRACKER_DAEMON || (p.kind == Q2_TRACKER && p.enemy.registry != 0)) {
        if (!tracker_think(g, a, e))
            return false;
        if (!live(g, id) || p.kind == Q2_TRACKER_DAEMON)
            return true;
    }
    if (p.kind == Q2_BFG_BALL) {
        if (p.next_ns <= g->now_ns) {
            if (p.armed && p.frame >= 5)
                return qa_session_release(g->services.session, id, e);
            if (g->options.edition == QA_Q2_RERELEASE && !bfg_ambient(g, id, body.origin, e))
                return false;
            if (!live(g, id))
                return true;
            if (!qa_world_body_read(g->services.world, id, &body, e))
                return false;
            if (p.armed) {
                if (p.frame == 0 && !bfg_effect(g, id, &p, body.origin, e))
                    return false;
                if (!live(g, id))
                    return true;
                ++a->projectile.frame;
                qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                                          .family = QA_GAME_Q2,
                                          .provider = g->options.owner,
                                          .actor = id,
                                          .time_ns = g->now_ns,
                                          .frame = a->projectile.frame,
                                          .resource = a->projectile.model};
                if (!qa_builtin_emit(&g->services, &event, e))
                    return false;
                a->projectile.next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            } else {
                if (!bfg_fly(g, id, &p, body.origin, e))
                    return false;
                if (!live(g, id))
                    return true;
                a->projectile.next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
            }
        }
    } else if (p.kind != Q2_PROX && p.kind != Q2_TESLA && p.kind != Q2_TRAP && p.kind != Q2_NUKE &&
               !(p.kind == Q2_GIB && p.phase == 1) && p.expire_ns <= g->now_ns) {
        if (p.kind == Q2_ION) {
            qa_builtin_event event = {.kind = QA_BUILTIN_IMPACT,
                                      .family = QA_GAME_Q2,
                                      .provider = g->options.owner,
                                      .actor = id,
                                      .time_ns = g->now_ns,
                                      .origin = body.origin,
                                      .code = 0xe4 + (int)(q2_random(g) * 4)};
            if (!qa_builtin_resource(&g->services, "q2:welding-sparks", &event.resource, e) ||
                !qa_builtin_emit(&g->services, &event, e))
                return false;
            if (!live(g, id))
                return true;
        }
        return p.kind == Q2_GRENADE ? grenade_explode(g, id, (qa_actor_id){0}, e)
                                    : qa_session_release(g->services.session, id, e);
    }
    if (p.kind == Q2_GRENADE && p.armed && g->options.edition == QA_Q2_RERELEASE &&
        p.next_ns <= g->now_ns) {
        float speed_squared = qa_vec_dot(body.velocity, body.velocity);
        if (speed_squared != 0) {
            float fraction = fminf(1, speed_squared / (p.speed * p.speed));
            qa_vec3 angles =
                qa_v3(-atan2f(body.velocity.z, hypotf(body.velocity.x, body.velocity.y)) *
                          57.29577951308232f,
                      atan2f(body.velocity.y, body.velocity.x) * 57.29577951308232f, 0);
            body.angles.x += qa_builtin_angle_delta(angles.x, body.angles.x) * fraction;
            body.angles.y = angles.y;
            body.angles.z += (float)((double)g->frame_ns / 1e9) * 360 * fraction;
            if (!qa_world_body_write(g->services.world, id, &body, e))
                return false;
        }
        a->projectile.next_ns = q2_deadline(g->now_ns, g->frame_ns);
    }
    if (!live(g, id))
        return true;
    if (g->services.physics == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 projectiles require the shared physics service");
        return false;
    }
    qa_source_frame frame = {.provider = g->options.owner,
                             .kind = g->options.edition == QA_Q2_CLASSIC ? QA_CLOCK_Q2_CLASSIC
                                                                         : QA_CLOCK_Q2_RERELEASE,
                             .phase = QA_ENTITY_PHYSICS,
                             .time_ns = g->now_ns,
                             .elapsed_ns = g->frame_ns,
                             .start_ns = g->now_ns >= g->frame_ns ? g->now_ns - g->frame_ns : 0};
    bool changed = false;
    if (!qa_builtin_step_projectile(&g->services, id, g->now_ns, &changed, e))
        return false;
    if (!live(g, id))
        return true;
    if (changed && !trajectory_changed(g, a, e))
        return false;
    qa_physics_result result;
    return qa_physics_step(g->services.physics, id, &frame, &result, e);
}
