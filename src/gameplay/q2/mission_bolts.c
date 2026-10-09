#include "internal.h"

typedef struct green_radius_context {
    qa_q2_game *game;
    qa_actor_id direct;
    bool hurt;
} green_radius_context;
static bool green_prepare(void *context, qa_damage_request *request, bool *allowed, qa_error *e) {
    green_radius_context *radius = context;
    *allowed = !(radius->hurt && qa_actor_id_equal(radius->direct, request->target));
    request->amount = truncf(request->amount);
    request->knockback = truncf(request->knockback);
    return q2_prepare_damage(radius->game, request, allowed, e);
}
bool q2_green_touch(qa_q2_game *g, q2_actor *a, const qa_touch_contact *contact, qa_error *e) {
    qa_actor_id id = a->id;
    q2_projectile p = a->projectile;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !q2_projectile_noise(g, &p, body.origin, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    bool hurt = q2_target_damageable(g, contact->other);
    if (p.damage >= 5) {
        green_radius_context context = {.game = g, .direct = contact->other, .hurt = hurt};
        qa_builtin_radius radius = {
            .attack =
                q2_projectile_attack(g, id, &p, 0, g->options.edition == QA_Q2_RERELEASE ? 5u : 1u),
            .origin = body.origin,
            .radius = p.radius,
            .damage = p.damage * (g->options.edition == QA_Q2_RERELEASE ? 2 : 3),
            .distance_scale = 0.5f,
            .self_scale = 1,
            .knockback_scale = 1,
            .ignore = qa_actor_reference_resolve(qa_session_actors(g->services.session), p.owner),
            .visibility_pass = id,
            .distance = QA_RADIUS_CENTER,
            .check_visibility = true,
            .trace = qa_collision_default_policy(QA_COLLISION_Q2),
            .context = &context,
            .prepare = green_prepare};
        radius.trace.contents_mask = qa_collision_contents_mask(1, QA_COLLISION_Q2);
        size_t count;
        if (!q2_radius_damage(g, &radius, &count, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
    }
    if (hurt && q2_actor_live(g, contact->other)) {
        bool player;
        if (!q2_target_creature(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p.owner), NULL, &player, e))
            return false;
        qa_attack attack = q2_projectile_attack(g, id, &p, player ? 50 : 43, 4);
        if (!q2_damage(g, &attack, contact->other, p.damage, 1, body.velocity, body.origin,
                       contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0), false, e))
            return false;
    } else if (!hurt &&
               !q2_projectile_event(g, id, QA_BUILTIN_IMPACT, "q2:blaster2", 0, body.origin,
                                    contact->has_plane ? contact->plane.normal : qa_v3(0, 0, 0), e))
        return false;
    return !q2_actor_live(g, id) || qa_session_release(g->services.session, id, e);
}
static bool fire_actor_bolt(qa_q2_game *g, qa_actor_id source, qa_actor_id credited_owner,
                        qa_vec3 start, qa_vec3 direction, float damage, float speed,
                        uint64_t effects, int means_of_death, q2_projectile_kind kind, qa_error *e) {
    q2_actor *a = q2_actor_get(g, source, true, e);
    if (a == NULL)
        return false;
    q2_weapon_call call = {.game = g,
                           .actor = a,
                           .state = &a->weapon,
                           .now_ns = g->now_ns,
                           .frame_ns = g->frame_ns,
                           .rerelease = g->options.edition == QA_Q2_RERELEASE,
                           .input = {.gravity = 800, .players_collide = true},
                           .has_attack_owner = true,
                           .attack_owner = credited_owner,
                           .has_projectile_effects = true,
                           .projectile_effects = effects};
    return q2_projectile_spawn(&call, kind,
                               start, direction, damage, 1, speed, kind == Q2_GREEN_BOLT ? 128 : 0, 0, 2,
                               means_of_death, 0, false, false, e);
}
bool q2_fire_actor_bolt(qa_q2_game *g, qa_actor_id source, qa_actor_id credited_owner,
                        qa_vec3 start, qa_vec3 direction, float damage, float speed,
                        uint64_t effects, int means_of_death, bool green, qa_error *e) {
    return fire_actor_bolt(g, source, credited_owner, start, direction, damage, speed, effects,
                            means_of_death, green ? Q2_GREEN_BOLT
                              : means_of_death == 58 ? Q2_BLUE_BOLT : Q2_BOLT, e);
}
bool q2_fire_actor_loogie(qa_q2_game *g, qa_actor_id source, qa_vec3 start,
                          qa_vec3 direction, qa_error *e) {
    return fire_actor_bolt(g, source, source, start, direction, 5, 550, 8, 38, Q2_LOOGIE, e);
}
bool q2_fire_actor_rocket(qa_q2_game *g, qa_actor_id source, qa_actor_id credited_owner,
                          qa_vec3 start, qa_vec3 direction, float damage, float speed,
                          float splash_damage, float radius, int direct_mod, int splash_mod,
                          qa_actor_id *out, qa_error *e) {
    if (out) *out = (qa_actor_id){0};
    if (!qa_vec_finite(start) || !qa_vec_finite(direction) || !isfinite(damage) ||
        !isfinite(speed) || speed <= 0 || !isfinite(splash_damage) || !isfinite(radius) ||
        radius < 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 actor rocket");
        return false;
    }
    q2_actor *a = q2_actor_get(g, source, true, e);
    if (a == NULL)
        return false;
    q2_weapon_call call = {.game = g,
                           .actor = a,
                           .state = &a->weapon,
                           .now_ns = g->now_ns,
                           .frame_ns = g->frame_ns,
                           .definition = &g->definitions[QA_Q2_ROCKETLAUNCHER],
                           .rerelease = g->options.edition == QA_Q2_RERELEASE,
                           .input = {.gravity = 800, .players_collide = true},
                           .has_attack_owner = true,
                           .attack_owner = credited_owner,
                           .spawned_projectile = out};
    return q2_projectile_spawn(&call, Q2_ROCKET, start, direction, damage, 0, speed, radius,
                               splash_damage, 8000 / speed, direct_mod, splash_mod, false, false,
                               e);
}
static bool heat_sight(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, qa_actor_id target,
                       const qa_body_state *body, bool *out, qa_error *e) {
    qa_builtin_actor_traits traits = {.view_height = 22};
    if (g->services.actor_traits != NULL)
        g->services.actor_traits(g->services.context, target, &traits);
    qa_trace_query query = {.start = origin,
                            .end = qa_vec_add(body->origin, qa_v3(0, 0, traits.view_height)),
                            .pass_actor = id,
                            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    query.policy.contents_mask = qa_collision_contents_mask(25, QA_COLLISION_Q2);
    qa_trace_result trace;
    if (!qa_world_trace(g->services.world, &query, &trace, e))
        return false;
    *out = trace.fraction == 1;
    return true;
}
static bool heat_rocket_run(qa_q2_game *g, q2_actor *a, qa_builtin_actor_snapshot *snapshot,
                            qa_error *e) {
    q2_projectile *p = &a->projectile;
    if (g->now_ns < p->next_ns)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 forward;
    qa_builtin_angle_vectors(body.angles, &forward, NULL, NULL);
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    float nearest = 0, old_dot = 1;
    qa_actor_id best = {0};
    qa_vec3 target_origin = {0};
    if (!qa_builtin_nearby(&g->services, body.origin, 1024, snapshot, e))
        return false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id target = snapshot->ids[i];
        if (!q2_actor_live(g, target))
            continue;
        bool player;
        if (!q2_target_creature(g, target, NULL, &player, e))
            return false;
        if (!player || qa_actor_id_equal(target, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner)))
            continue;
        qa_combat_state health;
        qa_error ignored;
        if (!qa_combat_read(g->services.combat, target, &health, &ignored) || health.health <= 0)
            continue;
        qa_body_state other;
        if (!qa_world_body_read(g->services.world, target, &other, e))
            return false;
        qa_vec3 delta = qa_vec_sub(other.origin, body.origin);
        float distance = qa_vec_length(delta);
        bool seen;
        if (!heat_sight(g, a->id, body.origin, target, &other, &seen, e))
            return false;
        if (!seen)
            continue;
        float alignment = qa_vec_dot(qa_vec_normalize(delta), forward);
        if (rr) {
            alignment = -alignment;
            if (alignment >= old_dot)
                continue;
            if (best.registry == 0 || alignment < old_dot || distance < nearest) {
                best = target;
                old_dot = alignment;
                nearest = distance;
                target_origin = other.origin;
            }
        } else if (alignment > 0.3f && (best.registry == 0 || truncf(distance) < nearest)) {
            best = target;
            nearest = truncf(distance);
            target_origin = other.origin;
        }
    }
    bool controlled = g->services.controls_trajectory != NULL &&
                      g->services.controls_trajectory(g->services.context, a->id);
    qa_vec3 movedir = p->movedir;
    if (rr && best.registry == 0)
        p->enemy = (qa_actor_reference){0};
    if (best.registry != 0) {
        if (!controlled) {
            qa_vec3 desired = qa_vec_normalize(qa_vec_sub(target_origin, body.origin));
            if (rr) {
                float alignment = qa_vec_dot(movedir, desired);
                if (alignment < 0.45f && alignment > -0.45f)
                    desired = qa_vec_scale(desired, -1);
                float cosine = fmaxf(-1, fminf(1, qa_vec_dot(movedir, desired))),
                      angle = acosf(cosine), sine = sinf(angle);
                float from = fabsf(cosine) > 0.9995f ? 1 - p->turn_fraction
                                                     : sinf((1 - p->turn_fraction) * angle) / sine;
                float to = fabsf(cosine) > 0.9995f ? p->turn_fraction
                                                   : sinf(p->turn_fraction * angle) / sine;
                movedir = qa_vec_normalize(
                    qa_vec_add(qa_vec_scale(movedir, from), qa_vec_scale(desired, to)));
            } else
                movedir = desired;
            body.angles =
                qa_v3(-atan2f(movedir.z, hypotf(movedir.x, movedir.y)) * 57.29577951308232f,
                      atan2f(movedir.y, movedir.x) * 57.29577951308232f, 0);
        }
        if (rr && qa_actor_reference_present(p->enemy) == 0) {
            qa_builtin_event event = {.kind = QA_BUILTIN_SOUND,
                                      .family = QA_GAME_Q2,
                                      .provider = g->options.owner,
                                      .actor = a->id,
                                      .time_ns = g->now_ns,
                                      .origin = body.origin,
                                      .channel = 1,
                                      .volume = 1,
                                      .attenuation = 0.25f};
            if (!qa_builtin_resource(&g->services, "weapons/railgr1a.wav", &event.resource, e) ||
                !qa_builtin_emit(&g->services, &event, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
        if (!rr || !qa_actor_reference_present(p->enemy)) {
            p->enemy = qa_actor_reference_from_actor(qa_session_actors(g->services.session), g->options.owner, best);
        }
    }
    if (!controlled && (rr || best.registry != 0)) {
        p->movedir = movedir;
        body.velocity = qa_vec_scale(movedir, rr ? p->speed : 500);
        if (!qa_world_body_write(g->services.world, a->id, &body, e))
            return false;
    }
    p->next_ns = q2_deadline(g->now_ns, rr ? g->frame_ns : 100 * Q2_MS);
    return true;
}
bool q2_heat_rocket_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (g->now_ns < a->projectile.next_ns)
        return true;
    qa_builtin_snapshot_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch == NULL)
        return false;
    bool ok = heat_rocket_run(g, a, &scratch->snapshot, e);
    qa_builtin_snapshot_release(scratch);
    return ok;
}
