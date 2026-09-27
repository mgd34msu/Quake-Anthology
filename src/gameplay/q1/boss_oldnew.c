#include "boss_internal.h"
#include "qa/game_q1_maps.h"

bool q1_major_boss_fields(qa_q1_game *g, q1_actor *e, const qa_q1_boss_fields *fields,
                          qa_error *error) {
    if (e->kind != Q1_MONSTER || (e->state.monster.addon.boss != Q1_BOSS_OLDNEW &&
                                  e->state.monster.addon.boss != Q1_BOSS_FINAL))
        return true;
    const char *names[] = {fields->wave1, fields->wave2, fields->wave3, fields->teleport_target};
    for (unsigned i = 0; i < 4; ++i)
        if (!qa_builtin_resource(&g->services, names[i] ? names[i] : "",
                                 &e->state.monster.source.boss.waves[i], error))
            return false;
    return true;
}
bool q1_major_boss_spawn(qa_q1_game *g, q1_actor *e, bool *handled, qa_error *error) {
    q1_monster *m = &e->state.monster;
    bool final = !strcmp(m->species->classname, "monster_boss_final");
    if (!final && strcmp(m->species->classname, "monster_oldone_new"))
        return true;
    *handled = true;
    m->addon.boss = final ? Q1_BOSS_FINAL : Q1_BOSS_OLDNEW;
    e->physics.flags = QA_PHYSICS_MONSTER;
    qa_combat_state traits;
    if (!qa_combat_read_traits(g->services.combat, e->id, &traits, error) ||
        !qa_builtin_resource(&g->services, "q1:monsters", &traits.team, error))
        return false;
    if (final) {
        m->source.boss.immune = true;
        ++g->total_monsters;
        return qa_builtin_resource(&g->services, "monster_boss", &e->classname, error) &&
               qa_combat_set_traits(g->services.combat, e->id, &traits, error);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, e->id, &body, error))
        return false;
    body.bounds = e->spawnflags & 128 ? (qa_bounds){{1, 1, -24}, {1, 1, 0}} : m->species->bounds;
    e->physics.solid = QA_PHYSICS_BOX;
    e->physics.motion = QA_PHYSICS_STEP;
    e->physics.yaw_speed = 10;
    e->physics.ideal_yaw = body.angles.y;
    e->max_health = 12000;
    e->aimed_damage = true;
    traits.can_take_damage = true;
    if (!q1_model(g, e, "progs/oldone.mdl", error) ||
        !qa_world_body_write(g->services.world, e->id, &body, error) || !q1_link(g, e, error) ||
        !qa_combat_set_health(g->services.combat, e->id, 12000, error) ||
        !qa_combat_set_traits(g->services.combat, e->id, &traits, error))
        return false;
    ++g->total_monsters;
    return q1_schedule(g, e, .1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_major_boss_effect(qa_q1_game *g, qa_damage_effect_stage stage,
                          const qa_damage_request *request, qa_damage_effect *effect,
                          qa_error *error) {
    (void)error;
    q1_actor *e = q1_entity(g, request->target);
    if (!e || e->kind != Q1_MONSTER || !effect->allowed)
        return true;
    q1_monster *m = &e->state.monster;
    if (stage == QA_DAMAGE_BEFORE_HEALTH) {
        if (m->addon.boss == Q1_BOSS_OLDNEW && m->source.boss.immune)
            effect->allowed = false;
        if (m->addon.boss == Q1_BOSS_FINAL && (e->spawnflags & 2) &&
            (m->source.boss.immune ||
             q1_classnamed(g, request->attack.attacker, "monster_lava_man")))
            effect->allowed = false;
    } else if (stage == QA_DAMAGE_AFTER_ARMOR && m->addon.boss == Q1_BOSS_FINAL &&
               (e->spawnflags & 2)) {
        q1_player *attacker = q1_player_get(g, request->attack.attacker);
        if (!attacker || !attacker->arsenal ||
            (attacker->weapon != QA_Q1_LIGHTNING && attacker->weapon != QA_Q1_MG3_LASER))
            effect->amount *= .8f;
    }
    return true;
}
static bool wave(qa_q1_game *g, q1_actor *e, unsigned index, qa_error *error) {
    qa_string_id target = e->state.monster.source.boss.waves[index];
    return !qa_strings_text(qa_session_strings(g->services.session), target).size ||
           q1_boss_targets(g, e, e->activator, target, error);
}
bool q1_major_boss_pain(qa_q1_game *g, q1_actor *e, qa_actor_id attacker, float damage,
                        qa_error *error) {
    (void)attacker;
    (void)damage;
    q1_monster *m = &e->state.monster;
    if (m->addon.boss == Q1_BOSS_FINAL)
        return q1_final_pain(g, e, error);
    if (m->source.boss.immune)
        return true;
    bool trigger = false;
    if (m->source.boss.phase == 0) {
        trigger = true;
        m->source.boss.phase = 1;
    }
    float health = q1_health(g, e->id);
    if (m->source.boss.phase == 1 && health < e->max_health * .75f) {
        trigger = true;
        m->source.boss.phase = 2;
        if (!q1_boss_child_spawn(g, e, Q1_CHILD_EYE, error) || !wave(g, e, 0, error))
            return false;
    }
    if (!q1_alive(g, e->id))
        return true;
    if (m->source.boss.phase == 2 && q1_health(g, e->id) < e->max_health * .5f) {
        trigger = true;
        m->source.boss.phase = 3;
        if (!wave(g, e, 1, error))
            return false;
    }
    if (!q1_alive(g, e->id))
        return true;
    if (m->source.boss.phase == 3 && q1_health(g, e->id) < e->max_health * .25f) {
        trigger = true;
        m->source.boss.phase = 4;
        if (!wave(g, e, 1, error))
            return false;
    }
    if (!trigger || !q1_alive(g, e->id))
        return true;
    m->source.boss.immune = true;
    if (!q1_sound(g, e->id, "orb/orb_pain.wav", 2, 1, error))
        return false;
    m->pain_finished = g->time + 2.1;
    return !q1_alive(g, e->id) || q1_monster_play(g, e, "oldnew_thrash1", error);
}
bool q1_major_boss_die(qa_q1_game *g, q1_actor *e, qa_actor_id attacker, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (m->counted_death)
        return true;
    m->enemy = attacker;
    m->source.boss.touch = false;
    if (!q1_boss_damageable(g, e, false, error))
        return false;
    if (m->addon.boss == Q1_BOSS_FINAL) {
        e->physics.flags &= ~(QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING);
        return q1_boss_targets(g, e, attacker, e->target, error) &&
               (!q1_alive(g, e->id) || q1_monster_play(g, e, "boss_final_death1", error));
    }
    if (!q1_monster_count_kill(g, e, attacker, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    m->source.boss.immune = true;
    m->source.boss.cycles = 0;
    return q1_monster_play(g, e, "oldnew_death1", error) && q1_boss_cleanup(g, error) &&
           (!q1_alive(g, e->id) ||
            q1_boss_sphere_manager(g, e, g->options.skill > 2 ? (int32_t)m->source.boss.phase : 1,
                                   false, error));
}
static bool attack(qa_q1_game *g, q1_actor *e, qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (m->source.boss.immune)
        return true;
    uint32_t phase = m->source.boss.phase;
    if (m->source.boss.cycles % 3 != 0 || phase == 0) {
        if (!q1_boss_sphere_manager(g, e, phase <= 3 ? 1 : 2, true, error))
            return false;
    } else {
        q1_boss_child_kind kind = Q1_CHILD_SWIPER;
        bool spawn = true;
        if (phase == 1) {
            kind = e->count == 0 ? Q1_CHILD_SPAMMER : Q1_CHILD_SWIPER;
            spawn = e->count == 0 || e->count == 1;
        } else if (phase == 2) {
            kind = e->count == 0   ? Q1_CHILD_VORTEX
                   : e->count == 1 ? Q1_CHILD_SWIPER
                                   : Q1_CHILD_SPAMMER;
            spawn = e->count >= 0 && e->count <= 2;
        } else if (phase == 3) {
            kind = e->count == 0 ? Q1_CHILD_BLASTER : Q1_CHILD_SWIPER;
            spawn = e->count >= 0 && e->count <= 3;
        } else {
            kind = e->count == 0   ? Q1_CHILD_SPAMMER
                   : e->count == 1 ? Q1_CHILD_EYE
                   : e->count == 2 ? Q1_CHILD_SWIPER
                                   : Q1_CHILD_BLASTER;
            spawn = e->count >= 0 && e->count <= 3;
        }
        if (spawn && !q1_boss_child_spawn(g, e, kind, error))
            return false;
        if (++e->count > (phase == 1 ? 1 : phase == 2 ? 2 : 3))
            e->count = 0;
    }
    ++m->source.boss.cycles;
    return true;
}
static bool autogun(qa_q1_game *g, q1_actor *e, float side, float offset, unsigned count,
                    qa_error *error) {
    q1_monster *m = &e->state.monster;
    if (m->source.boss.immune || count > m->source.boss.phase + 2 || g->options.skill == 0 ||
        count > (unsigned)g->options.skill + 1)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, e->id, &body, error))
        return false;
    qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
    qa_vec3 origin =
        qa_vec_add(qa_vec_add(body.origin, qa_v3(0, 0, 80)), qa_vec_scale(g->right, side * 64));
    return q1_sound(g, e->id, "weapons/spike2.wav", 1, 1, error) &&
           (!q1_alive(g, e->id) || q1_boss_autogun(g, e, origin, offset, error));
}
static bool lightstyle(qa_q1_game *g, q1_actor *e, const char *pattern, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_LIGHT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = e->id,
                              .time_ns = g->time_ns};
    return qa_builtin_resource(&g->services, pattern, &event.text, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
static bool finish(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (!q1_sound(g, e->id, "boss2/pop2.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, e->id))
        return true;
    qa_actor_id player;
    if (!q1_boss_first_player(g, &player, error))
        return false;
    if (!g->options.coop && q1_health(g, player) <= 0)
        return q1_monster_play(g, e, "oldnew_idle1", error);
    if (!q1_boss_gib_vectors(g, e, error) || !q1_remove(g, e, error))
        return false;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .code = 3,
                              .count = 3,
                              .time_ns = g->time_ns};
    return qa_builtin_resource(&g->services, "music", &event.resource, error) &&
           qa_builtin_emit(&g->services, &event, error) && lightstyle(g, e, "m", error) &&
           qa_q1_game_map_finish_addon(g, QA_Q1_MAP_END_DOPA, error);
}
bool q1_major_boss_action(qa_q1_game *g, q1_actor *e, q1_frame_action action, qa_error *error) {
    if (e->state.monster.addon.boss == Q1_BOSS_FINAL)
        return q1_final_action(g, e, action, error);
    q1_monster *m = &e->state.monster;
    qa_actor_id spawned;
    switch (action) {
    case Q1_ACTION_OLDNEW_OLDNEW_IDLE1:
        return q1_monster_ai(g, e, Q1_AI_STAND, 0, error);
    case Q1_ACTION_OLDNEW_OLDNEW_WALK1:
        if (!q1_monster_face(g, e, error))
            return false;
        m->source.boss.immune = false;
        return true;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK2:
        return q1_monster_face(g, e, error);
    case Q1_ACTION_OLDNEW_OLDNEW_WALK14:
        return q1_monster_face(g, e, error) && attack(g, e, error);
    case Q1_ACTION_OLDNEW_OLDNEW_WALK45:
        return q1_monster_face(g, e, error) && q1_spawn_shub_zombie(g, &spawned, error);
    case Q1_ACTION_OLDNEW_OLDNEW_THRASH4:
        return q1_boss_pain_lightning(g, e, qa_v3(0, 0, 100), error);
    case Q1_ACTION_OLDNEW_OLDNEW_THRASH14:
        m->source.boss.immune = true;
        return q1_boss_sphere_manager(
            g, e, g->options.skill > 2 ? (int32_t)m->source.boss.phase : 1, false, error);
    case Q1_ACTION_OLDNEW_OLDNEW_THRASH15:
        m->source.boss.immune = false;
        return true;
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH1:
        return q1_sound(g, e->id, "boss2/death.wav", 2, 1, error);
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH15:
        if (++m->source.boss.cycles != 3)
            m->next_frame = q1_frame_index("oldnew_death1");
        return true;
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH16:
        return lightstyle(g, e, "g", error);
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH17:
        return lightstyle(g, e, "c", error);
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH18:
        return lightstyle(g, e, "b", error);
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH19:
        return lightstyle(g, e, "a", error);
    case Q1_ACTION_OLDNEW_OLDNEW_DEATH20:
        return finish(g, e, error);
    default:
        break;
    }
    float side = 1, offset = 0;
    unsigned count = 0;
    switch (action) {
    case Q1_ACTION_OLDNEW_OLDNEW_WALK21:
        offset = .05f;
        count = 1;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK22:
        count = 2;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK23:
        offset = -.05f;
        count = 3;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK24:
        offset = -.1f;
        count = 4;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK25:
        offset = -.15f;
        count = 5;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK36:
        side = -1;
        offset = -.05f;
        count = 1;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK37:
        side = -1;
        count = 2;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK38:
        side = -1;
        offset = .05f;
        count = 3;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK39:
        side = -1;
        offset = .1f;
        count = 4;
        break;
    case Q1_ACTION_OLDNEW_OLDNEW_WALK40:
        side = -1;
        offset = .15f;
        count = 5;
        break;
    default:
        qa_error_set(error, QA_ERROR_FORMAT, action, "unknown Old One continuation");
        return false;
    }
    return q1_monster_face(g, e, error) &&
           (action != Q1_ACTION_OLDNEW_OLDNEW_WALK23 || q1_spawn_shub_zombie(g, &spawned, error)) &&
           autogun(g, e, side, offset, count, error);
}
