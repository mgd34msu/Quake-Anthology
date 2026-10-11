#include "qa/q2_sound.h"
#include "internal.h"

bool q2_entity_init_trigger(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    a->entity->direction = q2_movedir(b.angles);
    a->entity->visual.visible = false;
    b.angles = qa_v3(0, 0, 0);
    a->entity->touchable = true;
    return q2_entity_body(g, a, &b, false, e) && q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e);
}
bool q2_entity_clip(qa_q2_game *g, q2_actor *a, qa_actor_id other, bool *inside, qa_error *e) {
    *inside = true;
    if (!a->entity->has_inline)
        return true;
    qa_body_state trigger, b;
    qa_trace_result hit;
    if (!qa_world_body_read(g->services.world, a->id, &trigger, e) ||
        !qa_world_body_read(g->services.world, other, &b, e))
        return false;
    qa_trace_query q = {.start = b.origin,
                        .end = b.origin,
                        .shape = {.kind = QA_SHAPE_BOX, .bounds = b.bounds},
                        .policy = {.behavior = &qa_trace_behaviors[(g->options.edition == QA_Q2_RERELEASE) ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC],
                                   .contents_mask = qa_collision_contents_mask(UINT32_MAX, QA_GAME_Q2)},
                        .target = {.inline_model = true,
                                   .model = a->entity->collision.model,
                                   .origin = trigger.origin,
                                   .angles = trigger.angles}};
    qa_collision_geometry *geometry = qa_world_geometry(g->services.world);
    if (!qa_collision_trace(geometry, qa_world_trace_scratch(g->services.world,geometry), &q, &hit, e))
        return false;
    *inside = hit.start_solid || hit.all_solid;
    return true;
}
bool q2_entity_multi(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->think != Q2ET_NONE || s->dispatching)
        return true;
    s->activator = activator;
    s->dispatching = true;
    bool okay = q2_entity_targets(g, a, activator, false, e);
    if (!q2_actor_live(g, a->id))
        return okay;
    s->dispatching = false;
    if (!okay)
        return false;
    if (s->wait > 0)
        return q2_entity_schedule(g, a, Q2ET_MULTI_READY, s->wait);
    s->touchable = false;
    return q2_entity_schedule(g, a, Q2ET_FREE, (float)g->frame_ns / Q2_NS);
}
static bool wind_sound(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_entities *r = g->entity_runtime;
    size_t slot = r->wind_count, spare = SIZE_MAX;
    for (size_t i = 0; i < r->wind_count; i++) {
        if (qa_actor_id_equal(r->wind[i].actor, id)) {
            slot = i;
            break;
        }
        if (spare == SIZE_MAX && !q2_actor_live(g, r->wind[i].actor))
            spare = i;
    }
    if (slot == r->wind_count && spare != SIZE_MAX) {
        slot = spare;
        r->wind[slot] = (q2_wind_time){.actor = id};
    }
    if (slot == r->wind_count) {
        if (r->wind_count == r->wind_capacity) {
            size_t cap = r->wind_capacity ? r->wind_capacity * 2 : 16;
            if (cap < r->wind_capacity || cap > SIZE_MAX / sizeof(*r->wind)) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Q2 wind timer capacity overflow");
                return false;
            }
            q2_wind_time *p = realloc(r->wind, cap * sizeof(*p));
            if (!p) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 wind timers");
                return false;
            }
            r->wind = p;
            r->wind_capacity = cap;
        }
        r->wind[r->wind_count++] = (q2_wind_time){.actor = id};
    }
    if (r->wind[slot].until_ns >= g->now_ns)
        return true;
    r->wind[slot].until_ns = q2_deadline(g->now_ns, 1500 * Q2_MS);
    return q2_player_sound(g, id, QA_Q2_SOUND_MISC_WINDFLY, 0, e);
}
bool q2_trigger_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    *handled = true;
    if (!strcmp(name, "trigger_once") || !strcmp(name, "trigger_multiple")) {
        s->kind = Q2E_MULTI;
        if (!strcmp(name, "trigger_once")) {
            if (s->spawnflags & 1)
                s->spawnflags = (s->spawnflags & ~1u) | 4;
            s->wait = -1;
        } else if (s->wait == 0)
            s->wait = .2f;
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        if (qa_vec_dot(b.angles, b.angles) > 0)
            s->direction = q2_movedir(b.angles);
        s->usable = true;
        s->touchable = true;
        s->visual.visible = false;
        return q2_entity_solid(g, a,
                               (s->spawnflags & 4) ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_TRIGGER, e);
    }
    if (!strcmp(name, "trigger_push"))
        s->kind = Q2E_PUSH;
    else if (!strcmp(name, "trigger_hurt"))
        s->kind = Q2E_HURT;
    else if (!strcmp(name, "trigger_gravity"))
        s->kind = Q2E_GRAVITY;
    else if (!strcmp(name, "trigger_monsterjump"))
        s->kind = Q2E_MONSTERJUMP;
    else if (!strcmp(name, "trigger_teleport"))
        s->kind = Q2E_TELEPORT;
    else if (!strcmp(name, "trigger_disguise"))
        s->kind = Q2E_DISGUISE;
    else {
        *handled = false;
        return true;
    }
    if (s->kind == Q2E_GRAVITY && !q2_field_id(s, g->field_keys[QA_TARGET_KEY_GRAVITY]))
        return qa_session_release(g->services.session, a->id, e);
    if (s->kind == Q2E_TELEPORT && g->entity_runtime->services.ctf_map_rules) {
        if (!s->target)
            return qa_session_release(g->services.session, a->id, e);
        s->visual.visible = false;
        s->touchable = true;
        s->stage = 1;
        if (!q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e) || !q2_entity_show(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        body.origin = qa_vec_add(body.origin,
                                 qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
        body.bounds = (qa_bounds){0};
        body.velocity = qa_v3(0, 0, 0);
        q2_actor *sound;
        if (!q2_entity_native_spawn(g, "ctf_teleport_sound", &body, Q2E_POINT, &sound, e))
            return false;
        s->enemy = sound->id;
        return q2_entity_sound(g, sound, QA_Q2_SOUND_WORLD_HUM1, 0, 1, 1, 1, e);
    }
    if (!q2_entity_init_trigger(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->kind == Q2E_PUSH) {
        if (s->speed == 0)
            s->speed = 1000;
        if (rr && (s->spawnflags & 2)) {
            if (s->wait == 0)
                s->wait = 10;
            s->expires_ns = q2_deadline(g->now_ns, q2_item_seconds(.1f + s->wait));
            q2_entity_schedule(g, a, Q2ET_PUSH, .1f);
        }
        if (rr && s->targetname) {
            s->usable = true;
            if (s->spawnflags & 8)
                return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e);
        } else if (rr && (s->spawnflags & 8)) {
            s->touchable = false;
            s->visual.visible = true;
            a->physics.motion = QA_PHYSICS_PUSH;
            return q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e);
        }
    } else if (s->kind == Q2E_HURT) {
        if (s->damage == 0)
            s->damage = 5;
        s->usable = (s->spawnflags & 2) != 0;
        if (s->spawnflags & 1)
            return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e);
    } else if (s->kind == Q2E_GRAVITY || s->kind == Q2E_MONSTERJUMP) {
        if (s->kind == Q2E_MONSTERJUMP) {
            if (s->speed == 0)
                s->speed = 200;
            s->direction.z = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_HEIGHT], 200);
        }
        if (rr) {
            s->usable = (s->spawnflags & 3) != 0;
            if (s->spawnflags & 2)
                return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e);
        }
    } else if (s->kind == Q2E_TELEPORT) {
        s->delay = s->targetname && !(s->spawnflags & 8) ? 1 : 0;
        s->usable = s->targetname != 0;
    } else {
        s->usable = true;
        if (!(s->spawnflags & 2))
            return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e);
    }
    return true;
}
bool q2_trigger_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                    bool *handled, qa_error *e) {
    (void)other;
    q2_entity_state *s = a->entity;
    *handled = true;
    switch (s->kind) {
    case Q2E_MULTI:
        return a->physics.solid == QA_PHYSICS_NOT_SOLID
                   ? q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e)
                   : q2_entity_multi(g, a, activator, e);
    case Q2E_TELEPORT:
        s->delay = s->delay == 0 ? 1 : 0;
        return true;
    case Q2E_HURT:
        if (!(s->spawnflags & 2))
            s->usable = false;
        break;
    case Q2E_PUSH:
    case Q2E_GRAVITY:
    case Q2E_MONSTERJUMP:
    case Q2E_DISGUISE:
        break;
    default:
        *handled = false;
        return true;
    }
    return q2_entity_solid(
        g, a, a->physics.solid == QA_PHYSICS_NOT_SOLID ? QA_PHYSICS_TRIGGER : QA_PHYSICS_NOT_SOLID,
        e);
}
bool q2_trigger_touch(qa_q2_game *g, q2_actor *a, const qa_touch_contact *contact, bool *handled,
                      qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_actor_id id = contact->other;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits)
        g->services.actor_traits(g->services.context, id, &traits);
    *handled = true;
    if (s->kind == Q2E_MULTI) {
        if (traits.player ? (s->spawnflags & 2) != 0 : !traits.monster || !(s->spawnflags & 1))
            return true;
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, id, &b, e))
            return false;
        if (qa_vec_dot(q2_movedir(b.angles), s->direction) < 0)
            return true;
        return q2_entity_multi(g, a, id, e);
    }
    if (s->kind != Q2E_PUSH && s->kind != Q2E_HURT && s->kind != Q2E_GRAVITY &&
        s->kind != Q2E_MONSTERJUMP && s->kind != Q2E_TELEPORT && s->kind != Q2E_DISGUISE) {
        *handled = false;
        return true;
    }
    uint32_t exact = s->kind == Q2E_PUSH ? 16 : s->kind == Q2E_HURT ? 128 : 4;
    bool inside;
    if (rr && (s->spawnflags & exact) && (s->kind <= Q2E_MONSTERJUMP)) {
        if (!q2_entity_clip(g, a, id, &inside, e))
            return false;
        if (!inside)
            return true;
    }
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, id, &b, e))
        return false;
    qa_q2_entity_services *services = &g->entity_runtime->services;
    q2_actor *native = q2_actor_get(g, id, false, NULL);
    if (s->kind == Q2E_TELEPORT) {
        if (!traits.player || s->delay != 0)
            return true;
        qa_actor_id destination;
        qa_body_state to;
        if (!q2_map_find(g, 0, s->target, 0, &destination))
            return true;
        if (!qa_world_body_read(g->services.world, destination, &to, e))
            return false;
        return q2_entity_teleport(g, a, id, &to, s->stage == 1, false, e);
    }
    if (s->kind == Q2E_DISGUISE) {
        if (!traits.player)
            return true;
        if (!services->disguise) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 disguise service is not installed");
            return false;
        }
        return services->disguise(services->context, id, (s->spawnflags & 4) == 0, e);
    }
    if (s->kind == Q2E_GRAVITY) {
        float gravity = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_GRAVITY], 0);
        if (!rr)
            gravity = truncf(gravity);
        if (native && native->physics_bound)
            native->physics.gravity_scale = gravity;
        if (services->actor_gravity)
            return services->actor_gravity(services->context, id, gravity, e);
        if (native && native->physics_bound)
            return true;
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 trigger requires selected gravity service");
        return false;
    }
    if (s->kind == Q2E_MONSTERJUMP) {
        if (!traits.monster ||
            (native &&
             (native->physics.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING | QA_PHYSICS_DEAD))))
            return true;
        bool grounded = qa_actor_reference_present(b.ground);
        if (rr && !grounded) {
            qa_trace_result hit;
            qa_vec3 down = b.origin;
            down.z -= .25f;
            if (!q2_player_trace(g, id, b.origin, down, &b.bounds, 0x2010003, &hit, e))
                return false;
            grounded = hit.fraction < 1;
        }
        b.velocity.x = s->direction.x * s->speed;
        b.velocity.y = s->direction.y * s->speed;
        if (grounded)
            b.velocity.z = s->direction.z;
        b.ground = (qa_actor_reference){0};
        return qa_world_body_write(g->services.world, id, &b, e);
    }
    qa_combat_state combat;
    qa_error local = {0};
    bool has = qa_combat_read(g->services.combat, id, &combat, &local);
    if (!has && local.code != QA_ERROR_NOT_FOUND) {
        if (e)
            *e = local;
        return false;
    }
    if (s->kind == Q2E_HURT) {
        if (!has || !combat.can_take_damage || s->timestamp_ns > g->now_ns)
            return true;
        if (rr &&
            ((!traits.player && !traits.monster && !traits.damageable_target &&
              traits.classname != g->runtime_names[Q2_NAME_MISC_EXPLOBOX]) ||
             (traits.player && (s->spawnflags & 32)) || (traits.monster && (s->spawnflags & 64))))
            return true;
        s->timestamp_ns = q2_deadline(g->now_ns, (s->spawnflags & 16) ? Q2_NS
                                                 : rr                 ? 100 * Q2_MS
                                                                      : g->frame_ns);
        bool sound = rr ? s->sound_ns < g->now_ns
                        : g->frame_ns && ((g->now_ns + g->frame_ns / 2) / g->frame_ns) % 10 == 0;
        if (!(s->spawnflags & 4) && sound) {
            s->sound_ns = q2_deadline(g->now_ns, Q2_NS);
            if (!q2_player_sound(g, id, QA_Q2_SOUND_WORLD_ELECTRO, 0, e))
                return false;
            if (!q2_actor_live(g, a->id) || !q2_actor_live(g, id))
                return true;
        }
        return q2_entity_damage(g, a, id, a->id, s->damage, s->damage, 31,
                                (s->spawnflags & 8) ? 32 : 0, e);
    }
    if ((has && combat.health > 0) || traits.classname == g->runtime_names[Q2_NAME_GRENADE]) {
        b.velocity = qa_vec_scale(s->direction, s->speed * 10);
        if (!qa_world_body_write(g->services.world, id, &b, e))
            return false;
        if (traits.player) {
            if (native && native->client)
                native->client->rule.old_velocity = b.velocity;
            if (services->player_push &&
                !services->player_push(services->context, id, b.velocity, e))
                return false;
            if ((!rr || !(s->spawnflags & 4)) && q2_actor_live(g, id) && !wind_sound(g, id, e))
                return false;
        }
    }
    return !(s->spawnflags & 1) || !q2_actor_live(g, a->id) ||
           qa_session_release(g->services.session, a->id, e);
}
bool q2_trigger_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, bool *handled,
                      qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = true;
    if (think == Q2ET_MULTI_READY)
        return true;
    if (think != Q2ET_PUSH) {
        *handled = false;
        return true;
    }
    if (s->expires_ns <= g->now_ns) {
        s->touchable = !s->touchable;
        s->expires_ns = q2_deadline(g->now_ns, q2_item_seconds(.1f + s->wait));
    }
    if (s->touchable) {
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        qa_vec3 origin =
            qa_vec_add(b.origin, qa_vec_scale(qa_vec_add(b.bounds.mins, b.bounds.maxs), .5f));
        for (int i = 0; i < 10; i++) {
            origin.z += s->speed * .01f * ((float)i + q2_random(g));
            int color = 0x74 + (int)(q2_random(g) * 8);
            if (!q2_projectile_event(g, a->id, QA_BUILTIN_IMPACT, g->runtime_names[Q2_NAME_RESOURCE_Q2_TUNNEL_SPARKS], color, origin,
                                     qa_v3(0, 0, 0), e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
    }
    return q2_entity_schedule(g, a, Q2ET_PUSH, .1f);
}
