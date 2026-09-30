#include "internal.h"

static bool world_hit(const q2m_context *c, const qa_trace_result *trace) {
    return trace->hit != QA_TRACE_HIT_ACTOR ||
           (c->game->services.physics &&
            qa_actor_id_equal(trace->actor, c->game->services.physics->world_actor));
}
static bool trace(q2m_context *c, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                   uint32_t mask, qa_trace_result *out, qa_error *error) {
    qa_trace_query query = {.start = start, .end = end, .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = mask;
    if (c->game->options.edition == QA_Q2_RERELEASE)
        query.policy.contents_mask |= UINT32_C(0x40000000);
    if (bounds)
        query.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX, .bounds = *bounds};
    return qa_world_trace(c->game->services.world, &query, out, error);
}
static bool contents(q2m_context *c, qa_vec3 point, uint32_t *out, qa_error *error) {
    qa_point_query query = {.point = point, .pass_actor = c->actor->id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_point_contents result;
    if (!qa_world_point_contents(c->game->services.world, &query, &result, error))
        return false;
    *out = (uint32_t)result.contents;
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
    if (result.fraction == 1 || !(result.contents & 1) || !world_hit(c, &result)) {
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
        if (!q2m_alive(c) || result.fraction == 1 || !(result.contents & 1) ||
            !world_hit(c, &result) || fabsf(truncf(height - result.end.z)) > 8)
            return true;
    }
    *allowed = true;
    return true;
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
        c->body.ground = (qa_actor_id){0};
        return q2m_write_body(c, true, error);
    }
    if (!c->body.ground.registry)
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
    c->body.ground = (qa_actor_id){0};
    return q2m_write_body(c, true, error);
}
static bool pounce(q2m_context *c, const qa_body_state *enemy, qa_error *error) {
    if (c->actor->physics.gravity_direction.z > 0 || !enemy->ground.registry)
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
    if (fabsf(truncf(angles.y - c->body.angles.y)) > 45)
        return true;
    c->monster->ideal_yaw = angles.y;
    if (!q2m_change_yaw(c, error))
        return false;
    if (!q2m_alive(c) || qa_vec_length(delta) > 450)
        return true;
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
static bool shoot(q2m_context *c, qa_error *error) {
    qa_actor_id target = c->monster->enemy;
    if (!q2_actor_live(c->game, target))
        return true;
    qa_combat_state combat;
    qa_body_state enemy;
    if (!qa_combat_read(c->game->services.combat, target, &combat, error) ||
        !qa_world_body_read(c->game->services.world, target, &enemy, error))
        return false;
    if (!q2m_alive(c) || combat.health <= 0)
        return true;
    if (c->body.ground.registry && q2m_random(c->game) < .33f) {
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
    if (q2m_random(c->game) < .2f + .1f * (float)c->game->options.skill) {
        end = qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, qa_vec_length(direction) / 1000));
        direction = qa_vec_sub(end, start);
    }
    qa_trace_result result;
    if (!trace(c, start, end, NULL, UINT32_C(0x0200001b), &result, error))
        return false;
    if (!q2m_alive(c) || (!world_hit(c, &result) &&
        (result.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(result.actor, target))))
        return true;
    q2m_fire_spec spec = q2m_fire_default(c, Q2M_ATTACK_GREEN_BOLT, 15, 144, start, direction);
    spec.speed = 800;
    spec.has_projectile_effects = true;
    spec.projectile_effects = 8;
    return q2m_fire(c, &spec, error);
}
bool q2m_stalker_callback(q2m_context *c, const char *name, bool *handled, qa_error *error) {
    *handled = c->monster->definition->species == Q2M_STALKER;
    if (!*handled)
        return true;
    if (!strcmp(name, "stalker_shoot_attack"))
        return shoot(c, error);
    if (!strcmp(name, "stalker_shoot_attack2"))
        return q2m_random(c->game) >= .4f + .1f * (float)c->game->options.skill || shoot(c, error);
    if (!strcmp(name, "stalker_jump_straightup"))
        return jump_straight(c, error);
    if (!strcmp(name, "stalker_jump_up") || !strcmp(name, "stalker_jump_down")) {
        bool up = !strcmp(name, "stalker_jump_up");
        qa_vec3 forward, vertical;
        qa_builtin_angle_vectors(c->body.angles, &forward, NULL, &vertical);
        c->monster->timestamp_ns = c->game->now_ns;
        c->body.velocity = qa_vec_add(c->body.velocity,
            qa_vec_add(qa_vec_scale(forward, up ? 200 : 100), qa_vec_scale(vertical, up ? 450 : 300)));
        return q2m_write_body(c, true, error);
    }
    if (!strcmp(name, "stalker_jump_wait_land")) {
        if (q2m_random(c->game) < .3f + .1f * (float)c->game->options.skill &&
            c->game->now_ns >= c->monster->attack_ns) {
            c->monster->attack_ns = q2m_after(c->game->now_ns, .3);
            if (!shoot(c, error))
                return false;
            if (!q2m_alive(c))
                return true;
        }
        bool finished = c->body.ground.registry != 0;
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
