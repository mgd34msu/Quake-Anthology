#include "internal.h"
#include "maps/internal.h"

bool qa_q1_monster_shape(const char *classname, qa_bounds *bounds, uint32_t *flags) {
    const q1_species *species = q1_species_find(classname);
    if (!species || !bounds || !flags) return false;
    *bounds = species->bounds; *flags = species->flags;
    return true;
}
bool q1_monster_mission(const qa_q1_game *g, qa_actor_id actor, qa_monster_mission *out) {
    return g->host.missions.lookup && g->host.missions.lookup(g->host.missions.context, actor, out);
}
static bool body(qa_q1_game *g, q1_actor *entity, qa_body_state *out, qa_error *error) {
    return qa_world_body_read(g->services.world, entity->id, out, error);
}
qa_actor_id q1_find_target(const qa_q1_game *g, qa_string_id name) {
    if (!name || !qa_strings_text(qa_session_strings(g->services.session), name).size)
        return (qa_actor_id){0};
    if (g->host.find_target) {
        qa_actor_id target = {0};
        if (g->host.find_target(g->host.context, name, &target))
            return target;
    }
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *candidate = g->actors[i];
        if (candidate && candidate->active && candidate->targetname == name)
            return candidate->id;
    }
    return (qa_actor_id){0};
}
qa_actor_id q1_monster_route(const qa_q1_game *g, const q1_actor *entity) {
    qa_monster_mission mission;
    if (q1_monster_mission(g, entity->id, &mission)) {
        qa_actor_id goal = {0};
        if (mission.route(mission.context, entity->id, &goal, NULL)) return goal;
    }
    return q1_find_target(g, entity->state.monster.path);
}
static bool target_body(qa_q1_game *g, q1_actor *entity, qa_body_state *out) {
    return qa_world_body_read(g->services.world, q1_ref_actor(g, entity->state.monster.enemy), out, NULL);
}
static float range(qa_q1_game *g, q1_actor *entity, bool eyes) {
    qa_body_state self, other;
    if (!body(g, entity, &self, NULL) || !target_body(g, entity, &other))
        return INFINITY;
    qa_vec3 delta = qa_vec_sub(other.origin, self.origin);
    if (eyes) {
        qa_q1_target target;
        delta.z += q1_target(g, q1_ref_actor(g, entity->state.monster.enemy), &target) ? target.view_height : 25;
        delta.z -= entity->state.monster.species->species == QA_Q1_FISH ? 10 : 25;
    }
    return qa_vec_length(delta);
}
static bool eligible(qa_q1_game *g, qa_actor_id actor) {
    qa_q1_target target;
    return q1_alive(g, actor) && q1_health(g, actor) > 0 && q1_target(g, actor, &target) &&
           !target.notarget;
}
bool q1_monster_face(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id source = entity->id;
    qa_body_state self, target;
    if (!target_body(g, entity, &target))
        return true;
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    if (!body(g, entity, &self, error))
        return !q1_entity(g, source);
    entity = q1_entity(g, source);
    if (!entity)
        return true;
    qa_vec3 delta = qa_vec_sub(target.origin, self.origin);
    double yaw = atan2((double)delta.y, delta.x) * 180 /
                 3.14159265358979323846264338327950288;
    entity->physics.ideal_yaw = (float)(yaw < 0 ? yaw + 360 : yaw);
    return qa_physics_change_yaw(g->services.physics, source, (float)g->elapsed, error);
}
bool q1_monster_visible(qa_q1_game *g, q1_actor *entity, qa_actor_id target, bool *out,
                        qa_error *error) {
    qa_body_state self, other;
    if (!qa_world_body_read(g->services.world, target, &other, NULL)) {
        *out = false;
        return true;
    }
    if (!body(g, entity, &self, error))
        return false;
    qa_q1_target observation;
    float height = q1_target(g, target, &observation) ? observation.view_height : 25;
    qa_vec3 start = qa_vec_add(
        self.origin, qa_v3(0, 0, entity->state.monster.species->species == QA_Q1_FISH ? 10 : 25));
    qa_trace_result trace;
    if (!q1_trace(g, start, qa_vec_add(other.origin, qa_v3(0, 0, height)), entity->id, false,
                  &trace, error))
        return false;
    *out = trace.fraction == 1 && !(trace.in_open && trace.in_water);
    return true;
}
bool q1_monster_found(qa_q1_game *g, q1_actor *entity, qa_actor_id target, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    q1_actor *native_target = q1_entity(g, target);
    if (q1_ref_present(monster->charmer) &&
        (q1_ref_equal(q1_ref_from(g, target), monster->charmer) ||
         (native_target && native_target->kind == Q1_MONSTER &&
          q1_ref_equal(native_target->state.monster.charmer, monster->charmer)))) {
        monster->enemy = (q1_ref){0};
        return true;
    }
    monster->enemy = q1_ref_from(g, target);
    bool mg3 = monster->addon.enabled && g->options.program == QA_Q1_MG3;
    qa_q1_target observation;
    if (q1_target(g, target, &observation) && observation.player) {
        g->sight_actor = q1_ref_from(g, entity->id);
        g->sight_time = g->time;
    }
    if (mg3 && monster->species->species == QA_Q1_HELLKNIGHT &&
        (entity->spawnflags & (65536u | 8388608u)) && monster->pain_finished > g->time)
        return true;
    monster->search_until = g->time + 5;
    monster->refired = false;
    entity->physics.enemy = q1_ref_from(g, target);
    entity->physics.goal = q1_ref_from(g, target);
    if (g->options.program == QA_Q1_HIPNOTIC || mg3 || q1_ref_present(monster->charmer))
        monster->hostile_until = g->time + 1;
    if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3 || mg3)
        monster->attack_finished = g->time + 1;
    if (mg3) {
        qa_body_state self, other;
        if (!body(g, entity, &self, error) ||
            !qa_world_body_read(g->services.world, target, &other, error))
            return false;
        qa_vec3 delta = qa_vec_sub(other.origin, self.origin);
        entity->physics.ideal_yaw =
            qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
    }
    qa_monster_mission mission;
    if (q1_monster_mission(g, entity->id, &mission)) {
        qa_monster_combat_route route;
        if (!mission.found_target(mission.context, entity->id, error) ||
            !mission.combat_route(mission.context, entity->id, &route, error)) return false;
        if (route.goal.registry) monster->move_target = entity->physics.goal = q1_ref_from(g, route.goal);
        if (route.stand_ground) monster->pause_until = 99999999;
    }
    if (g->host.monster_found && !g->host.monster_found(g->host.context, entity->id, target, error))
        return false;
    const char *sound = monster->species->sight;
    if (monster->addon.heavy == Q1_HEAVY_RUNE_KNIGHT)
        sound = q1_random(g) < 0.5f ? "rknight/sight_01.wav" : "rknight/sight_03.wav";
    if (mg3 && monster->species->species == QA_Q1_HELLKNIGHT &&
        entity->physics.solid == QA_PHYSICS_NOT_SOLID && (entity->spawnflags & (65536u | 8388608u)))
        sound = "";
    if (monster->species->species == QA_Q1_ENFORCER &&
        (g->options.edition != QA_Q1_RERELEASE || g->options.program == QA_Q1_HIPNOTIC || mg3)) {
        int choice = (int)floorf(q1_random(g) * 3 + 0.5f);
        sound = choice == 1   ? "enforcer/sight1.wav"
                : choice == 2 ? "enforcer/sight2.wav"
                : choice == 0 ? "enforcer/sight3.wav"
                              : "enforcer/sight4.wav";
    }
    if (g->options.program == QA_Q1_HIPNOTIC &&
        (monster->species->species == QA_Q1_FISH ||
         (monster->species->species == QA_Q1_GREMLIN && monster->source.gremlin.stolen)))
        sound = "";
    if (*sound && !q1_sound(g, entity->id, sound, 2,
                            monster->species->species == QA_Q1_ARMAGON ? 0.1f : 1, error))
        return false;
    monster->next_frame = q1_frame_index(monster->species->run);
    if (monster->species->species == QA_Q1_SWORD && !monster->source.sword.awakened)
        monster->next_frame = q1_frame_index("sword_pause");
    if (monster->species->species == QA_Q1_MUMMY && monster->source.mummy.asleep)
        monster->next_frame = q1_frame_index("mummy_wake");
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_monster_find_target(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    *out = false;
    if (q1_ref_present(entity->state.monster.charmer))
        return q1_charmed_find_target(g, entity, out, error);
    bool mg3 = entity->state.monster.addon.enabled && g->options.program == QA_Q1_MG3;
    bool hipnotic = g->options.program == QA_Q1_HIPNOTIC || mg3;
    if (mg3) {
        qa_actor_id authored;
        if (!q1_addon_target(g, entity, &authored, error))
            return false;
        if (authored.registry) {
            *out = true;
            return q1_monster_found(g, entity, authored, error);
        }
    }
    qa_actor_id candidate = {0};
    q1_actor *sight = q1_entity(g, q1_ref_actor(g, g->sight_actor));
    if (sight && sight->kind == Q1_MONSTER && g->sight_time >= g->time - 0.1 &&
        !(entity->spawnflags & 3)) {
        if (hipnotic && q1_ref_equal(sight->state.monster.enemy, entity->state.monster.enemy))
            return true;
        candidate = q1_ref_actor(g, hipnotic ? q1_ref_from(g, sight->id) : sight->state.monster.enemy);
    } else if (g->host.check_client)
        (void)g->host.check_client(g->host.context, entity->id, &candidate);
    else {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Q1 monsters require shared source check-client admission");
        return false;
    }
    qa_q1_target observation;
    if (!candidate.registry || (!hipnotic && q1_health(g, candidate) <= 0) ||
        (hipnotic && q1_ref_equal(q1_ref_from(g, candidate), entity->state.monster.enemy)) ||
        !q1_target(g, candidate, &observation) || observation.notarget || observation.invisible)
        return true;
    qa_body_state self, other;
    if (!body(g, entity, &self, error) ||
        !qa_world_body_read(g->services.world, candidate, &other, error))
        return false;
    qa_vec3 delta = qa_vec_sub(other.origin, self.origin), eye_delta = delta;
    eye_delta.z +=
        observation.view_height - (entity->state.monster.species->species == QA_Q1_FISH ? 10 : 25);
    float distance = qa_vec_length(eye_delta);
    unsigned category = mg3               ? q1_mg3_range(entity, distance)
                        : distance < 120  ? 0
                        : distance < 500  ? 1
                        : distance < 1000 ? 2
                                          : 3;
    if (category == 3)
        return true;
    bool visible;
    if (!q1_monster_visible(g, entity, candidate, &visible, error))
        return false;
    if (!visible)
        return true;
    qa_vec3 angles = self.angles;
    if (g->options.edition == QA_Q1_RERELEASE && !mg3)
        angles.x = -angles.x;
    qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
    bool front = qa_vec_dot(qa_vec_normalize(delta), g->forward) >
                 (mg3 && (entity->spawnflags & 8192) ? 0.866f : 0.3f);
    if ((category == 2 && !front) ||
        (category == 1 && observation.hostile_until < g->time && !front))
        return true;
    if (hipnotic && !observation.player) {
        q1_actor *native = q1_entity(g, candidate);
        if (!native || native->kind != Q1_MONSTER)
            return true;
        if (!q1_ref_present(native->state.monster.charmer)) {
            candidate = q1_ref_actor(g, native->state.monster.enemy);
            if (!q1_target(g, candidate, &observation) || !observation.player)
                return true;
        }
    }
    if (!q1_monster_found(g, entity, candidate, error))
        return false;
    *out = true;
    return true;
}
static bool melee_attack(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (entity->state.monster.addon.heavy != Q1_HEAVY_NONE)
        return q1_heavy_melee(g, entity, error);
    const char *animation = NULL;
    switch (entity->state.monster.species->species) {
    case QA_Q1_DOG:
        animation = entity->state.monster.addon.demodog ? "demodog_atta1" : "dog_atta1";
        break;
    case QA_Q1_KNIGHT:
        animation = range(g, entity, true) < 80 ? "knight_atk1" : "knight_runatk1";
        break;
    case QA_Q1_DEMON:
        animation = "demon1_atta1";
        break;
    case QA_Q1_OGRE:
        animation = q1_random(g) > 0.5f ? "ogre_smash1" : "ogre_swing1";
        break;
    case QA_Q1_HELLKNIGHT:
        if (!q1_sound(g, entity->id, "hknight/slash1.wav", 1, 1, error))
            return false;
        g->hellknight_melee = g->hellknight_melee % 3 + 1;
        animation = g->hellknight_melee == 1   ? "hknight_slice1"
                    : g->hellknight_melee == 2 ? "hknight_smash1"
                                               : "hknight_watk1";
        break;
    case QA_Q1_SHAMBLER: {
        float r = q1_random(g);
        animation = r > 0.6f || q1_health(g, entity->id) == 600 ? "sham_smash1"
                    : r > 0.3f                                  ? "sham_swingr1"
                                                                : "sham_swingl1";
        break;
    }
    case QA_Q1_TARBABY:
        animation = "tbaby_jump1";
        break;
    case QA_Q1_FISH:
        animation = "f_attack1";
        break;
    case QA_Q1_ZOMBIE: {
        float r = q1_random(g);
        animation = r < 0.3f ? "zombie_atta1" : r < 0.6f ? "zombie_attb1" : "zombie_attc1";
        break;
    }
    case QA_Q1_EEL:
        animation = "eel_attack1";
        break;
    case QA_Q1_SWORD:
        animation = "sword_atk1";
        break;
    case QA_Q1_LAVA_MAN:
        animation = "lavaman_fire1";
        break;
    case QA_Q1_GREMLIN:
        return q1_gremlin_melee(g, entity, error);
    case QA_Q1_ARMAGON:
        animation = "armagon_stop1";
        break;
    case QA_Q1_SUPER_WRATH:
        return q1_overlord_melee(g, entity, error);
    case QA_Q1_MORPH:
        return q1_morph_melee(g, entity, error);
    case QA_Q1_SCOURGE:
        if (!q1_monster_play(g, entity, "scourge_melee1", error))
            return false;
        {
            float delay = 2 * q1_random(g);
            if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
                entity->state.monster.attack_finished = g->time + delay;
        }
        entity->state.monster.refired = false;
        return true;
    default:
        return true;
    }
    return q1_monster_play(g, entity, animation, error);
}
static bool clear_shot(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    qa_body_state self, target;
    if (!target_body(g, entity, &target)) {
        *out = false;
        return true;
    }
    if (!body(g, entity, &self, error))
        return false;
    qa_q1_target observation;
    float height =
        q1_target(g, q1_ref_actor(g, entity->state.monster.enemy), &observation) ? observation.view_height : 25;
    qa_trace_result trace;
    if (!q1_trace(g, qa_vec_add(self.origin, qa_v3(0, 0, 25)),
                  qa_vec_add(target.origin, qa_v3(0, 0, height)), entity->id, true, &trace, error))
        return false;
    *out = trace.hit == QA_TRACE_HIT_ACTOR &&
           q1_ref_equal(q1_ref_from(g, trace.actor), entity->state.monster.enemy) &&
           (entity->state.monster.species->species == QA_Q1_WIZARD ||
            !(trace.in_open && trace.in_water));
    return true;
}
static bool try_attack(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->addon.boss == Q1_BOSS_ORB)
        return q1_orb_check_attack(g, entity, out, error);
    if (m->addon.heavy != Q1_HEAVY_NONE)
        return q1_heavy_check_attack(g, entity, out, error);
    const q1_species *spec = m->species;
    *out = false;
    if (spec->species == QA_Q1_GREMLIN) {
        if (g->time < m->attack_finished)
            return true;
        if (range(g, entity, false) <= 90 && !m->source.gremlin.stolen) {
            m->attack_state = 1;
            *out = true;
            return true;
        }
        if (q1_random(g) < 0.03f + (m->source.gremlin.stolen ? 1 : 0)) {
            m->attack_state = 2;
            *out = true;
        }
        return true;
    }
    if (spec->species == QA_Q1_ARMAGON)
        return q1_armagon_attack(g, entity, out, error);
    if (spec->species == QA_Q1_LAVA_MAN)
        return q1_lavaman_attack(g, entity, out, error);
    float distance = range(g, entity, true);
    if (!isfinite(distance))
        return true;
    bool mg3 = m->addon.enabled && g->options.program == QA_Q1_MG3;
    unsigned category = mg3               ? q1_mg3_range(entity, distance)
                        : distance < 120  ? 0
                        : distance < 500  ? 1
                        : distance < 1000 ? 2
                                          : 3;
    if (spec->species == QA_Q1_SCOURGE) {
        qa_body_state self, other;
        if (!body(g, entity, &self, error) || !target_body(g, entity, &other))
            return false;
        qa_q1_target traits;
        qa_vec3 delta = qa_vec_sub(
            qa_vec_add(other.origin,
                       qa_v3(0, 0, q1_target(g, q1_ref_actor(g, m->enemy), &traits) ? traits.view_height : 25)),
            qa_vec_add(self.origin, qa_v3(0, 0, 25)));
        distance = qa_vec_length(delta);
        bool visible;
        if (distance <= 100) {
            if (!q1_can_damage(g, q1_ref_actor(g, m->enemy), entity->id, &visible, error))
                return false;
            if (visible) {
                m->attack_state = 1;
                *out = true;
                return true;
            }
        }
        if (g->time < m->attack_finished || delta.z > 64 || delta.z < -200 || distance > 1000 ||
            distance < 150)
            return true;
        if (!q1_monster_visible(g, entity, q1_ref_actor(g, m->enemy), &visible, error))
            return false;
        if (!visible)
            return true;
        if (!clear_shot(g, entity, &visible, error))
            return false;
        if (!visible)
            return true;
        float delay = 2 + 2 * q1_random(g);
        if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3)
            m->attack_finished = g->time + delay;
        m->refired = false;
        m->attack_state = 2;
        *out = true;
        return true;
    }
    if (spec->species == QA_Q1_DEMON || spec->species == QA_Q1_DOG) {
        if (category == 0) {
            m->attack_state = 1;
            *out = true;
            return true;
        }
        qa_body_state self, other;
        if (!body(g, entity, &self, error) || !target_body(g, entity, &other))
            return false;
        qa_vec3 delta = qa_vec_sub(other.origin, self.origin);
        float horizontal = hypotf(delta.x, delta.y);
        float low = other.origin.z + other.bounds.mins.z,
              height = other.bounds.maxs.z - other.bounds.mins.z;
        if (self.origin.z + self.bounds.mins.z > low + height * 0.75f ||
            self.origin.z + self.bounds.maxs.z < low + height * 0.25f)
            return true;
        if (spec->species == QA_Q1_DOG) {
            if (horizontal < 80 || horizontal > 150)
                return true;
        } else {
            if (horizontal < 100 || (horizontal > 200 && q1_random(g) < 0.9f))
                return true;
            if (!q1_sound(g, entity->id, "demon/djump.wav", 2, 1, error))
                return false;
        }
        m->attack_state = 2;
        *out = true;
        return true;
    }
    bool specialized = !strcmp(spec->classname, "monster_ogre") || m->addon.rocket_ogre ||
                       spec->species == QA_Q1_SHAMBLER;
    bool clear = false;
    if (!specialized && spec->species != QA_Q1_WIZARD) {
        if (!clear_shot(g, entity, &clear, error))
            return false;
        if (!clear)
            return true;
    }
    bool melee =
        spec->melee || (mg3 && spec->species == QA_Q1_ZOMBIE && (entity->spawnflags & 128));
    if (category == 0 && melee) {
        bool allowed = true;
        if (specialized && !q1_can_damage(g, q1_ref_actor(g, m->enemy), entity->id, &allowed, error))
            return false;
        if (allowed) {
            *out = true;
            if (specialized) {
                m->attack_state = 1;
                return true;
            }
            return melee_attack(g, entity, error);
        }
    }
    if (!spec->missile || (mg3 && spec->species == QA_Q1_ZOMBIE && (entity->spawnflags & 128)) ||
        g->time < m->attack_finished)
        return true;
    if (category == 3 || (spec->species == QA_Q1_SHAMBLER && distance > 600)) {
        if (spec->species == QA_Q1_WIZARD && m->sliding) {
            m->sliding = false;
            return q1_monster_play(g, entity, "wiz_run1", error);
        }
        return true;
    }
    if (specialized || spec->species == QA_Q1_WIZARD) {
        if (!clear_shot(g, entity, &clear, error))
            return false;
        if (!clear) {
            if (spec->species == QA_Q1_WIZARD && m->sliding) {
                m->sliding = false;
                return q1_monster_play(g, entity, "wiz_run1", error);
            }
            return true;
        }
    }
    if (specialized) {
        float delay = (spec->species == QA_Q1_SHAMBLER ? 2 : 1) + 2 * q1_random(g);
        if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3 || mg3)
            m->attack_finished = g->time + delay;
        m->refired = false;
        m->attack_state = 2;
        *out = true;
        return true;
    }
    if (category == 0 && spec->species != QA_Q1_WIZARD)
        m->attack_finished = 0;
    float chance = category == 0                   ? 0.9f
                   : category == 1                 ? spec->species == QA_Q1_WIZARD ? 0.6f
                                                     : melee                       ? 0.2f
                                                                                   : 0.4f
                   : spec->species == QA_Q1_WIZARD ? 0.2f
                   : melee                         ? 0.05f
                   : spec->species == QA_Q1_ARMY   ? 0.05f
                                                   : 0.1f;
    if (q1_random(g) >= chance) {
        if (spec->species == QA_Q1_WIZARD && m->sliding != (category < 2)) {
            m->sliding = category < 2;
            return q1_monster_play(g, entity, m->sliding ? "wiz_side1" : "wiz_run1", error);
        }
        return true;
    }
    *out = true;
    if (spec->species == QA_Q1_WIZARD) {
        m->sliding = false;
        m->attack_state = 2;
        return true;
    }
    const char *animation = spec->missile;
    if (spec->species == QA_Q1_ZOMBIE) {
        float r = q1_random(g);
        animation = r < 0.3f ? "zombie_atta1" : r < 0.6f ? "zombie_attb1" : "zombie_attc1";
    }
    if (!q1_monster_play(g, entity, animation, error))
        return false;
    double delay = spec->species == QA_Q1_ARMY ? 1 + q1_random(g) : 2 * q1_random(g);
    if (g->options.edition == QA_Q1_RERELEASE || g->options.skill != 3 ||
        spec->species == QA_Q1_ARMY || mg3)
        m->attack_finished = g->time + delay;
    m->refired = false;
    if (spec->species == QA_Q1_ARMY) {
        float value = q1_random(g);
        if (m->addon.enabled && value < 0.3f)
            m->lefty = !m->lefty;
    }
    return true;
}

