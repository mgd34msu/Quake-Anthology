#include "qa/q2_sound.h"
#include "internal.h"
#include "qa/game_q2_source.h"
#include "qa/text.h"

static qa_vec3 angles_for(qa_vec3 direction) {
    return qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                 atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
}
static bool show_frame(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION,
                              .family = QA_GAME_Q2,
                              .provider = g->options.owner,
                              .actor = a->id,
                              .time_ns = g->now_ns,
                              .resource = a->projectile.model,
                              .frame = a->projectile.frame};
    return qa_builtin_emit(&g->services, &event, e);
}
static qa_vec3 growth_angles(qa_q2_game *g) {
    qa_vec3 angles;
    for (unsigned i = 0; i < 2; ++i) {
        angles.x = floorf(q2_random(g) * 360);
        angles.y = floorf(q2_random(g) * 360);
        angles.z = floorf(q2_random(g) * 360);
    }
    return angles;
}
static bool gib_contents(qa_q2_game *g, qa_vec3 point, int32_t *contents, qa_error *e) {
    qa_point_query query = {.point = point, .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
    qa_point_contents result;
    if (!qa_world_point_contents(g->services.world, &query, &result, e))
        return false;
    *contents = qa_collision_point_contents_export(result.contents, QA_COLLISION_Q2, result.q1_opaque_token);
    return true;
}
bool q2_spawn_gib(qa_q2_game *g, qa_actor_id source, const char *model, float damage,
                  uint32_t flags, int skin, float scale, qa_error *e) {
    bool head = (flags & Q2_GIB_HEAD) != 0, metal = (flags & Q2_GIB_METALLIC) != 0,
         rr = g->options.edition == QA_Q2_RERELEASE,
         debris = rr && (flags & Q2_GIB_DEBRIS) != 0;
    qa_body_state source_body;
    if (!qa_world_body_read(g->services.world, source, &source_body, e))
        return false;
    qa_actor_id id = source;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "gib", &definition, e))
        return false;
    if (!head) {
        qa_builtin_spawn spawn = {.owner = g->options.owner, .definition = definition};
        if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
            return false;
    }
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    qa_vec3 half = qa_vec_scale(qa_vec_sub(source_body.bounds.maxs, source_body.bounds.mins), 0.5f);
    qa_vec3 center = qa_vec_add(qa_vec_add(source_body.origin, source_body.bounds.mins), half);
    if (!rr)
        center = qa_vec_add(center, qa_v3(-1, -1, -1));
    qa_vec3 origin = source_body.origin;
    if (!head || rr) {
        bool solid = false;
        for (int tries = 0; tries < (rr ? 3 : 1); ++tries) {
            qa_vec3 offset;
            offset.x = q2_crandom(g) * half.x;
            offset.y = q2_crandom(g) * half.y;
            offset.z = q2_crandom(g) * half.z;
            origin = qa_vec_add(center, offset);
            if (rr) {
                int32_t contents;
                if (!gib_contents(g, origin, &contents, e)) return false;
                solid = ((uint32_t)contents & 3u) != 0;
            }
            if (!solid)
                break;
        }
        if (rr && solid && !head)
            return qa_session_release(g->services.session, id, e);
    }
    qa_vec3 impulse;
    impulse.x = 100 * q2_crandom(g);
    impulse.y = 100 * q2_crandom(g);
    impulse.z = debris ? 100 + 100 * q2_crandom(g) : 200 + 100 * q2_random(g);
    float factor = debris ? (float)qa_source_float_to_i32(damage) :
        (damage < 50 ? 0.7f : 1.2f) * (metal ? 1 : 0.5f);
    qa_vec3 velocity = qa_vec_add(source_body.velocity, qa_vec_scale(impulse, factor));
    if (!debris) {
        velocity.x = fmaxf(-300, fminf(300, velocity.x));
        velocity.y = fmaxf(-300, fminf(300, velocity.y));
        velocity.z = fmaxf(200, fminf(500, velocity.z));
    }
    qa_vec3 angular = a->physics.angular_velocity;
    if (head && !rr)
        angular.y = q2_crandom(g) * 600;
    else {
        angular.x = q2_random(g) * 600;
        angular.y = q2_random(g) * 600;
        angular.z = q2_random(g) * 600;
    }
    qa_vec3 angles = head ? source_body.angles : qa_v3(0, 0, 0);
    if (rr) {
        angles.x = q2_random(g) * 359;
        angles.y = q2_random(g) * 359;
        angles.z = q2_random(g) * 359;
    }
    qa_combat_state combat;
    qa_error ignored;
    if (qa_combat_read_traits(g->services.combat, id, &combat, &ignored)) {
        combat.can_take_damage = true;
        combat.no_knockback = true;
        if (!qa_combat_set_traits(g->services.combat, id, &combat, e))
            return false;
    } else {
        combat = (qa_combat_state){.can_take_damage = true, .no_knockback = true};
        if (!qa_combat_create_actor(g->services.combat, id, &combat, e))
            return false;
    }
    uint64_t lifetime;
    if (rr) {
        float instagib;
        if (!qa_q2_source_value(g, QA_Q2_SOURCE_INSTAGIB, 0, &instagib, e)) return false;
        bool instant = qa_source_float_to_i32(instagib) != 0;
        lifetime = (uint64_t)((instant ? 1000u : 10000u) +
            q2_random_bounded(g, instant ? 4001u : 10001u)) * Q2_MS;
    } else lifetime = (uint64_t)((10 + q2_random(g) * 10) * 1e9);
    a->projectile = (q2_projectile){
        .kind = Q2_GIB,
        .classname = rr || !head ? definition : 0,
        .owner = qa_actor_reference_from_actor(qa_session_actors(g->services.session), g->options.owner, source),
        .effects = debris ? 0 : 2,
        .render_flags = rr ? ((1u << 24) | (1u << 13) | (debris ? 0 : 1u << 15)) : 0,
        .skin = (flags & Q2_GIB_SKINNED) != 0 ? skin : 0,
        .scale = rr ? scale : 1,
        .gib_flags = flags,
        .visible = true,
        .expire_ns = q2_deadline(g->now_ns, lifetime)};
    a->character_no_damage_effects = rr;
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = rr;
    a->physics.motion = metal ? QA_PHYSICS_BOUNCE : QA_PHYSICS_TOSS;
    a->physics.solid = QA_PHYSICS_NOT_SOLID;
    a->physics.clip_mask = 3;
    a->physics.angular_velocity = angular;
    if (rr) a->physics.flags = QA_PHYSICS_DEAD | ((flags & Q2_GIB_UPRIGHT) ? QA_PHYSICS_ALWAYS_TOUCH : 0);
    qa_body_state body = {.origin = origin, .velocity = velocity, .angles = angles};
    if (!qa_world_body_write(g->services.world, id, &body, e) ||
        !qa_world_set_collision(g->services.world, id, NULL, e) ||
        !qa_world_link(g->services.world, id, NULL, e)) return false;
    if (rr) {
        if (!gib_contents(g, origin, &a->physics.water_type, e)) return false;
        a->physics.water_level = ((uint32_t)a->physics.water_type & (8u | 16u | 32u)) ? 1 : 0;
    }
    return q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, model, 0, origin, angles, e);
}
static bool debris(qa_q2_game *g, qa_body_state body, const char *model, float speed,
                   qa_vec3 origin, qa_error *e) {
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "debris", &definition, e))
        return false;
    qa_vec3 impulse;
    impulse.x = 100 * q2_crandom(g);
    impulse.y = 100 * q2_crandom(g);
    impulse.z = 100 + 100 * q2_crandom(g);
    body.origin = origin;
    body.velocity = qa_vec_add(body.velocity, qa_vec_scale(impulse, speed));
    body.bounds = (qa_bounds){0};
    body.angles = qa_v3(0, 0, 0);
    body.ground = (qa_actor_reference){0};
    qa_combat_state combat = {.can_take_damage = true};
    qa_builtin_spawn spawn = {.owner = g->options.owner,
                              .definition = definition,
                              .body = body,
                              .combat = &combat,
                              .link = true};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    a->physics.motion = QA_PHYSICS_BOUNCE;
    a->physics.solid = QA_PHYSICS_NOT_SOLID;
    a->physics.angular_velocity.x = q2_random(g) * 600;
    a->physics.angular_velocity.y = q2_random(g) * 600;
    a->physics.angular_velocity.z = q2_random(g) * 600;
    a->projectile = (q2_projectile){
        .kind = Q2_DEBRIS,
        .visible = true,
        .scale = 1,
        .expire_ns = q2_deadline(g->now_ns, (uint64_t)((5 + q2_random(g) * 5) * 1e9))};
    return q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, model, 0, body.origin, body.angles, e);
}
bool q2_spawn_debris(qa_q2_game *g, qa_actor_id source, qa_error *e) {
    qa_body_state body;
    return qa_world_body_read(g->services.world, source, &body, e) &&
           debris(g, body, "models/objects/debris2/tris.md2", 2, body.origin, e);
}
bool q2_spawn_model_debris(qa_q2_game *g, qa_actor_id source, const char *model, float speed,
                           qa_vec3 origin, qa_error *e) {
    if (g == NULL || model == NULL || !isfinite(speed) || !qa_vec_finite(origin)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 scenery debris");
        return false;
    }
    qa_body_state body;
    return qa_world_body_read(g->services.world, source, &body, e) &&
           debris(g, body, model, speed, origin, e);
}
static bool gib_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *a = q2_actor_get(g, contact->self, false, e);
    if (a == NULL)
        return false;
    q2_projectile *p = &a->projectile;
    if (p->kind != Q2_GIB || p->armed)
        return true;
    if ((p->gib_flags & Q2_GIB_WIDOW_SIZED) != 0)
        return q2_widow_gib_touch(g, contact, e);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (g->options.edition == QA_Q2_RERELEASE && (p->gib_flags & Q2_GIB_WIDOW) == 0) {
        if ((p->gib_flags & Q2_GIB_UPRIGHT) != 0 && contact->has_plane &&
            contact->plane.normal.z > 0.7f) {
            body.angles.x = fmaxf(-5, fminf(5, body.angles.x));
            body.angles.z = fmaxf(-5, fminf(5, body.angles.z));
            return qa_world_body_write(g->services.world, a->id, &body, e);
        }
        return true;
    }
    if ((p->gib_flags & Q2_GIB_METALLIC) != 0 || !qa_actor_reference_present(body.ground))
        return true;
    p->armed = true;
    if (!contact->has_plane)
        return true;
    if (!q2_projectile_event(g, a->id, QA_BUILTIN_SOUND, QA_Q2_SOUND_MISC_FHIT3, 2, body.origin,
                             body.origin, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_vec3 right;
    qa_builtin_angle_vectors(angles_for(contact->plane.normal), NULL, &right, NULL);
    body.angles = angles_for(right);
    if (!qa_world_body_write(g->services.world, a->id, &body, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    const char *model = qa_strings_cstr(qa_session_strings(g->services.session), p->model);
    if (model != NULL && strcmp(model, "models/objects/gibs/sm_meat/tris.md2") == 0) {
        ++p->frame;
        p->phase = 1;
        p->next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
        return show_frame(g, a, e);
    }
    return true;
}
typedef struct gib_contact_call {
    qa_q2_game *game;
    const qa_touch_contact *contact;
} gib_contact_call;
static bool gib_contact(void *context, qa_actor_id actor, qa_error *e) {
    (void)actor;
    gib_contact_call *call = context;
    return gib_touch(call->game, call->contact, e);
}
bool q2_gib_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    gib_contact_call call = {g, contact};
    return qa_q2_run_actor(g, contact->self, gib_contact, &call, e);
}
bool q2_gib_reaction(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *outcome, qa_error *e) {
    if (g->options.edition == QA_Q2_CLASSIC ||
        (outcome->request.attack.cause.kind == QA_CAUSE_Q2 &&
         outcome->request.attack.cause.source.q2.means_of_death == 20))
        return qa_session_release(g->services.session, a->id, e);
    return true;
}
bool q2_gib_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_projectile *p = &a->projectile;
    if ((p->gib_flags & Q2_GIB_WIDOW_LEGS) != 0)
        return q2_widow_legs_think(g, a, e);
    if (p->kind == Q2_SPAWN_GROWTH) {
        if (p->next_ns > g->now_ns)
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        body.angles = growth_angles(g);
        if (!qa_world_body_write(g->services.world, a->id, &body, e))
            return false;
        if (g->now_ns < p->effect_ns && p->frame < 2)
            ++p->frame;
        if (g->now_ns >= p->effect_ns) {
            if ((p->effects & UINT64_C(0x10000000)) != 0 || p->frame == 0)
                return qa_session_release(g->services.session, a->id, e);
            --p->frame;
        }
        p->next_ns = q2_deadline(p->next_ns, 100 * Q2_MS);
        return qa_world_link(g->services.world, a->id, NULL, e) && show_frame(g, a, e);
    }
    if (p->kind == Q2_TRAP_ORBIT_GIB) {
        if (!q2_actor_live(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner)))
            return qa_session_release(g->services.session, a->id, e);
        q2_actor *trap = q2_actor_get(g, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner), false, e);
        if (trap == NULL)
            return false;
        if (trap->projectile.kind != Q2_TRAP || trap->projectile.frame != 5)
            return qa_session_release(g->services.session, a->id, e);
        if (g->now_ns < p->next_ns)
            return true;
        qa_body_state body, owner;
        if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
            !qa_world_body_read(g->services.world, qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner), &owner, e))
            return false;
        qa_vec3 up;
        qa_builtin_angle_vectors(owner.angles, NULL, NULL, &up);
        float seconds = (float)((double)g->frame_ns / 1e9),
              degrees = 150 * seconds + trap->projectile.delay,
              radians = degrees * 0.017453292519943295f;
        qa_vec3 delta = qa_vec_sub(owner.origin, body.origin);
        qa_vec3 rotated =
            qa_vec_add(qa_vec_add(qa_vec_scale(delta, cosf(radians)),
                                  qa_vec_scale(qa_vec_cross(up, delta), sinf(radians))),
                       qa_vec_scale(up, qa_vec_dot(up, delta) * (1 - cosf(radians))));
        qa_trace_query query = {.start = body.origin,
                                .end = qa_vec_sub(owner.origin, rotated),
                                .pass_actor = a->id,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = qa_collision_contents_mask(3, QA_COLLISION_Q2);
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, e))
            return false;
        body.origin = qa_vec_add(trace.end, qa_vec_scale(qa_vec_normalize(delta), 15 * seconds));
        body.angles.y += degrees;
        p->next_ns = q2_deadline(g->now_ns, g->frame_ns);
        return qa_world_body_write(g->services.world, a->id, &body, e) &&
               qa_world_link(g->services.world, a->id, NULL, e);
    }
    if (p->phase == 1 && g->now_ns >= p->next_ns) {
        ++p->frame;
        if (p->frame == 10) {
            p->phase = 0;
            p->expire_ns = q2_deadline(g->now_ns, (uint64_t)((8 + q2_random(g) * 10) * 1e9));
        } else
            p->next_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
        return show_frame(g, a, e);
    }
    return true;
}
bool q2_spawn_growth(qa_q2_game *g, qa_vec3 origin, unsigned size, qa_error *e) {
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, "spawngro", &definition, e))
        return false;
    qa_body_state body = {.origin = origin, .angles = growth_angles(g)};
    qa_builtin_spawn spawn = {.owner = g->options.owner, .definition = definition, .body = body};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (a == NULL)
        return false;
    a->projectile =
        (q2_projectile){.kind = Q2_SPAWN_GROWTH,
                        .visible = true,
                        .scale = 1,
                        .render_flags = 32768,
                        .effects = size == 2 ? 0 : UINT64_C(0x10000000),
                        .effect_ns = q2_deadline(g->now_ns, (size == 2 ? 2000 : 300) * Q2_MS),
                        .next_ns = q2_deadline(g->now_ns, 100 * Q2_MS),
                        .expire_ns = UINT64_MAX};
    a->physics_bound = true;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.motion = QA_PHYSICS_STATIONARY;
    a->physics.solid = QA_PHYSICS_NOT_SOLID;
    return qa_world_link(g->services.world, id, NULL, e) &&
           q2_projectile_event(g, id, QA_BUILTIN_ANIMATION,
                               size <= 1   ? "models/items/spawngro2/tris.md2"
                               : size == 2 ? "models/items/spawngro3/tris.md2"
                                           : "models/items/spawngro/tris.md2",
                               0, origin, body.angles, e);
}
static bool trap_capture_run(qa_q2_game *g, q2_actor *trap,
                             const qa_builtin_actor_snapshot *snapshot, qa_error *e) {
    qa_body_state own;
    if (!qa_world_body_read(g->services.world, trap->id, &own, e))
        return false;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id id = snapshot->ids[i];
        if (!q2_actor_live(g, id))
            continue;
        qa_builtin_actor_traits traits;
        if (!qa_q2_actor_traits(g, id, &traits))
            continue;
        const char *name =
            qa_strings_cstr(qa_session_strings(g->services.session), traits.classname);
        if (name == NULL || strcmp(name, "gib") != 0)
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            return false;
        if (qa_vec_length(qa_vec_sub(body.origin, own.origin)) > 128)
            continue;
        q2_actor *a = q2_actor_get(g, id, false, e);
        if (a == NULL)
            return false;
        a->projectile.kind = Q2_TRAP_ORBIT_GIB;
        a->projectile.owner = qa_actor_reference_source(g->options.owner, trap->wire_slot);
        a->projectile.next_ns = g->now_ns;
        a->physics.motion = QA_PHYSICS_STATIONARY;
        if (!q2_gib_think(g, a, e))
            return false;
    }
    return true;
}
bool q2_trap_capture_gibs(qa_q2_game *g, q2_actor *trap, qa_error *e) {
    qa_builtin_snapshot_frame *scratch = q2_scratch_acquire(g, e);
    if (scratch == NULL)
        return false;
    if (!qa_builtin_snapshot_reserve(&scratch->snapshot, g->capacity, e)) {
        qa_builtin_snapshot_release(scratch);
        return false;
    }
    scratch->snapshot.count = 0;
    for (q2_actor *a = g->first_actor; a != NULL; a = a->live_next)
        if (a->physics_bound)
            scratch->snapshot.ids[scratch->snapshot.count++] = a->id;
    bool ok = trap_capture_run(g, trap, &scratch->snapshot, e);
    qa_builtin_snapshot_release(scratch);
    return ok;
}
