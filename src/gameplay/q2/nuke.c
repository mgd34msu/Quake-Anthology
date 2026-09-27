#include "internal.h"

static bool nuke_sound(qa_q2_game *g, q2_actor *a, const char *path, int channel, float volume,
                       float attenuation, qa_error *e) {
    if (!q2_actor_live(g, a->id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .time_ns = g->now_ns,
                              .channel = channel,
                              .origin = body.origin,
                              .volume = volume,
                              .attenuation = attenuation};
    return qa_builtin_resource(&g->services, path, &event.resource, e) &&
           qa_builtin_emit(&g->services, &event, e);
}
static bool blind(qa_q2_game *g, qa_actor_id id, uint64_t duration, bool inside, qa_error *e) {
    if (!q2_actor_live(g, id))
        return true;
    uint64_t until = q2_deadline(g->now_ns, duration);
    return q2_player_nuke_blind(g, id, until, inside, e) &&
           (!q2_actor_live(g, id) || g->hooks.nuke_blind == NULL ||
            g->hooks.nuke_blind(g->hooks.context, id, until, inside, e));
}
static bool nuke_blast(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *snapshot,
                       qa_builtin_actor_snapshot *players, qa_error *e) {
    qa_actor_id id = a->id;
    q2_projectile p = a->projectile;
    qa_body_state body;
    qa_combat_state combat;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_combat_read_traits(g->services.combat, id, &combat, e))
        return false;
    combat.can_take_damage = false;
    if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
        return false;
    a->projectile.phase = 1;
    if (!q2_projectile_noise(g, &p, body.origin, e))
        return false;
    if (!qa_builtin_nearby(&g->services, body.origin, p.radius * 2, snapshot, e))
        return false;
    qa_actor_id *blinded = snapshot->sort;
    size_t count = 0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id target = snapshot->ids[i];
        if (!q2_actor_live(g, id))
            return true;
        if (qa_actor_id_equal(target, id) || !q2_target_damageable(g, target))
            continue;
        qa_builtin_actor_traits traits = {0};
        if (g->services.actor_traits != NULL)
            g->services.actor_traits(g->services.context, target, &traits);
        bool player = false, creature = q2_target_creature(g, target, &player);
        if (!creature && !traits.damageable_target)
            continue;
        qa_body_state other;
        if (!qa_world_body_read(g->services.world, target, &other, e))
            return false;
        qa_vec3 center = qa_vec_add(
            other.origin, qa_vec_scale(qa_vec_add(other.bounds.mins, other.bounds.maxs), 0.5f));
        float distance = qa_vec_length(qa_vec_sub(body.origin, center));
        float points =
            distance <= p.radius ? 10000 : p.damage / p.radius * (2 * p.radius - distance);
        if (points <= 0)
            continue;
        if (player) {
            if (!blind(g, target, 2 * Q2_NS, distance <= p.radius, e))
                return false;
            blinded[count++] = target;
        }
        if (!q2_actor_live(g, id))
            return true;
        if (!q2_actor_live(g, target))
            continue;
        qa_attack attack = q2_projectile_attack(g, id, &p, 47, 1);
        if (!q2_damage(g, &attack, target, truncf(points), truncf(points),
                       qa_vec_sub(other.origin, body.origin), body.origin, qa_v3(0, 0, 0), true, e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_builtin_players(&g->services, players, e))
        return false;
    for (size_t i = 0; i < players->count; ++i) {
        qa_actor_id target = players->ids[i];
        if (!q2_actor_live(g, id))
            return true;
        if (!q2_actor_live(g, target))
            continue;
        bool done = false;
        for (size_t i = 0; i < count; ++i)
            if (qa_actor_id_equal(blinded[i], target)) {
                done = true;
                break;
            }
        if (done)
            continue;
        qa_body_state other;
        if (!qa_world_body_read(g->services.world, target, &other, e))
            return false;
        qa_trace_query query = {.start = body.origin,
                                .end = other.origin,
                                .pass_actor = id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = 3;
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, e))
            return false;
        uint64_t duration = trace.fraction == 1 ? 2 * Q2_NS
                            : qa_vec_length(qa_vec_sub(other.origin, body.origin)) < 2048
                                ? 1500 * Q2_MS
                                : Q2_NS;
        if (!blind(g, target, duration, false, e))
            return false;
    }
    if (!q2_actor_live(g, id))
        return true;
    if ((p.damage > 400 && !nuke_sound(g, a, "items/damage3.wav", 3, 1, 1, e)) ||
        !nuke_sound(g, a, "weapons/grenlx1a.wav", 10, 1, 0, e) ||
        !q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, "q2:explosion1-big", 0, body.origin,
                             qa_v3(0, 0, 0), e) ||
        !q2_projectile_event(g, id, QA_BUILTIN_EXPLOSION, "q2:nukeblast", 0, body.origin,
                             qa_v3(0, 0, 0), e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    a->projectile.expire_ns = q2_deadline(g->now_ns, 3 * Q2_NS);
    a->projectile.effect_ns = 0;
    a->projectile.visible = false;
    a->projectile.speed = 100;
    a->projectile.next_ns = q2_deadline(g->now_ns, g->frame_ns);
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = id,
                              .time_ns = g->now_ns,
                              .resource = a->projectile.model,
                              .frame = a->projectile.frame,
                              .origin = body.origin};
    return qa_builtin_emit(&g->services, &event, e);
}
static bool nuke_explode(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (a->projectile.phase != 0)
        return true;
    q2_trace_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch == NULL)
        return false;
    q2_trace_frame *players = q2_scratch_acquire(g, e);
    if (players == NULL) {
        scratch->active = false;
        return false;
    }
    bool result = nuke_blast(g, a, &scratch->snapshot, &players->snapshot, e);
    players->active = false;
    scratch->active = false;
    return result;
}
static bool nuke_quake_run(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *players,
                           qa_error *e) {
    if (a->projectile.effect_ns < g->now_ns) {
        if (!nuke_sound(g, a, "world/rumble.wav", 0, 0.75f, 0, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        a->projectile.effect_ns = q2_deadline(g->now_ns, 500 * Q2_MS);
    }
    if (!qa_builtin_players(&g->services, players, e))
        return false;
    for (size_t i = 0; i < players->count; ++i) {
        qa_actor_id target = players->ids[i];
        if (!q2_actor_live(g, target))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, target, &body, e))
            return false;
        if (body.ground.registry == 0)
            continue;
        qa_combat_state combat;
        qa_error ignored;
        float mass =
            qa_combat_read(g->services.combat, target, &combat, &ignored) && combat.mass > 0
                ? combat.mass
                : 200;
        body.velocity.x += q2_crandom(g) * 150;
        body.velocity.y += q2_crandom(g) * 150;
        body.velocity.z = a->projectile.speed * 100 / mass;
        body.ground = (qa_actor_id){0};
        if (!qa_world_body_write(g->services.world, target, &body, e) ||
            !qa_world_link(g->services.world, target, NULL, e))
            return false;
    }
    if (g->now_ns >= a->projectile.expire_ns)
        return qa_session_release(g->services.session, a->id, e);
    a->projectile.next_ns = q2_deadline(g->now_ns, g->frame_ns);
    return true;
}
static bool nuke_quake(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_trace_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch == NULL)
        return false;
    bool ok = nuke_quake_run(g, a, &scratch->snapshot, e);
    scratch->active = false;
    return ok;
}
bool q2_nuke_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_projectile *p = &a->projectile;
    if (g->now_ns < p->next_ns)
        return true;
    if (p->phase != 0)
        return nuke_quake(g, a, e);
    if (g->now_ns > p->expire_ns)
        return nuke_explode(g, a, e);
    float multiplier = p->damage / 400;
    float divisor = multiplier == 1   ? 1.4f
                    : multiplier == 2 ? 2
                    : multiplier == 4 ? 3
                    : multiplier == 8 ? 5
                                      : 1;
    uint64_t remaining = p->expire_ns - g->now_ns;
    if (remaining <= 6 * Q2_NS) {
        if (++p->frame > 11)
            p->frame = 6;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        qa_point_query query = {.point = body.origin,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        qa_point_contents contents;
        if (!qa_world_point_contents(g->services.world, &query, &contents, e))
            return false;
        if (((uint32_t)contents.contents & 24u) != 0)
            return nuke_explode(g, a, e);
        qa_actor_collision collision;
        if (qa_world_get_collision(g->services.world, a->id, &collision)) {
            collision.owner = (qa_actor_id){0};
            if (!qa_world_set_collision(g->services.world, a->id, &collision, e))
                return false;
        }
        if (!qa_combat_set_health(g->services.combat, a->id, 1, e))
            return false;
        qa_builtin_event animation = {.kind = QA_BUILTIN_ANIMATION,
                                      .family = QA_GAME_Q2,
                                      .provider = g->options.owner,
                                      .actor = a->id,
                                      .time_ns = g->now_ns,
                                      .frame = p->frame};
        if (!qa_builtin_emit(&g->services, &animation, e))
            return false;
        if (!q2_projectile_event(g, a->id, QA_BUILTIN_MUZZLE, NULL,
                                 multiplier == 2   ? 37
                                 : multiplier == 4 ? 38
                                 : multiplier == 8 ? 39
                                                   : 36,
                                 body.origin, body.origin, e))
            return false;
    }
    if (p->effect_ns <= g->now_ns) {
        if (!nuke_sound(g, a, "weapons/nukewarn2.wav", 10, 1, 1.8f / divisor, e))
            return false;
        p->effect_ns = q2_deadline(g->now_ns, remaining <= 3 * Q2_NS   ? 300 * Q2_MS
                                              : remaining <= 6 * Q2_NS ? 500 * Q2_MS
                                                                       : Q2_NS);
    }
    p->next_ns = q2_deadline(g->now_ns, remaining <= 6 * Q2_NS ? 100 * Q2_MS : g->frame_ns);
    return true;
}
bool q2_nuke_reaction(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *outcome, qa_error *e) {
    const qa_actor_record *attacker =
        qa_actors_get(qa_session_actors(g->services.session), outcome->request.attack.attacker);
    const char *name = attacker == NULL ? NULL
                                        : qa_strings_cstr(qa_session_strings(g->services.session),
                                                          attacker->definition);
    return name != NULL && strcmp(name, "nuke") == 0
               ? qa_session_release(g->services.session, a->id, e)
               : nuke_explode(g, a, e);
}
bool q2_fire_nuke(qa_q2_game *g, qa_actor_id owner, qa_vec3 start, qa_vec3 direction, float speed,
                  float multiplier, qa_error *e) {
    if (g == NULL || !q2_actor_live(g, owner) || !qa_vec_finite(start) ||
        !qa_vec_finite(direction) || !isfinite(speed) || speed <= 0 || !isfinite(multiplier) ||
        multiplier <= 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 nuke launch");
        return false;
    }
    qa_actor_definition definition;
    qa_item_id weapon;
    if (!qa_builtin_resource(&g->services, "nuke", &definition, e) ||
        !qa_builtin_resource(&g->services, "q2:ammo_nuke", &weapon, e))
        return false;
    qa_vec3 angles =
                qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                      atan2f(direction.y, direction.x) * 57.29577951308232f, 0),
            right, up;
    qa_builtin_angle_vectors(angles, NULL, &right, &up);
    float lift = 200 + q2_crandom(g) * 10, side = q2_crandom(g) * 10;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .role = QA_COLLISION_SOLID,
                                    .contents = 2,
                                    .owner = owner};
    qa_combat_state combat = {.health = 10000, .can_take_damage = true};
    qa_builtin_spawn spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .collision = &collision,
        .combat = &combat,
        .body = {.origin = start,
                 .velocity =
                     qa_vec_add(qa_vec_add(qa_vec_scale(direction, speed), qa_vec_scale(up, lift)),
                                qa_vec_scale(right, side)),
                 .bounds = {qa_v3(-8, -8, 0), qa_v3(8, 8, 16)}}};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    qa_attack attack = {.time_ns = g->now_ns,
                        .attacker = owner,
                        .inflictor = id,
                        .projectile = id,
                        .weapon = weapon,
                        .weapon_provider = g->options.owner,
                        .powerup_applied = true,
                        .powerup_owner = g->options.owner,
                        .cause = qa_q2_damage_cause(g->options.edition, g->options.product, 47, 1)};
    a->projectile = (q2_projectile){.kind = Q2_NUKE,
                                    .owner = owner,
                                    .attack = attack,
                                    .damage = 400 * multiplier,
                                    .radius = multiplier == 1 ? 512 : 512 + 128 * multiplier,
                                    .direct_mod = 47,
                                    .splash_mod = 47,
                                    .effects = 32,
                                    .render_flags = 0x8000,
                                    .scale = 1,
                                    .visible = true,
                                    .dodgeable = true,
                                    .born_ns = g->now_ns,
                                    .expire_ns = q2_deadline(g->now_ns, 10 * Q2_NS),
                                    .next_ns = q2_deadline(g->now_ns, g->frame_ns)};
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    a->physics.motion = QA_PHYSICS_BOUNCE;
    a->physics.solid = QA_PHYSICS_BOX;
    a->physics.clip_mask =
        g->options.edition == QA_Q2_RERELEASE ? Q2_PROJECTILE_MASK : Q2_SHOT_MASK;
    if (!q2_launch_behavior(g, a, QA_BUILTIN_GRENADE, NULL, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    return qa_world_body_read(g->services.world, id, &spawn.body, e) &&
           qa_world_link(g->services.world, id, NULL, e) &&
           q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, "models/weapons/g_nuke/tris.md2", 0,
                               spawn.body.origin, spawn.body.angles, e);
}