bool q1_monster_melee(qa_q1_game *g, q1_actor *entity, float maximum, float scale, unsigned rolls,
                      bool sight, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (!q1_ref_present(m->enemy) || range(g, entity, false) > maximum)
        return true;
    if (sight) {
        bool visible;
        if (!q1_can_damage(g, q1_ref_actor(g, m->enemy), entity->id, &visible, error))
            return false;
        if (!visible)
            return true;
    }
    float damage = 0;
    for (unsigned i = 0; i < rolls; ++i)
        damage += q1_random(g);
    return q1_damage(g, q1_ref_actor(g, m->enemy), entity->id, entity->id, damage * scale, QA_Q1_WEAPON_COUNT,
                     error);
}
bool q1_monster_ai(qa_q1_game *g, q1_actor *entity, q1_ai ai, float distance, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    bool found_target, moved;
    qa_body_state self;
    if (ai == Q1_AI_STAND || ai == Q1_AI_WALK || ai == Q1_AI_RUN) {
        bool stop;
        if (!q1_addon_contents(g, entity, &stop, error))
            return false;
        if (stop)
            return true;
    }
    switch (ai) {
    case Q1_AI_STAND:
        if (!q1_monster_find_target(g, entity, &found_target, error))
            return false;
        if (!found_target && g->time > m->pause_until && q1_monster_route(g, entity).registry &&
            strcmp(m->species->walk, m->species->stand))
            return q1_monster_play(g, entity, m->species->walk, error);
        return true;
    case Q1_AI_TURN:
        if (!q1_monster_find_target(g, entity, &found_target, error))
            return false;
        return found_target || q1_monster_face(g, entity, error);
    case Q1_AI_WALK: {
        if (q1_ref_present(m->charmer))
            return q1_charmed_walk(g, entity, distance, error);
        if (!q1_monster_find_target(g, entity, &found_target, error))
            return false;
        if (found_target || g->time < m->pause_until)
            return true;
        qa_actor_id goal = q1_monster_route(g, entity);
        if (!goal.registry) {
            m->pause_until = (float)g->time + 999999;
            return q1_monster_play(g, entity, m->species->stand, error);
        }
        return qa_physics_q1_move_to_goal(g->services.physics, entity->id, goal, distance, false,
                                          error);
    }
    case Q1_AI_RUN: {
        qa_monster_mission mission;
        if (q1_monster_mission(g, entity->id, &mission)) {
            qa_monster_combat_route route;
            if (!mission.combat_route(mission.context, entity->id, &route, error)) return false;
            if (route.goal.registry)
                return qa_physics_q1_move_to_goal(g->services.physics, entity->id, route.goal, distance, false, error);
            if (route.stand_ground) distance = 0;
        }
        if (m->addon.enabled && g->options.program == QA_Q1_MG3)
            m->hostile_until = g->time + 1;
        if (q1_ref_present(m->charmer) && (!q1_ref_present(m->enemy) || q1_ref_equal(m->enemy, m->charmer) ||
                                    q1_health(g, q1_ref_actor(g, m->enemy)) <= 0)) {
            m->enemy = (q1_ref){0};
            return q1_charmed_hunt(g, entity, false, error);
        }
        if (!eligible(g, q1_ref_actor(g, m->enemy))) {
            m->attack_state = 0;
            if (eligible(g, q1_ref_actor(g, m->old_enemy))) {
                m->enemy = m->old_enemy;
                m->old_enemy = (q1_ref){0};
            } else {
                m->enemy = (q1_ref){0};
                m->old_enemy = (q1_ref){0};
                return q1_monster_play(g, entity,
                                       q1_monster_route(g, entity).registry ? m->species->walk
                                                                            : m->species->stand,
                                       error);
            }
        }
        bool seen;
        if (!q1_monster_visible(g, entity, q1_ref_actor(g, m->enemy), &seen, error))
            return false;
        if (m->addon.enabled && g->options.program == QA_Q1_MG3)
            g->enemy_visible = seen;
        if (seen)
            m->search_until = g->time + 5;
        if (g->options.coop && !q1_ref_present(m->charmer) && m->search_until < g->time) {
            if (!q1_monster_find_target(g, entity, &found_target, error))
                return false;
            if (found_target)
                return true;
        }
        if (m->addon.enabled && g->options.program == QA_Q1_MG3)
            g->enemy_range = (uint8_t)q1_mg3_range(entity, range(g, entity, true));
        if (m->attack_state == 3) {
            bool attacking = false;
            if (seen && !try_attack(g, entity, &attacking, error))
                return false;
            if (attacking)
                return true;
            if (!q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error))
                return false;
            float offset = m->lefty ? 40 : -40;
            if (g->time > m->dodge_after) {
                m->lefty = !m->lefty;
                m->dodge_after = g->time + 0.8;
            }
            qa_body_state target;
            if (!body(g, entity, &self, error) || !target_body(g, entity, &target))
                return false;
            qa_vec3 delta = qa_vec_sub(target.origin, self.origin);
            entity->physics.ideal_yaw =
                qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
            if (!qa_physics_walk_move(g->services.physics, entity->id,
                                      entity->physics.ideal_yaw + offset, distance,
                                      (float)g->elapsed, true, true, &moved, error))
                return false;
            if (!moved) {
                m->lefty = !m->lefty;
                m->dodge_after = g->time + 0.8;
                if (!qa_physics_walk_move(g->services.physics, entity->id,
                                          entity->physics.ideal_yaw - offset, distance,
                                          (float)g->elapsed, true, true, &moved, error))
                    return false;
            }
            return !q1_alive(g, entity->id) ||
                   qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed, error);
        }
        if (m->attack_state) {
            if (!q1_monster_face(g, entity, error) || !body(g, entity, &self, error))
                return false;
            float delta = fabsf(qa_builtin_angle_delta(self.angles.y, entity->physics.ideal_yaw));
            if (delta <= 45) {
                uint8_t state = m->attack_state;
                m->attack_state = 0;
                return state == 1 ? melee_attack(g, entity, error)
                                  : !m->species->missile ||
                                        q1_monster_play(g, entity, m->species->missile, error);
            }
            return true;
        }
        bool attacking = false;
        if (seen && !try_attack(g, entity, &attacking, error))
            return false;
        if (attacking)
            return true;
        if (m->sliding) {
            if (!q1_monster_face(g, entity, error))
                return false;
            float yaw = entity->physics.ideal_yaw + (m->lefty ? 90 : -90);
            if (!qa_physics_walk_move(g->services.physics, entity->id, yaw, distance,
                                      (float)g->elapsed, true, true, &moved, error))
                return false;
            if (moved)
                return true;
            m->lefty = !m->lefty;
            if (m->addon.boss == Q1_BOSS_ORB) {
                m->sliding = false;
                m->attack_state = 0;
                m->next_frame = q1_frame_index(m->species->run);
                return true;
            }
            return qa_physics_walk_move(g->services.physics, entity->id, yaw + 180, distance,
                                        (float)g->elapsed, true, true, &moved, error);
        }
        if ((g->options.program == QA_Q1_HIPNOTIC || q1_ref_present(m->charmer)) && g->run_straight &&
            g->time > m->straight_after) {
            g->run_straight = false;
            if (!body(g, entity, &self, error) ||
                !qa_physics_walk_move(g->services.physics, entity->id, self.angles.y, distance,
                                      (float)g->elapsed, true, true, &moved, error))
                return false;
            if (moved)
                return true;
            m->straight_after = g->time + 3;
        }
        return q1_addon_move(g, entity, distance, seen, error);
    }
    case Q1_AI_FACE:
        return q1_monster_face(g, entity, error);
    case Q1_AI_CHARGE:
        if (!q1_monster_face(g, entity, error))
            return false;
        return !q1_ref_present(m->enemy) || qa_physics_q1_move_to_goal(g->services.physics, entity->id,
                                                                q1_ref_actor(g, m->enemy), distance, false, error);
    case Q1_AI_CHARGE_SIDE:
    case Q1_AI_MELEE_SIDE: {
        qa_body_state target;
        if (!q1_monster_face(g, entity, error) || !body(g, entity, &self, error))
            return false;
        if (target_body(g, entity, &target)) {
            qa_builtin_angle_vectors(self.angles, &g->forward, &g->right, &g->up);
            qa_vec3 delta =
                qa_vec_sub(qa_vec_sub(target.origin, qa_vec_scale(g->right, 30)), self.origin);
            float yaw = atan2f(delta.y, delta.x) * 57.29577951308232f;
            if (!qa_physics_walk_move(g->services.physics, entity->id, yaw, 20, (float)g->elapsed,
                                      true, true, &moved, error))
                return false;
        }
        return ai == Q1_AI_CHARGE_SIDE || q1_monster_melee(g, entity, 60, 3, 3, true, error);
    }
    case Q1_AI_MELEE:
        return q1_monster_melee(g, entity, 60, 3, 3, false, error);
    case Q1_AI_PAIN:
    case Q1_AI_PAINFORWARD:
    case Q1_AI_FORWARD:
        if (!body(g, entity, &self, error))
            return false;
        return qa_physics_walk_move(g->services.physics, entity->id,
                                    self.angles.y + (ai == Q1_AI_PAIN ? 180 : 0), distance,
                                    (float)g->elapsed, true, true, &moved, error);
    }
    return false;
}

