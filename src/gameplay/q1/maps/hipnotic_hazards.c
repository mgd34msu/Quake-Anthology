#include "internal.h"

bool q1_hipnotic_lightning_claimed(const qa_q1_game *g, qa_actor_id actor) {
    for (uint32_t i = 0; i < g->capacity; ++i) {
        const q1_actor *e = g->actors[i];
        if (!e || !e->active)
            continue;
        if (e->kind == Q1_TIMER && e->think == Q1_THINK_HAMMER_BOLT &&
            e->state.projectile.count == 1 &&
            q1_ref_equal(e->state.projectile.enemy, q1_ref_from(g, actor)))
            return true;
        if (e->map && e->map->kind == Q1_MAP_TESLA_BOLT && e->count == 1 &&
            q1_ref_equal(e->map->pending.hazard.enemy, q1_ref_from(g, actor)))
            return true;
    }
    return false;
}

static bool scan(qa_q1_game *g, q1_actor *e, float radius, bool monsters, bool unclaimed,
                  qa_builtin_snapshot_frame **out, qa_error *error) {
    qa_actor_id id = e->id;
    *out = NULL;
    qa_vec3 view_offset = e->map->view_offset;
    qa_body_state self;
    if (!qa_world_body_read(g->services.world, id, &self, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    qa_vec3 eye = qa_vec_add(self.origin, view_offset);
    qa_builtin_snapshot_frame *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    for (size_t i = 0; i < list->snapshot.count / 2; ++i) {
        qa_actor_id actor = list->snapshot.ids[i];
        list->snapshot.ids[i] = list->snapshot.ids[list->snapshot.count - i - 1];
        list->snapshot.ids[list->snapshot.count - i - 1] = actor;
    }
    size_t count = 0;
    for (size_t i = 0; i < list->snapshot.count; ++i) {
        qa_actor_id actor = list->snapshot.ids[i];
        qa_q1_target target;
        qa_builtin_actor_traits traits = {0};
        const q1_actor *native = q1_entity_const(g, actor);
        bool monster = native && (native->physics.flags & QA_PHYSICS_MONSTER);
        if (g->services.actor_traits &&
            g->services.actor_traits(g->services.context, actor, &traits))
            monster |= traits.monster;
        if (!q1_alive(g, id))
            break;
        if (!q1_target(g, actor, &target) || target.notarget ||
            (!target.player && (!monsters || !monster)) || q1_health(g, actor) <= 0 ||
            (unclaimed && q1_hipnotic_lightning_claimed(g, actor)))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, actor, &body, NULL))
            continue;
        if (!q1_alive(g, id))
            break;
        if (!q1_alive(g, actor))
            continue;
        native = q1_entity_const(g, actor);
        qa_vec3 center = qa_vec_add(
            body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
        if (qa_vec_length(qa_vec_sub(center, self.origin)) > radius)
            continue;
        qa_trace_result trace;
        qa_vec3 end = qa_vec_add(body.origin, qa_v3(0, 0, target.player ? 22 :
                                                    native && native->map
                                                        ? native->map->view_offset.z :
                                                          native ? target.view_height : 0));
        if (!q1_trace(g, eye, end, id, false, &trace, error)) {
            qa_builtin_snapshot_release(list);
            return false;
        }
        if (!q1_alive(g, id))
            break;
        if (!q1_alive(g, actor) || trace.fraction != 1 || (trace.in_open && trace.in_water))
            continue;
        list->snapshot.ids[count++] = actor;
        if (unclaimed && (double)count == e->count)
            break;
    }
    list->snapshot.count = count;
    *out = list;
    return true;
}

static bool beam(qa_q1_game *g, q1_actor *e, qa_vec3 start, qa_vec3 end, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_BEAM, .family = QA_GAME_Q1,
                              .provider = g->options.provider, .actor = e->id,
                              .time_ns = g->time_ns, .origin = start, .end = end, .code = 2};
    return qa_builtin_emit(&g->services, &event, error);
}
static bool electric(qa_q1_game *g, q1_actor *e, qa_vec3 start, qa_vec3 end,
                      qa_actor_id from, qa_error *error) {
    qa_actor_id inflictor = from.registry ? from : g->maps->world_actor;
    if (!inflictor.registry && g->services.physics)
        inflictor = g->services.physics->world_actor;
    if (!inflictor.registry)
        return q1_map_fail(error, "Hipnotic lightning requires worldspawn");
    return q1_electric_rays(g, from, inflictor, e->id, start, end, e->damage, e->damage * 4,
                            225, qa_v3(0, 0, 100), Q1_LIGHTNING_PARTICLES |
                            Q1_LIGHTNING_REMEMBER_ALL | Q1_LIGHTNING_WETSUIT,
                            QA_Q1_WEAPON_COUNT, "electric", error);
}
static bool seen_beam(qa_q1_game *g, q1_actor *e, qa_vec3 start, qa_vec3 end,
                       qa_error *error) {
    qa_actor_id client = {0}, id = e->id;
    if (!g->host.check_client)
        return q1_map_fail(error, "Hipnotic lightning requires client visibility service");
    if (!g->host.check_client(g->host.context, id, &client, error)) return false;
    return !q1_alive(g, id) || !client.registry || beam(g, e, start, end, error);
}

