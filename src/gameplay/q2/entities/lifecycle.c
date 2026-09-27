#include "internal.h"
#include "qa/text.h"

static int integer(float n) {
    return n >= 2147483647.0f ? INT_MAX : n <= -2147483648.0f ? INT_MIN : (int)n;
}
static uint32_t flags(qa_q2_game *g, q2_entity_state *s) {
    const char *text = q2_field_text(g, s, "spawnflags");
    double v;
    if (!qa_parse_number((qa_bytes){(const uint8_t *)text, strlen(text)}, &v, NULL) ||
        !isfinite(v) || v < INT32_MIN || v > UINT32_MAX)
        return 0;
    v = trunc(v);
    return (uint32_t)(v < 0 ? v + 4294967296.0 : v);
}
bool qa_q2_entity_spawn(qa_q2_game *g, qa_actor_id id, const qa_q2_map_fields *fields,
                        bool *handled, qa_error *e) {
    if (!g || !fields || !handled || (fields->count && !fields->properties) ||
        fields->count > SIZE_MAX / sizeof(q2_field) || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 map entity");
        return false;
    }
    *handled = false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        return false;
    if (a->entity) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 map entity already initialized");
        return false;
    }
    q2_entity_state *s = calloc(1, sizeof(*s));
    if (!s) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 map entity");
        return false;
    }
    if (fields->count) {
        s->fields = calloc(fields->count, sizeof(*s->fields));
        if (!s->fields) {
            free(s);
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q2 authored fields");
            return false;
        }
        qa_strings *strings = qa_session_strings(g->services.session);
        for (size_t i = 0; i < fields->count; i++) {
            if (!qa_strings_intern(strings, fields->properties[i].key, &s->fields[i].key, e) ||
                !qa_strings_intern(strings, fields->properties[i].value, &s->fields[i].value, e)) {
                free(s->fields);
                free(s);
                return false;
            }
        }
        s->field_count = fields->count;
    }
    a->entity = s;
    a->entity_game = g;
    s->classname = q2_field_id(g, s, "classname");
    if (!s->classname) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
        s->classname = record->definition;
    }
    if (!qa_strings_cstr(qa_session_strings(g->services.session), s->classname)) {
        q2_entity_release_state(a);
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 map entity requires a classname");
        return false;
    }
    s->ordinal = fields->ordinal;
    s->spawnflags = flags(g, s);
    s->targetname = q2_field_id(g, s, "targetname");
    s->target = q2_field_id(g, s, "target");
    s->killtarget = q2_field_id(g, s, "killtarget");
    s->message = q2_field_id(g, s, "message");
    s->team = q2_field_id(g, s, "team");
    s->map = q2_field_id(g, s, "map");
    s->noise = q2_field_id(g, s, "noise");
    s->speed = q2_field_float(g, s, "speed", 0);
    s->accel = q2_field_float(g, s, "accel", 0);
    s->decel = q2_field_float(g, s, "decel", 0);
    s->wait = q2_field_float(g, s, "wait", 0);
    s->delay = q2_field_float(g, s, "delay", 0);
    s->damage = q2_field_float(g, s, "dmg", 0);
    s->health = q2_field_float(g, s, "health", 0);
    s->count = integer(q2_field_float(g, s, "count", 0));
    s->style = integer(q2_field_float(g, s, "style", 0));
    s->volume = q2_field_float(g, s, "volume", 0);
    s->attenuation = q2_field_float(g, s, "attenuation", 0);
    s->visual = (qa_q2_visual){.scale = q2_field_float(g, s, "scale", 1),
                               .alpha = q2_field_float(g, s, "alpha", 1),
                               .old_frame = -1};
    s->visual.models[0] = q2_field_id(g, s, "model");
    s->visual.skin = integer(q2_field_float(g, s, "skinnum", 0));
    if (g->options.edition == QA_Q2_RERELEASE) {
        const char *color = q2_field_text(g, s, "rgba");
        if (!*color)
            color = q2_field_text(g, s, "_color");
        if (*color)
            s->visual.skin = (int32_t)q2_entity_color(color);
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    const char *model = q2_field_text(g, s, "model");
    if (*model == '*') {
        char *end;
        errno = 0;
        unsigned long number = strtoul(model + 1, &end, 10);
        if (end == model + 1 || *end || errno || number > UINT32_MAX) {
            qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 inline model");
            return false;
        }
        s->has_inline = true;
        s->collision.model = (uint32_t)number;
        if (!qa_collision_model_bounds(qa_world_geometry(g->services.world), s->collision.model,
                                       &body.bounds, e))
            return false;
        if (!qa_world_body_write(g->services.world, id, &body, e))
            return false;
    }
    if (!a->physics_bound) {
        a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
        a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    }
    if (!q2_entity_bind(g, a, e))
        return false;
    if (!q2_player_map_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (!q2_light_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (g->options.edition == QA_Q2_RERELEASE && !q2_q64_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (g->options.edition == QA_Q2_RERELEASE && !q2_rerelease_entity_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (!q2_trigger_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (!q2_target_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (!q2_turret_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    if (!q2_mover_spawn(g, a, handled, e))
        return false;
    if (!q2_actor_live(g, id) || *handled)
        return true;
    return q2_scenery_spawn(g, a, handled, e);
}
bool qa_q2_entity_use(qa_q2_game *g, qa_actor_id id, qa_actor_id other, qa_actor_id activator,
                      qa_error *e) {
    q2_actor *a = q2_ent(g, id);
    if (a && a->projectile.kind == Q2_PROJECTILE_NONE && a->item &&
        (a->item->spawn.spawnflags & 1)) {
        a->item->spawn.spawnflags &= ~1u;
        return qa_q2_item_enable(g, id, e);
    }
    if (!a || !a->entity->usable || a->projectile.kind != Q2_PROJECTILE_NONE)
        return true;
    if (a->entity->kind == Q2E_DYNAMIC_LIGHT)
        return q2_light_use(g, a, e);
    if (a->entity->kind == Q2E_CAMERA)
        return q2_q64_use(g, a, activator, e);
    bool handled;
    if (g->options.edition == QA_Q2_RERELEASE) {
        if (!q2_rerelease_entity_use(g, a, other, activator, &handled, e))
            return false;
        if (handled || !q2_actor_live(g, id))
            return true;
    }
    if (!q2_trigger_use(g, a, other, activator, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, id))
        return true;
    if (!q2_target_use(g, a, other, activator, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, id))
        return true;
    if (!q2_mover_use(g, a, other, activator, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, id))
        return true;
    return q2_scenery_use(g, a, other, activator, &handled, e);
}
bool q2_entity_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (a->projectile.kind != Q2_PROJECTILE_NONE || !a->entity)
        return true;
    if (!q2_scenery_prethink(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id) || a->entity->think == Q2ET_NONE || a->entity->due_ns > g->now_ns)
        return true;
    q2_entity_think think = a->entity->think;
    a->entity->think = Q2ET_NONE;
    a->entity->due_ns = 0;
    if (think == Q2ET_FREE)
        return qa_session_release(g->services.session, a->id, e);
    if (think == Q2ET_DYNAMIC_LIGHT)
        return q2_light_think(g, a, e);
    if (think >= Q2ET_TURRET_INIT && think <= Q2ET_TURRET_DRIVER)
        return q2_turret_think(g, a, think, e);
    if (think >= Q2ET_PLAYER_SECURITY && think <= Q2ET_PLAYER_START_DROP)
        return q2_player_map_think(g, a, think, e);
    if (think >= Q2ET_EYE_SETUP && think <= Q2ET_CAMERA_DUMMY)
        return q2_q64_think(g, a, think, e);
    if (think >= Q2ET_MOVE_BEGIN && think <= Q2ET_MOVE_ACCEL)
        return q2_move_tick(g, a, think, e);
    bool handled;
    if (g->options.edition == QA_Q2_RERELEASE) {
        if (!q2_rerelease_entity_think(g, a, think, &handled, e))
            return false;
        if (handled || !q2_actor_live(g, a->id))
            return true;
    }
    if (!q2_trigger_think(g, a, think, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, a->id))
        return true;
    if (!q2_target_think(g, a, think, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, a->id))
        return true;
    if (!q2_mover_think(g, a, think, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, a->id))
        return true;
    if (think == Q2ET_SCENERY)
        return q2_scenery_think(g, a, &handled, e);
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Q2 entity has an invalid think continuation");
    return false;
}
bool q2_entity_touch(qa_q2_game *g, const qa_touch_contact *contact, qa_error *e) {
    q2_actor *a = q2_ent(g, contact->self);
    if (!a || !a->entity->touchable || a->projectile.kind != Q2_PROJECTILE_NONE ||
        a->physics.solid == QA_PHYSICS_NOT_SOLID)
        return true;
    bool handled;
    if (g->options.edition == QA_Q2_RERELEASE) {
        if (!q2_rerelease_entity_touch(g, a, contact, &handled, e))
            return false;
        if (handled || !q2_actor_live(g, a->id))
            return true;
    }
    if (!q2_trigger_touch(g, a, contact, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, a->id))
        return true;
    if (!q2_mover_touch(g, a, contact, &handled, e))
        return false;
    if (handled || !q2_actor_live(g, a->id))
        return true;
    return q2_scenery_touch(g, a, contact, &handled, e);
}
bool qa_q2_entity_blocked(qa_q2_game *g, qa_actor_id id, qa_actor_id obstacle, qa_error *e) {
    q2_actor *a = q2_ent(g, id);
    if (a && (a->entity->kind == Q2E_TURRET_BASE || a->entity->kind == Q2E_TURRET_BREACH))
        return q2_turret_blocked(g, a, obstacle, e);
    return !a || q2_mover_blocked(g, a, obstacle, e);
}
bool q2_entity_reaction(qa_q2_game *g, const qa_damage_outcome *o, qa_error *e) {
    q2_actor *a = q2_ent(g, o->request.target);
    if (!a || a->projectile.kind != Q2_PROJECTILE_NONE)
        return true;
    return a->entity->mover ? q2_mover_reaction(g, a, o, e) : q2_scenery_reaction(g, a, o, e);
}
bool qa_q2_entity_field(qa_q2_game *g, qa_actor_id id, const char *key, qa_string_id *value) {
    q2_actor *a = q2_ent(g, id);
    if (!a || !key || !value)
        return false;
    *value = q2_field_id(g, a->entity, key);
    return true;
}
bool qa_q2_entity_team(qa_q2_game *g, qa_actor_id id, qa_actor_id *master, qa_actor_id *next) {
    q2_actor *a = q2_ent(g, id);
    if (!a || !a->entity->mover || !master || !next)
        return false;
    *master = a->entity->mover->master;
    *next = a->entity->mover->next;
    return true;
}
bool qa_q2_entities_post_spawn(qa_q2_game *g, qa_error *e) {
    if (!g) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 provider");
        return false;
    }
    for (q2_actor *a = g->first_actor; a; a = a->live_next) {
        if (!a->entity || !a->entity->mover)
            continue;
        q2_entity_state *s = a->entity;
        q2_mover *m = s->mover;
        m->master = a->id;
        m->next = (qa_actor_id){0};
        a->physics.flags &= ~QA_PHYSICS_TEAM_SLAVE;
        if (!s->team)
            continue;
        q2_actor *last = NULL;
        for (q2_actor *b = g->first_actor; b; b = b->live_next) {
            if (!b->entity || !b->entity->mover || b->entity->team != s->team)
                continue;
            if (!last)
                m->master = b->id;
            if (qa_actor_id_equal(b->id, a->id)) {
                if (!qa_actor_id_equal(m->master, a->id))
                    a->physics.flags |= QA_PHYSICS_TEAM_SLAVE;
            }
            if (last && qa_actor_id_equal(last->id, a->id)) {
                m->next = b->id;
                break;
            }
            last = b;
        }
    }
    return true;
}