bool q1_monster_play(qa_q1_game *g, q1_actor *entity, const char *name, qa_error *error) {
    if (!q1_alive(g, entity->id))
        return true;
    uint16_t index = q1_frame_index(name);
    if (index == UINT16_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "missing Q1 native frame %s", name);
        return false;
    }
    entity->state.monster.next_frame = index;
    return q1_monster_frame(g, entity, error);
}
bool qa_q1_monster_activate(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_actor *entity = q1_entity(g, actor);
    if (!entity || entity->kind != Q1_MONSTER) return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, actor, &combat, error)) return false;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->physics.solid = QA_PHYSICS_BOX;
    combat.can_take_damage = true;
    return q1_model(g, entity, entity->state.monster.species->model, error) &&
        qa_combat_set_traits(g->services.combat, actor, &combat, error) && q1_link(g, entity, error);
}
bool q1_monster_mission_turn(qa_q1_game *g, q1_actor *entity, bool *active, qa_error *error) {
    qa_monster_mission mission;
    *active = true;
    if (!q1_monster_mission(g, entity->id, &mission) || !mission.active || q1_health(g, entity->id) <= 0) return true;
    qa_actor_id actor = entity->id;
    qa_monster_activation activation;
    if (!mission.active(mission.context, entity->id, &activation, error)) return false;
    if (!q1_alive(g, actor)) { *active = false; return true; }
    *active = activation.active;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error)) return false;
    if (!*active) {
        entity->physics.motion = QA_PHYSICS_STATIONARY;
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        combat.can_take_damage = false;
        entity->model = 0;
        return qa_combat_set_traits(g->services.combat, entity->id, &combat, error) && q1_link(g, entity, error);
    }
    if (entity->physics.solid == QA_PHYSICS_NOT_SOLID && !qa_q1_monster_activate(g, entity->id, error)) return false;
    if (activation.activator.registry && entity->think == Q1_THINK_MONSTER_START &&
        !q1_monster_start(g, entity, error)) return false;
    return !activation.activator.registry || !q1_alive(g, entity->id) || q1_monster_use(g, entity, activation.activator, error);
}
bool q1_monster_frame(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    if (m->addon.boss != Q1_BOSS_FINAL && q1_health(g, entity->id) > 0 && q1_ref_present(m->enemy) &&
        !eligible(g, q1_ref_actor(g, m->enemy)) &&
        !(m->species->species == QA_Q1_GREMLIN && m->source.gremlin.gorging)) {
        m->enemy = eligible(g, q1_ref_actor(g, m->old_enemy)) ? m->old_enemy : (q1_ref){0};
        m->old_enemy = (q1_ref){0};
        m->attack_state = 0;
        const char *next = q1_ref_present(m->enemy)                      ? m->species->run
                           : q1_monster_route(g, entity).registry ? m->species->walk
                                                                  : m->species->stand;
        m->next_frame = q1_frame_index(next);
    }
    if (m->next_frame >= q1_frame_count) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q1 monster frame outside table");
        return false;
    }
    if (m->addon.infected && !m->addon.transformed && m->species->species == QA_Q1_ARMY)
        m->next_frame = q1_infected_frame(m->next_frame);
    if (g->options.program == QA_Q1_MG3 && m->species->species == QA_Q1_LAVA_MAN)
        m->next_frame = q1_mg3_lavaman_frame(m->next_frame);
    if (m->addon.boss == Q1_BOSS_SHUB_ZOMBIE)
        m->next_frame = q1_shub_zombie_frame(m->next_frame);
    const q1_frame *frame = &q1_frames[m->next_frame];
    bool rocket_frame = m->addon.rocket_ogre && q1_rocket_ogre_override(frame->name);
    if (rocket_frame && !strcmp(frame->name, "ogre_stand5") &&
        !q1_rocket_ogre_frame(g, entity, frame->name, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (frame->frame == UINT16_MAX)
        return q1_monster_action(g, entity, q1_frame_operations[frame->operation].action, error);
    m->current_frame = m->next_frame;
    m->next_frame = frame->next;
    entity->frame = frame->frame;
    if (!q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error))
        return false;
    if (g->options.edition == QA_Q1_CLASSIC) {
        if (!strcmp(frame->name, "boss_idle1") || !strcmp(frame->name, "f_death2"))
            return true;
        if (!strcmp(frame->name, "f_death21")) {
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
            return q1_link(g, entity, error);
        }
        if (!strcmp(frame->name, "sham_magic11") && g->options.skill == 3)
            return q1_monster_action(g, entity, Q1_ACTION_SHAM_MAGIC10, error);
    }
    for (uint8_t i = 0; i < frame->count; ++i) {
        if (!q1_alive(g, entity->id))
            return true;
        const q1_frame_operation *op = &q1_frame_operations[frame->operation + i];
        if (rocket_frame && (op->kind == Q1_FRAME_ACTION || op->kind == Q1_FRAME_SOUND))
            continue;
        switch (op->kind) {
        case Q1_FRAME_AI:
            if (!q1_monster_ai(g, entity, op->ai, op->distance, error))
                return false;
            break;
        case Q1_FRAME_SOLID:
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
            if (!q1_link(g, entity, error))
                return false;
            break;
        case Q1_FRAME_ACTION:
            if (!q1_monster_action(g, entity, op->action, error))
                return false;
            break;
        case Q1_FRAME_SOUND: {
            float value = op->chance < 0 ? 0 : q1_random(g);
            if ((op->chance < 0 || (op->greater ? value > op->chance : value < op->chance)) &&
                !q1_sound(g, entity->id, op->text, op->channel, op->attenuation, error))
                return false;
            break;
        }
        case Q1_FRAME_LIGHTSTYLE: {
            qa_builtin_event event = {.kind = QA_BUILTIN_LIGHT,
                                      .family = QA_GAME_Q1,
                                      .provider = g->options.provider,
                                      .actor = entity->id,
                                      .time_ns = g->time_ns};
            if (!qa_builtin_resource(&g->services, op->text, &event.text, error) ||
                !qa_builtin_emit(&g->services, &event, error))
                return false;
            break;
        }
        }
    }
    return !rocket_frame || !strcmp(frame->name, "ogre_stand5") || !q1_alive(g, entity->id) ||
           q1_rocket_ogre_frame(g, entity, frame->name, error);
}