static bool bolt(qa_q1_game *g, q1_actor *e, bool tesla, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_state *s = e->map;
    qa_body_state body, target;
    if (tesla) {
        q1_actor *owner = q1_entity(g, q1_ref_actor(g, e->owner));
        if (owner && owner->map && (owner->map->kind == Q1_MAP_TESLA ||
                                    owner->map->kind == Q1_MAP_GODS_WRATH))
            owner->map->pending.hazard.attack = 2;
    }
    bool expired = g->time > s->active_until;
    if (!expired && tesla)
        expired = !qa_world_body_read(g->services.world, q1_ref_actor(g, s->pending.hazard.enemy), &target, NULL);
    if (!q1_alive(g, id))
        return true;
    if (expired)
        return q1_remove(g, e, error);
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    qa_vec3 end = s->pending.hazard.endpoint;
    if (tesla) {
        qa_trace_result trace;
        if (!q1_trace(g, body.origin, target.origin, id, false, &trace, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (trace.fraction != 1 || q1_health(g, q1_ref_actor(g, s->pending.hazard.enemy)) <= 0 ||
            qa_vec_length(qa_vec_sub(body.origin, target.origin)) > s->distance + 10)
            return q1_remove(g, e, error);
        end = trace.end;
    }
    if (!(tesla ? beam(g, e, body.origin, end, error)
               : seen_beam(g, e, body.origin, end, error)))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!electric(g, e, body.origin, end, q1_ref_actor(g, s->pending.hazard.last_victim), error))
        return false;
    return !q1_alive(g, id) || q1_map_schedule(g, e, .1,
             tesla ? Q1_MAP_TESLA_BOLT_TICK : Q1_MAP_HIP_BOLT_TICK, error);
}

static bool make_bolt(qa_q1_game *g, q1_actor *e, bool tesla, qa_vec3 start, qa_vec3 end,
                       qa_actor_id enemy, qa_error *error) {
    qa_actor_id source = e->id;
    float distance = e->map->distance, duration = e->map->duration, damage = e->damage;
    qa_actor_id from = q1_ref_actor(g, tesla ? e->map->pending.hazard.last_victim : q1_ref_from(g, source));
    q1_actor *created;
    if (!q1_create(g, tesla ? "hipnotic_tesla_lightning" : "hipnotic_lightning", Q1_MAP,
                    tesla ? source : (qa_actor_id){0}, &created, error))
        return false;
    if (!q1_alive(g, source))
        return q1_remove(g, created, error);
    qa_actor_id created_id = created->id;
    q1_map_state *s = q1_map_allocate(g, created, error);
    if (!s) {
        qa_error cleanup = {0};
        q1_remove(g, created, &cleanup);
        return false;
    }
    s->kind = tesla ? Q1_MAP_TESLA_BOLT : Q1_MAP_HIP_BOLT;
    s->distance = distance;
    s->active_until = g->time + (tesla && duration <= 0 ? 9999 : duration);
    s->pending.hazard.enemy = q1_ref_from(g, enemy);
    s->pending.hazard.endpoint = end;
    s->pending.hazard.last_victim = q1_ref_from(g, from);
    created->damage = damage;
    created->count = tesla ? 1 : 0;
    qa_body_state body = {.origin = start};
    bool ok = qa_world_body_write(g->services.world, created_id, &body, error);
    if (ok && q1_alive(g, created_id) && !q1_alive(g, source))
        return q1_remove(g, created, error);
    if (ok && q1_alive(g, created_id))
        ok = tesla ? q1_map_schedule(g, created, 0, Q1_MAP_TESLA_BOLT_TICK, error)
                   : bolt(g, created, false, error);
    if (!ok && q1_alive(g, created_id)) {
        qa_error cleanup = {0};
        q1_remove(g, created, &cleanup);
    }
    return ok;
}

static bool lightning_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_state *s = e->map;
    if (g->time >= s->pending.hazard.sound_after) {
        if (!q1_sound(g, id, e->spawnflags & 2 ? "weapons/lstart.wav" : "weapons/lhit.wav",
                       0, 1, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (s->kind == Q1_MAP_HIP_LIGHTNING_TRIGGERED)
            s->pending.hazard.sound_after = g->time + .1;
    }
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    qa_vec3 start = body.origin, end;
    if (q1_map_text(g, e->target)) {
        qa_actor_id enemy = q1_ref_actor(g, s->pending.hazard.enemy);
        if (!q1_alive(g, enemy))
            enemy = g->maps->world_actor;
        if (!qa_world_body_read(g->services.world, enemy, &target, error))
            return false;
        end = target.origin;
    } else {
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        s->movedir = g->forward;
        qa_trace_result trace;
        if (!q1_trace(g, start, qa_vec_add(start, qa_vec_scale(s->movedir, 600)), id,
                       false, &trace, error))
            return false;
        end = trace.end;
    }
    if (!q1_alive(g, id))
        return true;
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta);
    float distance = qa_vec_length(delta) / 30, remainder = distance - floorf(distance);
    if (remainder > 0) {
        qa_vec3 offset = qa_vec_scale(direction, (remainder - 1) * 15);
        start = qa_vec_add(start, offset);
        end = qa_vec_sub(end, offset);
    }
    if (s->duration > .1f)
        return make_bolt(g, e, false, start, end, (qa_actor_id){0}, error);
    if (!seen_beam(g, e, start, end, error))
        return false;
    return !q1_alive(g, id) || electric(g, e, start, end, id, error);
}

static bool tesla_tick(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_state *s = e->map;
    if (!s->pending.hazard.enabled)
        return q1_map_schedule(g, e, .25, Q1_MAP_TESLA_TICK, error);
    uint8_t attack = s->pending.hazard.attack;
    if (attack <= 1) {
        qa_builtin_snapshot_frame *list;
        if (!scan(g, e, s->distance, e->spawnflags & 1, true, &list, error))
            return false;
        if (!list)
            return true;
        bool found = list->snapshot.count != 0, ok = true;
        if (attack == 1) {
            qa_body_state body;
            if (!qa_world_body_read(g->services.world, id, &body, error))
                ok = false;
            for (size_t i = 0; ok && i < list->snapshot.count && q1_alive(g, id); ++i) {
                qa_actor_id actor = list->snapshot.ids[i];
                if (!q1_alive(g, actor))
                    continue;
                ok = q1_sound(g, id, "hipweap/mjolhit.wav", 0, 1, error);
                if (ok && q1_alive(g, id))
                    ok = make_bolt(g, e, true, body.origin, qa_v3(0, 0, 0), actor, error);
            }
        }
        qa_builtin_snapshot_release(list);
        if (!ok || !q1_alive(g, id))
            return ok;
        if (attack == 1) {
            s->pending.hazard.attack = 2;
            return q1_map_schedule(g, e, 1, Q1_MAP_TESLA_TICK, error);
        }
        if (found) {
            if (e->wait > 0 && !q1_sound(g, id, "misc/tesla.wav", 0, 1, error))
                return false;
            if (!q1_alive(g, id))
                return true;
            s->pending.hazard.attack = 1;
            return q1_map_schedule(g, e, e->wait, Q1_MAP_TESLA_TICK, error);
        }
        if (e->delay > 0 && g->time > s->pending.hazard.search_until)
            s->pending.hazard.attack = 3;
        return q1_map_schedule(g, e, .25, Q1_MAP_TESLA_TICK, error);
    }
    if (attack == 2) {
        s->pending.hazard.attack = 3;
        return q1_map_schedule(g, e, .2, Q1_MAP_TESLA_TICK, error);
    }
    s->pending.hazard.attack = 0;
    if (s->kind == Q1_MAP_GODS_WRATH) {
        q1_map_cancel(g, e);
        return true;
    }
    return q1_map_schedule(g, e, .1, Q1_MAP_TESLA_TICK, error);
}

static bool mine_explode(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    if (!q1_radius(g, id, id, 110, (qa_actor_id){0}, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_sound(g, id, "weapons/r_exp3.wav", 1, 1, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_effect(g, QA_BUILTIN_EXPLOSION, id, body.origin, 0, 0, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_sound(g, id, "misc/null.wav", 2, 1, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_sprite_prepare(g, e, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_link(g, e, error))
        return false;
    return !q1_alive(g, id) || q1_schedule(g, e, .1, Q1_THINK_SPRITE, error);
}

static bool mine_home(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_state *s = e->map;
    e->frame = (e->frame + 1) % 9;
    if (!q1_map_schedule(g, e, .2, Q1_MAP_MINE_HOME, error))
        return false;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (s->pending.hazard.search_until < g->time) {
        qa_builtin_snapshot_frame *list;
        if (!scan(g, e, 2000, false, false, &list, error))
            return false;
        if (!list)
            return true;
        float closest = 2000;
        qa_actor_id selected = {0};
        for (size_t i = 0; i < list->snapshot.count; ++i)
            if (qa_world_body_read(g->services.world, list->snapshot.ids[i], &target, NULL)) {
                float distance = qa_vec_length(qa_vec_sub(target.origin, body.origin));
                if (distance < closest) {
                    closest = distance;
                    selected = list->snapshot.ids[i];
                }
            }
        qa_builtin_snapshot_release(list);
        if (!q1_alive(g, id))
            return true;
        if (selected.registry && !q1_sound(g, id, "hipitems/spikmine.wav", 2, 1, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        s->pending.hazard.enemy = q1_ref_from(g, selected);
        s->pending.hazard.search_until = g->time + 1.3;
    }
    bool target_exists = qa_world_body_read(g->services.world, q1_ref_actor(g, s->pending.hazard.enemy), &target, NULL);
    if (!q1_alive(g, id))
        return true;
    if (!target_exists) {
        if (!q1_sound(g, id, "misc/null.wav", 2, 1, error))
            return false;
        body.velocity = qa_v3(0, 0, 0);
    } else {
        qa_builtin_angle_vectors(body.angles, &g->forward, &g->right, &g->up);
        qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
        bool in_front = qa_vec_dot(qa_vec_normalize(delta), g->forward) > .3f;
        body.velocity = qa_vec_scale(qa_vec_normalize(qa_vec_add(delta, qa_v3(0, 0, 10))),
                                     (float)g->options.skill * 50.0f + (in_front ? 50.0f : 150.0f));
    }
    return !q1_alive(g, id) || qa_world_body_write(g->services.world, id, &body, error);
}

static bool gravity_pull(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    qa_body_state self;
    if (!qa_world_body_read(g->services.world, id, &self, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    qa_builtin_snapshot_frame *list;
    if (!scan(g, e, e->map->distance, e->spawnflags & 1, true, &list, error))
        return false;
    if (!list)
        return true;
    bool ok = true;
    for (size_t i = 0; ok && i < list->snapshot.count && q1_alive(g, id); ++i) {
        qa_actor_id actor = list->snapshot.ids[i];
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, actor, &body, NULL))
            continue;
        if (!q1_alive(g, id))
            break;
        if (!q1_alive(g, actor))
            continue;
        float speed = e->speed;
        if ((e->spawnflags & 2) && qa_q1_game_power_expires(g, actor, QA_Q1_WETSUIT) > g->time)
            speed *= .6f;
        body.velocity = qa_vec_add(body.velocity,
            qa_vec_scale(qa_vec_normalize(qa_vec_sub(self.origin, body.origin)), speed));
        ok = qa_world_body_write(g->services.world, actor, &body, error);
        if (ok && q1_alive(g, actor) && g->services.motion_changed) {
            qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = body};
            ok = g->services.motion_changed(g->services.context, actor, &change, error);
        }
    }
    qa_builtin_snapshot_release(list);
    return ok && (!q1_alive(g, id) || q1_map_schedule(g, e, .1, Q1_MAP_GRAVITY_PULL, error));
}

bool q1_map_hip_hazard_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_state *s = e->map;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, e->id, &body, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    e->physics.solid = QA_PHYSICS_NOT_SOLID;
    e->physics.motion = QA_PHYSICS_STATIONARY;
    s->use_enabled = true;
    s->pending.hazard.switch_due = s->initial_think;
    switch (s->kind) {
    case Q1_MAP_SPIKE_MINE:
        if (g->options.deathmatch)
            return q1_remove(g, e, error);
        e->physics.solid = QA_PHYSICS_BOX;
        e->physics.motion = QA_PHYSICS_FLY_MISSILE;
        e->physics.flags |= QA_PHYSICS_MONSTER;
        e->physics.angular_velocity = qa_v3(-50, 100, 150);
        e->max_health = g->options.skill <= 1 ? 200 : 400;
        ++g->total_monsters;
        s->touch_enabled = true;
        s->use_enabled = false;
        if (!q1_model(g, e, "progs/spikmine.mdl", error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (!qa_combat_set_health(g->services.combat, id, e->max_health, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (!q1_link(g, e, error))
            return false;
        return !q1_alive(g, id) || q1_map_schedule(g, e, .2, Q1_MAP_MINE_FIRST, error);
    case Q1_MAP_HIP_LIGHTNING:
        s->pending.hazard.enabled = true;
        /* fall through */
    case Q1_MAP_HIP_LIGHTNING_TRIGGERED:
    case Q1_MAP_HIP_LIGHTNING_SWITCHED:
        e->wait = e->wait != 0 ? e->wait : 1;
        e->damage = e->damage != 0 ? e->damage : 30;
        s->duration = s->duration != 0 ? s->duration : .1f;
        return q1_map_schedule(g, e, .25, Q1_MAP_HIP_LIGHTNING_FIRST, error);
    case Q1_MAP_TESLA:
    case Q1_MAP_GODS_WRATH:
        e->wait = e->wait != 0 ? e->wait : 2;
        e->damage = e->damage != 0 ? e->damage : 2.0f + 5.0f * (float)g->options.skill;
        s->duration = s->duration != 0 ? s->duration : -1;
        s->distance = s->distance != 0 ? s->distance : 600;
        e->delay = e->delay != 0 ? e->delay : s->kind == Q1_MAP_GODS_WRATH ? 5 : -1;
        float initial_delay = q1_random(g);
        s->pending.hazard.switch_due = 0;
        if (s->kind == Q1_MAP_GODS_WRATH) {
            e->wait = 0;
            s->pending.hazard.enabled = true;
            return true;
        }
        return q1_map_schedule(g, e, initial_delay, Q1_MAP_TESLA_TICK, error);
    case Q1_MAP_GRAVITY_WELL:
        s->use_enabled = false;
        s->touch_enabled = true;
        e->physics.solid = QA_PHYSICS_TRIGGER;
        e->damage = e->damage != 0 ? e->damage : 10000;
        e->speed = e->speed != 0 ? e->speed : 210;
        s->distance = s->distance != 0 ? s->distance : 600;
        body.bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (!q1_link(g, e, error))
            return false;
        return !q1_alive(g, id) || q1_map_schedule(g, e, .1, Q1_MAP_GRAVITY_PULL, error);
    default:
        return q1_map_fail(error, "Invalid Hipnotic hazard spawn kind");
    }
}

bool q1_map_hip_hazard_use(qa_q1_game *g, q1_actor *e, qa_actor_id activator, qa_error *error) {
    q1_map_state *s = e->map;
    qa_actor_id id = e->id;
    if (s->kind == Q1_MAP_SPIKE_MINE) {
        qa_q1_target target;
        if (q1_target(g, activator, &target) && q1_alive(g, id) &&
            target.player && !target.invisible) {
            s->pending.hazard.enemy = q1_ref_from(g, activator);
            return q1_map_schedule(g, e, .1, Q1_MAP_MINE_HOME, error);
        }
        return true;
    }
    if (s->kind == Q1_MAP_GODS_WRATH) {
        if (s->pending.hazard.attack)
            return true;
        s->pending.hazard.search_until = g->time + e->delay;
        s->pending.hazard.last_victim = q1_ref_from(g, activator);
        return tesla_tick(g, e, error);
    }
    if (s->kind == Q1_MAP_TESLA || s->kind == Q1_MAP_HIP_LIGHTNING_SWITCHED) {
        s->pending.hazard.enabled = !s->pending.hazard.enabled;
        if (s->pending.hazard.enabled && e->think != Q1_THINK_NONE)
            return q1_map_schedule(g, e, s->pending.hazard.switch_due - g->time,
                        s->kind == Q1_MAP_TESLA ? Q1_MAP_TESLA_TICK : s->action, error);
        return true;
    }
    return lightning_use(g, e, error);
}

bool q1_map_hip_hazard_reaction(qa_q1_game *g, q1_actor *e,
                                const qa_damage_outcome *outcome, qa_error *error) {
    if (e->map->kind != Q1_MAP_SPIKE_MINE || outcome->result.reaction != QA_REACTION_DEATH ||
        e->map->pending.hazard.killed)
        return true;
    qa_actor_id id = e->id;
    e->map->pending.hazard.killed = true;
    if (!q1_map_damageable(g, e, false, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_monster_death_report(g, e, outcome->request.attack.attacker, true, error))
        return false;
    if (!q1_alive(g, id))
        return true;
    if (!q1_map_targets(g, e, outcome->request.attack.attacker, error))
        return false;
    return !q1_alive(g, id) || mine_explode(g, e, error);
}

bool q1_map_hip_hazard_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (e->map->kind == Q1_MAP_GRAVITY_WELL) {
        if (e->map->cooldown > g->time || !q1_damageable(g, other))
            return true;
        qa_actor_id id = e->id;
        if (!q1_damage(g, other, id, id, e->damage, QA_Q1_WEAPON_COUNT, error))
            return false;
        if (q1_alive(g, id))
            e->map->cooldown = g->time + .2;
        return true;
    }
    if (e->map->kind != Q1_MAP_SPIKE_MINE)
        return true;
    qa_actor_id id = e->id;
    float health = q1_health(g, id);
    if (health > 0) {
        static const q1_runtime_name ignored[] = {Q1_NAME_TRAP_SPIKE_MINE, Q1_NAME_MISSILE, Q1_NAME_GRENADE,
                                              Q1_NAME_HIPLASER, Q1_NAME_PROXIMITY_GRENADE};
        for (size_t i = 0; i < sizeof(ignored) / sizeof(*ignored); ++i)
            if (q1_classnamed(g, other, g->runtime_names[ignored[i]]))
                return true;
        if (!q1_damage(g, id, id, id, health + 10, QA_Q1_WEAPON_COUNT, error))
            return false;
    }
    return !q1_alive(g, id) || mine_explode(g, e, error);
}

bool q1_map_hip_hazard_think(qa_q1_game *g, q1_actor *e, q1_map_action action,
                             qa_error *error) {
    q1_map_state *s = e->map;
    switch (action) {
    case Q1_MAP_MINE_FIRST: {
        qa_actor_id id = e->id;
        e->aimed_damage = true;
        s->use_enabled = true;
        if (!q1_map_damageable(g, e, true, error))
            return false;
        return !q1_alive(g, id) || q1_map_schedule(g, e, .1, Q1_MAP_MINE_HOME, error);
    }
    case Q1_MAP_MINE_HOME:
        return mine_home(g, e, error);
    case Q1_MAP_HIP_LIGHTNING_FIRST: {
        if (q1_map_text(g, e->target)) {
            qa_target_cursor cursor = {0};
            qa_actor_id target = {0};
            (void)qa_targets_next(g->maps->options.targets, e->target, &cursor, &target);
            s->pending.hazard.enemy = q1_ref_from(g, target);
        }
        if (s->kind == Q1_MAP_HIP_LIGHTNING_TRIGGERED) {
            q1_map_cancel(g, e);
            return true;
        }
        return q1_map_schedule(g, e, s->pending.hazard.switch_due + e->wait - g->time,
                               Q1_MAP_HIP_LIGHTNING_TICK, error);
    }
    case Q1_MAP_HIP_LIGHTNING_TICK: {
        qa_actor_id id = e->id;
        if (s->pending.hazard.enabled && !lightning_use(g, e, error))
            return false;
        if (!q1_alive(g, id))
            return true;
        if (!s->pending.hazard.pulsing) {
            double pause = (e->spawnflags & 1) ? e->wait * q1_random(g) : e->wait;
            s->pending.hazard.pulsing = true;
            s->pending.hazard.pulse_until = g->time + s->duration - .1;
            s->pending.hazard.sound_after = fmax(s->pending.hazard.pulse_until, g->time + .3);
            s->pending.hazard.cycle_until = g->time + fmax(pause, s->duration);
        }
        if (g->time >= s->pending.hazard.pulse_until) {
            s->pending.hazard.pulsing = false;
            return q1_map_schedule(g, e, s->pending.hazard.cycle_until - g->time,
                                   Q1_MAP_HIP_LIGHTNING_TICK, error);
        }
        return q1_map_schedule(g, e, .2, Q1_MAP_HIP_LIGHTNING_TICK, error);
    }
    case Q1_MAP_HIP_BOLT_TICK:
        return bolt(g, e, false, error);
    case Q1_MAP_TESLA_BOLT_TICK:
        return bolt(g, e, true, error);
    case Q1_MAP_TESLA_TICK:
        return tesla_tick(g, e, error);
    case Q1_MAP_GRAVITY_PULL:
        return gravity_pull(g, e, error);
    default:
        return q1_map_fail(error, "Invalid Hipnotic hazard continuation");
    }
}
