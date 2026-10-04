#include "internal.h"
#include "qa/game_q1_maps.h"

static bool body(qa_q1_game *g, q1_actor *e, qa_body_state *out, qa_error *error) {
    return qa_world_body_read(g->services.world, e->id, out, error);
}
static qa_vec3 target(qa_q1_game *g, q1_actor *e) {
    qa_body_state value;
    return qa_world_body_read(g->services.world, e->state.monster.enemy, &value, NULL)
               ? value.origin
               : qa_v3(0, 0, 0);
}
static qa_vec3 angles(qa_vec3 direction) {
    return qa_v3(qa_builtin_angle_mod(atan2f(direction.z, hypotf(direction.x, direction.y)) *
                                      57.29577951308232f),
                 qa_builtin_angle_mod(atan2f(direction.y, direction.x) * 57.29577951308232f), 0);
}
static float signed_random(qa_q1_game *g) { return 2 * q1_random(g) - 1; }
static bool bloody(qa_q1_game *g) { return (qa_q1_game_campaign_flags(g) & 64) != 0; }
static bool remove_child(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_actor *child = q1_entity(g, e->state.monster.source.heavy.child);
    e->state.monster.source.heavy.child = (qa_actor_id){0};
    return !child || !qa_actor_id_equal(child->owner, e->id) ||
           !q1_classnamed(g, child->id, "lightning_child") || q1_remove(g, child, error);
}
static bool child_frame(qa_q1_game *g, q1_actor *e, int32_t frame) {
    e->effects |= 2;
    q1_actor *child = q1_entity(g, e->state.monster.source.heavy.child);
    if (child && qa_actor_id_equal(child->owner, e->id) &&
        q1_classnamed(g, child->id, "lightning_child"))
        child->frame = frame;
    return true;
}
static bool lightning_child(qa_q1_game *g, q1_actor *e, bool fast, qa_error *error) {
    if (!q1_monster_face(g, e, error) ||
        !q1_schedule(g, e, e->next_think - g->time + .2, Q1_THINK_MONSTER_FRAME, error))
        return false;
    e->effects |= 2;
    if (!q1_monster_face(g, e, error))
        return false;
    qa_body_state parent;
    if (!body(g, e, &parent, error))
        return false;
    q1_actor *child;
    if (!q1_create(g, fast ? "" : "lightning_child", Q1_TIMER, fast ? (qa_actor_id){0} : e->id,
                   &child, error))
        return false;
    e->state.monster.source.heavy.child = child->id;
    qa_body_state value = {.origin = parent.origin, .angles = parent.angles};
    if (!q1_model(g, child, "progs/s_light.mdl", error) ||
        !qa_world_body_write(g->services.world, child->id, &value, error) ||
        !q1_link(g, child, error) ||
        !q1_schedule(g, child, fast ? .7 : 1.4, Q1_THINK_REMOVE, error)) {
        q1_remove(g, child, NULL);
        return false;
    }
    return true;
}
static bool cast_lightning(qa_q1_game *g, q1_actor *e, qa_error *error) {
    e->effects |= 2;
    if (!q1_monster_face(g, e, error))
        return false;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 origin = qa_vec_add(value.origin, qa_v3(0, 0, 40));
    qa_vec3 delta = qa_vec_sub(qa_vec_add(target(g, e), qa_v3(0, 0, 16)), origin);
    float distance = fmaxf(600, fminf(1000, qa_vec_length(delta)));
    qa_trace_result trace;
    if (!q1_trace(g, origin,
                  qa_vec_add(value.origin, qa_vec_scale(qa_vec_normalize(delta), distance)), e->id,
                  false, &trace, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = e->id,
                              .time_ns = g->time_ns,
                              .origin = origin,
                              .end = trace.end,
                              .code = 1};
    if (!qa_builtin_emit(&g->services, &event, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    ++e->state.monster.source.heavy.lightning_count;
    return q1_lightning_rays(g, e->id, e->id, origin, trace.end, 10, 40, 246, qa_v3(0, 0, 100),
                             Q1_LIGHTNING_REMEMBER_ALL | Q1_LIGHTNING_PARTICLES, QA_Q1_WEAPON_COUNT,
                             NULL, error);
}
static bool attack_check(qa_q1_game *g, q1_actor *e) {
    int cap = 3 + (int)floorf(q1_random(g) * 2 + .5f) - (bloody(g) ? 1 : 0);
    return ++e->count > (float)cap;
}
static float vertical_offset(qa_q1_game *g, q1_actor *e, qa_vec3 origin) {
    qa_vec3 delta = qa_vec_sub(target(g, e), origin);
    if (delta.z > 30)
        return 100 + signed_random(g) * 50;
    if (delta.z < -30)
        return -100 + signed_random(g) * 50;
    float distance = qa_vec_length(delta);
    return distance > 300   ? 50 + signed_random(g) * 25
           : distance < 150 ? -50 + signed_random(g) * 25
                            : 0;
}
static bool heavy_spike(qa_q1_game *g, q1_actor *owner, qa_vec3 origin, qa_vec3 velocity,
                        q1_actor **out, qa_error *error) {
    if (!q1_projectile_spawn(g, owner->id, QA_Q1_WEAPON_COUNT, Q1_SPIKE, origin, velocity, out,
                             error))
        return false;
    (*out)->state.projectile.kind = Q1_HEAVY_SPIKE;
    if (qa_builtin_resource(&g->services, "knightspike", &(*out)->classname, error))
        return true;
    q1_remove(g, *out, NULL);
    return false;
}
static bool shambler_shot(qa_q1_game *g, q1_actor *e, float y, float z, qa_vec3 offset, bool hands,
                          qa_error *error) {
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 aim = angles(qa_vec_sub(target(g, e), value.origin));
    if (hands) {
        aim.y += y * (12 + signed_random(g) * 4);
        aim.x += z * (12 + signed_random(g) * 4);
    } else
        aim.y += y * 5;
    qa_builtin_angle_vectors(aim, &g->forward, &g->right, &g->up);
    qa_vec3 origin = qa_vec_add(
        qa_vec_add(qa_vec_add(value.origin,
                              qa_vec_scale(qa_vec_add(value.bounds.mins, value.bounds.maxs), .5f)),
                   qa_vec_scale(g->forward, 20)),
        offset);
    qa_vec3 direction = qa_vec_normalize(g->forward);
    if (!hands)
        direction.z = -direction.z + (q1_random(g) - .5f) * .1f;
    q1_actor *shot;
    if (!heavy_spike(g, e, origin, qa_vec_scale(direction, 1000), &shot, error))
        return false;
    if (!q1_model(g, shot, "progs/rogue/plasma.mdl", error))
        goto fail;
    float x = 300 * signed_random(g), ay = 300 * signed_random(g), az = 300 * signed_random(g);
    shot->physics.angular_velocity = qa_v3(x, ay, az);
    shot->physics.motion = QA_PHYSICS_TOSS;
    if (!qa_world_body_read(g->services.world, shot->id, &value, error))
        goto fail;
    value.bounds = (qa_bounds){0};
    value.velocity = qa_vec_scale(direction, signed_random(g) * 100 + (hands ? 500 : 400));
    value.velocity.z = (hands ? 100 : 125) + signed_random(g) * 50;
    qa_body_state owner;
    if (!body(g, e, &owner, error))
        goto fail;
    value.velocity.z += vertical_offset(g, e, owner.origin);
    shot->effects |= 64;
    if (qa_world_body_write(g->services.world, shot->id, &value, error) && q1_link(g, shot, error))
        return true;
fail:
    q1_remove(g, shot, NULL);
    return false;
}
static bool blast(qa_q1_game *g, q1_actor *e, qa_vec3 offset, bool hands, qa_error *error) {
    for (int x = hands ? -1 : -5; x <= (hands ? 1 : 5); ++x)
        for (int y = hands ? -1 : 0; y <= (hands ? 1 : 0); ++y) {
            if (!shambler_shot(g, e, (float)x, (float)y, offset, hands, error))
                return false;
            if (!q1_alive(g, e->id))
                return true;
        }
    return hands || q1_sound(g, e->id, "zombie/z_shot1.wav", 1, 1, error);
}
static bool claw(qa_q1_game *g, q1_actor *e, float side, qa_error *error) {
    if (!e->state.monster.enemy.registry)
        return true;
    if (!q1_monster_ai(g, e, Q1_AI_CHARGE, 10, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    if (qa_vec_length(qa_vec_sub(target(g, e), value.origin)) > 100)
        return true;
    if (!q1_monster_melee(g, e, 100, 20, 3, false, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    if (!q1_sound(g, e->id, "shambler/smack.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, e->id) || side == 0)
        return true;
    if (!body(g, e, &value, error))
        return false;
    qa_builtin_angle_vectors(value.angles, &g->forward, &g->right, &g->up);
    return q1_meat_spray(g, e, qa_vec_add(value.origin, qa_vec_scale(g->forward, 16)),
                         qa_vec_scale(g->right, side), error);
}
static bool smash(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (!blast(g, e, qa_v3(0, 0, 0), false, error))
        return false;
    if (!q1_alive(g, e->id) || !e->state.monster.enemy.registry)
        return true;
    if (!q1_monster_ai(g, e, Q1_AI_CHARGE, 0, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    if (qa_vec_length(qa_vec_sub(target(g, e), value.origin)) > 100)
        return true;
    bool visible;
    if (!q1_can_damage(g, e->state.monster.enemy, e->id, &visible, error))
        return false;
    if (!visible)
        return true;
    if (!q1_monster_melee(g, e, 100, 40, 3, false, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    if (!q1_sound(g, e->id, "shambler/smack.wav", 2, 1, error))
        return false;
    for (unsigned i = 0; i < 2; ++i) {
        if (!q1_alive(g, e->id))
            return true;
        if (!body(g, e, &value, error))
            return false;
        qa_vec3 origin = qa_vec_add(value.origin, qa_vec_scale(g->forward, 16));
        qa_vec3 velocity = qa_vec_scale(g->right, signed_random(g) * 100);
        if (!q1_meat_spray(g, e, origin, velocity, error))
            return false;
    }
    return true;
}
bool q1_heavy_melee(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->state.monster.addon.heavy == Q1_HEAVY_RUNE_KNIGHT) {
        g->rune_knight_melee = g->rune_knight_melee % 3 + 1;
        if (!q1_sound(g, e->id, "hknight/slash1.wav", 1, 1, error))
            return false;
        return !q1_alive(g, e->id) || q1_monster_play(g, e,
                                                      g->rune_knight_melee == 1   ? "rknight_slice1"
                                                      : g->rune_knight_melee == 2 ? "rknight_smash1"
                                                                                  : "rknight_watk1",
                                                      error);
    }
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 delta = qa_vec_sub(value.origin, target(g, e));
    float distance = qa_vec_length(delta);
    const char *frame;
    if (distance < 110)
        frame = q1_random(g) < .8f   ? "supsham_smash1"
                : q1_random(g) < .5f ? "supsham_swingl1"
                                     : "supsham_swingr1";
    else if (distance < 200 || e->wait > g->time) {
        qa_builtin_angle_vectors(value.angles, &g->forward, &g->right, &g->up);
        float chance = qa_vec_dot(g->right, qa_vec_normalize(delta));
        frame = chance < .45f  ? "supsham_swingr1"
                : chance < .9f ? "supsham_swingl1"
                               : "supsham_smash1";
    } else {
        e->count = 0;
        bool slow = q1_random(g) > .4f;
        if (!q1_monster_play(g, e, slow ? "supsham_magic1" : "supsham_magic_b1", error))
            return false;
        e->wait = (float)(g->time + (slow ? 5 : 3));
        return true;
    }
    return q1_monster_play(g, e, frame, error);
}
static bool rune_shot(qa_q1_game *g, q1_actor *e, float offset, unsigned variant, qa_error *error) {
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 delta = qa_vec_sub(target(g, e), value.origin), direction, origin;
    qa_vec3 center = qa_vec_add(
        value.origin, qa_vec_scale(qa_vec_add(value.bounds.mins, value.bounds.maxs), .5f));
    if (!variant) {
        qa_vec3 aim = angles(delta);
        aim.y += offset * 6;
        qa_builtin_angle_vectors(aim, &g->forward, &g->right, &g->up);
        origin = qa_vec_add(center, qa_vec_scale(g->forward, 20));
        direction = qa_vec_normalize(g->forward);
        direction.z = -direction.z + (q1_random(g) - .5f) * .1f;
    } else {
        qa_builtin_angle_vectors(value.angles, &g->forward, &g->right, &g->up);
        origin = qa_vec_add(center, qa_vec_scale(g->forward, 10));
        direction = qa_vec_normalize(delta);
        if (variant == 1) {
            origin.z += 10;
            direction.z += .017f * offset * 6;
            direction.y += (q1_random(g) - .5f) * .2f;
        } else {
            origin.z += 14;
            direction.y += (q1_random(g) - .5f) * .1f * (offset + 2);
            direction.z += (q1_random(g) - .5f) * .1f * (offset + 2);
        }
        direction = qa_vec_normalize(direction);
    }
    q1_actor *shot;
    if (!heavy_spike(g, e, origin, qa_vec_scale(direction, 1000), &shot, error))
        return false;
    if (!q1_model(g, shot, "progs/diamond_trail.mdl", error) ||
        !qa_world_body_read(g->services.world, shot->id, &value, error))
        goto fail;
    value.velocity = qa_vec_scale(direction, bloody(g) ? 600 : 400);
    shot->effects = 64;
    if (qa_world_body_write(g->services.world, shot->id, &value, error) && q1_link(g, shot, error))
        return q1_sound(g, e->id, "hknight/attack1.wav", 1, 1, error);
fail:
    q1_remove(g, shot, NULL);
    return false;
}
static bool rune_magic(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_body_state value;
    if (!body(g, e, &value, error))
        return false;
    qa_vec3 delta = qa_vec_sub(target(g, e), value.origin);
    float chance = qa_vec_length(delta) > 300 ? .6f : .2f;
    if (q1_random(g) < chance)
        return q1_monster_play(g, e, "rknight_magicb1", error);
    return q1_monster_play(
        g, e, q1_random(g) > (delta.z > 100 ? .7f : .5f) ? "rknight_magica1" : "rknight_magicc1",
        error);
}
static bool cleanup_orbs(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < snapshot->shared.count; ++i) {
        q1_actor *child = q1_entity(g, snapshot->shared.ids[i]);
        if (child && qa_actor_id_equal(child->owner, e->id) &&
            q1_classnamed(g, child->id, "monster_super_shambler") &&
            !q1_schedule(g, child, .1, Q1_THINK_HEAVY_SOURCE_DIE, error)) {
            ok = false;
            break;
        }
    }
    snapshot->borrowed = false;
    return ok;
}

bool q1_heavy_check_attack(qa_q1_game *g, q1_actor *e, bool *attacking, qa_error *error) {
    *attacking = false;
    q1_monster *m = &e->state.monster;
    if (!m->enemy.registry)
        return true;
    qa_body_state self, other;
    if (!body(g, e, &self, error) ||
        !qa_world_body_read(g->services.world, m->enemy, &other, error))
        return false;
    qa_q1_target traits;
    float eye = q1_target(g, m->enemy, &traits) ? traits.view_height : 25;
    qa_trace_result trace;
    if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 25)),
                  qa_vec_add(other.origin, qa_v3(0, 0, eye)), e->id, true, &trace, error))
        return false;
    if (trace.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(trace.actor, m->enemy) ||
        (trace.in_open && trace.in_water))
        return true;
    unsigned range = q1_mg3_range(e, qa_vec_length(qa_vec_sub(other.origin, self.origin)));
    if (range == 0 && m->species->melee) {
        *attacking = true;
        return q1_heavy_melee(g, e, error);
    }
    if (!m->species->missile || g->time < m->attack_finished || range == 3)
        return true;
    if (range == 0)
        m->attack_finished = 0;
    float chance = range == 0   ? .9f
                   : range == 1 ? (m->species->melee ? .2f : .4f)
                   : range == 2 ? (m->species->melee ? .05f : .1f)
                                : 0;
    if (q1_random(g) >= chance)
        return true;
    if (!q1_monster_play(g, e, m->species->missile, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    m->refired = false;
    m->attack_finished = g->time + 2 * q1_random(g);
    *attacking = true;
    return true;
}

bool q1_heavy_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    q1_monster *m = &e->state.monster;
    switch (action) {
    case Q1_ACTION_SUPSHAM_REMOVECHILD:
        return remove_child(g, e, error);
    case Q1_ACTION_SUPCASTLIGHTNING:
        return cast_lightning(g, e, error);
    case Q1_ACTION_SUPSHAM_MELEE:
    case Q1_ACTION_RKNIGHT_MELEE:
        return q1_heavy_melee(g, e, error);
    case Q1_ACTION_SUPSHAM_MISSILE: {
        float r = q1_random(g);
        return q1_monster_play(g, e,
                               r < .36f   ? "supsham_magic1"
                               : r < .66f ? "supsham_swingr1"
                                          : "supsham_swingl1",
                               error);
    }
    case Q1_ACTION_CLEANUP_ORBS:
        return cleanup_orbs(g, e, error);
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SMASH10:
        return smash(g, e, error);
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SMASH12:
        if (!q1_monster_ai(g, e, Q1_AI_CHARGE, 4, error))
            return false;
        if (q1_alive(g, e->id) && attack_check(g, e))
            m->next_frame = q1_frame_index("supsham_magic1");
        return true;
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SWINGL7:
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SWINGR7: {
        bool left = action == Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SWINGL7;
        qa_vec3 offset = qa_vec_add(qa_vec_scale(g->right, left ? 16 : -16), qa_v3(0, 0, 32));
        if (!blast(g, e, offset, true, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        if (!q1_monster_ai(g, e, Q1_AI_CHARGE, left ? 5 : 6, error))
            return false;
        return !q1_alive(g, e->id) || claw(g, e, left ? 250 : -250, error);
    }
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SWINGL9:
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SWINGR9: {
        bool left = action == Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_SWINGL9;
        if (!q1_monster_ai(g, e, Q1_AI_CHARGE, left ? 8 : 1, error) ||
            (!left && q1_alive(g, e->id) && !q1_monster_ai(g, e, Q1_AI_CHARGE, 10, error)))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        qa_body_state value;
        if (!body(g, e, &value, error))
            return false;
        qa_builtin_angle_vectors(value.angles, &g->forward, &g->right, &g->up);
        if (attack_check(g, e))
            m->next_frame = q1_frame_index("supsham_magic1");
        else if (q1_random(g) < .5f)
            m->next_frame = q1_frame_index(left ? "supsham_swingr1" : "supsham_swingl1");
        return true;
    }
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_MAGIC1:
        if (!q1_monster_face(g, e, error) ||
            !q1_sound(g, e->id, "shambler/sattck1.wav", 1, 1, error))
            return false;
        e->count = 0;
        return true;
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_MAGIC3:
        return lightning_child(g, e, false, error);
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_MAGIC_B3:
        return lightning_child(g, e, true, error);
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_MAGIC4:
        return child_frame(g, e, 1);
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_MAGIC5:
        return child_frame(g, e, 2);
    case Q1_ACTION_MG3_SUPER_SHAMBLER_SUPSHAM_MAGIC4B:
        return child_frame(g, e, 3);
    case Q1_ACTION_RKNIGHT_MAGIC:
        return rune_magic(g, e, error);
    case Q1_ACTION_RKNIGHT_RUN:
        return q1_monster_play(g, e, e->spawnflags & 2 ? "rknight_runb1" : "rknight_run1", error);
    case Q1_ACTION_RK_IDLE_SOUND: {
        if (q1_random(g) >= .2f)
            return true;
        float r = q1_random(g);
        return q1_sound(g, e->id,
                        r < .3f   ? "rknight/idle_02.wav"
                        : r < .6f ? "rknight/idle_03.wav"
                                  : "rknight/idle_05.wav",
                        2, 1, error);
    }
    case Q1_ACTION_RKNIGHT_PAIN_SOUND: {
        float r = q1_random(g);
        return q1_sound(g, e->id,
                        r < .3f   ? "rknight/pain_01.wav"
                        : r < .6f ? "rknight/pain_02.wav"
                                  : "rknight/pain_03.wav",
                        2, 1, error);
    }
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB6:
        if (!q1_monster_face(g, e, error))
            return false;
        m->source.heavy.nails = (int32_t)floorf(q1_random(g) * 8 + .5f);
        return true;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB12:
        if (!q1_monster_face(g, e, error) || !rune_shot(g, e, 3, 2, error))
            return false;
        if (--m->source.heavy.nails > 0)
            m->next_frame = q1_frame_index("rknight_magicb12");
        return true;
    default:
        break;
    }
    float offset;
    unsigned variant;
    switch (action) {
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICA8:
        offset = 1.5f;
        variant = 1;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICA9:
        offset = .5f;
        variant = 1;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICA10:
        offset = -.5f;
        variant = 1;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICA11:
        offset = -1.5f;
        variant = 1;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICA12:
        offset = -2.5f;
        variant = 1;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB7:
        offset = -2;
        variant = 2;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB8:
        offset = -1;
        variant = 2;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB9:
        offset = 0;
        variant = 2;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB10:
        offset = 1;
        variant = 2;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICB11:
        offset = 2;
        variant = 2;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICC6:
        offset = -2;
        variant = 0;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICC7:
        offset = -1;
        variant = 0;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICC8:
        offset = 0;
        variant = 0;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICC9:
        offset = 1;
        variant = 0;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICC10:
        offset = 2;
        variant = 0;
        break;
    case Q1_ACTION_MG3_RKNIGHT_RKNIGHT_MAGICC11:
        offset = 3;
        variant = 0;
        break;
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown MG3 heavy continuation");
        return false;
    }
    return (!variant || q1_monster_face(g, e, error)) && rune_shot(g, e, offset, variant, error);
}

bool q1_heavy_pain(qa_q1_game *g, q1_actor *e, qa_actor_id attacker, float damage,
                   qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (m->addon.heavy == Q1_HEAVY_RUNE_KNIGHT) {
        if (m->pain_finished > g->time)
            return true;
        if (g->time - m->pain_finished > 5) {
            if (!q1_monster_play(g, e, "rknight_pain1", error))
                return false;
            m->pain_finished = g->time + 1;
            return true;
        }
        if (q1_random(g) * 30 > damage)
            return true;
        m->pain_finished = g->time + 1;
        return q1_monster_play(g, e, "rknight_pain1", error);
    }
    float health = q1_health(g, e->id);
    if (!q1_sound(g, e->id, "shambler/shurt2.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    qa_q1_target traits;
    if (damage >= health && q1_target(g, attacker, &traits) && traits.player) {
        q1_player *player = q1_player_get(g, attacker);
        const char *achievements[2] = {
            player && player->arsenal && player->weapon == QA_Q1_AXE ? "ACH_CLOSE_SHAVE" : NULL,
            m->source.heavy.lightning_count == 0 ? "ACH_SHAMBLER_DANCE" : NULL};
        for (unsigned i = 0; i < 2; ++i)
            if (achievements[i]) {
                qa_builtin_event event = {.kind = QA_BUILTIN_ACHIEVEMENT,
                                          .family = QA_GAME_Q1,
                                          .provider = g->options.provider,
                                          .actor = attacker,
                                          .time_ns = g->time_ns};
                if (!qa_builtin_resource(&g->services, achievements[i], &event.text, error) ||
                    !qa_builtin_emit(&g->services, &event, error))
                    return false;
            }
    }
    if (health <= 0 || 25 + q1_random(g) * 400 > damage || m->pain_finished > g->time)
        return true;
    m->pain_finished = g->time + 5;
    return remove_child(g, e, error) && q1_monster_play(g, e, "supsham_pain1", error);
}

bool q1_heavy_die(qa_q1_game *g, q1_actor *e, qa_error *error) {
    bool shambler = e->state.monster.addon.heavy == Q1_HEAVY_SUPER_SHAMBLER;
    if (shambler && !remove_child(g, e, error))
        return false;
    float health = q1_health(g, e->id);
    if (health < (shambler ? -60 : -40)) {
        if (!q1_sound(g, e->id, "player/udeath.wav", 2, 1, error))
            return false;
        if (!q1_alive(g, e->id))
            return true;
        if (!q1_gib_head(g, e, shambler ? "h_shams" : "h_hellkn", health, error))
            return false;
        qa_body_state value;
        if (!body(g, e, &value, error))
            return false;
        static const char *const models[] = {"gib1", "gib2", "gib3"};
        for (unsigned i = 0; i < 3; ++i) {
            if (!q1_alive(g, e->id))
                return true;
            if (!q1_gib_at(g, e->id, value.origin, health, models[i], error))
                return false;
        }
        return true;
    }
    if (!q1_sound(g, e->id,
                  shambler             ? "shambler/sdeath.wav"
                  : q1_random(g) < .5f ? "rknight/death_01.wav"
                                       : "rknight/death_02.wav",
                  2, 1, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    return q1_monster_play(g, e,
                           shambler             ? "supsham_death1"
                           : q1_random(g) > .5f ? "rknight_die1"
                                                : "rknight_dieb1",
                           error);
}

bool q1_heavy_spike_touch(qa_q1_game *g, q1_actor *shot, qa_actor_id other, qa_error *error) {
    if (qa_actor_id_equal(shot->owner, other))
        return true;
    qa_physics_properties physics;
    if (g->services.physics && g->services.physics->services.read &&
        g->services.physics->services.read(g->services.physics->services.context, other,
                                           &physics) &&
        physics.solid == QA_PHYSICS_TRIGGER)
        return true;
    qa_body_state value;
    if (!body(g, shot, &value, error))
        return false;
    qa_point_query query = {.point = value.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (contents.contents == -6)
        return q1_remove(g, shot, error);
    if (q1_damageable(g, other)) {
        if (!q1_effect(g, QA_BUILTIN_IMPACT, other, value.origin, 9, 1, error) ||
            !q1_damage(g, other, shot->id, shot->owner, 9, QA_Q1_WEAPON_COUNT, error))
            return false;
    } else if (!q1_effect(g, QA_BUILTIN_IMPACT, shot->id, value.origin, 0, 8, error))
        return false;
    return !q1_alive(g, shot->id) || q1_remove(g, shot, error);
}
