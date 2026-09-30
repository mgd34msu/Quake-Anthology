#include "medic.h"
#include "reinforcements.h"
#include "../entities/internal.h"

enum { MEDIC_CABLE_FIRST = 218, MEDIC_CONTACT = 219, MEDIC_HEAL_SOUND = 220,
       MEDIC_REVIVE = 226, MEDIC_RETRACT = 228 };

static const qa_vec3 cable_offsets[] = {
    {45, -9.2f, 15.5f}, {48.4f, -9.7f, 15.2f}, {47.8f, -9.8f, 15.8f},
    {47.3f, -9.3f, 14.3f}, {45.4f, -10.1f, 13.1f}, {41.9f, -12.7f, 12},
    {37.8f, -15.8f, 11.2f}, {34.3f, -18.4f, 10.7f}, {32.7f, -19.7f, 10.4f},
    {32.7f, -19.7f, 10.4f}
};

static bool rerelease(const q2m_context *c) {
    return c->game->options.edition == QA_Q2_RERELEASE;
}

static bool rogue(const q2m_context *c) {
    return rerelease(c) || c->game->options.product == QA_Q2_ROGUE ||
           c->monster->definition->species == Q2M_MEDIC_COMMANDER;
}

static bool medic_species(const struct qa_q2_monster *m) {
    return m->definition->species == Q2M_MEDIC ||
           m->definition->species == Q2M_MEDIC_COMMANDER;
}

static q2_actor *native_actor(qa_q2_game *g, qa_actor_id id) {
    if (!q2_actor_live(g, id) || id.slot >= g->capacity)
        return NULL;
    q2_actor *a = g->actors[id.slot];
    return a && qa_actor_id_equal(a->id, id) && a->monster &&
                   a->monster->definition && a->projectile.kind == Q2_PROJECTILE_NONE
               ? a : NULL;
}

static void enemy(q2m_context *c, qa_actor_id id) {
    c->monster->enemy = id;
    c->actor->physics.enemy = id;
    if (c->actor->entity)
        c->actor->entity->enemy = id;
}

