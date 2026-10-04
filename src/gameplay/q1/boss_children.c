#include "boss_internal.h"

static bool read(qa_q1_game *g, q1_actor *e, qa_body_state *body, qa_error *error) {
    return qa_world_body_read(g->services.world, e->id, body, error);
}
static bool set_health(qa_q1_game *g, q1_actor *e, float health, qa_error *error) {
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, e->id, &traits, error))
        return false;
    traits.can_take_damage = true;
    e->aimed_damage = true;
    e->max_health = health;
    e->state.boss_child.reactive = true;
    return qa_combat_set_health(g->services.combat, e->id, health, error) &&
           qa_combat_set_traits(g->services.combat, e->id, &traits, error);
}
bool q1_boss_targets(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_string_id target,
                     qa_error *error) {
    if (g->services.use_targets)
        return g->services.use_targets(g->services.context, e->id, activator, target, e->killtarget,
                                       e->delay, error);
    if (!qa_strings_text(qa_session_strings(g->services.session), target).size &&
        !qa_strings_text(qa_session_strings(g->services.session), e->killtarget).size &&
        !qa_strings_text(qa_session_strings(g->services.session), e->message).size)
        return true;
    qa_error_set(error, QA_ERROR_ARGUMENT, e->id.slot, "MG3 boss requires target routing");
    return false;
}
static bool armed_eye(qa_q1_game *g, q1_actor *owner, q1_boss_child_kind kind, float side,
                      q1_actor **out, qa_error *error) {
    qa_body_state body;
    if (!read(g, owner, &body, error))
        return false;
    q1_actor *e;
    if (!q1_boss_child_create(g, "oldnew_child", kind, owner->id, &e, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    body.origin =
        qa_vec_add(body.origin,
                   qa_vec_add(qa_vec_scale(g->right, side * 300),
                              qa_vec_add(qa_vec_scale(g->up, 80), qa_vec_scale(g->forward, 200))));
    body.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, 64}};
    body.velocity = qa_v3(0, 0, 0);
    body.ground = (qa_actor_id){0};
    e->physics.solid = QA_PHYSICS_BOX;
    e->physics.motion = QA_PHYSICS_STEP;
    e->physics.flags = QA_PHYSICS_MONSTER | QA_PHYSICS_FLYING;
    e->state.boss_child.sign = side;
    if (!q1_model(g, e, "progs/teleporter_eye.mdl", error) ||
        !qa_world_body_write(g->services.world, e->id, &body, error) ||
        !q1_boss_teledeath(g, e, error) || !set_health(g, e, 120, error))
        goto fail;
    ++g->total_monsters;
    if (!qa_q1_spawn_teleport_fog(g, body.origin, NULL, error))
        goto fail;
    e->effects = 64;
    if (!q1_link(g, e, error))
        goto fail;
    *out = e;
    return true;
fail:
    q1_remove(g, e, NULL);
    return false;
}
bool q1_boss_child_spawn(qa_q1_game *g, q1_actor *owner, q1_boss_child_kind kind, qa_error *error) {
    if (kind == Q1_CHILD_BLASTER || kind == Q1_CHILD_VORTEX) {
        for (unsigned i = 0; i < (kind == Q1_CHILD_BLASTER ? 2u : 1u); ++i) {
            float side = kind == Q1_CHILD_BLASTER                       ? (i == 0 ? 1 : -1)
                         : owner->state.monster.source.boss.vortex_side ? 1
                                                                        : -1;
            q1_actor *e;
            if (!armed_eye(g, owner, kind, side, &e, error))
                return false;
            if (kind == Q1_CHILD_VORTEX) {
                owner->state.monster.source.boss.vortex_side =
                    !owner->state.monster.source.boss.vortex_side;
                e->state.boss_child.enemy = q1_boss_enemy(owner);
            } else
                e->damage = -side;
            if (!q1_schedule(g, e, 2, Q1_THINK_BOSS_CHILD, error))
                return false;
        }
        return true;
    }
    qa_body_state parent;
    if (!read(g, owner, &parent, error))
        return false;
    q1_actor *e;
    bool eye = kind == Q1_CHILD_EYE;
    if (!q1_boss_child_create(g, eye ? "oldnew_eye" : "oldnew_child", kind, owner->id, &e, error))
        return false;
    qa_body_state body = {.origin = parent.origin};
    double delay = .1;
    if (eye) {
        body.origin.z += 300;
        body.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, 64}};
        if (!qa_q1_spawn_teleport_fog(g, body.origin, NULL, error) ||
            !q1_model(g, e, "progs/teleporter_eye.mdl", error))
            goto fail;
        e->physics.solid = QA_PHYSICS_BOX;
        e->physics.motion = QA_PHYSICS_FLY;
        e->speed = 100;
        if (!set_health(g, e, 300, error) ||
            !q1_boss_first_player(g, &e->state.boss_child.enemy, error))
            goto fail;
    } else if (kind == Q1_CHILD_SWIPER) {
        body.angles = parent.angles;
        if (!q1_sound(g, e->id, "weapons/lstart.wav", 1, 1, error))
            goto fail;
        e->state.boss_child.sign = owner->state.monster.source.boss.swipe_side;
        owner->state.monster.source.boss.swipe_side = !owner->state.monster.source.boss.swipe_side;
        delay = .025;
    } else {
        e->state.boss_child.enemy = q1_boss_enemy(owner);
        if (!e->state.boss_child.enemy.registry &&
            !q1_boss_first_player(g, &e->state.boss_child.enemy, error))
            goto fail;
    }
    if (!qa_world_body_write(g->services.world, e->id, &body, error) ||
        !q1_schedule(g, e, delay, Q1_THINK_BOSS_CHILD, error) || !q1_link(g, e, error))
        goto fail;
    return true;
