#include "qa/q2_sound.h"
#include "internal.h"
#include "reinforcements.h"

static bool physics_changed(void *context, qa_actor_id id, qa_error *error) {
    qa_q2_game *g = context;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->monster || !a->monster->definition ||
        a->monster->definition->species != Q2M_STALKER)
        return true;
    q2m_context c = {.game = g, .actor = a, .monster = a->monster};
    if (!q2m_refresh(&c, error)) return false;
    if (!q2m_alive(&c) || qa_actor_reference_present(c.body.ground) || a->physics.gravity_direction.z <= 0)
        return true;
    a->physics.gravity_direction.z = -1;
    c.body.angles.z += 180;
    if (c.body.angles.z > 360) c.body.angles.z -= 360;
    return q2m_write_body(&c, true, error);
}
bool qa_q2_monster_physics_changed(qa_q2_game *g, qa_actor_id id, bool was_grounded,
                                   qa_error *error) {
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 physics change owner");
        return false;
    }
    if (!was_grounded || g->options.edition != QA_Q2_RERELEASE || !q2_actor_live(g, id))
        return true;
    return qa_q2_run_actor(g, id, physics_changed, g, error);
}

static bool world_hit(const q2m_context *c, const qa_trace_result *trace) {
    return trace->hit != QA_TRACE_HIT_ACTOR ||
           (c->game->services.physics &&
            qa_actor_id_equal(trace->actor, c->game->services.physics->world_actor));
}
static bool trace(q2m_context *c, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                   uint32_t mask, qa_trace_result *out, qa_error *error) {
    qa_trace_query query = {.start = start, .end = end, .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_GAME_Q2)};
    query.policy.contents_mask = qa_collision_contents_mask(mask, QA_GAME_Q2);
    if (c->game->options.edition == QA_Q2_RERELEASE)
        query.policy.contents_mask = qa_collision_bits_union(query.policy.contents_mask, qa_collision_contents_mask(UINT32_C(0x40000000), QA_GAME_Q2));
    if (bounds)
        query.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX, .bounds = *bounds};
    return qa_world_trace(c->game->services.world, &query, out, error);
}
static bool contents(q2m_context *c, qa_vec3 point, uint32_t *out, qa_error *error) {
    qa_point_query query = {.point = point, .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_GAME_Q2)};
    qa_point_contents result;
    if (!qa_world_point_contents(c->game->services.world, &query, &result, error))
        return false;
    *out = (uint32_t)qa_collision_point_contents_export(result.contents, QA_GAME_Q2, result.q1_opaque_token);
    return true;
}
static bool transition(q2m_context *c, bool *allowed, qa_error *error) {
    *allowed = false;
    bool ceiling = c->actor->physics.gravity_direction.z > 0;
    float margin = ceiling ? c->body.bounds.mins.z - 8 : c->body.bounds.maxs.z + 8;
    qa_vec3 end = c->body.origin;
    end.z += ceiling ? -384 : c->monster->spawned_by == Q2M_SPAWN_WIDOW ? 256 : 180;
    qa_trace_result result;
    if (!trace(c, c->body.origin, end, &c->body.bounds, Q2M_MONSTER_MASK, &result, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (result.fraction == 1 || !(qa_collision_contents_export(result.contents, QA_GAME_Q2, result.q1_opaque_token) & 1) || !world_hit(c, &result)) {
        float normal = result.contact ? result.contact_plane.normal.z : 0;
        if (ceiling ? normal < .9f : normal > -.9f)
            return true;
    }
    float height = result.end.z + margin;
    const float xs[4] = {c->body.bounds.mins.x, c->body.bounds.maxs.x,
                         c->body.bounds.maxs.x, c->body.bounds.mins.x};
    const float ys[4] = {c->body.bounds.mins.y, c->body.bounds.mins.y,
                         c->body.bounds.maxs.y, c->body.bounds.maxs.y};
    for (size_t i = 0; i < 4; ++i) {
        qa_vec3 start = qa_v3(c->body.origin.x + xs[i] - (xs[i] < 0 ? 1 : -1),
                              c->body.origin.y + ys[i] - (ys[i] < 0 ? 1 : -1),
                              c->body.origin.z);
        end = start;
        end.z = height;
        if (!trace(c, start, end, NULL, Q2M_MONSTER_MASK, &result, error))
            return false;
        if (!q2m_alive(c) || result.fraction == 1 || !(qa_collision_contents_export(result.contents, QA_GAME_Q2, result.q1_opaque_token) & 1) ||
            !world_hit(c, &result) || fabsf(truncf(height - result.end.z)) > 8)
            return true;
    }
    *allowed = true;
    return true;
}
static bool reactivate(q2m_context *c, qa_error *error) {
    c->monster->stand_ground = false;
    return q2m_set_move(c, Q2M_MOVE_stalker_move_false_death_end, true, error);
}
bool q2m_stalker_pain(q2m_context *c, bool reacts, bool chainfist, qa_error *error) {
    struct qa_q2_monster *m = c->monster;
    bool rerelease = c->game->options.edition == QA_Q2_RERELEASE;
    if (rerelease)
        m->skin = c->combat.health < m->max_health * .5f ? 1 : 0;
    else if (c->combat.health < truncf(m->max_health * .5f))
        m->skin = 1;
    if ((!rerelease && c->game->options.skill == 3) || !qa_actor_reference_present(c->body.ground))
        return true;
    if ((m->move->id == Q2M_MOVE_stalker_move_false_death_end) ||
        (m->move->id == Q2M_MOVE_stalker_move_false_death_start))
        return true;
    if ((m->move->id == Q2M_MOVE_stalker_move_false_death))
        return reactivate(c, error);
    float threshold = rerelease ? m->max_health * .25f : truncf(m->max_health * .25f);
    if (c->combat.health > 0 && c->combat.health < threshold &&
        q2m_random(c->game) < (rerelease ? .3f : .2f * (float)c->game->options.skill)) {
        bool allowed = true;
        if (c->actor->physics.gravity_direction.z > 0 && !transition(c, &allowed, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (allowed) {
            c->body.angles.z = 0;
            c->actor->physics.gravity_direction = qa_v3(0, 0, -1);
            m->stand_ground = true;
            if (!q2m_set_move(c, Q2M_MOVE_stalker_move_false_death_start, true, error))
                return false;
            return !q2m_alive(c) || q2m_write_body(c, true, error);
        }
    }
    if (c->game->now_ns < m->pain_ns)
        return true;
    m->pain_ns = q2m_after(c->game->now_ns, 3);
    if (rerelease && !q2m_sound(c, QA_Q2_SOUND_STALKER_PAIN, 2, 1, error))
        return false;
    if (!q2m_alive(c) || (m->pending_damage <= 10 && (!rerelease || !chainfist)))
        return true;
    if (q2m_random(c->game) < .5f) {
        if (!q2m_set_move(c, Q2M_MOVE_stalker_move_jump_straightup, true, error))
            return false;
    } else if ((!rerelease || reacts) && !q2m_set_move(c, Q2M_MOVE_stalker_move_pain, true, error))
        return false;
    return rerelease || !q2m_alive(c) || q2m_sound(c, QA_Q2_SOUND_STALKER_PAIN, 1, 1, error);
}
static bool jump_straight(q2m_context *c, qa_error *error) {
    if (c->monster->dead)
        return true;
    bool ceiling = c->actor->physics.gravity_direction.z > 0, allowed;
    if (ceiling) {
        if (!transition(c, &allowed, error))
            return false;
        if (!allowed || !q2m_alive(c))
            return true;
        c->actor->physics.gravity_direction.z = -1;
        c->body.angles.z += 180;
        if (c->body.angles.z > 360)
            c->body.angles.z -= 360;
        c->body.ground = (qa_actor_reference){0};
        return q2m_write_body(c, true, error);
    }
    if (!qa_actor_reference_present(c->body.ground))
        return true;
    c->body.velocity.x += q2m_random(c->game) * 10 - 5;
    c->body.velocity.y += q2m_random(c->game) * 10 - 5;
    c->body.velocity.z -= 400 * c->actor->physics.gravity_direction.z;
    if (!q2m_write_body(c, false, error) || !transition(c, &allowed, error))
        return false;
    if (!allowed || !q2m_alive(c))
        return true;
    c->actor->physics.gravity_direction.z = 1;
    c->body.angles.z = 180;
    c->body.ground = (qa_actor_reference){0};
    return q2m_write_body(c, true, error);
}
static bool pounce(q2m_context *c, const qa_body_state *enemy, qa_error *error) {
    if (c->actor->physics.gravity_direction.z > 0 || !qa_actor_reference_present(enemy->ground))
        return true;
    uint32_t value;
    if (!contents(c, enemy->origin, &value, error))
        return false;
    if (!q2m_alive(c) || (value & 56))
        return true;
    q2_actor *native = q2_actor_get(c->game, c->monster->enemy, false, NULL);
    if (native && native->monster) {
        if (native->monster->water_level > 0)
            return true;
    } else {
        qa_vec3 feet = enemy->origin;
        feet.z += enemy->bounds.mins.z + 1;
        if (!contents(c, feet, &value, error))
            return false;
        if (!q2m_alive(c) || (value & 56))
            return true;
    }
    const float xs[4] = {enemy->bounds.mins.x, enemy->bounds.maxs.x,
                         enemy->bounds.maxs.x, enemy->bounds.mins.x};
    const float ys[4] = {enemy->bounds.mins.y, enemy->bounds.mins.y,
                         enemy->bounds.maxs.y, enemy->bounds.maxs.y};
    for (size_t i = 0; i < 4; ++i) {
        if (!contents(c, qa_v3(xs[i], ys[i], enemy->bounds.mins.z - .25f), &value, error))
            return false;
        if (!q2m_alive(c) || !(value & 3))
            return true;
    }
    qa_vec3 delta = qa_vec_sub(enemy->origin, c->body.origin);
    qa_vec3 angles = q2m_vector_angles(delta);
    if (!isfinite(angles.y) ||
        (c->game->options.edition == QA_Q2_RERELEASE
             ? fabsf(angles.y - c->body.angles.y)
             : fabsf(truncf(angles.y - c->body.angles.y))) > 45)
        return true;
    c->monster->ideal_yaw = angles.y;
    if (!q2m_change_yaw(c, error))
        return false;
    if (!q2m_alive(c) || qa_vec_length(delta) > 450)
        return true;
    if (c->game->options.edition == QA_Q2_RERELEASE) {
        static const float pitches[] = {-80, -70, -60, -50, -40, -30, -20, -10, -5};
        float gravity = c->game->services.physics ? c->game->services.physics->gravity : 800;
        for (float speed = 400.1f; speed <= 800; speed += 200) {
            float best = FLT_MAX, chosen = 0;
            for (size_t i = 0; i < sizeof(pitches) / sizeof(pitches[0]); ++i) {
                qa_vec3 direction;
                qa_builtin_angle_vectors(qa_v3(pitches[i], angles.y, angles.z), &direction, NULL, NULL);
                qa_vec3 velocity = qa_vec_scale(direction, speed), origin = c->body.origin;
                for (unsigned step = 0; step < 30; ++step) {
                    velocity.z -= gravity * .1f;
                    qa_trace_query query = {.start = origin,
                        .end = qa_vec_add(origin, qa_vec_scale(velocity, .1f)),
                        .policy = qa_collision_default_policy(QA_GAME_Q2)};
                    query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x46000003), QA_GAME_Q2);
                    qa_trace_result result;
                    if (!qa_world_trace(c->game->services.world, &query, &result, error))
                        return false;
                    if (!q2m_alive(c))
                        return true;
                    origin = result.end;
                    if (result.fraction >= 1)
                        continue;
                    if (result.has_surface && (qa_collision_surface_export(result.surface_flags, QA_GAME_Q2) & 4))
                        break;
                    qa_vec3 normal = result.contact ? result.contact_plane.normal : qa_v3(0, 0, 0);
                    origin = qa_vec_add(origin, normal);
                    qa_vec3 difference = qa_vec_sub(origin, enemy->origin);
                    float distance = qa_vec_dot(difference, difference);
                    bool target = result.hit == QA_TRACE_HIT_ACTOR &&
                                  qa_actor_id_equal(result.actor, c->monster->enemy);
                    if (!target && result.hit == QA_TRACE_HIT_ACTOR && c->game->services.actor_traits) {
                        qa_builtin_actor_traits traits = {0};
                        c->game->services.actor_traits(c->game->services.context, result.actor, &traits);
                        if (!q2m_alive(c))
                            return true;
                        target = traits.player;
                    }
                    if (target || (normal.z >= .7f && distance < 128 * 128 && distance < best)) {
                        best = distance;
                        chosen = pitches[i];
                    }
                    break;
                }
            }
            if (best != FLT_MAX) {
                qa_builtin_angle_vectors(qa_v3(chosen, angles.y, angles.z), &c->body.velocity, NULL, NULL);
                c->body.velocity = qa_vec_scale(c->body.velocity, speed);
                return q2m_write_body(c, true, error);
            }
        }
        return true;
    }
    bool high = delta.z >= 32;
    qa_vec3 target = enemy->origin;
    target.z += high ? 32 : 0;
    qa_trace_result result;
    if (!trace(c, c->body.origin, enemy->origin, NULL, Q2M_MONSTER_MASK, &result, error))
        return false;
    if (!q2m_alive(c))
        return true;
    if (result.fraction < 1 &&
        (result.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(result.actor, c->monster->enemy)))
        high = true;
    delta = qa_vec_sub(target, c->body.origin);
    float horizontal = hypotf(delta.x, delta.y), vertical = fabsf(delta.z);
    float distance = hypotf(horizontal, vertical);
    float angle = vertical == 0 ? 0 : atanf(vertical / horizontal) * (delta.z > 0 ? -1 : 1);
    float speed = 400.1f, low = NAN, upper = NAN;
    while (speed <= 800) {
        float cosine = cosf(angle);
        float first = asinf(distance * 800 * cosine * cosine / (speed * speed) - sinf(angle));
        low = (first - angle) / 2;
        upper = (3.14159265358979323846f - first - angle) / 2;
        if (!isnan(low) || !isnan(upper))
            break;
        speed += 200;
    }
    float chosen = !high && !isnan(low) ? low : upper;
    if (isnan(chosen))
        return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(c->body.angles, &forward, NULL, NULL);
    c->body.velocity = qa_vec_scale(qa_vec_normalize(forward), speed * cosf(chosen));
    float gravity = c->game->services.physics ? c->game->services.physics->gravity : 800;
    c->body.velocity.z = speed * sinf(chosen) + .5f * gravity * .1f;
    return q2m_write_body(c, true, error);
}
bool q2m_blocked_tesla(q2m_context *c, bool *accepted, qa_error *error) {
    *accepted = false;
    qa_actor_id target = c->monster->enemy;
    if (!q2_actor_live(c->game, target) || !c->game->services.actor_traits)
        return true;
    qa_builtin_actor_traits traits = {0};
    bool described = c->game->services.actor_traits(c->game->services.context, target, &traits);
    if (!q2m_alive(c) || !q2_actor_live(c->game, target) ||
        !qa_actor_id_equal(c->monster->enemy, target) || !described || !traits.player ||
        q2m_random(c->game) < .25f + .05f * (float)c->game->options.skill)
        return true;
    bool visible;
    if (!q2m_visible(c, target, &visible, error))
        return false;
    if (!q2m_alive(c) || !q2_actor_live(c->game, target) ||
        !qa_actor_id_equal(c->monster->enemy, target) || !visible)
        return true;
    if (traits.classname != c->game->runtime_names[Q2_NAME_TESLA])
        return true;
    c->monster->source_blocked = true;
    if (!q2m_source_attack(c, false, error))
        return false;
    if (q2m_alive(c))
        c->monster->source_blocked = false;
    *accepted = true;
    return true;
}
bool q2m_blocked_platform(q2m_context *c, const qa_body_state *enemy, float distance,
                          bool *accepted, qa_error *error) {
    *accepted = false;
    float self_min = c->body.origin.z + c->body.bounds.mins.z;
    float self_max = c->body.origin.z + c->body.bounds.maxs.z;
    int position = enemy->origin.z + enemy->bounds.mins.z >= self_max ? 1
                 : enemy->origin.z + enemy->bounds.maxs.z <= self_min ? -1 : 0;
    if (!position)
        return true;
    q2_actor *platform = q2_actor_get(c->game, qa_actor_reference_resolve(qa_session_actors(c->game->services.session), c->body.ground), false, NULL);
    if (!platform || !platform->entity || platform->entity->kind != Q2E_PLAT) {
        qa_vec3 forward;
        qa_builtin_angle_vectors(c->body.angles, &forward, NULL, NULL);
        qa_vec3 start = qa_vec_add(c->body.origin, qa_vec_scale(forward, distance));
        qa_vec3 end = start;
        end.z -= 384;
        qa_trace_result result;
        if (!trace(c, start, end, NULL, Q2M_MONSTER_MASK, &result, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (result.fraction == 1 || result.all_solid || result.start_solid ||
            result.hit != QA_TRACE_HIT_ACTOR)
            return true;
        platform = q2_actor_get(c->game, result.actor, false, NULL);
    }
    if (!platform || !platform->entity || platform->entity->kind != Q2E_PLAT ||
        !platform->entity->usable || !platform->entity->mover)
        return true;
    bool aboard = qa_actor_id_equal(qa_actor_reference_resolve(qa_session_actors(c->game->services.session), c->body.ground), platform->id);
    int phase = platform->entity->mover->phase;
    if ((position > 0 && (aboard ? phase == 0 : phase == 2)) ||
        (position < 0 && (aboard ? phase == 2 : phase == 0))) {
        *accepted = true;
        return qa_q2_entity_use(c->game, platform->id, c->actor->id, c->actor->id, error);
    }
    return true;
}
static bool blocked_jump(q2m_context *c, const qa_body_state *enemy, bool *accepted,
                         qa_error *error) {
    *accepted = false;
    bool rerelease = c->game->options.edition == QA_Q2_RERELEASE;
    if (rerelease && ((c->monster->spawnflags & 16) || c->monster->jump_ns > c->game->now_ns))
        return true;
    float self_min = c->body.origin.z + c->body.bounds.mins.z;
    float enemy_min = enemy->origin.z + enemy->bounds.mins.z;
    float step_height = rerelease ? 18 : 16;
    int position = enemy_min > self_min + step_height ? 1
                 : enemy_min < self_min - step_height ? -1 : 0;
    if (!position)
        return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(c->body.angles, &forward, NULL, NULL);
    qa_vec3 ahead = qa_vec_add(c->body.origin, qa_vec_scale(forward, 48));
    qa_vec3 start = ahead, end = ahead;
    qa_trace_result result;
    if (position < 0) {
        if (!trace(c, c->body.origin, ahead, &c->body.bounds, Q2M_MONSTER_MASK, &result, error))
            return false;
        if (!q2m_alive(c) || result.fraction < 1)
            return true;
        end.z = (rerelease ? self_min : c->body.bounds.mins.z) - 257;
    } else {
        start.z = c->body.origin.z + c->body.bounds.maxs.z + 68;
    }
    if (!trace(c, start, end, NULL, Q2M_MONSTER_MASK | Q2M_WATER_MASK, &result, error))
        return false;
    if (!q2m_alive(c) || result.fraction == 1 || result.all_solid || result.start_solid)
        return true;
    if (rerelease && position < 0 && (qa_collision_contents_export(result.contents, QA_GAME_Q2, result.q1_opaque_token) & 32)) {
        qa_trace_result deep;
        if (!trace(c, result.end, end, NULL, Q2M_MONSTER_MASK, &deep, error))
            return false;
        if (!q2m_alive(c))
            return true;
        qa_vec3 water = deep.end;
        water.z += c->body.bounds.mins.z + 1;
        uint32_t value;
        if (!contents(c, water, &value, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (value & Q2M_WATER_MASK) {
            float sample = (c->monster->view_height - c->body.bounds.mins.z) * .5f;
            water.z += sample;
            if (!contents(c, water, &value, error))
                return false;
            if (!q2m_alive(c))
                return true;
            if (value & Q2M_WATER_MASK) {
                water.z += sample;
                if (!contents(c, water, &value, error))
                    return false;
                if (!q2m_alive(c) || (value & Q2M_WATER_MASK))
                    return true;
            }
        }
    }
    if (!((uint32_t)qa_collision_contents_export(result.contents, QA_GAME_Q2, result.q1_opaque_token) & (rerelease ? 35u : 3u)))
        return true;
    if (position < 0) {
        if (self_min - result.end.z < 24 || enemy_min - result.end.z > 32 ||
            !result.contact || result.contact_plane.normal.z < .9f)
            return true;
    } else {
        if (result.end.z - self_min > 68)
            return true;
        qa_trace_result wall;
        if (!trace(c, c->body.origin, qa_vec_add(c->body.origin, qa_vec_scale(forward, 64)),
                   NULL, Q2M_MONSTER_MASK, &wall, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (wall.fraction < 1 && !wall.all_solid && !wall.start_solid && wall.contact) {
            c->monster->ideal_yaw = q2m_vector_angles(wall.contact_plane.normal).y + 180;
            if (c->monster->ideal_yaw > 360)
                c->monster->ideal_yaw -= 360;
            if (!q2m_change_yaw(c, error))
                return false;
            if (!q2m_alive(c))
                return true;
        }
    }
    if (rerelease) {
        c->monster->jump_ns = q2m_after(c->game->now_ns, 3);
        c->monster->dodging = false;
        if (c->monster->attack_state == Q2M_SLIDING)
            c->monster->attack_state = Q2M_STRAIGHT;
    }
    *accepted = true;
    bool up = rerelease ? position > 0 : enemy->origin.z >= c->body.origin.z;
    return q2m_set_move(c, up ? Q2M_MOVE_stalker_move_jump_up : Q2M_MOVE_stalker_move_jump_down, true, error);
}
bool q2m_stalker_blocked(q2m_context *c, float distance, bool *accepted, qa_error *error) {
    *accepted = false;
    qa_actor_id target = c->monster->enemy;
    if (!q2_actor_live(c->game, target))
        return true;
    qa_body_state enemy;
    qa_combat_state combat;
    if (!qa_world_body_read(c->game->services.world, target, &enemy, error))
        return !q2m_alive(c) || !q2_actor_live(c->game, target);
    if (!q2m_alive(c) || !q2_actor_live(c->game, target))
        return true;
    if (!qa_combat_read(c->game->services.combat, target, &combat, error))
        return !q2m_alive(c) || !q2_actor_live(c->game, target);
    if (!q2m_alive(c) || !q2_actor_live(c->game, target) || combat.health <= 0)
        return true;
    bool rerelease = c->game->options.edition == QA_Q2_RERELEASE;
    if (!rerelease) {
        if (!q2m_blocked_tesla(c, accepted, error))
            return false;
        if (!q2m_alive(c) || *accepted)
            return true;
    }
    if (c->actor->physics.gravity_direction.z > 0) {
        bool allowed;
        if (!transition(c, &allowed, error))
            return false;
        if (!q2m_alive(c) || !allowed)
            return true;
        c->actor->physics.gravity_direction.z = -1;
        c->body.angles.z += 180;
        if (c->body.angles.z > 360)
            c->body.angles.z -= 360;
        c->body.ground = (qa_actor_reference){0};
        *accepted = true;
        return q2m_write_body(c, true, error);
    }
    if (!rerelease) {
        bool visible;
        if (!q2m_visible(c, target, &visible, error))
            return false;
        if (!q2m_alive(c))
            return true;
        if (visible) {
            *accepted = true;
            return pounce(c, &enemy, error);
        }
    }
    if (!blocked_jump(c, &enemy, accepted, error))
        return false;
    if (!q2m_alive(c) || *accepted)
        return true;
    if (!q2m_blocked_platform(c, &enemy, distance, accepted, error))
        return false;
    if (!q2m_alive(c) || *accepted || !rerelease)
        return true;
    bool visible;
    if (!q2m_visible(c, target, &visible, error))
        return false;
    if (q2m_alive(c) && visible && q2m_random(c->game) < .1f) {
        *accepted = true;
        return pounce(c, &enemy, error);
    }
    return true;
}
static bool shoot(q2m_context *c, qa_error *error) {
    qa_actor_id target = c->monster->enemy;
    if (!q2_actor_live(c->game, target))
        return true;
    qa_combat_state combat;
    qa_body_state enemy;
    if (!qa_combat_read(c->game->services.combat, target, &combat, error))
        return !q2m_alive(c) || !q2_actor_live(c->game, target);
    if (!q2m_alive(c) || !q2_actor_live(c->game, target))
        return true;
    if (!qa_world_body_read(c->game->services.world, target, &enemy, error))
        return !q2m_alive(c) || !q2_actor_live(c->game, target);
    if (!q2m_alive(c) || combat.health <= 0)
        return true;
    if (qa_actor_reference_present(c->body.ground) && q2m_random(c->game) < .33f) {
        bool okay = qa_vec_length(qa_vec_sub(enemy.origin, c->body.origin)) > 256 ||
                            q2m_random(c->game) < .5f
                        ? pounce(c, &enemy, error) : jump_straight(c, error);
        if (!okay)
            return false;
        if (!q2m_alive(c))
            return true;
    }
    qa_vec3 start = q2m_project_offset(c, qa_v3(24, 0, 6));
    qa_vec3 direction = qa_vec_sub(enemy.origin, start), end = enemy.origin;
    bool rerelease = c->game->options.edition == QA_Q2_RERELEASE;
    float chance = rerelease ? .3f : .2f + .1f * (float)c->game->options.skill;
    if (q2m_random(c->game) < chance) {
        if (rerelease) {
            bool available;
            if (!q2m_predict_from(c, start, 1000, true, 0, &end, &direction, &available, error))
                return false;
            if (!q2m_alive(c) || !available)
                return true;
        } else {
            end = qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, qa_vec_length(direction) / 1000));
            direction = qa_vec_sub(end, start);
        }
    }
    qa_trace_result result;
    if (!trace(c, start, end, NULL, rerelease ? Q2_PROJECTILE_MASK : Q2_SHOT_MASK, &result, error))
        return false;
    if (!q2m_alive(c) || (!world_hit(c, &result) &&
        (result.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(result.actor, target))))
        return true;
    q2m_fire_spec spec = q2m_fire_default(c, Q2M_ATTACK_GREEN_BOLT, rerelease ? 5 : 15,
                                         144, start, direction);
    spec.speed = 800;
    spec.has_projectile_effects = true;
    spec.projectile_effects = 8;
    return q2m_fire(c, &spec, error);
}
bool q2m_stalker_callback(q2m_context *c, q2m_callback_id name, bool *handled, qa_error *error) {
    *handled = c->monster->definition->species == Q2M_STALKER;
    if (!*handled)
        return true;
    if (name == Q2M_CALLBACK_stalker_footstep)
        return !qa_actor_reference_present(c->body.ground) ||
               q2m_emit(c, QA_BUILTIN_EFFECT, "q2:entity-event", 8, c->body.origin,
                          qa_v3(0, 0, 0), 0, error);
    if (name == Q2M_CALLBACK_stalker_heal) {
        int skill = c->game->options.skill;
        float health = c->combat.health + (skill == 2 ? 2 : skill == 3 ? 3 : 1);
        bool rerelease = c->game->options.edition == QA_Q2_RERELEASE;
        if (rerelease)
            c->monster->skin = health < c->monster->max_health * .5f ? 1 : 0;
        else if (health > truncf(c->monster->max_health * .5f))
            c->monster->skin = 0;
        bool full = health >= c->monster->max_health;
        if (!qa_combat_set_health(c->game->services.combat, c->actor->id,
                                   full ? c->monster->max_health : health, error))
            return false;
        return !q2m_alive(c) || !full || reactivate(c, error);
    }
    if (name == Q2M_CALLBACK_stalker_shoot_attack)
        return shoot(c, error);
    if (name == Q2M_CALLBACK_stalker_shoot_attack2) {
        float chance = c->game->options.edition == QA_Q2_RERELEASE
                           ? .5f : .4f + .1f * (float)c->game->options.skill;
        return q2m_random(c->game) >= chance || shoot(c, error);
    }
    if (name == Q2M_CALLBACK_stalker_jump_straightup)
        return jump_straight(c, error);
    if ((name == Q2M_CALLBACK_stalker_jump_up) || (name == Q2M_CALLBACK_stalker_jump_down)) {
        bool up = (name == Q2M_CALLBACK_stalker_jump_up);
        qa_vec3 forward, vertical;
        qa_builtin_angle_vectors(c->body.angles, &forward, NULL, &vertical);
        if (c->game->options.edition == QA_Q2_CLASSIC) {
            c->monster->timestamp_ns = c->game->now_ns;
        }
        c->body.velocity = qa_vec_add(c->body.velocity,
            qa_vec_add(qa_vec_scale(forward, up ? 200 : 100), qa_vec_scale(vertical, up ? 450 : 300)));
        return q2m_write_body(c, true, error);
    }
    if (name == Q2M_CALLBACK_stalker_jump_wait_land) {
        float chance = c->game->options.edition == QA_Q2_RERELEASE
                           ? .4f : .3f + .1f * (float)c->game->options.skill;
        if (q2m_random(c->game) < chance &&
            c->game->now_ns >= c->monster->attack_ns) {
            c->monster->attack_ns = q2m_after(c->game->now_ns, .3);
            if (!shoot(c, error))
                return false;
            if (!q2m_alive(c))
                return true;
        }
        bool finished = qa_actor_reference_present(c->body.ground);
        if (!finished) {
            c->actor->physics.gravity_scale = 1.3f;
            if (c->game->options.edition == QA_Q2_CLASSIC)
                finished = c->game->now_ns > q2m_after(c->monster->timestamp_ns, 3);
            else {
                qa_vec3 forward;
                qa_builtin_angle_vectors(c->body.angles, &forward, NULL, NULL);
                qa_vec3 projected = qa_v3(c->body.velocity.x * forward.x,
                                          c->body.velocity.y * forward.y,
                                          c->body.velocity.z * forward.z);
                if (qa_vec_length(projected) < 150) {
                    float z = c->body.velocity.z;
                    c->body.velocity = qa_vec_scale(forward, 150);
                    c->body.velocity.z = z;
                    if (!q2m_write_body(c, true, error))
                        return false;
                    if (!q2m_alive(c))
                        return true;
                }
                finished = c->monster->jump_ns < c->game->now_ns;
            }
        }
        if (finished)
            c->actor->physics.gravity_scale = 1;
        c->monster->next_frame = c->monster->frame + (finished ? 1 : 0);
        return true;
    }
    *handled = false;
    return true;
}