static bool health(q2m_context *c, qa_actor_id id, float *out, qa_error *error) {
    *out = 0;
    if (!q2_actor_live(c->game, id))
        return true;
    qa_combat_state state;
    qa_error local = {0};
    if (!qa_combat_read(c->game->services.combat, id, &state, &local)) {
        if (local.code == QA_OK || local.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = local;
        return false;
    }
    if (q2m_alive(c) && q2_actor_live(c->game, id))
        *out = state.health;
    return true;
}

static bool player(q2m_context *c, qa_actor_id id) {
    qa_builtin_actor_traits traits = {0};
    return q2_actor_live(c->game, id) && c->game->services.actor_traits &&
           c->game->services.actor_traits(c->game->services.context, id, &traits) &&
           q2m_alive(c) && q2_actor_live(c->game, id) && traits.player;
}

static bool heal_effects(q2m_context *patient, qa_error *error) {
    patient->actor->extra_effects &= ~UINT64_C(256);
    patient->monster->render_flags &= ~(1024u | 2048u | 4096u);
    if (patient->monster->resurrecting) {
        patient->actor->extra_effects |= 256u;
        patient->monster->render_flags |= 1024u;
    }
    return q2m_show(patient, error);
}

bool q2m_medic_cleanup_patient(q2m_context *c, qa_actor_id id, qa_error *error) {
    if (!q2_actor_live(c->game, id) || id.slot >= c->game->capacity)
        return true;
    q2_actor *actor = c->game->actors[id.slot];
    if (!actor || !qa_actor_id_equal(actor->id, id))
        return true;
    if (!actor->monster) {
        qa_combat_state traits;
        if (!qa_combat_read_traits(c->game->services.combat, id, &traits, error))
            return false;
        if (!q2m_alive(c) || !q2_actor_live(c->game, id) ||
            c->game->actors[id.slot] != actor)
            return true;
        traits.can_take_damage = true;
        return qa_combat_set_traits(c->game->services.combat, id, &traits, error);
    }
    q2m_context target = {.game = c->game, .actor = actor, .monster = actor->monster};
    target.monster->healer = (qa_actor_id){0};
    qa_combat_state traits;
    if (!qa_combat_read_traits(c->game->services.combat, id, &traits, error))
        return !q2m_alive(c) || !q2m_alive(&target);
    if (!q2m_alive(c) || !q2m_alive(&target))
        return true;
    traits.can_take_damage = true;
    if (!qa_combat_set_traits(c->game->services.combat, id, &traits, error))
        return !q2m_alive(c) || !q2m_alive(&target);
    if (!q2m_alive(c) || !q2m_alive(&target))
        return true;
    target.monster->can_take_damage = true;
    target.monster->resurrecting = false;
    return heal_effects(&target, error);
}

static bool restore_enemy(q2m_context *c, bool *restored, qa_error *error) {
    *restored = false;
    qa_actor_id previous = c->monster->old_enemy;
    float previous_health;
    if (!health(c, previous, &previous_health, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (previous_health > 0) {
        enemy(c, c->monster->old_enemy);
        *restored = true;
        return q2m_hunt_target(c, !rerelease(c), error);
    }
    enemy(c, (qa_actor_id){0});
    c->monster->goal = c->monster->old_enemy = (qa_actor_id){0};
    c->actor->physics.goal = (qa_actor_id){0};
    if (!q2m_find_target(c, restored, error))
        return false;
    if (!q2m_alive(c) || *restored)
        return true;
    c->monster->pause_ns = UINT64_C(100000000) * Q2M_SECOND;
    return q2m_set_move(c, c->monster->definition->stand_move,
                        rerelease(c) || c->monster->definition->species == Q2M_FIXBOT, error);
}

static bool cleanup(q2m_context *c, bool change_frame, qa_error *error) {
    if (!q2m_medic_cleanup_patient(c, c->monster->enemy, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (rerelease(c)) {
        bool restored;
        if (!restore_enemy(c, &restored, error))
            return false;
        if (!q2m_alive(c) || !restored)
            return true;
    }
    if (change_frame)
        c->monster->next_frame = MEDIC_RETRACT;
    return true;
}

bool q2m_medic_abort(q2m_context *c, bool change_frame, bool gib, bool mark,
                    qa_error *error) {
    qa_actor_id id = c->monster->enemy;
    if (!q2m_medic_cleanup_patient(c, id, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (!rerelease(c) && change_frame)
        c->monster->next_frame = MEDIC_RETRACT;
    id = c->monster->enemy;
    q2_actor *actor = native_actor(c->game, id);
    bool present = actor != NULL;
    q2m_context target = {.game = c->game, .actor = actor,
                          .monster = actor ? actor->monster : NULL};
    if (present && mark) {
        q2_actor *previous = native_actor(c->game, target.monster->bad_medic[0]);
        unsigned index = previous && medic_species(previous->monster) ? 1 : 0;
        target.monster->bad_medic[index] = c->actor->id;
    }
    if (present && gib) {
        float amount = target.monster->gib_health == 0 ? 500 : -target.monster->gib_health;
        if (!qa_world_body_read(c->game->services.world, id, &target.body, error))
            return !q2m_alive(c) || !q2m_alive(&target);
        if (!q2m_alive(c) || !q2m_alive(&target))
            return true;
        qa_attack attack = {.attacker = c->actor->id, .inflictor = c->actor->id,
                            .weapon_provider = c->game->options.owner,
                            .cause = qa_q2_damage_cause(c->game->options.edition,
                                                       c->game->options.product, 0, 0)};
        if (!q2_damage(c->game, &attack, id, amount, 0, (qa_vec3){0},
                        target.body.origin, qa_v3(0, 0, 1), false, error))
            return false;
        if (!q2m_alive(c))
            return true;
    }
    if (rerelease(c)) {
        if (!cleanup(c, change_frame, error))
            return false;
        if (!q2m_alive(c))
            return true;
    } else {
        qa_actor_id previous = c->monster->old_enemy;
        enemy(c, q2_actor_live(c->game, previous) ? previous : (qa_actor_id){0});
    }
    c->monster->medic = false;
    c->monster->resurrect_target = (qa_actor_id){0};
    c->monster->medic_tries = 0;
    return true;
}

bool q2m_medic_acquire(q2m_context *c, bool preserve_enemy, bool *acquired,
                      qa_error *error) {
    *acquired = false;
    if (!medic_species(c->monster) || c->monster->medic || c->monster->dead ||
        (rerelease(c) && c->monster->react_ns > c->game->now_ns))
        return true;
    bool source_rogue = rogue(c);
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &c->body, error))
        return !q2m_alive(c);
    if (!q2m_alive(c))
        return true;
    float radius = source_rogue && c->monster->stand_ground ? 400 : 1024;
    q2_trace_frame *nearby = q2_nearby(c->game, c->body.origin, radius, error);
    if (!nearby)
        return false;
    qa_actor_id best = {0};
    float best_health = -FLT_MAX;
    bool result = true;
    for (size_t i = 0; i < nearby->snapshot.count && q2m_alive(c); ++i) {
        qa_actor_id id = nearby->snapshot.ids[i];
        if (qa_actor_id_equal(id, c->actor->id))
            continue;
        q2_actor *actor = native_actor(c->game, id);
        if (!actor)
            continue;
        q2m_context candidate = {.game = c->game, .actor = actor,
                                 .monster = actor->monster};
        struct qa_q2_monster *m = candidate.monster;
        if (m->good_guy || !m->corpse || m->gibbed)
            continue;
        if (!source_rogue) {
            if ((candidate.actor->entity && candidate.actor->entity->owner.registry) ||
                m->healer.registry)
                continue;
        } else {
            if (qa_actor_id_equal(m->bad_medic[0], c->actor->id) ||
                qa_actor_id_equal(m->bad_medic[1], c->actor->id))
                continue;
            q2_actor *healer = native_actor(c->game, m->healer);
            if (healer) {
                float healer_health;
                if (!health(c, healer->id, &healer_health, error)) {
                    result = false;
                    break;
                }
                if (!q2m_alive(c) || !q2m_alive(&candidate))
                    continue;
                if (healer_health > 0 && q2_actor_live(c->game, healer->id) &&
                    native_actor(c->game, healer->id) == healer && healer->monster->medic)
                    continue;
            }
        }
        float candidate_health;
        if (!health(c, id, &candidate_health, error)) {
            result = false;
            break;
        }
        if (!q2m_alive(c) || !q2m_alive(&candidate) || candidate_health > 0)
            continue;
        if (m->corpse_phase != Q2M_CORPSE_IDLE &&
            (!source_rogue ||
             (rerelease(c) ? m->corpse_phase != Q2M_CORPSE_DEAD_THINK
                           : m->corpse_phase != Q2M_CORPSE_FLIES_ON &&
                             m->corpse_phase != Q2M_CORPSE_FLIES_OFF)))
            continue;
        bool visible;
        if (!q2m_visible(c, id, &visible, error)) {
            result = false;
            break;
        }
        if (!q2m_alive(c) || !q2m_alive(&candidate) || !visible)
            continue;
        if (source_rogue) {
            if (!qa_world_body_read(c->game->services.world, c->actor->id,
                                    &c->body, error)) {
                result = !q2m_alive(c);
                break;
            }
            if (!q2m_alive(c) || !q2m_alive(&candidate))
                continue;
            if (!qa_world_body_read(c->game->services.world, id,
                                    &candidate.body, error)) {
                if (!q2m_alive(c))
                    break;
                if (!q2m_alive(&candidate))
                    continue;
                result = false;
                break;
            }
            if (!q2m_alive(c) || !q2m_alive(&candidate) ||
                qa_vec_length(qa_vec_sub(c->body.origin, candidate.body.origin)) <= 32)
                continue;
        }
        if (candidate.monster->max_health > best_health) {
            best = id;
            best_health = candidate.monster->max_health;
        }
    }
    nearby->active = false;
    if (!result || !q2m_alive(c))
        return result;
    q2_actor *target = native_actor(c->game, best);
    if (!target)
        return true;
    if (preserve_enemy || source_rogue)
        c->monster->old_enemy = c->monster->enemy;
    enemy(c, best);
    c->monster->resurrect_target = best;
    c->monster->medic = true;
    if (source_rogue) {
        target->monster->healer = c->actor->id;
        c->monster->timestamp_ns = q2m_after(c->game->now_ns, 10);
    } else if (target->entity) {
        target->entity->owner = c->actor->id;
    } else {
        target->monster->healer = c->actor->id;
    }
    *acquired = true;
    return q2m_found_target(c, best, error);
}

static bool sound(q2m_context *c, const char *normal, const char *commander,
                   int channel, float attenuation, qa_error *error) {
    return q2m_sound(c, rogue(c) && c->combat.mass != 400 ? commander : normal,
                     channel, attenuation, error);
}

static void clear_targets(q2m_context *c, q2m_context *target) {
    struct qa_q2_monster *m = target->monster;
    m->spawnflags = 0;
    m->combat_target = 0;
    qa_q2_entity_state *entity = target->actor->entity;
    if (entity) {
        entity->spawnflags = 0;
        entity->target = entity->targetname = 0;
        for (size_t i = 0; i < entity->field_count; ++i) {
            const char *key = qa_strings_cstr(
                qa_session_strings(c->game->services.session), entity->fields[i].key);
            if (key && (!strcmp(key, "combattarget") || !strcmp(key, "deathtarget") ||
                        (rerelease(c) && (!strcmp(key, "healthtarget") ||
                                          !strcmp(key, "itemtarget")))))
                entity->fields[i].value = 0;
        }
        if (target->actor->entity_targets)
            qa_targets_changed(target->actor->entity_targets);
    }
    if (!rogue(c))
        return;
    m->ignore_shots = m->do_not_count = m->good_guy = m->target_anger = false;
    m->brutal = m->medic = m->resurrecting = m->stand_ground = false;
    m->temporary_stand_ground = m->hold_frame = m->ducked = m->dodging = false;
    m->charging = m->manual_steering = m->combat_point = m->lost_sight = false;
    m->pursue_next = m->pursue_temporary = m->pursuit_last_seen = false;
    m->sound_target.present = false;
    if (!rerelease(c))
        m->spawned_by = Q2M_SPAWN_NONE;
}

static bool source_world(qa_q2_game *game, const qa_trace_result *trace) {
    return trace->hit == QA_TRACE_HIT_NONE || trace->hit == QA_TRACE_HIT_WORLD ||
           (trace->hit == QA_TRACE_HIT_ACTOR && game->services.physics &&
            qa_actor_id_equal(trace->actor, game->services.physics->world_actor));
}

static bool idle_without_enemy(q2m_context *c, qa_error *error) {
    enemy(c, (qa_actor_id){0});
    bool found;
    if (!q2m_find_target(c, &found, error))
        return false;
    if (!q2m_alive(c) || found)
        return true;
    c->monster->pause_ns = q2m_after(c->game->now_ns, 100000000);
    return q2m_set_move(c, c->monster->definition->stand_move, false, error);
}

static bool revive(q2m_context *c, q2m_context *target, qa_error *error) {
    clear_targets(c, target);
    qa_actor_id target_id = target->actor->id;
    if (rogue(c)) {
        target->monster->healer = c->actor->id;
        qa_bounds expanded = target->body.bounds;
        expanded.maxs.z += 48;
        qa_trace_query query = {.start = target->body.origin,
                                 .end = target->body.origin,
                                 .shape = {.kind = QA_SHAPE_BOX, .bounds = expanded},
                                 .pass_actor = target_id,
                                 .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = Q2M_MONSTER_MASK |
                                     (rerelease(c) ? Q2_PLAYER_CONTENTS : 0);
        qa_trace_result trace;
        if (!qa_world_trace(c->game->services.world, &query, &trace, error))
            return false;
        if (!q2m_alive(c) || !q2m_alive(target))
            return true;
        if (trace.start_solid || trace.all_solid || !source_world(c->game, &trace))
            return q2m_medic_abort(c, true, true, false, error);
        target->monster->do_not_count = true;
    } else if (target->actor->entity) {
        target->actor->entity->owner = c->actor->id;
    }
    if (!q2m_revive(target, error))
        return false;
    if (!q2m_alive(c) || !q2m_alive(target))
        return true;
    if (!rogue(c) && target->actor->entity)
        target->actor->entity->owner = (qa_actor_id){0};
    target->monster->start_due_ns = c->game->now_ns;
    bool handled;
    if (!q2m_lifecycle_tick(target, &handled, error))
        return false;
    if (!q2m_alive(c) || !q2m_alive(target))
        return true;
    if (!rogue(c)) {
        target->monster->resurrecting = true;
        target->monster->healer = (qa_actor_id){0};
        qa_actor_id old_enemy = c->monster->old_enemy;
        bool is_player = player(c, old_enemy);
        if (!q2m_alive(c) || !q2m_alive(target))
            return true;
        if (is_player) {
            old_enemy = c->monster->old_enemy;
            enemy(target, old_enemy);
            return q2m_found_target(target, old_enemy, error);
        }
        return true;
    }
    target->monster->resurrecting = false;
    target->monster->ignore_shots = target->monster->do_not_count = true;
    target->actor->extra_effects &= ~UINT64_C(0x4000);
    target->monster->healer = (qa_actor_id){0};
    qa_actor_id previous = c->monster->old_enemy;
    float previous_health;
    if (!health(c, previous, &previous_health, error))
        return false;
    if (!q2m_alive(c) || !q2m_alive(target))
        return true;
    if (previous_health > 0) {
        previous = c->monster->old_enemy;
        enemy(target, previous);
        if (!q2m_found_target(target, previous, error))
            return false;
    } else {
        if (!idle_without_enemy(target, error))
            return false;
        if (!q2m_alive(c))
            return true;
        c->monster->old_enemy = (qa_actor_id){0};
        if (!idle_without_enemy(c, error))
            return false;
    }
    return !q2m_alive(c) || !rerelease(c) || cleanup(c, false, error);
}

static bool cable(q2m_context *c, qa_error *error) {
    qa_actor_id target_id = c->monster->enemy;
    if (rerelease(c)) {
        q2_actor *actor = q2_actor_live(c->game, target_id) &&
                                  target_id.slot < c->game->capacity
                              ? c->game->actors[target_id.slot] : NULL;
        if (!actor || !qa_actor_id_equal(actor->id, target_id) ||
            (actor->extra_effects & 2u))
            return q2m_medic_abort(c, false, false, false, error);
        if (player(c, target_id))
            return true;
    }
    if (!q2m_alive(c))
        return true;
    q2_actor *actor = native_actor(c->game, target_id);
    if (!actor)
        return !rogue(c) || q2m_medic_abort(c, !rerelease(c), false, false, error);
    q2m_context target = {.game = c->game, .actor = actor, .monster = actor->monster};
    if (rogue(c)) {
        if (target.actor->extra_effects & 2u)
            return q2m_medic_abort(c, !rerelease(c), false, false, error);
        float target_health;
        if (!health(c, target_id, &target_health, error))
            return false;
        if (!q2m_alive(c) || !q2m_alive(&target))
            return true;
        if (target_health > 0)
            return q2m_medic_abort(c, !rerelease(c), false, false, error);
    }
    int frame = c->monster->frame;
    unsigned offset = (unsigned)(frame - MEDIC_CABLE_FIRST);
    if (offset >= sizeof(cable_offsets) / sizeof(*cable_offsets)) {
        qa_error_set(error, QA_ERROR_FORMAT, c->actor->id.slot,
                      "Medic cable callback outside its authored frames");
        return false;
    }
    qa_body_state geometry;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &geometry, error))
        return !q2m_alive(c);
    if (!q2m_alive(c) || !q2m_alive(&target))
        return true;
    qa_vec3 angles = geometry.angles;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &c->body, error))
        return !q2m_alive(c);
    if (!q2m_alive(c) || !q2m_alive(&target))
        return true;
    q2m_context projection = *c;
    projection.body.angles = angles;
    qa_vec3 start = q2m_project_offset(&projection, cable_offsets[offset]);
    if (!qa_world_body_read(c->game->services.world, target_id, &target.body, error))
        return !q2m_alive(c) || !q2m_alive(&target);
    if (!q2m_alive(c) || !q2m_alive(&target))
        return true;
    qa_vec3 direction = qa_vec_sub(start, target.body.origin);
    float distance = qa_vec_length(direction);
    if (!rogue(c)) {
        float pitch = q2m_vector_angles(direction).x;
        if (pitch < -180)
            pitch += 360;
        if (distance > 256 || fabsf(pitch) > 45)
            return true;
    } else if (distance < 32) {
        return q2m_medic_abort(c, true, true, false, error);
    }
    qa_trace_query query = {.start = start, .end = target.body.origin,
                             .pass_actor = c->actor->id,
                             .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = rogue(c) ? 3u : Q2M_ATTACK_MASK;
    qa_trace_result trace;
    if (!qa_world_trace(c->game->services.world, &query, &trace, error))
        return false;
    if (!q2m_alive(c) || !q2m_alive(&target))
        return true;
    if (trace.fraction != 1 &&
        (trace.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(trace.actor, target_id))) {
        if (!rogue(c))
            return true;
        if (source_world(c->game, &trace)) {
            if (c->monster->medic_tries > 1)
                return q2m_medic_abort(c, true, false, true, error);
            ++c->monster->medic_tries;
            return cleanup(c, true, error);
        }
        return q2m_medic_abort(c, true, false, false, error);
    }
    frame = c->monster->frame;
    if (frame == MEDIC_CONTACT) {
        if (rogue(c)) {
            if (!qa_combat_read(c->game->services.combat, c->actor->id, &c->combat, error))
                return !q2m_alive(c);
            if (!q2m_alive(c) || !q2m_alive(&target))
                return true;
        }
        const char *path = rogue(c) && c->combat.mass != 400
                               ? "medic_commander/medatck3a.wav" : "medic/medatck3.wav";
        if (rogue(c)) {
            if (!qa_world_body_read(c->game->services.world, target_id, &target.body, error))
                return !q2m_alive(c) || !q2m_alive(&target);
            if (!q2m_alive(c) || !q2m_alive(&target))
                return true;
        }
        if (!q2m_sound(&target, path, 0, 1, error))
            return false;
        if (!q2m_alive(c) || !q2m_alive(&target))
            return true;
        target.monster->resurrecting = true;
        if (rogue(c)) {
            target.monster->can_take_damage = false;
            if (!heal_effects(&target, error))
                return !q2m_alive(c) || !q2m_alive(&target);
            if (!q2m_alive(c) || !q2m_alive(&target))
                return true;
            qa_combat_state traits;
            if (!qa_combat_read_traits(c->game->services.combat, target_id, &traits, error))
                return !q2m_alive(c) || !q2m_alive(&target);
            if (!q2m_alive(c) || !q2m_alive(&target))
                return true;
            traits.can_take_damage = false;
            if (!qa_combat_set_traits(c->game->services.combat, target_id, &traits, error))
                return !q2m_alive(c) || !q2m_alive(&target);
            if (!q2m_alive(c) || !q2m_alive(&target))
                return true;
        }
    } else if (frame == MEDIC_REVIVE) {
        if (!revive(c, &target, error))
            return false;
        if (!q2m_alive(c) || rerelease(c))
            return true;
    } else if (frame == MEDIC_HEAL_SOUND) {
        if (rogue(c)) {
            if (!qa_combat_read(c->game->services.combat, c->actor->id, &c->combat, error))
                return !q2m_alive(c);
            if (!q2m_alive(c) || !q2m_alive(&target))
                return true;
        }
        if (!qa_world_body_read(c->game->services.world, c->actor->id, &c->body, error))
            return !q2m_alive(c);
        if (!q2m_alive(c) || !q2m_alive(&target))
            return true;
        if (!sound(c, "medic/medatck4.wav", "medic_commander/medatck4a.wav", 1, 1, error))
            return false;
    }
    if (!q2m_alive(c))
        return true;
    if (rogue(c))
        target_id = c->monster->enemy;
    if (!q2_actor_live(c->game, target_id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(c->game->services.world, target_id, &body, error))
        return !q2m_alive(c) || !q2_actor_live(c->game, target_id);
    if (!q2m_alive(c) || !q2_actor_live(c->game, target_id))
        return true;
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &c->body, error))
        return !q2m_alive(c);
    if (!q2m_alive(c) || !q2_actor_live(c->game, target_id))
        return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(c->body.angles, &forward, NULL, NULL);
    qa_vec3 end = body.origin;
    end.z += (body.bounds.mins.z + body.bounds.maxs.z) * .5f;
    return q2m_emit(c, QA_BUILTIN_BEAM, "q2:medic-cable", 0,
                     qa_vec_add(start, qa_vec_scale(forward, 8)), end, 1, error);
}

static void finish_dodge(q2m_context *c) {
    c->monster->dodging = false;
    if (rerelease(c) && c->monster->attack_state == Q2M_SLIDING)
        c->monster->attack_state = Q2M_STRAIGHT;
}

static bool target_distance(q2m_context *c, float *out, qa_error *error) {
    *out = INFINITY;
    qa_actor_id id = c->monster->enemy;
    if (!q2_actor_live(c->game, id))
        return true;
    qa_body_state body;
    qa_error observed = {0};
    if (!qa_world_body_read(c->game->services.world, id, &body, &observed)) {
        if (observed.code == QA_ERROR_NOT_FOUND || !q2_actor_live(c->game, id))
            return true;
        if (error)
            *error = observed;
        return false;
    }
    if (!q2m_alive(c) || !q2_actor_live(c->game, id))
        return true;
    if (!q2m_refresh(c, error))
        return !q2m_alive(c);
    if (!q2_actor_live(c->game, id))
        return true;
    *out = qa_vec_length(qa_vec_sub(body.origin, c->body.origin));
    return true;
}

bool q2m_medic_attack_move(q2m_context *c, float distance, const char **move,
                          qa_error *error) {
    struct qa_q2_monster *m = c->monster;
    if (!rogue(c)) {
        *move = m->medic ? "medic_move_attackCable" : "medic_move_attackBlaster";
        return true;
    }
    finish_dodge(c);
    if (m->source_blocked) {
        if (!q2m_set_move(c, "medic_move_callReinforcements", rerelease(c), error))
            return false;
        if (!q2m_alive(c))
            return true;
        m->source_blocked = false;
    }
    float roll = rerelease(c) ? q2_rerelease_float(c->game, 0, 1) : q2m_random(c->game);
    bool commander = c->combat.mass > 400;
    bool slots = rerelease(c) ? m->monster_slots > m->monster_used : m->monster_slots > 2;
    if (m->medic)
        *move = commander && roll > .8 && slots ? "medic_move_callReinforcements"
                                                 : "medic_move_attackCable";
    else
        *move = m->attack_state == Q2M_BLIND ||
                    (commander && roll > .2 &&
                     (rerelease(c) ? distance > 20 : distance >= 80) && slots)
                    ? "medic_move_callReinforcements" : "medic_move_attackBlaster";
    return true;
}

bool q2m_medic_check_attack(q2m_context *c, bool *handled, bool *selected, bool *started,
                           qa_error *error) {
    *handled = false;
    *selected = false;
    *started = false;
    if (!medic_species(c->monster))
        return true;
    struct qa_q2_monster *m = c->monster;
    if (m->medic) {
        *handled = true;
        float distance = INFINITY;
        if (rogue(c)) {
            if (!q2_actor_live(c->game, m->enemy))
                return q2m_medic_abort(c, true, false, false, error);
            if (m->timestamp_ns < c->game->now_ns) {
                if (!q2m_medic_abort(c, true, false, true, error))
                    return false;
                if (q2m_alive(c))
                    c->monster->timestamp_ns = 0;
                return true;
            }
            if (!target_distance(c, &distance, error))
                return false;
            if (!q2m_alive(c))
                return true;
            if (distance >= 410) {
                m->attack_state = Q2M_STRAIGHT;
                return true;
            }
        }
        const char *move = NULL;
        if (!q2m_medic_attack_move(c, distance, &move, error))
            return false;
        if (!q2m_alive(c))
            return true;
        *selected = true;
        *started = true;
        return q2m_set_move(c, move, rerelease(c), error);
    }
    if (!rogue(c))
        return true;
    qa_actor_id target = m->enemy;
    bool is_player = player(c, target);
    if (!q2m_alive(c))
        return true;
    if (is_player) {
        bool visible;
        if (!q2m_visible(c, target, &visible, error))
            return false;
        if (!q2m_alive(c))
            return true;
        bool slots = rerelease(c) ? m->monster_slots > m->monster_used : m->monster_slots > 2;
        if (!visible && slots) {
            m->attack_state = Q2M_BLIND;
            *handled = *selected = true;
            return true;
        }
    }
    if ((!rerelease(c) || m->monster_slots != 0) &&
        (rerelease(c) ? q2_rerelease_float(c->game, 0, 1) : q2m_random(c->game)) < .8) {
        bool slots;
        if (rerelease(c)) {
            int64_t remaining = m->monster_slots;
            if (!q2m_summon_subtract(&remaining, m->monster_used, error))
                return false;
            slots = (double)remaining > (double)m->monster_slots * .8;
        } else {
            slots = m->monster_slots > 5;
        }
        if (slots) {
            float distance;
            if (!target_distance(c, &distance, error))
                return false;
            if (!q2m_alive(c))
                return true;
            if (distance > 150) {
                m->source_blocked = true;
                m->attack_state = Q2M_MISSILE;
                *handled = *selected = true;
                return true;
            }
        }
    }
    if (m->stand_ground && (rerelease(c) || c->game->options.skill > 0)) {
        m->attack_state = Q2M_MISSILE;
        *handled = *selected = true;
    }
    return true;
}

bool q2m_medic_callback(q2m_context *c, const char *name, bool *handled,
                       qa_error *error) {
    *handled = false;
    if (!medic_species(c->monster))
        return true;
    *handled = true;
    if (!strcmp(name, "medic_cable_attack"))
        return cable(c, error);
    if (!strcmp(name, "medic_hook_launch"))
        return sound(c, "medic/medatck2.wav", "medic_commander/medatck2c.wav", 1, 1, error);
    if (!strcmp(name, "medic_hook_retract")) {
        bool ok = q2m_sound(c, "medic/medatck5.wav", 1, 1, error);
        if (!ok || !q2m_alive(c))
            return ok;
        if (!rogue(c)) {
            q2_actor *target = native_actor(c->game, c->monster->enemy);
            if (target)
                target->monster->resurrecting = false;
            return true;
        }
        c->monster->medic = false;
        c->monster->resurrect_target = (qa_actor_id){0};
        if (rerelease(c)) {
            bool restored;
            return restore_enemy(c, &restored, error);
        }
        if (q2_actor_live(c->game, c->monster->old_enemy)) {
            enemy(c, c->monster->old_enemy);
            return true;
        }
        c->monster->old_enemy = (qa_actor_id){0};
        return idle_without_enemy(c, error);
    }
    if (!strcmp(name, "medic_continue")) {
        bool visible;
        if (!q2m_visible(c, c->monster->enemy, &visible, error))
            return false;
        return !q2m_alive(c) || !visible ||
               (rerelease(c) ? q2_rerelease_float(c->game, 0, 1) : q2m_random(c->game)) > .95 ||
               q2m_set_move(c, "medic_move_attackHyperBlaster", false, error);
    }
  if (!strcmp(name, "medic_quick_attack") && rerelease(c)) {
        if (q2_rerelease_float(c->game, 0, 1) >= .5f)
            return true;
        if (!q2m_set_move(c, "medic_move_attackHyperBlaster", false, error))
            return false;
        if (q2m_alive(c))
            c->monster->next_frame = 192;
    return true;
  }
  if (!strcmp(name, "medic_shrink") && rerelease(c)) {
    if (!qa_world_body_read(c->game->services.world, c->actor->id, &c->body, error))
      return false;
    if (!q2m_alive(c))
      return true;
    c->body.bounds.maxs.z = -2;
    c->actor->physics.solid = QA_PHYSICS_CORPSE;
    if (!q2m_write_body(c, false, error))
      return false;
    if (!q2m_alive(c))
      return true;
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                     .shape = QA_SHAPE_BOX,
                                     .contents = (int32_t)UINT32_C(0x04000000),
                                     .role = QA_COLLISION_SOLID,
                                     .dead_monster = true};
    if (!qa_world_set_collision(c->game->services.world, c->actor->id, &collision, error))
      return false;
    return !q2m_alive(c) || q2m_link(c, error);
  }
    bool run = !strcmp(name, "medic_run"), search = !strcmp(name, "medic_search");
    if (run || search || !strcmp(name, "medic_idle")) {
        if (run && (rogue(c) || c->game->options.product != QA_Q2_XATRIX))
            finish_dodge(c);
        if (!run && !sound(c, search ? "medic/medsrch1.wav" : "medic/idle.wav",
                           search ? "medic_commander/medsrch.wav" : "medic_commander/medidle.wav",
                           2, 2, error))
            return false;
        if (!q2m_alive(c))
            return true;
        bool acquire = run ? !c->monster->medic
                           : !c->monster->old_enemy.registry || (!rogue(c) && !search);
        bool acquired = false;
        if (acquire && !q2m_medic_acquire(c, run || search || rogue(c), &acquired, error))
            return false;
        return !q2m_alive(c) || !run || acquired ||
               q2m_set_move(c, c->monster->stand_ground ? "medic_move_stand" : "medic_move_run",
                             rerelease(c), error);
    }
    *handled = false;
    return true;
}

bool q2m_medic_died(q2m_context *c, qa_error *error) {
    if (!rogue(c)) {
        if (!medic_species(c->monster))
            return true;
        q2_actor *target = native_actor(c->game, c->monster->enemy);
        if (target && target->entity && qa_actor_id_equal(target->entity->owner, c->actor->id))
            target->entity->owner = (qa_actor_id){0};
        if (target && qa_actor_id_equal(target->monster->healer, c->actor->id))
            target->monster->healer = (qa_actor_id){0};
        return true;
    }
    if (!c->monster->medic)
        return true;
    if (!q2m_medic_cleanup_patient(c, c->monster->enemy, error))
        return false;
    if (q2m_alive(c)) {
        c->monster->medic = false;
        c->monster->resurrect_target = (qa_actor_id){0};
    }
    return true;
}