fail:
    q1_remove(g, e, NULL);
    return false;
}
bool q1_boss_cleanup(qa_q1_game *g, qa_error *error) {
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < snapshot->shared.count; ++i) {
        q1_actor *e = q1_entity(g, snapshot->shared.ids[i]);
        if (e && e->kind == Q1_BOSS_CHILD &&
            (q1_classnamed(g, e->id, "oldnew_child") || q1_classnamed(g, e->id, "oldnew_eye")) &&
            !q1_boss_child_schedule(g, e, Q1_CHILD_CLEANUP, .1, error)) {
            ok = false;
            break;
        }
    }
    snapshot->borrowed = false;
    if (!ok)
        return false;
    q1_actor *timer;
    return q1_boss_child_create(g, "", Q1_CHILD_ZOMBIE_CLEANUP, (qa_actor_id){0}, &timer, error) &&
           q1_schedule(g, timer, 2, Q1_THINK_BOSS_CHILD, error);
}
static bool eye_die(qa_q1_game *g, q1_actor *e, qa_actor_id attacker, qa_error *error) {
    if (e->state.boss_child.counted_death)
        return true;
    e->state.boss_child.counted_death = true;
    if (e->physics.flags & QA_PHYSICS_MONSTER) {
        if (!q1_monster_death_report(g, e, attacker, true, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        e->physics.flags &= ~(uint32_t)(QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING);
        e->state.boss_child.enemy = attacker;
        if (!q1_boss_targets(g, e, attacker, e->target, error))
            return false;
    }
    return !q1_alive(g, e->id) ||
           (q1_boss_colored_explosion(g, e, error) && q1_remove(g, e, error));
}
bool q1_boss_child_reaction(qa_q1_game *g, q1_actor *e, const qa_damage_outcome *outcome,
                            qa_error *error) {
    if (!e->state.boss_child.reactive)
        return true;
    if (outcome->result.reaction == QA_REACTION_DEATH)
        return eye_die(g, e, outcome->request.attack.attacker, error);
    if (outcome->result.reaction != QA_REACTION_PAIN || e->state.boss_child.kind != Q1_CHILD_EYE)
        return true;
    qa_body_state body, attacker = {0};
    if (!read(g, e, &body, error))
        return false;
    (void)qa_world_body_read(g->services.world, outcome->request.attack.attacker, &attacker, NULL);
    qa_vec3 delta = qa_vec_sub(body.origin, attacker.origin);
    delta.z = 0;
    qa_vec3 impulse =
        qa_vec_scale(qa_vec_add(qa_vec_scale(qa_vec_normalize(delta), .8f), qa_v3(0, 0, .2f)),
                     outcome->result.applied_damage * 10);
    body.velocity = qa_vec_add(body.velocity, impulse);
    return qa_world_body_write(g->services.world, e->id, &body, error);
}
static bool eye_chase(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_vec3 target = q1_boss_target(g, e);
    e->speed = qa_vec_length(body.velocity);
    if (e->speed < 300)
        e->speed += 10;
    qa_vec3 velocity =
        qa_vec_add(qa_vec_scale(qa_vec_normalize(qa_vec_sub(target, body.origin)), .3f),
                   qa_vec_scale(qa_vec_normalize(body.velocity), .7f));
    if (body.origin.z < target.z + 32) {
        velocity.z = 0;
        velocity = qa_vec_normalize(velocity);
    }
    body.angles = q1_boss_angles(velocity);
    body.velocity = qa_vec_scale(velocity, e->speed);
    if (!qa_world_body_write(g->services.world, e->id, &body, error) ||
        !q1_schedule(g, e, .1, Q1_THINK_BOSS_CHILD, error))
        return false;
    if ((int32_t)e->count % 2 == 0) {
        e->effects |= 2;
        if (!q1_sound(g, e->id, "misc/power.wav", 2, 1, error))
            return false;
    }
    e->count++;
    return true;
}
static bool blaster(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_q1_target traits;
    if ((!q1_target(g, e->state.boss_child.enemy, &traits) || !traits.player) &&
        !q1_boss_first_player(g, &e->state.boss_child.enemy, error))
        return false;
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(q1_boss_target(g, e), body.origin));
    direction = qa_vec_normalize(
        qa_vec_add(direction, qa_vec_scale(q1_boss_cross(direction, qa_v3(0, 0, 1)),
                                           cosf(e->count * 15) * .5f * e->state.boss_child.sign)));
    body.angles = q1_boss_angles(direction);
    if (!qa_world_body_write(g->services.world, e->id, &body, error))
        return false;
    q1_actor *shot;
    if (!q1_boss_shot(g, e->id, qa_vec_add(body.origin, qa_vec_scale(direction, 8)), direction,
                      qa_vec_scale(direction, 500), "progs/rogue/sphere.mdl", Q1_BOSS_BLAST_SHOT,
                      &shot, error))
        return false;
    shot->effects = 64;
    if (!q1_sound(g, e->id, "weapons/spike2.wav", 1, 1, error))
        return false;
    shot->physics.angular_velocity = qa_vec_scale(qa_v3(300, 300, 300), 2 * q1_random(g) - 1);
    if (!q1_schedule(g, e, .2, Q1_THINK_BOSS_CHILD, error))
        return false;
    return ++e->count <= 72 ||
           q1_damage(g, e->id, g->services.physics->world_actor, g->services.physics->world_actor,
                     500, QA_Q1_WEAPON_COUNT, error);
}
static bool spammer(qa_q1_game *g, q1_actor *e, qa_error *error) {
    static const int8_t offsets[] = {0, 1, -3, 2, -2, 3, -1, 4, 2, -1, -3, 2, -4};
    qa_body_state body, owner = {0};
    if (!read(g, e, &body, error))
        return false;
    (void)qa_world_body_read(g->services.world, e->owner, &owner, NULL);
    body.angles = owner.angles;
    if (!qa_world_body_write(g->services.world, e->id, &body, error))
        return false;
    if (e->count < 0 || e->count >= (float)(sizeof(offsets) / sizeof(*offsets)))
        return q1_remove(g, e, error);
    qa_builtin_angle_vectors(qa_vec_add(body.angles, qa_v3(0, offsets[(unsigned)e->count] * 6, 0)),
                             &g->forward, &g->right, &g->up);
    qa_vec3 origin =
        qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, 50)), qa_vec_scale(g->forward, 48));
    float oomph = fmaxf(200, qa_vec_length(qa_vec_sub(q1_boss_target(g, e), body.origin)) * .8f);
    qa_vec3 velocity = qa_vec_scale(g->forward, oomph + 25 * e->count);
    velocity.z = 200;
    q1_actor *shot;
    if (!q1_boss_child_create(g, "spam", Q1_CHILD_SPAM, e->owner, &shot, error))
        return false;
    shot->physics.solid = QA_PHYSICS_BOX;
    shot->physics.motion = QA_PHYSICS_TOSS;
    shot->effects = 64;
    body = (qa_body_state){.origin = origin, .velocity = velocity};
    if (!q1_model(g, shot, "progs/rogue/plasma.mdl", error) ||
        !qa_world_body_write(g->services.world, shot->id, &body, error) ||
        !q1_sound(g, e->id, "weapons/grenade.wav", 1, 1, error))
        return false;
    e->count++;
    return q1_link(g, shot, error) && q1_schedule(g, e, .1, Q1_THINK_BOSS_CHILD, error);
}
static bool beam(qa_q1_game *g, q1_actor *e, qa_vec3 start, qa_vec3 end, int32_t style,
                 qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = e->id,
                              .origin = start,
                              .end = end,
                              .code = style,
                              .time_ns = g->time_ns};
    return qa_builtin_emit(&g->services, &event, error);
}
static bool swiper(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_body_state body;
    if (!read(g, e, &body, error))
        return false;
    float fraction = e->count / 50;
    fraction *= fraction;
    qa_builtin_angle_vectors(
        qa_vec_add(body.angles,
                   qa_v3(0,
                         e->state.boss_child.sign == 0 ? -60 + fraction * 180 : 60 - fraction * 180,
                         0)),
        &g->forward, &g->right, &g->up);
    qa_vec3 start = qa_vec_add(body.origin, qa_vec_scale(g->forward, 130));
    qa_trace_result trace;
    if (!q1_trace(g, start, qa_vec_add(body.origin, qa_vec_scale(g->forward, 1000)), e->id, true,
                  &trace, error))
        return false;
    if (trace.hit == QA_TRACE_HIT_ACTOR && q1_health(g, trace.actor) != 0 &&
        !q1_damage(g, trace.actor, e->owner.registry ? e->owner : g->services.physics->world_actor,
                   e->owner, q1_classnamed(g, trace.actor, "monster_szombie") ? 100 : 25,
                   QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    if ((int32_t)e->count % 2 == 0 && !beam(g, e, start, trace.end, 3, error))
        return false;
    if (e->state.boss_child.sound_after < g->time) {
        if (!q1_sound(g, e->id, "weapons/lhit.wav", 1, 1, error))
            return false;
        e->state.boss_child.sound_after = g->time + .6;
    }
    return q1_schedule(g, e, .025, ++e->count > 50 ? Q1_THINK_REMOVE : Q1_THINK_BOSS_CHILD, error);
}
bool q1_boss_child_think(qa_q1_game *g, q1_actor *e, qa_error *error) {
    switch (e->state.boss_child.kind) {
    case Q1_CHILD_SPHERE:
    case Q1_CHILD_SPHERE_RING:
    case Q1_CHILD_SPHERE_CHUNK:
        return q1_boss_sphere_think(g, e, error);
    case Q1_CHILD_EYE:
        return eye_chase(g, e, error);
    case Q1_CHILD_BLASTER:
    case Q1_CHILD_VORTEX:
        return blaster(g, e, error);
    case Q1_CHILD_SPAMMER:
        return spammer(g, e, error);
    case Q1_CHILD_SWIPER:
        return swiper(g, e, error);
    case Q1_CHILD_SPAM_BEAM: {
        qa_body_state body;
        if (!read(g, e, &body, error))
            return false;
        return beam(g, e, body.origin, qa_vec_add(body.origin, qa_v3(0, 0, 500)), 1, error) &&
               q1_boss_child_schedule(g, e, Q1_CHILD_SPAM_EXPLODE, .1, error);
    }
    case Q1_CHILD_SPAM_EXPLODE:
        return q1_boss_colored_explosion(g, e, error) &&
               q1_radius(g, e->id, e->id, 100, g->services.physics->world_actor, QA_Q1_WEAPON_COUNT,
                         error) &&
               (!q1_alive(g, e->id) || q1_remove(g, e, error));
    case Q1_CHILD_CLEANUP:
        return q1_damage(g, e->id, e->id, e->id, 5000, QA_Q1_WEAPON_COUNT, error);
    case Q1_CHILD_ZOMBIE_CLEANUP: {
        q1_actor_snapshot *snapshot;
        if (!q1_snapshot_actors(g, &snapshot, error))
            return false;
        qa_actor_id zombie = {0};
        for (size_t i = 0; i < snapshot->shared.count; ++i)
            if (q1_classnamed(g, snapshot->shared.ids[i], "monster_szombie")) {
                zombie = snapshot->shared.ids[i];
                break;
            }
        snapshot->borrowed = false;
        if (zombie.registry &&
            !q1_damage(g, zombie, g->services.physics->world_actor,
                       g->services.physics->world_actor, 500, QA_Q1_WEAPON_COUNT, error))
            return false;
        return q1_schedule(g, e, zombie.registry ? .2 + q1_random(g) * .5 : 1.5,
                           Q1_THINK_BOSS_CHILD, error);
    }
    case Q1_CHILD_FINAL_SPIRAL:
    case Q1_CHILD_FINAL_CIRCLE:
    case Q1_CHILD_FINAL_END:
        return q1_final_child_think(g, e, error);
    case Q1_CHILD_SPAM:
    case Q1_CHILD_TELEDEATH:
        qa_error_set(error, QA_ERROR_FORMAT, e->state.boss_child.kind,
                     "unscheduled MG3 child continuation");
        return false;
    }
    qa_error_set(error, QA_ERROR_FORMAT, e->state.boss_child.kind,
                 "unknown MG3 child continuation");
    return false;
}
bool q1_boss_child_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    switch (e->state.boss_child.kind) {
    case Q1_CHILD_TELEDEATH:
        return q1_boss_teledeath_touch(g, e, other, error);
    case Q1_CHILD_EYE:
        if (qa_actor_id_equal(other, g->services.physics->world_actor) ||
            qa_actor_id_equal(other, e->owner))
            return true;
        return q1_health(g, other) == 0 ||
               q1_damage(g, other, e->id, e->owner, 500, QA_Q1_WEAPON_COUNT, error);
    case Q1_CHILD_SPAM: {
        if (qa_actor_id_equal(other, e->owner))
            return true;
        if (qa_actor_id_equal(other, g->services.physics->world_actor)) {
            qa_body_state body;
            if (!read(g, e, &body, error))
                return false;
            body.velocity = qa_v3(0, 0, 0);
            body.angles = qa_v3(0, q1_random(g) * 360, 0);
            e->physics.motion = QA_PHYSICS_STATIONARY;
            e->physics.solid = QA_PHYSICS_NOT_SOLID;
            return qa_world_body_write(g->services.world, e->id, &body, error) &&
                   q1_model(g, e, "maps/bmodel/b_splash.bsp", error) && q1_link(g, e, error) &&
                   q1_boss_child_schedule(g, e, Q1_CHILD_SPAM_BEAM, 2, error);
        }
        return (q1_health(g, other) == 0 ||
                q1_damage(g, other, e->owner.registry ? e->owner : g->services.physics->world_actor,
                          e->id, 10, QA_Q1_WEAPON_COUNT, error)) &&
               (!q1_alive(g, e->id) || q1_remove(g, e, error));
    }
    default:
        return true;
    }
}
bool q1_boss_blast_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (qa_actor_id_equal(other, g->services.physics->world_actor))
        return q1_remove(g, e, error);
    if (qa_actor_id_equal(e->owner, other) || q1_classnamed(g, other, "sphere"))
        return true;
    if (q1_classnamed(g, other, "monster_oldone_new"))
        return q1_remove(g, e, error);
    qa_physics_properties p;
    if (g->services.physics->services.read(g->services.physics->services.context, other, &p) &&
        p.solid == QA_PHYSICS_TRIGGER)
        return true;
    return (q1_health(g, other) == 0 ||
            q1_damage(g, other, e->id, e->owner, 15, QA_Q1_WEAPON_COUNT, error)) &&
           (!q1_alive(g, e->id) || q1_remove(g, e, error));
}
