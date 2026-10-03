#include "internal.h"

static q1_actor *field(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_addon_field(g, e->map->kind) ? e : NULL;
}
q1_addon_contact *q1_map_addon_contact(qa_q1_game *g, qa_actor_id id, bool create, qa_error *error) {
    if (!q1_alive(g, id) || id.slot >= g->capacity)
        return NULL;
    if (!g->maps->addon_contacts) {
        if (!create)
            return NULL;
        g->maps->addon_contacts = calloc(g->capacity, sizeof(*g->maps->addon_contacts));
        if (!g->maps->addon_contacts) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 addon contact continuations");
            return NULL;
        }
    }
    q1_addon_contact *row = &g->maps->addon_contacts[id.slot];
    if (!qa_actor_id_equal(row->actor, id)) {
        if (!create)
            return NULL;
        *row = (q1_addon_contact){.actor = id};
    }
    return row;
}
void q1_map_addon_released(qa_q1_game *g, qa_actor_id id) {
    if (!g->maps || !g->maps->addon_contacts || id.slot >= g->capacity)
        return;
    q1_addon_contact *row = &g->maps->addon_contacts[id.slot];
    if (qa_actor_id_equal(row->actor, id))
        *row = (q1_addon_contact){0};
}
void q1_map_addon_clone(qa_q1_game *g, qa_actor_id source, qa_actor_id destination) {
    q1_addon_contact *row = g->maps ? q1_map_addon_contact(g, source, false, NULL) : NULL;
    if (row && destination.slot < g->capacity) {
        g->maps->addon_contacts[destination.slot] = *row;
        g->maps->addon_contacts[destination.slot].actor = destination;
    }
}
bool q1_map_addon_hunger(qa_q1_game *g, qa_actor_id id, float value, qa_error *error) {
    if (!isfinite(value))
        return q1_map_fail(error, "invalid Q1 addon hunger deadline");
    q1_addon_contact *row = q1_map_addon_contact(g, id, true, error);
    if (!row)
        return false;
    row->hunger_time = value;
    row->has_hunger = true;
    return true;
}
bool q1_map_addon_lore_admit(qa_q1_game *g, qa_actor_id id, bool *admitted, qa_error *error) {
    *admitted = false;
    q1_addon_contact *row = q1_map_addon_contact(g, id, true, error);
    if (!row)
        return !q1_alive(g, id);
    if (g->time > row->lore_active) {
        row->lore_active = g->time + .5;
        *admitted = true;
    }
    return true;
}
bool q1_map_addon_quad_mark(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_addon_contact *row = q1_map_addon_contact(g, id, true, error);
    if (!row)
        return !q1_alive(g, id);
    row->super_time = 2;
    return true;
}
bool q1_map_addon_field_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    if ((!g->options.coop && (e->spawnflags & 32768u)) ||
        (g->options.program == QA_Q1_MG3 &&
         (e->spawnflags & (262144u << qa_q1_mg3_rune_count(*g->maps->options.server_flags)))))
        return q1_remove(g, e, error);
    q1_map_kind kind = e->map->kind;
    if (kind == Q1_MAP_PUSH && g->options.program == QA_Q1_MG3) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        e = field(g, id);
        if (!e)
            return true;
        qa_vec3 direction = e->map->movedir;
        if (qa_vec_dot(body.angles, body.angles) == 0 && qa_vec_dot(direction, direction) == 0) {
            e->map->movedir = qa_v3(1, 0, 0);
            e->map->has_movedir = true;
        } else if (qa_vec_dot(direction, direction) != 0) {
            e->map->movedir = qa_vec_normalize(direction);
            e->map->has_movedir = true;
        }
    }
    if (!q1_map_trigger_init(g, e, true, error))
        return false;
    e = field(g, id);
    if (!e)
        return true;
    e->map->touch_enabled = true;
    e->map->use_enabled = kind != Q1_MAP_SHELTER;
    if (kind == Q1_MAP_HURT) {
        e->damage = e->damage != 0 ? e->damage : 5;
        e->wait = e->wait != 0 ? e->wait : 1;
        if (e->spawnflags & 1)
            e->map->field_state = 1;
        if (!qa_builtin_resource(&g->services, "trigger_hurt", &e->map->netname, error))
            return false;
    } else if (kind == Q1_MAP_PUSH) {
        e->speed = e->speed != 0 ? e->speed : 1000;
        if (e->spawnflags & 4)
            e->physics.solid = QA_PHYSICS_NOT_SOLID;
        if (!qa_builtin_resource(&g->services, "trigger_push", &e->map->netname, error))
            return false;
    } else {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        e = field(g, id);
        if (!e)
            return true;
        qa_vec3 size = qa_vec_sub(body.bounds.maxs, body.bounds.mins);
        e->map->pending.addon.origin = qa_vec_add(body.bounds.mins, qa_vec_scale(size, .5f));
        e->map->movedir = size.x < size.y
            ? size.x < size.z ? qa_v3(1, 0, 0) : qa_v3(0, 0, 1)
            : size.y < size.z ? qa_v3(0, 1, 0) : qa_v3(0, 0, 1);
        if (e->spawnflags & 1)
            e->map->movedir = qa_vec_scale(e->map->movedir, -1);
    }
    e = field(g, id);
    return !e || q1_link(g, e, error);
}
bool q1_map_addon_field_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->map->kind == Q1_MAP_HURT) {
        e->map->field_state = 1 - e->map->field_state;
        return true;
    }
    if (e->map->kind != Q1_MAP_PUSH)
        return true;
    qa_actor_id id = e->id;
    bool enable = e->physics.solid != QA_PHYSICS_TRIGGER;
    e->physics.solid = enable ? QA_PHYSICS_TRIGGER : QA_PHYSICS_NOT_SOLID;
    if (enable) {
        if (!g->host.force_retouch)
            return q1_map_fail(error, "Q1 push toggle requires source retouch service");
        if (!g->host.force_retouch(g->host.context, 1, error))
            return false;
    }
    e = field(g, id);
    return !e || q1_link(g, e, error);
}
static bool motion(qa_q1_game *g, qa_actor_id actor, const qa_body_state *body,
                     qa_error *error) {
    if (!qa_world_body_write(g->services.world, actor, body, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_LAUNCH, .body = *body};
    return g->services.motion_changed(g->services.context, actor, &change, error);
}
static bool horde_manager(const qa_q1_game *g) {
    for (uint32_t i = 0; i < g->capacity; ++i) {
        q1_actor *e = g->actors[i];
        if (!e || !e->active)
            continue;
        qa_bytes name = qa_strings_text(qa_session_strings(g->services.session), e->classname);
        if (name.size == 13 && !memcmp(name.data, "horde_manager", 13))
            return true;
    }
    return false;
}
static bool push(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    qa_actor_id id = e->id;
    qa_vec3 direction = e->map->movedir;
    float speed = e->speed;
    uint32_t flags = e->spawnflags;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, other, &body, error))
        return false;
    if (!field(g, id) || !q1_alive(g, other))
        return true;
    qa_builtin_actor_traits traits = {0};
    g->services.actor_traits(g->services.context, other, &traits);
    if (!field(g, id) || !q1_alive(g, other))
        return true;
    qa_vec3 delta = qa_vec_scale(direction, speed * 10);
    qa_vec3 velocity = flags & 2 ? qa_vec_add(body.velocity, qa_vec_scale(delta, (float)g->elapsed))
                                : delta;
    if (g->options.program != QA_Q1_MG3 && horde_manager(g)) {
        q1_actor *native = q1_entity(g, other);
        if (native)
            native->spawnflags &= ~4u;
        if (!traits.player) {
            if (q1_classnamed(g, other, "item_artifact_invulnerability") ||
                q1_classnamed(g, other, "item_artifact_super_damage")) {
                if (!field(g, id) || !q1_alive(g, other))
                    return true;
                body.velocity = velocity;
                return motion(g, other, &body, error);
            } else
                return true;
        }
    }
    bool living = q1_health(g, other) > 0;
    bool grenade = living ? false : q1_classnamed(g, other, "grenade");
    if (!field(g, id) || !q1_alive(g, other))
        return true;
    if (living || grenade) {
        q1_addon_contact *row = q1_map_addon_contact(g, other, false, NULL);
        if (row && row->sheltered)
            return true;
        bool jump = g->options.program == QA_Q1_MG3 && (flags & 16);
        if (jump && !body.ground.registry)
            return true;
        body.velocity = velocity;
        if (traits.monster)
            body.ground = (qa_actor_id){0};
        if (!motion(g, other, &body, error))
            return false;
        if (!field(g, id) || !q1_alive(g, other))
            return true;
        q1_actor *native = q1_entity(g, other);
        if (native && traits.monster)
            native->physics.flags &= ~(uint32_t)QA_PHYSICS_ONGROUND;
        if (traits.player) {
            row = q1_map_addon_contact(g, other, true, error);
            if (!row)
                return !q1_alive(g, other);
            if (row->fly_sound < g->time) {
                row->fly_sound = g->time + (jump ? .5 : 1.5);
                if (!q1_sound(g, other, jump ? "weapons/sgun1.wav"
                                : g->options.program == QA_Q1_MG3 && (flags & 8)
                                    ? "player/inh2o.wav" : "ambience/windfly.wav", 0, 1, error))
                    return false;
            }
        }
    }
    e = field(g, id);
    return !e || !(flags & 1) || q1_remove(g, e, error);
}
bool q1_map_addon_field_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other,
                              qa_error *error) {
    qa_actor_id id = e->id;
    if (e->map->kind == Q1_MAP_PUSH)
        return push(g, e, other, error);
    if (e->map->kind == Q1_MAP_HURT) {
        if (e->physics.solid != QA_PHYSICS_TRIGGER ||
            e->map->field_state != 0 || !q1_damageable(g, other))
            return true;
        e = field(g, id);
        if (!e || !q1_alive(g, other))
            return true;
        if (e->spawnflags & 8) {
            qa_builtin_actor_traits traits = {0};
            bool monster = g->services.actor_traits(g->services.context, other, &traits) && traits.monster;
            e = field(g, id);
            if (!e || !monster || !q1_alive(g, other))
                return true;
        }
        float amount = e->damage;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        if (!q1_damage(g, other, id, id, amount, QA_Q1_WEAPON_COUNT, error))
            return false;
        e = field(g, id);
        return !e || q1_map_schedule(g, e, e->wait, Q1_MAP_REARM, error);
    }
    if (q1_health(g, other) <= 0 && !q1_classnamed(g, other, "grenade"))
        return true;
    e = field(g, id);
    if (!e || !q1_alive(g, other))
        return true;
    qa_vec3 plane = e->map->pending.addon.origin, normal = e->map->movedir;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, other, &body, error))
        return false;
    if (!field(g, id) || !q1_alive(g, other))
        return true;
    q1_addon_contact *row = q1_map_addon_contact(g, other, true, error);
    if (!row)
        return !q1_alive(g, other);
    row->sheltered = qa_vec_dot(qa_vec_sub(body.origin, plane), normal) >= 0;
    return true;
}
