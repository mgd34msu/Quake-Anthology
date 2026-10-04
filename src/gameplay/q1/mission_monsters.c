#include "internal.h"
#include <stdio.h>

bool q1_monster_drop_floor(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_trace_query query = {.start = body.origin,
                            .end = qa_vec_add(body.origin, qa_v3(0, 0, -256)),
                            .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                            .pass_actor = entity->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    query.policy.q1_move = QA_Q1_MOVE_NO_MONSTERS;
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, error))
        return false;
    if (trace.fraction == 1 || trace.all_solid)
        return true;
    entity->physics.flags |= QA_PHYSICS_ONGROUND;
    body.origin = trace.end;
    body.ground = trace.actor;
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error);
}
static bool eel_pitch(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    if (contents.contents != -3)
        return q1_monster_drop_floor(g, entity, error) &&
               q1_damage(g, entity->id, entity->id, (qa_actor_id){0}, 6, QA_Q1_WEAPON_COUNT, error);
    if (g->time < entity->delay)
        return true;
    int pitch = entity->state.monster.source.eel.pitch;
    if (pitch > 10)
        pitch = -10;
    if (pitch != 0) {
        body.angles.x += pitch < 0 ? -1.5f : 1.5f;
        if (!qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
    }
    entity->state.monster.source.eel.pitch = (int16_t)(pitch + 1);
    return true;
}
static bool eel_zap(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_body_state self;
    if (!qa_world_body_read(g->services.world, entity->id, &self, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_radius_snapshot(g, self.origin, 85, &snapshot, error))
        return false;
    bool result = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id target = snapshot->snapshot.ids[i - 1];
        qa_q1_target traits;
        if (q1_classnamed(g, target, "monster_eel") || !q1_target(g, target, &traits) ||
            !traits.player || !q1_damageable(g, target))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, target, &body, NULL))
            continue;
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), 0.5f));
        float distance = qa_vec_length(qa_vec_sub(self.origin, center));
        if (distance > 85)
            continue;
        float points = 45 - fmaxf(0, 0.5f * distance);
        if (qa_actor_id_equal(target, entity->id))
            points *= 0.5f;
        bool visible = false;
        if (points > 0 && !q1_can_damage(g, target, entity->id, &visible, error)) {
            result = false;
            break;
        }
        if (visible &&
            !q1_damage(g, target, entity->id, entity->id, points, QA_Q1_WEAPON_COUNT, error)) {
            result = false;
            break;
        }
        if (!q1_alive(g, entity->id))
            break;
    }
    qa_builtin_snapshot_release(snapshot);
    return result;
}
static bool mummy_wake(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    entity->state.monster.source.mummy.asleep = false;
    return q1_monster_play(g, entity, "mummy_paine12", error);
}
static bool mummy_fire(qa_q1_game *g, q1_actor *entity, qa_vec3 offset, qa_error *error) {
    if (!q1_monster_face(g, entity, error))
        return false;
    qa_body_state self, target;
    if (!qa_world_body_read(g->services.world, entity->state.monster.enemy, &target, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &self, error))
        return false;
    if (!q1_sound(g, entity->id, "zombie/z_shot1.wav", 1, 1, error))
        return false;
    qa_vec3 origin =
        qa_vec_add(qa_vec_add(qa_vec_add(self.origin, qa_vec_scale(g->forward, offset.x)),
                              qa_vec_scale(g->right, offset.y)),
                   qa_vec_scale(g->up, offset.z - 24));
    qa_builtin_angle_vectors(self.angles, &g->forward, &g->right, &g->up);
    qa_vec3 velocity = qa_vec_scale(qa_vec_normalize(qa_vec_sub(target.origin, origin)), 600);
    velocity.z = 200;
    q1_actor *grenade;
    if (!q1_projectile_spawn(g, entity->id, QA_Q1_WEAPON_COUNT, Q1_ZOMBIE_GRENADE, origin, velocity,
                             &grenade, error))
        return false;
    return qa_builtin_resource(&g->services, "mummy_grenade", &grenade->classname, error);
}
bool q1_mission_monster_action(qa_q1_game *g, q1_actor *entity, q1_frame_action action,
                               qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->species->species == QA_Q1_GREMLIN)
        return q1_gremlin_action(g, entity, action, error);
    if (m->species->species == QA_Q1_ARMAGON)
        return q1_armagon_action(g, entity, action, error);
    if (m->species->species == QA_Q1_DRAGON)
        return q1_dragon_action(g, entity, action, error);
    if (m->species->species == QA_Q1_SCOURGE)
        return q1_scourge_action(g, entity, action, error);
    if (m->species->species == QA_Q1_WRATH)
        return q1_wrath_action(g, entity, action, error);
    if (m->species->species == QA_Q1_LAVA_MAN)
        return q1_lavaman_action(g, entity, action, error);
    if (m->species->species == QA_Q1_SUPER_WRATH)
        return q1_overlord_action(g, entity, action, error);
    if (m->species->species == QA_Q1_MORPH)
        return q1_morph_action(g, entity, action, error);
    switch (action) {
    case Q1_ACTION_HIPDECOY_DECOY_STAND1:
        if (!qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed, error))
            return false;
        if (m->counter >= 5)
            m->counter = 0;
        entity->frame = 12 + (int32_t)m->counter++;
        return g->time <= m->pause_until || q1_monster_play(g, entity, "decoy_walk1", error);
    case Q1_ACTION_HIPDECOY_DECOY_WALK1: {
        qa_actor_id goal = q1_monster_route(g, entity);
        if (goal.registry &&
            !qa_physics_q1_move_to_goal(g->services.physics, entity->id, goal, 12, false, error))
            return false;
        if (m->counter == 6)
            m->counter = 0;
        if (m->counter == 2 || m->counter == 5) {
            float r = q1_random(g);
            unsigned step = r < 0.14f   ? 1
                            : r < 0.29f ? 2
                            : r < 0.43f ? 3
                            : r < 0.58f ? 4
                            : r < 0.72f ? 5
                            : r < 0.86f ? 6
                                        : 7;
            char path[32];
            snprintf(path, sizeof(path), "misc/foot%u.wav", step);
            qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = entity->id,
                                      .time_ns = g->time_ns,
                                      .channel = 2,
                                      .volume = 0.5f,
                                      .attenuation = 1};
            if (!qa_builtin_resource(&g->services, path, &sound.resource, error) ||
                !qa_builtin_emit(&g->services, &sound, error))
                return false;
        }
        entity->frame += (int32_t)m->counter++;
        return true;
    }
    case Q1_ACTION_DROPTOFLOOR:
        return q1_monster_drop_floor(g, entity, error);
    case Q1_ACTION_EEL_PITCH_CHANGE:
        return eel_pitch(g, entity, error);
    case Q1_ACTION_EEL_EEL_ATTACK8:
    case Q1_ACTION_EEL_EEL_ATTACK9:
    case Q1_ACTION_EEL_EEL_ATTACK10:
    case Q1_ACTION_EEL_EEL_ATTACK11:
        entity->skin = action == Q1_ACTION_EEL_EEL_ATTACK8    ? 1
                       : action == Q1_ACTION_EEL_EEL_ATTACK9  ? 2
                       : action == Q1_ACTION_EEL_EEL_ATTACK10 ? 3
                                                              : 4;
        if (action == Q1_ACTION_EEL_EEL_ATTACK8)
            entity->effects = 8;
        if (action == Q1_ACTION_EEL_EEL_ATTACK11)
            entity->effects = 4;
        if (!q1_monster_ai(g, entity, Q1_AI_CHARGE, 8, error) || !eel_pitch(g, entity, error))
            return false;
        return action != Q1_ACTION_EEL_EEL_ATTACK8 ||
               q1_sound(g, entity->id, "eel/eatt1.wav", 1, 1, error);
    case Q1_ACTION_EEL_EEL_ATTACK12: {
        entity->skin = 5;
        qa_body_state self, target;
        if (qa_world_body_read(g->services.world, m->enemy, &target, NULL)) {
            qa_trace_result trace;
            if (!qa_world_body_read(g->services.world, entity->id, &self, error) ||
                !q1_trace(g, self.origin, target.origin, entity->id, true, &trace, error))
                return false;
            if (trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, m->enemy) &&
                !(trace.in_open && trace.in_water) && !eel_zap(g, entity, error))
                return false;
        }
        entity->skin = 0;
        entity->effects = 0;
        return true;
    }
    case Q1_ACTION_EEL_EEL_DEATH1:
        entity->skin = 0;
        entity->effects = 0;
        return q1_sound(g, entity->id, "eel/edie3r.wav", 2, 1, error);
    case Q1_ACTION_EEL_EEL_DEATH11:
        entity->physics.flags -= QA_PHYSICS_SWIMMING;
        return true;
    case Q1_ACTION_EEL_EEL_PAIN1:
        if (m->pain_finished > g->time)
            return true;
        m->pain_finished = g->time + 1;
        entity->skin = 0;
        return q1_sound(g, entity->id, "eel/epain3.wav", 2, 1, error);
    case Q1_ACTION_SWORD_PAUSE: {
        double delay = entity->delay;
        entity->delay = 0;
        m->next_frame = q1_frame_index("sword_run1");
        m->source.sword.awakened = true;
        return q1_schedule(g, entity, delay, Q1_THINK_MONSTER_FRAME, error);
    }
    case Q1_ACTION_INVIS_SW_SWORD_RUN1:
        entity->effects = 8;
        return q1_monster_ai(g, entity, Q1_AI_RUN, 14, error);
    case Q1_ACTION_INVIS_SW_SWORD_ATK1:
        return q1_sound(g, entity->id, "knight/sword1.wav", 0, 1, error) &&
               q1_monster_ai(g, entity, Q1_AI_CHARGE, 14, error);
    case Q1_ACTION_INVIS_SW_SWORD_DIE7: {
        qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                                  .family = QA_GAME_Q1,
                                  .provider = g->options.provider,
                                  .actor = entity->id,
                                  .time_ns = g->time_ns,
                                  .channel = 1,
                                  .volume = 0.5f,
                                  .attenuation = 1};
        return qa_builtin_resource(&g->services, "player/axhit2.wav", &event.resource, error) &&
               qa_builtin_emit(&g->services, &event, error);
    }
    case Q1_ACTION_MUMMY_MUMMY_RUN1:
        if (!q1_monster_ai(g, entity, Q1_AI_RUN, 2, error))
            return false;
        m->in_pain = 0;
        return true;
    case Q1_ACTION_MUMMY_MUMMY_ATTA13:
        return mummy_fire(g, entity, qa_v3(-10, -22, 30), error);
    case Q1_ACTION_MUMMY_MUMMY_ATTB14:
        return mummy_fire(g, entity, qa_v3(-10, -24, 29), error);
    case Q1_ACTION_MUMMY_MUMMY_ATTC12:
        return mummy_fire(g, entity, qa_v3(-12, -19, 29), error);
    case Q1_ACTION_MUMMY_MUMMY_PAINE11:
        return q1_schedule(g, entity, entity->next_think - g->time + 5, Q1_THINK_MONSTER_FRAME,
                           error);
    case Q1_ACTION_MUMMY_MUMMY_PAINE12: {
        if (!q1_sound(g, entity->id, "zombie/z_idle.wav", 2, 2, error))
            return false;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        body.bounds = m->species->bounds;
        entity->physics.solid = QA_PHYSICS_BOX;
        if (!qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
        bool moved;
        if (!qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true,
                                  true, &moved, error))
            return false;
        if (!moved) {
            m->next_frame = q1_frame_index("mummy_paine11");
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        }
        return q1_link(g, entity, error);
    }
    case Q1_ACTION_MUMMY_WAKE:
        return mummy_wake(g, entity, error);
    case Q1_ACTION_MUMMY_MISSILE: {
        if (m->source.mummy.asleep)
            return mummy_wake(g, entity, error);
        float r = q1_random(g);
        return q1_monster_play(g, entity,
                               r < 0.3f   ? "mummy_atta1"
                               : r < 0.6f ? "mummy_attb1"
                                          : "mummy_attc1",
                               error);
    }
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, action,
                     "Q1 authored frame action is not implemented");
        return false;
    }
}
bool q1_mission_monster_pain(qa_q1_game *g, q1_actor *entity, float damage, qa_error *error) {
    (void)damage;
    q1_monster *m = &entity->state.monster;
    switch (m->species->species) {
    case QA_Q1_ARMAGON:
        if (q1_health(g, entity->id) <= 0 || damage < 25 || m->pain_finished > g->time)
            return true;
        m->pain_finished = g->time + 2;
        return q1_sound(g, entity->id, "armagon/pain.wav", 2, 1, error);
    case QA_Q1_DRAGON:
        return q1_dragon_pain(g, entity, error);
    case QA_Q1_MORPH:
        return q1_morph_pain(g, entity, error);
    case QA_Q1_SUPER_WRATH: {
        if (m->pain_finished > g->time)
            return true;
        float random = q1_random(g);
        if (random > 0.2f)
            return true;
        m->pain_finished = g->time + 2;
        return q1_monster_play(g, entity, random < 0.15f ? "overlord_pn_a01" : "overlord_pn_b01",
                               error) &&
               q1_sound(g, entity->id, "wrath/wpain.wav", 2, 1, error);
    }
    case QA_Q1_LAVA_MAN:
        if (g->options.program == QA_Q1_MG3 && entity->count == 0) {
            ++entity->count;
            m->pain_finished = g->time + 2;
            return q1_monster_play(g, entity, "lavaman_shocka1", error);
        }
        if (m->pain_finished > g->time || q1_random(g) >= 0.05f)
            return true;
        m->pain_finished = g->time + 2;
        return q1_monster_play(g, entity, "lavaman_shocka1", error);
    case QA_Q1_DECOY:
        return q1_monster_play(g, entity, "decoy_stand1", error);
    case QA_Q1_WRATH: {
        if (m->pain_finished > g->time)
            return true;
        float random = q1_random(g);
        if (random > 0.1f) {
            m->pain_finished = g->time + 0.5;
            return true;
        }
        m->pain_finished = g->time + 3;
        return q1_monster_play(g, entity, random < 0.07f ? "wrath_pn_a01" : "wrath_pn_b01",
                               error) &&
               q1_sound(g, entity->id, "wrath/wpain.wav", 2, 1, error);
    }
    case QA_Q1_SCOURGE:
        if (q1_random(g) * 50 > damage || m->pain_finished > g->time)
            return true;
        (void)q1_random(g);
        m->pain_finished = g->time + 2;
        return q1_sound(g, entity->id, "scourge/pain.wav", 2, 1, error) &&
               q1_monster_play(g, entity, "scourge_pain1", error);
    case QA_Q1_EEL:
        return q1_monster_play(g, entity, "eel_pain1", error);
    case QA_Q1_SWORD:
        if (m->source.sword.pain_disabled)
            return true;
        m->source.sword.pain_disabled = true;
        m->source.sword.awakened = true;
        entity->delay = 0;
        m->next_frame = q1_frame_index("sword_run1");
        return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    case QA_Q1_MUMMY: {
        if (m->source.mummy.asleep)
            return mummy_wake(g, entity, error);
        if (m->pain_finished > g->time)
            return true;
        float r = q1_random(g);
        if (r > 0.24f)
            return true;
        m->pain_finished = g->time + 2.5;
        return q1_monster_play(g, entity,
                               r < 0.06f   ? "mummy_paina1"
                               : r < 0.12f ? "mummy_painb1"
                               : r < 0.18f ? "mummy_painc1"
                                           : "mummy_paind1",
                               error);
    }
    default:
        qa_error_set(error, QA_ERROR_UNSUPPORTED, m->species->species,
                     "Q1 mission monster pain is not implemented");
        return false;
    }
}
bool q1_mission_monster_die(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->species->species == QA_Q1_ARMAGON)
        return q1_monster_play(g, entity, "armagon_die1", error);
    if (m->species->species == QA_Q1_DRAGON)
        return q1_monster_play(g, entity, "dragon_death1", error);
    if (m->species->species == QA_Q1_LAVA_MAN)
        return q1_monster_play(g, entity, "lavaman_death1", error);
    if (m->species->species == QA_Q1_SUPER_WRATH)
        return q1_monster_play(g, entity, "overlord_die02", error);
    if (m->species->species == QA_Q1_MORPH) {
        if (!q1_sound(g, entity->id, "guard/death.wav", 2, 1, error))
            return false;
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        m->next_frame = q1_frame_index("morph_die1");
        return q1_link(g, entity, error) &&
               q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    }
    if (m->species->species == QA_Q1_DECOY)
        return q1_monster_play(g, entity, "decoy_stand1", error);
    if (m->species->species == QA_Q1_WRATH)
        return q1_monster_play(g, entity, "wrath_die02", error);
    if (m->species->species == QA_Q1_SCOURGE) {
        q1_actor *trigger = q1_entity(g, m->source.scourge.trigger);
        if (trigger && !q1_remove(g, trigger, error))
            return false;
        m->source.scourge.silent = false;
        if (!q1_scourge_action(g, entity, Q1_ACTION_SCOURGE_THINK, error))
            return false;
        if (q1_health(g, entity->id) >= -35)
            return q1_sound(g, entity->id, "scourge/pain2.wav", 2, 1, error) &&
                   q1_monster_play(g, entity, "scourge_die1", error);
        if (!q1_sound(g, entity->id, "player/udeath.wav", 2, 1, error) ||
            !q1_gib(g, entity, "h_scourg", true, error))
            return false;
        return q1_gib(g, entity, "gib1", false, error) && q1_gib(g, entity, "gib2", false, error) &&
               q1_gib(g, entity, "gib3", false, error);
    }
    if (m->species->species == QA_Q1_SWORD) {
        entity->effects = 0;
        return q1_monster_play(g, entity, q1_random(g) < 0.5f ? "sword_die1" : "sword_dieb1",
                               error);
    }
    if (m->species->species == QA_Q1_EEL) {
        entity->physics.flags += QA_PHYSICS_SWIMMING;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        body.bounds = (qa_bounds){0};
        if (!qa_world_body_write(g->services.world, entity->id, &body, error))
            return false;
        if (q1_health(g, entity->id) >= -12)
            return q1_monster_play(g, entity, "eel_death1", error);
        entity->skin = 0;
        entity->effects = 0;
        if (!q1_gib(g, entity, "eelgib", true, error))
            return false;
        for (unsigned i = 0; i < 3; ++i)
            if (!q1_gib(g, entity, "gib1", false, error))
                return false;
        return true;
    }
    if (m->species->species == QA_Q1_MUMMY) {
        if (!qa_combat_set_health(g->services.combat, entity->id, -35, error) ||
            !q1_sound(g, entity->id, "zombie/z_gib.wav", 2, 1, error) ||
            !q1_gib(g, entity, "h_zombie", true, error))
            return false;
        return q1_gib(g, entity, "gib1", false, error) && q1_gib(g, entity, "gib2", false, error) &&
               q1_gib(g, entity, "gib3", false, error);
    }
    qa_error_set(error, QA_ERROR_UNSUPPORTED, m->species->species,
                 "Q1 mission monster death is not implemented");
    return false;
}