bool q1_ctf_monster_removed(qa_bytes classname) {
    static const char *const removed[] = {
        "monster_army", "monster_dog", "monster_ogre", "monster_ogre_marksman",
        "monster_knight", "monster_hell_knight", "monster_wizard", "monster_demon1",
        "monster_shambler", "monster_zombie", "monster_tarbaby", "monster_fish",
        "monster_enforcer", "monster_shalrath", "monster_boss", "monster_oldone"};
    for (size_t i = 0; i < sizeof(removed) / sizeof(*removed); ++i)
        if (classname.size == strlen(removed[i]) &&
            !memcmp(classname.data, removed[i], classname.size))
            return true;
    return false;
}
static bool monster_spawn(qa_q1_game *g, q1_actor *entity, const q1_species *spec,
                           bool delayed_start, qa_error *error) {
    if (g->options.program == QA_Q1_CTF &&
        q1_ctf_monster_removed(qa_strings_text(qa_session_strings(g->services.session),
                                             entity->classname)))
        return q1_remove(g, entity, error);
    if (!g->services.physics) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "native Q1 monsters require shared physics");
        return false;
    }
    bool infected = strstr(spec->classname, "_infected") != NULL;
    unsigned corpse = infected && spec->species == QA_Q1_HELLKNIGHT
                          ? (entity->spawnflags & 65536u)     ? 1
                            : (entity->spawnflags & 8388608u) ? 2
                                                              : 0
                          : 0;
    if (corpse)
        spec = q1_infected_form(QA_Q1_HELLKNIGHT, corpse);
    entity->kind = Q1_MONSTER;
    entity->state.monster = (q1_monster){.species = spec,
                                         .birth_epoch = 1,
                                         .path = entity->target,
                                         .current_frame = q1_frame_index(spec->stand),
                                         .next_frame = q1_frame_index(spec->stand)};
    bool addon = g->options.program >= QA_Q1_DOPA && g->options.program <= QA_Q1_MG3 &&
                 spec->species <= QA_Q1_ZOMBIE;
    if (addon && (g->options.coop ? (entity->spawnflags & 131072u) : (entity->spawnflags & 32768u)))
        return q1_remove(g, entity, error);
    if (addon && g->options.program == QA_Q1_MG3) {
        unsigned runes = qa_q1_mg3_rune_count(qa_q1_game_campaign_flags(g));
        if (entity->spawnflags & (262144u << runes))
            return q1_remove(g, entity, error);
    }
    q1_monster *monster = &entity->state.monster;
    bool boss_handled;
    if (!q1_boss_spawn(g, entity, &boss_handled, error))
        return false;
    if (boss_handled)
        return true;
    monster->addon.enabled = addon;
    monster->addon.infected = infected;
    monster->addon.infection_count_pending = infected;
    monster->addon.infected_kind = (uint8_t)spec->species;
    monster->addon.corpse = (uint8_t)corpse;
    monster->addon.demodog = !strcmp(spec->classname, "monster_demodog");
    monster->addon.heavy =
        !strcmp(spec->classname, "monster_super_shambler")  ? Q1_HEAVY_SUPER_SHAMBLER
        : !strcmp(spec->classname, "monster_ranged_knight") ? Q1_HEAVY_RUNE_KNIGHT
                                                            : Q1_HEAVY_NONE;
    if (infected || monster->addon.demodog) {
        const char *name = monster->addon.demodog            ? "monster_dog"
                           : spec->species == QA_Q1_ARMY     ? "monster_army"
                           : spec->species == QA_Q1_KNIGHT   ? "monster_knight"
                           : spec->species == QA_Q1_ENFORCER ? "monster_enforcer"
                                                             : "monster_hell_knight";
        if (!qa_builtin_resource(&g->services, name, &entity->classname, error))
            return false;
    }
    monster->addon.rocket_ogre = !strcmp(spec->classname, "monster_ogre_rocket");
    if (monster->addon.rocket_ogre) {
        monster->addon.projectiles = monster->addon.projectile_max = 2;
        if (!qa_builtin_resource(&g->services, "monster_ogre", &entity->classname, error))
            return false;
    }
    entity->max_health = spec->health;
    if (spec->species == QA_Q1_DECOY && g->maps) {
        qa_builtin_snapshot_frame *players;
        if (!q1_snapshot_players(g, &players, error))
            return false;
        q1_player *player = players->snapshot.count ? q1_player_get(g, players->snapshot.ids[0])
                                                    : NULL;
        q1_map_state *map = q1_map_allocate(g, entity, error);
        if (map)
            map->color_map = player && player->source_client ? (int32_t)(player->client_slot + 1) : 0;
        qa_builtin_snapshot_release(players);
        if (!map)
            return false;
    }
    entity->aimed_damage = true;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.yaw_speed = spec->flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING) ? 10 : 20;
    if (spec->species == QA_Q1_SCOURGE) {
        entity->physics.yaw_speed = 60;
        entity->state.monster.attack_state = 3;
    }
    if (spec->species == QA_Q1_WRATH)
        entity->physics.yaw_speed = 35;
    entity->physics.flags |= spec->flags;
    qa_body_state state;
    if (!body(g, entity, &state, error))
        return false;
    state.bounds = spec->bounds;
    entity->physics.ideal_yaw = state.angles.y;
    if (addon && !infected && !monster->addon.demodog && monster->addon.boss == Q1_BOSS_NONE &&
        spec->species != QA_Q1_DEMON && spec->species != QA_Q1_OGRE &&
        spec->species != QA_Q1_SHAMBLER && spec->species != QA_Q1_SHALRATH)
        state.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 40}};
    bool hanging = addon && g->options.program == QA_Q1_MG3 && (entity->spawnflags & 8388608u);
    qa_monster_mission mission;
    bool has_mission = q1_monster_mission(g, entity->id, &mission);
    if (has_mission) entity->spawnflags = mission.ambush ? spec->species == QA_Q1_ZOMBIE ? 2u : 1u : 0;
    bool crucified = spec->species == QA_Q1_ZOMBIE && ((entity->spawnflags & 1) || hanging);
    if (has_mission) {
        if (!mission.spawned(mission.context, entity->id, error)) return false;
    } else if (!crucified && spec->species != QA_Q1_DECOY)
        ++g->total_monsters;
    if (spec->species == QA_Q1_DRAGON)
        return q1_dragon_spawn(g, entity, error);
    if (spec->species == QA_Q1_MORPH)
        return q1_morph_spawn(g, entity, error);
    if (spec->species == QA_Q1_LAVA_MAN) {
        if (g->options.program == QA_Q1_MG3) {
            monster->addon.enabled = true;
            entity->spawnflags |= 16384u;
        }
        if (g->options.program == QA_Q1_MG3
                ? qa_strings_text(qa_session_strings(g->services.session), entity->targetname)
                          .size != 0
                : (entity->spawnflags & 2) != 0) {
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
            entity->physics.motion = QA_PHYSICS_STATIONARY;
            entity->model = 0;
            entity->aimed_damage = false;
            return true;
        }
        return q1_lavaman_awake(g, entity, (qa_actor_id){0}, error);
    }
    monster->path_end = true;
    if (!qa_combat_set_health(g->services.combat, entity->id, spec->health, error) ||
        !qa_world_body_write(g->services.world, entity->id, &state, error) ||
        !q1_model(g, entity, spec->model, error))
        return false;
    if (addon && !crucified) {
        if (spec->species == QA_Q1_FISH)
            entity->spawnflags |= 16384u;
        monster->lefty = true;
        monster->addon.allow_path = spec->species != QA_Q1_FISH && spec->species != QA_Q1_WIZARD;
        if (infected)
            monster->addon.allow_path =
                spec->species == QA_Q1_KNIGHT || spec->species == QA_Q1_HELLKNIGHT;
        monster->addon.combat_style =
            monster->addon.demodog || spec->species == QA_Q1_KNIGHT ||
                    spec->species == QA_Q1_DEMON || spec->species == QA_Q1_TARBABY ||
                    spec->species == QA_Q1_FISH
                ? 2
            : spec->species == QA_Q1_OGRE || spec->species == QA_Q1_HELLKNIGHT ||
                    spec->species == QA_Q1_SHAMBLER
                ? 3
                : 1;
        if (monster->addon.heavy == Q1_HEAVY_RUNE_KNIGHT)
            monster->addon.combat_style = 1;
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        entity->physics.motion = QA_PHYSICS_STATIONARY;
        entity->model = 0;
        entity->aimed_damage = false;
        monster->addon.waiting = (entity->spawnflags & 4) != 0;
        if (monster->addon.waiting) {
            if (g->options.program != QA_Q1_MG3 && g->services.cvar) {
                qa_string_id name;
                float horde = 0;
                if (!qa_builtin_resource(&g->services, "horde", &name, error) ||
                    !g->services.cvar(q1_cvar_context(g), name, &horde, error))
                    return false;
                if (horde != 0)
                    --g->total_monsters;
            }
            return true;
        }
        return q1_schedule(g, entity, q1_random(g) * 0.5 - g->time, Q1_THINK_MONSTER_START, error);
    }
    if (spec->species == QA_Q1_GREMLIN && !q1_gremlin_spawn(g, entity, error))
        return false;
    if (spec->species == QA_Q1_ARMAGON && !q1_armagon_spawn(g, entity, error))
        return false;
    if (spec->species == QA_Q1_EEL)
        entity->delay = (float)(g->time + q1_random(g) * 6);
    if (spec->species == QA_Q1_SWORD && entity->delay == 0)
        entity->delay = 10;
    if (spec->species == QA_Q1_MUMMY) {
        if (entity->spawnflags & 4) {
            entity->max_health = 1000;
            if (!qa_combat_set_health(g->services.combat, entity->id, 1000, error))
                return false;
        }
        if (entity->spawnflags & 2) {
            entity->state.monster.source.mummy.asleep = true;
            state.bounds.maxs = qa_v3(16, 16, -16);
            entity->physics.solid = QA_PHYSICS_NOT_SOLID;
            if (!qa_world_body_write(g->services.world, entity->id, &state, error))
                return false;
        }
    }
    if (spec->species == QA_Q1_BOSS) {
        entity->model = 0;
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        return q1_link(g, entity, error);
    }
    if (spec->species == QA_Q1_OLDONE) {
        qa_combat_state combat;
        if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
            return false;
        combat.can_take_damage = true;
        return qa_combat_set_traits(g->services.combat, entity->id, &combat, error) &&
               q1_link(g, entity, error) &&
               q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
    }
    if (crucified) {
        entity->physics.motion = QA_PHYSICS_STATIONARY;
        return q1_link(g, entity, error) &&
               q1_monster_play(g, entity, hanging ? "zombie_hang1" : "zombie_cruc1", error);
    }
    if (!delayed_start)
        return true;
    double delay = q1_random(g) * 0.5;
    delay += spec->species == QA_Q1_ARMY || spec->species == QA_Q1_DOG ? 0.1 : -g->time;
    return q1_schedule(g, entity, delay, Q1_THINK_MONSTER_START, error);
}
bool q1_monster_spawn(qa_q1_game *g, q1_actor *entity, const q1_species *spec, qa_error *error) {
    return monster_spawn(g, entity, spec, true, error);
}
bool q1_become_decoy(qa_q1_game *g, qa_vec3 origin, qa_string_id target,
                      qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    q1_actor *decoy;
    if (!q1_create(g, "monster_decoy", Q1_MONSTER, (qa_actor_id){0}, &decoy, error))
        return false;
    qa_actor_id id = decoy->id;
    decoy->target = target;
    qa_body_state state;
    if (!body(g, decoy, &state, error))
        goto failed;
    state.origin = origin;
    if (!qa_world_body_write(g->services.world, id, &state, error) ||
        !monster_spawn(g, decoy, q1_species_find("monster_decoy"), false, error))
        goto failed;
    if (!q1_alive(g, id))
        return true;
    decoy->physics.flags |= QA_PHYSICS_MONSTER;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, id, &combat, error))
        goto failed;
    combat.can_take_damage = true;
    if (!qa_combat_set_traits(g->services.combat, id, &combat, error) ||
        !q1_link(g, decoy, error))
        goto failed;
    if (!q1_alive(g, id))
        return true;
    qa_actor_id goal = q1_monster_route(g, decoy);
    decoy->state.monster.move_target = decoy->physics.goal = q1_ref_from(g, goal);
    if (goal.registry) {
        qa_body_state destination;
        if (!qa_world_body_read(g->services.world, goal, &destination, error))
            goto failed;
        qa_vec3 delta = qa_vec_sub(destination.origin, origin);
        decoy->physics.ideal_yaw =
            qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
    }
    if (goal.registry && q1_classnamed(g, goal, "path_corner")) {
        if (!q1_monster_play(g, decoy, "decoy_walk1", error))
            goto failed;
    } else
        decoy->state.monster.pause_until = 99999999;
    if (!q1_alive(g, id))
        return true;
    if (!q1_monster_play(g, decoy, "decoy_stand1", error))
        goto failed;
    if (!q1_alive(g, id))
        return true;
    if (!q1_schedule(g, decoy, decoy->next_think - g->time + q1_random(g) * 0.5,
                     Q1_THINK_MONSTER_FRAME, error))
        goto failed;
    *out = id;
    return true;
