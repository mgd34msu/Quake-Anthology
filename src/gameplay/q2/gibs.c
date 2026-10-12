#include "qa/q2_sound.h"
#include "internal.h"
#include "qa/game_q2_source.h"
#include "qa/text.h"

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
    qa_point_query query = {.point = point, .policy = qa_collision_ruleset_policy(g->options.rules)};
    qa_point_contents result;
    if (!qa_world_point_contents(g->services.world, &query, &result, e))
        return false;
    *contents = qa_collision_point_contents_export(result.contents, QA_GAME_Q2, result.q1_opaque_token);
    return true;
}
bool q2_spawn_gib(qa_q2_game *g, qa_actor_id source, q2_runtime_name model, float damage,
                  uint32_t flags, int skin, float scale, qa_error *e) {
    bool head = (flags & Q2_GIB_HEAD) != 0, metal = (flags & Q2_GIB_METALLIC) != 0,
         rr = g->options.rules == QA_RULESET_Q2_RERELEASE,
         debris = rr && (flags & Q2_GIB_DEBRIS) != 0;
    qa_body_state source_body;
    if (!qa_world_body_read(g->services.world, source, &source_body, e))
        return false;
    qa_actor_id id = source;
    qa_actor_definition definition;
    definition=g->runtime_names[Q2_NAME_GIB];
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
    float factor = debris ? (float)qa_source_to_i32(damage) :
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
        bool instant = qa_source_to_i32(instagib) != 0;
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
    a->physics = qa_physics_properties_default(g->options.rules);
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
    return q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, g->runtime_names[model], 0, origin, angles, e);
}
static bool debris(qa_q2_game *g, qa_body_state body, q2_runtime_name model, float speed,
                   qa_vec3 origin, qa_error *e) {
    qa_actor_definition definition;
    definition=g->runtime_names[Q2_NAME_RESOURCE_DEBRIS];
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
    a->physics = qa_physics_properties_default(g->options.rules);
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
    return q2_projectile_event(g, id, QA_BUILTIN_ANIMATION, g->runtime_names[model], 0, body.origin, body.angles, e);
}
bool q2_spawn_debris(qa_q2_game *g, qa_actor_id source, qa_error *e) {
    qa_body_state body;
    return qa_world_body_read(g->services.world, source, &body, e) &&
           debris(g, body, Q2_NAME_RESOURCE_MODELS_OBJECTS_DEBRIS2_TRIS_MD2, 2, body.origin, e);
}
bool q2_spawn_model_debris(qa_q2_game *g, qa_actor_id source, q2_runtime_name model, float speed,
                           qa_vec3 origin, qa_error *e) {
    if (g == NULL || model == Q2_NAME_NONE || !isfinite(speed) || !qa_vec_finite(origin)) {
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
    if (g->options.rules == QA_RULESET_Q2_RERELEASE && (p->gib_flags & Q2_GIB_WIDOW) == 0) {
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
    if (!q2_projectile_event(g, a->id, QA_BUILTIN_SOUND, g->runtime_names[QA_Q2_SFX_MISC_FHIT3], 2, body.origin,
                             body.origin, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_vec3 right;
    qa_angle_vectors(qa_vec_angles(contact->plane.normal,QA_DIRECTION_FLOAT,QA_VECTOR_NEGATIVE_PITCH), NULL, &right, NULL,QA_ANGLE_FLOAT_MATH);
    body.angles = qa_vec_angles(right,QA_DIRECTION_FLOAT,QA_VECTOR_NEGATIVE_PITCH);
    if (!qa_world_body_write(g->services.world, a->id, &body, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (p->model == g->runtime_names[Q2_NAME_RESOURCE_MODELS_OBJECTS_GIBS_SM_MEAT_TRIS_MD2]) {
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
    if (g->options.rules == QA_RULESET_Q2_CLASSIC ||
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
        qa_angle_vectors(owner.angles, NULL, NULL, &up,QA_ANGLE_FLOAT_MATH);
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
                                .policy = qa_collision_ruleset_policy(g->options.rules)};
        query.policy.contents_mask = qa_collision_contents_mask(3, QA_GAME_Q2);
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
bool q2_admit_growth_part(qa_q2_game *game, q2_runtime_name classname, qa_body_state body, q2_actor **out, qa_ruleset_id rules,
                       qa_error *error) {
    qa_string_id definition=game->runtime_names[classname];
    qa_builtin_spawn spawn = {.owner = game->options.owner, .definition = definition, .body = body};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&game->services, &spawn, &id, error))
        return false;
    q2_actor *actor = q2_actor_get(game, id, true, error);
    if (!actor) {
        (void)qa_session_release(game->services.session, id, NULL);
        return false;
    }
    if (rules == QA_RULESET_Q2_RERELEASE) {
        actor->projectile.classname = definition;
        actor->projectile.visible = true;
        actor->projectile.alpha = 1;
    }
    actor->physics = qa_physics_properties_default(rules);
    actor->physics.motion = QA_PHYSICS_STATIONARY;
    actor->physics.solid = QA_PHYSICS_NOT_SOLID;
    actor->physics_bound = true;
    *out = actor;
    return true;
}

bool q2_spawn_growth(qa_q2_game *g, qa_vec3 origin, unsigned size, qa_error *e) {
    qa_body_state body = {.origin = origin, .angles = growth_angles(g)};
    q2_actor *a;
    if (!q2_admit_growth_part(g, Q2_NAME_RESOURCE_SPAWNGRO, body, &a, QA_RULESET_Q2_CLASSIC, e)) return false;
    qa_actor_id id=a->id;
    a->projectile =
        (q2_projectile){.kind = Q2_SPAWN_GROWTH,
                        .visible = true,
                        .scale = 1,
                        .render_flags = 32768,
                        .effects = size == 2 ? 0 : UINT64_C(0x10000000),
                        .effect_ns = q2_deadline(g->now_ns, (size == 2 ? 2000 : 300) * Q2_MS),
                        .next_ns = q2_deadline(g->now_ns, 100 * Q2_MS),
                        .expire_ns = UINT64_MAX};
    return qa_world_link(g->services.world, id, NULL, e) &&
           q2_projectile_event(g, id, QA_BUILTIN_ANIMATION,
                               size <= 1   ? g->runtime_names[Q2_NAME_RESOURCE_MODELS_ITEMS_SPAWNGRO2_TRIS_MD2]
                               : size == 2 ? g->runtime_names[Q2_NAME_RESOURCE_MODELS_ITEMS_SPAWNGRO3_TRIS_MD2]
                                           : g->runtime_names[Q2_NAME_RESOURCE_MODELS_ITEMS_SPAWNGRO_TRIS_MD2],
                               0, origin, body.angles, e);
}
static int trap_gib_order(void *context, qa_actor_id left, qa_actor_id right) {
    qa_q2_game *g = context;
    uint64_t first = g->actors[left.slot]->source_order;
    uint64_t second = g->actors[right.slot]->source_order;
    return first < second ? -1 : first > second ? 1 : left.slot < right.slot ? -1 : left.slot > right.slot;
}
static bool trap_gib_include(void *context, qa_actor_id id) {
    qa_q2_game *g = context;
    q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    qa_builtin_actor_traits traits;
    return q2_actor_live(g, id) && a && qa_actor_id_equal(a->id, id) && a->physics_bound &&
        qa_q2_actor_traits(g, id, &traits) && traits.classname == g->runtime_names[Q2_NAME_GIB];
}
bool q2_trap_capture_gibs(qa_q2_game *g, q2_actor *trap, qa_error *e) {
    qa_body_state own;
    if (!qa_world_body_read(g->services.world, trap->id, &own, e))
        return false;
    qa_world_actor_snapshot nearby;
    qa_world_radius_query query = {.origin = own.origin, .radius = 128,
        .metric = QA_WORLD_RADIUS_ORIGIN, .include = trap_gib_include, .context = g,
        .read_policy = QA_WORLD_RADIUS_READ_REQUIRED};
    if (!qa_world_radius_snapshot_capture(g->services.world, &query, &nearby, e))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < nearby.count; ++i) {
        qa_actor_id id = nearby.actors[i];
        q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
        if (q2_actor_live(g, id) && a && qa_actor_id_equal(a->id, id))
            nearby.actors[count++] = id;
    }
    nearby.count = count;
    qa_world_snapshot_sort(&nearby, trap_gib_order, g);
    bool okay = false;
    for (size_t i = 0; i < nearby.count; ++i) {
        qa_actor_id id = nearby.actors[i];
        if (!q2_actor_live(g, id))
            continue;
        qa_builtin_actor_traits traits;
        if (!qa_q2_actor_traits(g, id, &traits))
            continue;
        if (traits.classname != g->runtime_names[Q2_NAME_GIB])
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, e))
            goto out;
        if (!q2_actor_live(g, id)) continue;
        if (qa_vec_length(qa_vec_sub(body.origin, own.origin)) > 128)
            continue;
        q2_actor *a = q2_actor_get(g, id, false, e);
        if (a == NULL)
            goto out;
        uint32_t number;
        if (!qa_q2_wire_entity_number(g, trap->id, &number, e)) goto out;
        a->projectile.kind = Q2_TRAP_ORBIT_GIB;
        a->projectile.owner = qa_actor_reference_source(g->options.owner, number);
        a->projectile.next_ns = g->now_ns;
        a->physics.motion = QA_PHYSICS_STATIONARY;
        if (!q2_gib_think(g, a, e))
            goto out;
    }
    okay = true;
out:
    qa_world_snapshot_release(&nearby);
    return okay;
}
