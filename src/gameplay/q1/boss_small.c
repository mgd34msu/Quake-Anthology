#include "boss_internal.h"

static bool body(qa_q1_game *g, q1_actor *e, qa_body_state *out, qa_error *error) {
    return qa_world_body_read(g->services.world, e->id, out, error);
}
static qa_vec3 enemy_origin(qa_q1_game *g, q1_actor *e) {
    qa_body_state value;
    return qa_world_body_read(g->services.world, q1_ref_actor(g, e->state.monster.enemy), &value, NULL)
               ? value.origin
               : qa_v3(0, 0, 0);
}
qa_vec3 q1_boss_angles(qa_vec3 v) {
    return qa_v3(qa_builtin_angle_mod(atan2f(v.z, hypotf(v.x, v.y)) * 57.29577951308232f),
                 qa_builtin_angle_mod(atan2f(v.y, v.x) * 57.29577951308232f), 0);
}
bool q1_boss_first_player(qa_q1_game *g, q1_ref *out, qa_error *error) {
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_players(g, &snapshot, error)) return false;
    *out = snapshot->snapshot.count ? q1_ref_from(g, snapshot->snapshot.ids[0]) : (q1_ref){0};
    qa_builtin_snapshot_release(snapshot);
    return true;
}

bool q1_boss_damageable(qa_q1_game *g, q1_actor *e, bool enabled, qa_error *error) {
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, e->id, &traits, error))
        return false;
    traits.can_take_damage = enabled;
    if ((e->physics.flags & QA_PHYSICS_MONSTER) &&
        !qa_builtin_resource(&g->services, "q1:monsters", &traits.team, error))
        return false;
    return qa_combat_set_traits(g->services.combat, e->id, &traits, error);
}
static void ghost_destination(qa_q1_game *g, q1_actor *e) {
    qa_builtin_angle_vectors(qa_v3(0, q1_random(g) * 360, 0), &g->forward, &g->right, &g->up);
    e->state.monster.source.boss.destination =
        qa_vec_add(e->state.monster.source.boss.anchor, qa_vec_scale(g->forward, e->wait));
}
bool q1_boss_spawn(qa_q1_game *g, q1_actor *e, bool *handled, qa_error *error) {
    q1_monster *m = &e->state.monster;
    *handled = false;
    if (!strcmp(m->species->classname, "monster_orb")) {
        m->addon.boss = Q1_BOSS_ORB;
        return true;
    }
    bool ghost = !strcmp(m->species->classname, "monster_ghost");
    if (!ghost && strcmp(m->species->classname, "monster_szombie"))
        return q1_major_boss_spawn(g, e, handled, error);
    *handled = true;
    m->addon.boss = ghost ? Q1_BOSS_GHOST : Q1_BOSS_SHUB_ZOMBIE;
    m->path_end = ghost;
    if (ghost) {
        static const char *const deaths[] = {"ghost_diea1", "ghost_dieb1", "ghost_diec1",
                                             "ghost_died1", "ghost_diee1"};
        unsigned choice = (unsigned)(q1_random(g) * 5);
        m->source.boss.death_frame = q1_frame_index(deaths[choice < 5 ? choice : 4]);
    }
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    value.bounds = m->species->bounds;
    e->max_health = m->species->health;
    e->aimed_damage = !ghost;
    e->physics.solid = ghost ? QA_PHYSICS_BOX : QA_PHYSICS_NOT_SOLID;
    e->physics.motion = ghost ? QA_PHYSICS_FLY : QA_PHYSICS_STEP;
    if (!ghost) {
        qa_body_state owner;
        value.origin = qa_world_body_read(g->services.world, q1_ref_actor(g, e->owner), &owner, NULL)
                           ? owner.origin
                           : qa_v3(0, 0, 0);
        e->physics.flags |= QA_PHYSICS_MONSTER;
        e->physics.ideal_yaw = value.angles.y;
        if (e->physics.yaw_speed == 0)
            e->physics.yaw_speed = 20;
        m->addon.combat_style = 2;
        m->in_pain = 2;
        ++g->total_monsters;
        if (!q1_boss_first_player(g, &m->enemy, error))
            return false;
    }
    if (!qa_combat_set_health(g->services.combat, e->id, e->max_health, error) ||
        !q1_boss_damageable(g, e, true, error) || !q1_model(g, e, m->species->model, error) ||
        !qa_world_body_write(g->services.world, e->id, &value, error) || !q1_link(g, e, error))
        return false;
    if (!ghost) {
        e->frame = 174;
        m->current_frame = q1_frame_index("szombie_paine10");
        m->next_frame = q1_frame_index("szombie_paine11");
        return q1_schedule(g, e, 2, Q1_THINK_MONSTER_FRAME, error);
    }
    m->source.boss.touch = true;
    m->source.boss.anchor = value.origin;
    if (e->wait == 0)
        e->wait = 128;
    ghost_destination(g, e);
    if (!q1_monster_play(g, e, "ghost_stand1", error))
        return false;
    e->count = floorf(q1_random(g) * 5 + .5f) + 3;
    return true;
}
bool q1_boss_pain_lightning(qa_q1_game *g, q1_actor *e, qa_vec3 offset, qa_error *error) {
    float pitch = q1_random(g) * 180 + 180, yaw = q1_random(g) * 360;
    qa_builtin_angle_vectors(qa_v3(pitch, yaw, 0), &g->forward, &g->right, &g->up);
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 origin = qa_vec_add(value.origin, offset);
    qa_trace_result trace;
    if (!q1_trace(g, origin, qa_vec_add(origin, qa_vec_scale(g->forward, 1000)), e->id, false,
                  &trace, error) ||
        !q1_sound(g, e->id, "misc/power.wav", 4, 1, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = e->id,
                              .time_ns = g->time_ns,
                              .origin = origin,
                              .end = trace.end,
                              .code = 3};
    return qa_builtin_emit(&g->services, &event, error);
}
bool q1_boss_colored_explosion(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = e->id,
                              .time_ns = g->time_ns,
                              .origin = value.origin,
                              .code = 244,
                              .count = 3};
    return qa_builtin_resource(&g->services, "colored-explosion", &event.resource, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
bool q1_boss_die(qa_q1_game *g, q1_actor *e, qa_actor_id attacker, qa_error *error) {
    q1_monster *m = &e->state.monster;
    m->dead = true;
    if (m->addon.boss == Q1_BOSS_OLDNEW || m->addon.boss == Q1_BOSS_FINAL)
        return q1_major_boss_die(g, e, attacker, error);
    if (m->addon.boss == Q1_BOSS_GHOST) {
        m->next_frame = m->source.boss.death_frame;
        return q1_monster_frame(g, e, error);
    }
    if (m->counted_death)
        return true;
    m->enemy = q1_ref_from(g, attacker);
    m->source.boss.touch = false;
    if (!q1_boss_damageable(g, e, false, error) || !q1_monster_count_kill(g, e, attacker, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    if (m->addon.boss == Q1_BOSS_ORB)
        return q1_monster_play(g, e, "orb_death1", error);
    qa_body_state value;
    if (!body(g, e, &value, error) || !q1_sound(g, e->id, "zombie/z_gib.wav", 2, 1, error))
        return false;
    float health = q1_health(g, e->id);
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (unsigned i = 0; i < 3; ++i) {
        if (!q1_alive(g, e->id))
            return true;
        if (!q1_gib_at(g, e->id, value.origin, health, models[i], error))
            return false;
    }
    return !q1_alive(g, e->id) || q1_remove(g, e, error);
}
static bool actor_physics(qa_q1_game *g, qa_actor_id actor, qa_physics_properties *out) {
    return g->services.physics->services.read(g->services.physics->services.context, actor, out);
}
bool q1_boss_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (!m->source.boss.touch)
        return true;
    if (m->addon.boss == Q1_BOSS_GHOST) {
        (void)q1_random(g);
        qa_q1_target traits;
        if (q1_health(g, other) == 0 || m->pain_finished > g->time ||
            !q1_target(g, other, &traits) || !traits.player)
            return true;
        m->source.boss.touch = false;
        return q1_boss_die(g, e, other, error);
    }
    if (m->addon.boss != Q1_BOSS_ORB)
        return true;
    qa_physics_properties p;
    if (actor_physics(g, other, &p) &&
        (p.solid == QA_PHYSICS_TRIGGER || (p.solid == QA_PHYSICS_BOX && q1_health(g, other) == 0)))
        return true;
    return q1_boss_colored_explosion(g, e, error) &&
           q1_radius(g, e->id, e->id, 100, g->services.physics->world_actor, QA_Q1_WEAPON_COUNT,
                     error) &&
           (!q1_alive(g, e->id) || q1_remove(g, e, error));
}
bool q1_ghost_bubbles(qa_q1_game *g, q1_actor *timer, qa_error *error) {
    qa_physics_properties owner;
    if (!actor_physics(g, q1_ref_actor(g, timer->owner), &owner) || owner.water_level != 3)
        return true;
    qa_body_state value;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, timer->owner), &value, error) ||
        !q1_spawn_bubble(g, qa_vec_add(value.origin, qa_v3(0, 0, 24)), qa_v3(0, 0, 15), false,
                         error))
        return false;
    timer->count -= 1;
    return timer->count <= 0 ? q1_remove(g, timer, error)
                             : q1_schedule(g, timer, .1, Q1_THINK_GHOST_BUBBLES, error);
}
static bool ghost_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    q1_monster *m = &e->state.monster;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    switch (action) {
    case Q1_ACTION_GHOST_GHOST_STAND1:
        value.velocity = qa_v3(0, 0, 0);
        return qa_world_body_write(g->services.world, e->id, &value, error);
    case Q1_ACTION_GHOST_GHOST_STAND5:
        if (--e->count == 0) {
            m->next_frame = q1_frame_index("ghost_run1");
            ghost_destination(g, e);
        }
        return true;
    case Q1_ACTION_GHOST_GHOST_RUN1: {
        qa_vec3 direction = qa_vec_sub(m->source.boss.destination, value.origin);
        direction.z = 0;
        direction = qa_vec_normalize(direction);
        value.angles = q1_boss_angles(direction);
        value.velocity = qa_vec_scale(direction, 150);
        return qa_world_body_write(g->services.world, e->id, &value, error);
    }
    case Q1_ACTION_GHOST_GHOST_RUN6:
        e->count = floorf(q1_random(g) * 5 + .5f) + 3;
        return true;
    case Q1_ACTION_GHOST_GHOST_DIEA1:
        if (e->physics.water_level == 3) {
            q1_actor *timer;
            if (!q1_create(g, "death_bubbles", Q1_TIMER, e->id, &timer, error))
                return false;
            timer->count = 20;
            qa_body_state state = {.origin = value.origin};
            if (!qa_world_body_write(g->services.world, timer->id, &state, error) ||
                !q1_schedule(g, timer, .1, Q1_THINK_GHOST_BUBBLES, error) ||
                !q1_sound(g, e->id, "player/h2odeath.wav", 2, 0, error))
                return false;
        } else {
            static const char *const sounds[] = {"player/death1.wav", "player/death2.wav",
                                                 "player/death3.wav", "player/death4.wav",
                                                 "player/death5.wav"};
            unsigned index = (unsigned)floorf(q1_random(g) * 4 + .5f);
            if (!q1_sound(g, e->id, sounds[index], 2, 0, error))
                return false;
        }
        if (!q1_alive(g, e->id))
            return true;
        if (!body(g, e, &value, error))
            return false;
        value.velocity = qa_v3(0, 0, 0);
        return qa_world_body_write(g->services.world, e->id, &value, error);
    case Q1_ACTION_GHOST_GHOST_DIEA11:
        return qa_q1_spawn_teleport_fog(g, value.origin, NULL, error) && q1_remove(g, e, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown ghost continuation");
        return false;
    }
}
bool q1_orb_check_attack(qa_q1_game *g, q1_actor *e, bool *out, qa_error *error) {
    *out = false;
    q1_monster *m = &e->state.monster;
    if (g->time < m->attack_finished || !g->enemy_visible)
        return true;
    unsigned range = g->enemy_range;
    if (range == 3) {
        if (m->sliding || m->attack_state) {
            m->sliding = false;
            m->attack_state = 0;
            return q1_monster_play(g, e, "orb_run1", error);
        }
        return true;
    }
    qa_body_state self, other;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, m->enemy), &other, NULL))
        return true;
    if (!body(g, e, &self, error))
        return false;
    qa_q1_target traits;
    float eye = q1_target(g, q1_ref_actor(g, m->enemy), &traits) ? traits.view_height : 25;
    qa_trace_result trace;
    if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 25)),
                  qa_vec_add(other.origin, qa_v3(0, 0, eye)), e->id, true, &trace, error))
        return false;
    if (trace.hit != QA_TRACE_HIT_ACTOR || !q1_ref_equal(q1_ref_from(g, trace.actor), m->enemy)) {
        if (m->sliding || m->attack_state) {
            m->sliding = false;
            m->attack_state = 0;
            return q1_monster_play(g, e, "orb_run1", error);
        }
        return true;
    }
    if (q1_random(g) < (range == 0 ? .9f : range == 1 ? .6f : range == 2 ? .2f : 0)) {
        m->sliding = false;
        m->attack_state = 2;
        *out = true;
        return true;
    }
    if (range == 2) {
        if (m->sliding || m->attack_state) {
            m->sliding = false;
            m->attack_state = 0;
            return q1_monster_play(g, e, "orb_run1", error);
        }
    } else if (!m->sliding) {
        m->sliding = true;
        m->attack_state = 0;
        return q1_monster_play(g, e, "orb_side1", error);
    }
    return true;
}
static bool orb_blast(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_body_state value, target = {0};
    if (!body(g, e, &value, error))
        return false;
    (void)qa_world_body_read(g->services.world, q1_ref_actor(g, e->state.monster.enemy), &target, NULL);
    float speed = g->options.skill > 2 ? 500 : g->options.skill > 0 ? 450 : 400;
    unsigned count = 4 + (unsigned)floorf(q1_random(g) * 2 + .5f);
    qa_builtin_angle_vectors(q1_boss_angles(qa_vec_sub(target.origin, value.origin)), &g->forward,
                             &g->right, &g->up);
    qa_vec3 origin = qa_vec_add(value.origin, qa_vec_scale(g->forward, 15));
    float time = qa_vec_length(qa_vec_sub(target.origin, origin)) / speed;
    target.velocity.z = 0;
    qa_vec3 direction = qa_vec_normalize(
        qa_vec_sub(qa_vec_add(target.origin, qa_vec_scale(target.velocity, time / 4)), origin));
    if (!q1_effect(g, QA_BUILTIN_EXPLOSION, e->id, origin, 0, 0, error))
        return false;
    for (; count; --count) {
        if (!q1_alive(g, e->id))
            return true;
        float horizontal = (2 * q1_random(g) - 1) * .1f;
        float vertical = (2 * q1_random(g) - 1) * .1f;
        qa_vec3 aim =
            qa_vec_normalize(qa_vec_add(direction, qa_vec_add(qa_vec_scale(g->right, horizontal),
                                                              qa_vec_scale(g->up, vertical))));
        q1_actor *shot;
        if (!q1_projectile_spawn(g, e->id, QA_Q1_WEAPON_COUNT, Q1_SPIKE,
                                 qa_vec_add(origin, qa_vec_scale(aim, 8)), qa_vec_scale(aim, 1000),
                                 &shot, error))
            return false;
        shot->state.projectile.kind = Q1_ORB_ROCK;
        if (!qa_builtin_resource(&g->services, "rock", &shot->classname, error) ||
            !q1_model(g, shot, "progs/rogue/sphere.mdl", error) || !body(g, shot, &value, error)) {
            q1_remove(g, shot, NULL);
            return false;
        }
        value.velocity = qa_vec_scale(aim, speed + (2 * q1_random(g) - 1) * 100);
        value.bounds = (qa_bounds){0};
        float x = 300 * (2 * q1_random(g) - 1), y = 300 * (2 * q1_random(g) - 1);
        float z = 300 * (2 * q1_random(g) - 1);
        shot->physics.angular_velocity = qa_v3(x, y, z);
        if (count % 2 == 0) {
            shot->count = 1;
            shot->effects |= 64;
        }
        if (!qa_world_body_write(g->services.world, shot->id, &value, error)) {
            q1_remove(g, shot, NULL);
            return false;
        }
    }
    return true;
}
static bool orb_idle(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (m->idle_until >= g->time)
        return true;
    m->idle_until = g->time + 15 + q1_random(g) * 10;
    return q1_sound(g, e->id, "boss2/sight.wav", 2, 2, error);
}
static bool orb_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    q1_monster *m = &e->state.monster;
    switch (action) {
    case Q1_ACTION_ORB_ORB_STAND1:
        return q1_monster_ai(g, e, Q1_AI_STAND, 0, error);
    case Q1_ACTION_ORB_ORB_WALK1:
        return q1_monster_ai(g, e, Q1_AI_WALK, 8, error) &&
               (!q1_alive(g, e->id) || orb_idle(g, e, error));
    case Q1_ACTION_ORB_ORB_SIDE1:
        if (!q1_monster_face(g, e, error))
            return false;
        /* fall through */
    case Q1_ACTION_ORB_ORB_RUN1:
        return q1_monster_ai(g, e, Q1_AI_RUN, 16, error) &&
               (!q1_alive(g, e->id) || orb_idle(g, e, error));
    case Q1_ACTION_ORB_ORB_FAST1:
        m->source.boss.shots = 1 + (int32_t)floorf(q1_random(g) * 3 + .5f);
        return q1_monster_face(g, e, error);
    case Q1_ACTION_ORB_ORB_FAST3:
        return q1_schedule(g, e, .3, Q1_THINK_MONSTER_FRAME, error) && q1_monster_face(g, e, error);
    case Q1_ACTION_ORB_ORB_FAST4:
        if (!q1_monster_face(g, e, error) || !orb_blast(g, e, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        if (--m->source.boss.shots != 0)
            m->next_frame = q1_frame_index("orb_fast2");
        return q1_schedule(g, e, .2, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_ORB_ORB_FAST5:
        if (!q1_monster_face(g, e, error))
            return false;
        m->counter = 0;
        m->attack_finished = g->time + 2;
        m->attack_state = 0;
        m->sliding = g->enemy_range < 2 && g->enemy_visible;
        m->next_frame = q1_frame_index(m->sliding ? "orb_side1" : "orb_run1");
        return true;
    case Q1_ACTION_ORB_ORB_PAIN1:
        m->source.boss.shocks = 3;
        return q1_schedule(g, e, .2, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_ORB_ORB_PAIN2:
        if (!q1_boss_pain_lightning(g, e, qa_v3(0, 0, 0), error))
            return false;
        if (--m->source.boss.shocks != 0)
            m->next_frame = q1_frame_index("orb_pain2");
        return q1_schedule(g, e, .3, Q1_THINK_MONSTER_FRAME, error);
    case Q1_ACTION_ORB_ORB_DEATH1: {
        qa_body_state value;
        if (!body(g, e, &value, error))
            return false;
        float x = -200 + 400 * q1_random(g), y = -200 + 400 * q1_random(g);
        float z = 150 + 150 * q1_random(g);
        value.velocity = qa_v3(x, y, z);
        value.bounds = (qa_bounds){0};
        e->physics.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
        e->physics.solid = QA_PHYSICS_BOX;
        return qa_world_body_write(g->services.world, e->id, &value, error) &&
               q1_link(g, e, error) && q1_sound(g, e->id, "orb/orb_death.wav", 2, 1, error);
    }
    case Q1_ACTION_ORB_ORB_DEATH3:
        m->source.boss.touch = true;
        return true;
    case Q1_ACTION_ORB_ORB_DEATH4:
        return q1_schedule(g, e, 9999, Q1_THINK_MONSTER_FRAME, error);
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown orb continuation");
        return false;
    }
}
static bool shub_grenade(qa_q1_game *g, q1_actor *e, qa_vec3 offset, qa_error *error) {
    if (!q1_monster_face(g, e, error) || !q1_sound(g, e->id, "zombie/z_shot1.wav", 1, 1, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 origin =
        qa_vec_add(value.origin, qa_vec_add(qa_vec_scale(g->forward, offset.x),
                                            qa_vec_add(qa_vec_scale(g->right, offset.y),
                                                       qa_vec_scale(g->up, offset.z - 24))));
    qa_builtin_angle_vectors(value.angles, &g->forward, &g->right, &g->up);
    qa_vec3 velocity = qa_vec_scale(qa_vec_normalize(qa_vec_sub(enemy_origin(g, e), origin)), 600);
    velocity.z = 200;
    q1_actor *shot;
    if (!q1_projectile_spawn(g, e->id, QA_Q1_WEAPON_COUNT, Q1_ZOMBIE_GRENADE, origin, velocity,
                             &shot, error))
        return false;
    shot->classname = 0;
    shot->state.projectile.kind = Q1_SHUB_GRENADE;
    if (!body(g, shot, &value, error))
        return false;
    value.angles = qa_v3(0, 0, 0);
    return qa_world_body_write(g->services.world, shot->id, &value, error);
}
static bool shub_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    q1_monster *m = &e->state.monster;
    q1_ai ai = Q1_AI_PAIN;
    float distance = 0;
    switch (action) {
    case Q1_ACTION_SZOMBIE_SZOMBIE_STAND1:
        return q1_monster_ai(g, e, Q1_AI_STAND, 0, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_WALK1:
        ai = Q1_AI_WALK;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_WALK2:
        ai = Q1_AI_WALK;
        distance = 2;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_WALK3:
        ai = Q1_AI_WALK;
        distance = 3;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_WALK5:
        ai = Q1_AI_WALK;
        distance = 1;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_WALK19:
        return q1_monster_ai(g, e, Q1_AI_WALK, 0, error) &&
               (!q1_alive(g, e->id) || q1_random(g) >= .2f ||
                q1_sound(g, e->id, "zombie/z_idle.wav", 2, 2, error));
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN1:
        if (!q1_monster_ai(g, e, Q1_AI_RUN, 1, error))
            return false;
        m->in_pain = 0;
        return true;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN2:
        ai = Q1_AI_RUN;
        distance = 1;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN3:
        ai = Q1_AI_RUN;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN5:
        ai = Q1_AI_RUN;
        distance = 2;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN6:
        ai = Q1_AI_RUN;
        distance = 3;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN7:
        ai = Q1_AI_RUN;
        distance = 4;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN15:
        ai = Q1_AI_RUN;
        distance = 6;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN16:
        ai = Q1_AI_RUN;
        distance = 7;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_RUN18:
        if (!q1_monster_ai(g, e, Q1_AI_RUN, 8, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        if (q1_random(g) < .2f && !q1_sound(g, e->id, "zombie/z_idle.wav", 2, 2, error))
            return false;
        return !q1_alive(g, e->id) || q1_random(g) <= .8f ||
               q1_sound(g, e->id, "zombie/z_idle1.wav", 2, 2, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_ATTA1:
        return q1_monster_face(g, e, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_ATTA13:
        return shub_grenade(g, e, qa_v3(-10, -22, 30), error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_ATTB14:
        return shub_grenade(g, e, qa_v3(-10, -24, 29), error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_ATTC12:
        return shub_grenade(g, e, qa_v3(-12, -19, 29), error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINA1:
        return q1_sound(g, e->id, "zombie/z_pain.wav", 2, 1, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINB1:
        return q1_sound(g, e->id, "zombie/z_pain1.wav", 2, 1, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINA2:
        ai = Q1_AI_PAINFORWARD;
        distance = 3;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINA3:
        ai = Q1_AI_PAINFORWARD;
        distance = 1;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINA4:
        distance = 1;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINA5:
        distance = 3;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINB2:
        distance = 2;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINB3:
        distance = 8;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINB4:
        distance = 6;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINB9:
        return q1_sound(g, e->id, "zombie/z_fall.wav", 4, 1, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINE1:
        return q1_sound(g, e->id, "zombie/z_pain.wav", 2, 1, error) &&
               qa_combat_set_health(g->services.combat, e->id, 60, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINE3:
        distance = 5;
        break;
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINE10:
        if (!q1_sound(g, e->id, "zombie/z_fall.wav", 4, 1, error))
            return false;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        return !q1_alive(g, e->id) || q1_link(g, e, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINE11:
        return q1_schedule(g, e, e->next_think - g->time + 5, Q1_THINK_MONSTER_FRAME, error) &&
               qa_combat_set_health(g->services.combat, e->id, 60, error);
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINE12: {
        if (!qa_combat_set_health(g->services.combat, e->id, 60, error) ||
            !q1_sound(g, e->id, "zombie/z_idle.wav", 2, 2, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        e->physics.solid = QA_PHYSICS_BOX;
        bool moved;
        if (!qa_physics_walk_move(g->services.physics, e->id, 0, 0, (float)g->elapsed, true, true,
                                  &moved, error))
            return false;
        if (!moved) {
            m->next_frame = q1_frame_index("szombie_paine11");
            e->physics.solid = QA_PHYSICS_NOT_SOLID;
        }
        return q1_link(g, e, error);
    }
    case Q1_ACTION_SZOMBIE_SZOMBIE_PAINE25:
        ai = Q1_AI_PAINFORWARD;
        distance = 5;
        break;
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown Shub zombie continuation");
        return false;
    }
    return q1_monster_ai(g, e, ai, distance, error);
}
bool q1_boss_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    switch (e->state.monster.addon.boss) {
    case Q1_BOSS_GHOST:
        return ghost_action(g, e, action, error);
    case Q1_BOSS_ORB:
        return orb_action(g, e, action, error);
    case Q1_BOSS_SHUB_ZOMBIE:
        return shub_action(g, e, action, error);
    case Q1_BOSS_OLDNEW:
    case Q1_BOSS_FINAL:
        return q1_major_boss_action(g, e, action, error);
    case Q1_BOSS_NONE:
        break;
    }
    qa_error_set(error, QA_ERROR_FORMAT, action, "boss continuation without native state");
    return false;
}
bool q1_orb_rock_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (q1_ref_equal(e->owner, q1_ref_from(g, other)))
        return true;
    if (q1_classnamed(g, other, "monster_orb") || q1_classnamed(g, other, "monster_lava_man") ||
        q1_classnamed(g, other, "monster_super_shambler"))
        return q1_remove(g, e, error);
    qa_physics_properties p;
    if (q1_classnamed(g, other, "rock") ||
        (actor_physics(g, other, &p) && p.solid == QA_PHYSICS_TRIGGER))
        return true;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_point_query query = {.point = value.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (qa_collision_contents_export(contents.contents, QA_COLLISION_Q1, contents.q1_opaque_token) == -6)
        return q1_remove(g, e, error);
    if (q1_damageable(g, other)) {
        if (!q1_effect(g, QA_BUILTIN_IMPACT, other, value.origin, 18, 1, error) ||
            !q1_damage(g, other, e->id, q1_ref_actor(g, e->owner), 18, QA_Q1_WEAPON_COUNT, error))
            return false;
    } else if (e->count != 0 && !q1_effect(g, QA_BUILTIN_IMPACT, e->id, value.origin, 0, 8, error))
        return false;
    return !q1_alive(g, e->id) || q1_remove(g, e, error);
}
bool q1_shub_grenade_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (e->state.projectile.remove_touch)
        return q1_remove(g, e, error);
    if (q1_ref_equal(e->owner, q1_ref_from(g, other)))
        return true;
    if (q1_classnamed(g, other, "monster_oldone_new") || q1_classnamed(g, other, "oldnew_child"))
        return q1_remove(g, e, error);
    if (q1_damageable(g, other))
        return q1_damage(g, other, e->id, q1_ref_actor(g, e->owner), 10, QA_Q1_WEAPON_COUNT, error) &&
               (!q1_alive(g, e->id) ||
                (q1_sound(g, e->id, "zombie/z_hit.wav", 1, 1, error) && q1_remove(g, e, error)));
    if (!q1_sound(g, e->id, "zombie/z_miss.wav", 1, 1, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    value.velocity = qa_v3(0, 0, 0);
    e->physics.angular_velocity = qa_v3(0, 0, 0);
    e->state.projectile.remove_touch = true;
    return qa_world_body_write(g->services.world, e->id, &value, error);
}
bool q1_spawn_shub_zombie(qa_q1_game *g, qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    size_t alive = 0, eligible = 0;
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        q1_actor *e = q1_entity(g, snapshot->snapshot.ids[i]);
        if (!e)
            continue;
        alive += q1_classnamed(g, e->id, "monster_szombie");
        if (q1_classnamed(g, e->id, "info_szombie_spawn") && e->wait != 0 && e->wait < g->time)
            ++eligible;
    }
    if (alive > 32 || !eligible) {
        qa_builtin_snapshot_release(snapshot);
        return true;
    }
    size_t selected = (size_t)floorf(q1_random(g) * (float)(eligible - 1) + .5f);
    qa_actor_id point = {0};
    for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
        q1_actor *e = q1_entity(g, snapshot->snapshot.ids[i]);
        if (e && q1_classnamed(g, e->id, "info_szombie_spawn") && selected-- == 0) {
            point = e->id;
            e->wait = (float)(g->time + 8);
            break;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    q1_actor *zombie;
    if (!q1_create(g, "monster_szombie", Q1_MONSTER, point, &zombie, error))
        return false;
    if (!q1_monster_spawn(g, zombie, q1_species_find("monster_szombie"), error)) {
        q1_remove(g, zombie, NULL);
        return false;
    }
    *out = zombie->id;
    return true;
}
bool q1_spawn_homing_flame(qa_q1_game *g, q1_actor *source, qa_actor_id *out, qa_error *error) {
    qa_body_state value, target = {0};
    if (!body(g, source, &value, error))
        return false;
    q1_ref enemy = source->kind == Q1_MONSTER ? source->state.monster.enemy : source->physics.enemy;
    qa_q1_target traits;
    if ((!q1_target(g, q1_ref_actor(g, enemy), &traits) || !traits.player) &&
        !q1_boss_first_player(g, &enemy, error))
        return false;
    (void)qa_world_body_read(g->services.world, q1_ref_actor(g, enemy), &target, NULL);
    qa_vec3 origin = qa_vec_add(value.origin, qa_v3(0, 0, 4));
    q1_actor *flame;
    if (!q1_projectile_spawn(g, (qa_actor_id){0}, QA_Q1_WEAPON_COUNT, Q1_SPIKE, origin,
                             qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, origin)), 400),
                             &flame, error))
        return false;
    flame->classname = 0;
    flame->effects = 64;
    flame->speed = 400;
    flame->state.projectile.enemy = enemy;
    flame->state.projectile.expires = g->time;
    if (!body(g, flame, &value, error)) {
        q1_remove(g, flame, NULL);
        return false;
    }
    value.angles = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, flame->id, &value, error) ||
        !q1_model(g, flame, "progs/flame2.mdl", error) ||
        !q1_schedule(g, flame, .1, Q1_THINK_HOMING_FLAME, error)) {
        q1_remove(g, flame, NULL);
        return false;
    }
    *out = flame->id;
    return q1_remove(g, source, error);
}
bool q1_homing_flame_think(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->state.projectile.expires + 3 < g->time)
        return q1_boss_colored_explosion(g, e, error) &&
               q1_radius(g, e->id, e->id, 100, g->services.physics->world_actor, QA_Q1_WEAPON_COUNT,
                         error) &&
               (!q1_alive(g, e->id) || q1_remove(g, e, error));
    qa_body_state value, target = {0};
    if (!body(g, e, &value, error))
        return false;
    (void)qa_world_body_read(g->services.world, q1_ref_actor(g, e->state.projectile.enemy), &target, NULL);
    e->speed = fmaxf(0, e->speed - 10);
    value.velocity = qa_vec_scale(
        qa_vec_add(qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, value.origin)), .3f),
                   qa_vec_scale(qa_vec_normalize(value.velocity), .7f)),
        e->speed);
    return qa_world_body_write(g->services.world, e->id, &value, error) &&
           q1_schedule(g, e, .1, Q1_THINK_HOMING_FLAME, error);
}