failed: {
    qa_error original = error != NULL ? *error : (qa_error){0};
    if (q1_alive(g, id))
        qa_session_release(g->services.session, id, NULL);
    if (error != NULL)
        *error = original;
    return false;
}
}
bool q1_monster_start(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    qa_body_state state;
    if (!body(g, entity, &state, error))
        return false;
    bool addon = m->addon.enabled;
    if (addon) {
        m->addon.waiting = false;
        m->addon.started = true;
        entity->physics.solid = QA_PHYSICS_BOX;
        entity->physics.motion = QA_PHYSICS_STEP;
        entity->physics.yaw_speed = 20;
        entity->aimed_damage = true;
        if (!q1_model(g, entity, m->species->model, error))
            return false;
    }
    if (!(entity->physics.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))) {
        state.origin.z += 1;
        qa_trace_query query = {.start = state.origin,
                                .end = qa_vec_add(state.origin, qa_v3(0, 0, -256)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = state.bounds},
                                .pass_actor = entity->id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        if (trace.fraction < 1 && !trace.all_solid) {
            state.origin = trace.end;
            state.ground = trace.hit == QA_TRACE_HIT_WORLD ? qa_actor_reference_source(g->options.provider, 0) : q1_ref_from(g, trace.actor);
            entity->physics.flags |= QA_PHYSICS_ONGROUND;
        }
        if (!qa_world_body_write(g->services.world, entity->id, &state, error))
            return false;
        bool moved;
        if (!qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true,
                                  true, &moved, error))
            return false;
    }
    if (addon && (entity->physics.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING))) {
        bool moved;
        if (!qa_physics_walk_move(g->services.physics, entity->id, 0, 0, (float)g->elapsed, true,
                                  true, &moved, error))
            return false;
    }
    if (addon && (entity->spawnflags & 4)) {
        if (!body(g, entity, &state, error) ||
            !q1_spawn_teledeath(g, state.origin, entity->id, 0.2, false, NULL, error))
            return false;
        if ((entity->spawnflags & 16) &&
            !q1_effect(g, QA_BUILTIN_TELEPORT, entity->id, state.origin, 0, 0, error))
            return false;
        if (!q1_alive(g, entity->id))
            return true;
    }
    qa_monster_mission mission;
    bool has_mission = q1_monster_mission(g, entity->id, &mission);
    if (!has_mission && !addon && m->species->species == QA_Q1_FISH && g->options.edition == QA_Q1_CLASSIC)
        ++g->total_monsters;
    entity->physics.flags |= QA_PHYSICS_MONSTER;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, entity->id, &combat, error))
        return false;
    combat.can_take_damage = true;
    if (!qa_builtin_resource(&g->services, "q1:monsters", &combat.team, error))
        return false;
    if (!qa_combat_set_traits(g->services.combat, entity->id, &combat, error) ||
        !q1_link(g, entity, error))
        return false;
    qa_actor_id goal = addon ? q1_find_target(g, entity->target) : q1_monster_route(g, entity);
    if (has_mission) {
        if (!mission.route(mission.context, entity->id, &goal, error) ||
            !mission.started(mission.context, entity->id, error)) return false;
        m->move_target = entity->physics.goal = q1_ref_from(g, goal);
        m->path = goal.registry ? entity->target : 0;
    }
    if (!addon && m->species->species >= QA_Q1_GREMLIN) {
        m->move_target = q1_ref_from(g, goal);
        entity->physics.goal = q1_ref_from(g, goal);
    }
    if (addon) {
        qa_actor_id authored;
        if (!q1_addon_target(g, entity, &authored, error))
            return false;
        if (authored.registry)
            return q1_monster_found(g, entity, authored, error);
    }
    if (addon) {
        m->path = goal.registry ? entity->target : QA_STRING_NONE;
        m->move_target = q1_ref_from(g, goal);
        entity->physics.goal = q1_ref_from(g, goal);
        if (goal.registry && qa_world_body_read(g->services.world, goal, &state, NULL)) {
            qa_body_state self;
            if (!body(g, entity, &self, error))
                return false;
            qa_vec3 delta = qa_vec_sub(state.origin, self.origin);
            entity->physics.ideal_yaw =
                qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
        }
        bool path = goal.registry && q1_classnamed(g, goal, "path_corner");
        m->addon.path_wait = path && (entity->spawnflags & 4096);
        if (!path || m->addon.path_wait)
            goal = (qa_actor_id){0};
    }
    if (!goal.registry)
        m->pause_until = 99999999;
    if (!q1_monster_play(g, entity, goal.registry ? m->species->walk : m->species->stand, error))
        return false;
    if (m->species->species == QA_Q1_MUMMY && m->source.mummy.asleep)
        m->next_frame = q1_frame_index(
            qa_strings_text(qa_session_strings(g->services.session), m->path).size
                ? "mummy_wake"
                : "mummy_sleep");
    if (addon && (entity->spawnflags & 4)) {
        if (g->options.program != QA_Q1_MG3) {
            for (uint32_t i = 0; i < g->capacity; ++i)
                if (g->actors[i] && q1_classnamed(g, g->actors[i]->id, "horde_manager")) {
                    ++g->total_monsters;
                    break;
                }
        }
        return !(entity->spawnflags & 8) || q1_monster_use(g, entity, q1_ref_actor(g, entity->activator), error);
    }
    if (addon || !(entity->physics.flags & QA_PHYSICS_FLYING))
        return q1_schedule(g, entity, entity->next_think - g->time + q1_random(g) * 0.5,
                           Q1_THINK_MONSTER_FRAME, error);
    return true;
}
