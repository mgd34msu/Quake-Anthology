#include "internal.h"
#include <float.h>

enum { ROTATE_TRAIN_FIND, ROTATE_TRAIN_NEXT, ROTATE_TRAIN_WAIT, ROTATE_TRAIN_STOP };
static bool train_action(qa_q1_game *, qa_actor_id, qa_error *);
static bool train_tick(qa_q1_game *, qa_actor_id, qa_error *);

static q1_actor *rotation(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_rotation(e->map->kind) ? e : NULL;
}
static bool same_text(qa_q1_game *g, qa_string_id id, const char *text) {
    qa_bytes value = qa_strings_text(qa_session_strings(g->services.session), id);
    return value.size == strlen(text) && !memcmp(value.data, text, value.size);
}
static qa_string_id text_field(qa_q1_game *g, qa_actor_id actor, const char *key) {
    qa_target_field value;
    return qa_targets_field(g->maps->options.targets, actor, key, &value) &&
                   value.kind == QA_TARGET_FIELD_TEXT ? value.value.text : (qa_string_id){0};
}
static qa_vec3 normalized(qa_vec3 angles) {
    return qa_v3(angles.x - floorf(angles.x / 360) * 360,
                 angles.y - floorf(angles.y / 360) * 360,
                 angles.z - floorf(angles.z / 360) * 360);
}
static bool publish(qa_q1_game *g, qa_actor_id actor, const qa_body_state *body,
                     bool teleport, qa_error *error) {
    if (!qa_world_body_write(g->services.world, actor, body, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    qa_builtin_motion_change change = {.body = *body,
        .reason = teleport ? QA_BUILTIN_MOTION_TELEPORT : QA_BUILTIN_MOTION_LAUNCH};
    if (!g->services.motion_changed(g->services.context, actor, &change, error))
        return false;
    return !q1_alive(g, actor) || qa_world_link(g->services.world, actor, NULL, error);
}
static bool target_snapshot(qa_q1_game *g, qa_string_id name,
                             q1_actor_snapshot **out, qa_error *error) {
    q1_actor_snapshot *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    size_t count = 0;
    if (q1_map_text(g, name))
        for (size_t i = 0; i < list->shared.count; ++i) {
            qa_authored_target fields;
            if (qa_targets_read(g->maps->options.targets, list->shared.ids[i], &fields) &&
                fields.targetname == name)
                list->shared.ids[count++] = list->shared.ids[i];
        }
    list->shared.count = count;
    *out = list;
    return true;
}
static bool link_targets(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    qa_string_id name = e->target;
    qa_body_state self;
    if (!qa_world_body_read(g->services.world, id, &self, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    if (!g->maps->rotated_targets) {
        g->maps->rotated_targets = calloc(g->capacity, sizeof(*g->maps->rotated_targets));
        if (!g->maps->rotated_targets) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 rotation targets");
            return false;
        }
    }
    e->map->pending.rotation.origin = self.origin;
    e->map->pending.rotation.linked = true;
    q1_actor_snapshot *list;
    if (!target_snapshot(g, name, &list, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < list->shared.count && rotation(g, id); ++i) {
        qa_actor_id actor = list->shared.ids[i];
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, actor, &body, error)) {
            ok = false;
            break;
        }
        if (!rotation(g, id))
            break;
        if (!q1_alive(g, actor))
            continue;
        qa_string_id classname = text_field(g, actor, "classname");
        bool wall = same_text(g, classname, "func_movewall");
        bool object = same_text(g, classname, "rotate_object");
        qa_vec3 center = wall ? qa_vec_add(body.origin,
            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f)) : body.origin;
        qa_vec3 relative = qa_vec_sub(center, self.origin);
        g->maps->rotated_targets[actor.slot] = (q1_rotate_target){.actor = actor, .owner = id,
            .original = relative, .current = relative, .type = wall ? 1 : object ? 0 : 2};
        q1_actor *native = q1_entity(g, actor);
        if ((wall || object) && native)
            native->owner = id;
    }
    list->borrowed = false;
    return ok;
}
typedef enum target_transform { TARGET_ROTATE, TARGET_FINAL, TARGET_PLACE } target_transform;
static bool transform_targets(qa_q1_game *g, qa_actor_id id, target_transform mode,
                                qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    qa_string_id name = e->target;
    qa_vec3 original = e->map->pending.rotation.origin;
    qa_body_state self;
    if (!qa_world_body_read(g->services.world, id, &self, error))
        return false;
    if (!rotation(g, id))
        return true;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(self.angles, &forward, &right, &up);
    q1_actor_snapshot *list;
    if (!target_snapshot(g, name, &list, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < list->shared.count && rotation(g, id); ++i) {
        qa_actor_id actor = list->shared.ids[i];
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, actor, &body, error)) {
            ok = false;
            break;
        }
        if (!rotation(g, id))
            break;
        if (!q1_alive(g, actor))
            continue;
        q1_rotate_target empty = {.actor = actor};
        q1_rotate_target *row = g->maps->rotated_targets
                                   ? &g->maps->rotated_targets[actor.slot] : &empty;
        if (!qa_actor_id_equal(row->actor, actor))
            row = &empty;
        if (mode == TARGET_FINAL) {
            body.velocity = qa_v3(0, 0, 0);
            if (row->type == 0)
                body.angles = self.angles;
        } else if (mode == TARGET_PLACE) {
            body.origin = row->type == 1
                ? qa_vec_add(qa_vec_sub(self.origin, original),
                             qa_vec_sub(row->current, row->original))
                : qa_vec_add(row->current, self.origin);
        } else {
            qa_vec3 next = qa_vec_add(qa_vec_add(qa_vec_scale(forward, row->original.x),
                                                 qa_vec_scale(right, -row->original.y)),
                                       qa_vec_scale(up, row->original.z));
            if (row->type == 1) {
                next = qa_vec_add(qa_vec_sub(self.origin, original),
                                    qa_vec_sub(next, row->original));
                body.velocity = qa_vec_scale(qa_vec_sub(next, body.origin), 25);
            } else {
                body.origin = qa_vec_add(next, self.origin);
                if (row->type == 0)
                    body.angles = self.angles;
            }
            row->current = next;
        }
        ok = publish(g, actor, &body, mode == TARGET_PLACE, error);
    }
    list->borrowed = false;
    return ok;
}
void q1_map_rotation_released(qa_q1_game *g, qa_actor_id id) {
    if (g->maps && g->maps->rotated_targets && id.slot < g->capacity) {
        q1_rotate_target *row = &g->maps->rotated_targets[id.slot];
        if (qa_actor_id_equal(row->actor, id))
            *row = (q1_rotate_target){0};
    }
}
static bool noise(qa_q1_game *g, qa_actor_id id, unsigned slot, qa_error *error) {
    q1_actor *e = rotation(g, id);
    return !e || !q1_map_text(g, e->map->noise[slot]) ||
        q1_sound_resource(g, id, e->map->noise[slot], 0, 1, 1, error);
}
static bool reverse_door(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e || e->map->kind != Q1_MAP_ROTATE_DOOR)
        return true;
    q1_map_rotation *r = &e->map->pending.rotation;
    bool closing = r->phase == 7;
    e->frame = 1 - e->frame;
    r->destination = closing ? r->dest2 : r->dest1;
    r->rate = qa_vec_scale(qa_vec_sub(r->destination, closing ? r->dest1 : r->dest2),
                            1 / e->speed);
    r->phase = closing ? 6 : 7;
    r->end_time = g->time + e->speed - (r->end_time - g->time);
    r->last_time = g->time;
    if (!noise(g, id, 2, error))
        return false;
    e = rotation(g, id);
    return !e || q1_map_schedule(g, e, .02, Q1_MAP_ROTATE_DOOR_TICK, error);
}
static bool reverse_group(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    qa_string_id group = e->map->group;
    if (!q1_map_text(g, group))
        return reverse_door(g, id, error);
    q1_actor_snapshot *list;
    if (!q1_snapshot_actors(g, &list, error))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < list->shared.count; ++i) {
        q1_actor *member = rotation(g, list->shared.ids[i]);
        if (member && member->map->kind == Q1_MAP_ROTATE_DOOR &&
            member->map->group == group)
            list->shared.ids[count++] = member->id;
    }
    list->shared.count = count;
    bool ok = true;
    for (size_t i = 0; ok && i < list->shared.count; ++i)
        ok = reverse_door(g, list->shared.ids[i], error);
    list->borrowed = false;
    return ok;
}
static bool continuous(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    q1_map_rotation *r = &e->map->pending.rotation;
    double elapsed = g->time - r->last_time;
    r->last_time = g->time;
    if (r->phase == 2) {
        e->count = fminf(1, e->count + (float)(e->map->counter_value * elapsed));
        elapsed *= e->count;
    } else if (r->phase == 3) {
        e->count -= (float)(e->map->counter_value * elapsed);
        if (e->count < 0) {
            if (!transform_targets(g, id, TARGET_FINAL, error))
                return false;
            e = rotation(g, id);
            if (e) {
                e->map->pending.rotation.phase = 1;
                q1_map_cancel(g, e);
            }
            return true;
        }
        elapsed *= e->count;
    }
    qa_vec3 rate = r->rate;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!rotation(g, id))
        return true;
    body.angles = normalized(qa_vec_add(body.angles, qa_vec_scale(rate, (float)elapsed)));
    if (!publish(g, id, &body, false, error) ||
        !transform_targets(g, id, TARGET_ROTATE, error))
        return false;
    e = rotation(g, id);
    return !e || q1_map_schedule(g, e, .02, Q1_MAP_ROTATE_TICK, error);
}
bool q1_map_rotation_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_rotation *r = &e->map->pending.rotation;
    if (e->map->kind == Q1_MAP_ROTATE_TRAIN) {
        if (r->next == ROTATE_TRAIN_FIND)
            return true;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        return !rotation(g, id) || body.velocity.x != 0 || body.velocity.y != 0 || body.velocity.z != 0 ||
               train_action(g, id, error);
    }
    if (e->map->kind == Q1_MAP_ROTATE_ENTITY) {
        e->frame = 1 - e->frame;
        if (r->phase == 0 && (e->spawnflags & 1)) {
            if (e->speed != 0) {
                e->count = 1;
                r->phase = 3;
            } else {
                r->phase = 1;
                q1_map_cancel(g, e);
            }
        } else if (r->phase == 1) {
            r->last_time = g->time;
            e->count = 0;
            r->phase = e->speed != 0 ? 2 : 0;
            return q1_map_schedule(g, e, .02, Q1_MAP_ROTATE_TICK, error);
        } else if (r->phase == 2) {
            if (e->spawnflags & 1)
                r->phase = 3;
        } else if (r->phase == 3)
            r->phase = 2;
        return true;
    }
    if (e->map->kind != Q1_MAP_ROTATE_DOOR || (r->phase != 4 && r->phase != 5))
        return true;
    if (!r->linked && !link_targets(g, id, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    e->frame = 1 - e->frame;
    bool closed = r->phase == 4;
    r->destination = closed ? r->dest2 : r->dest1;
    r->rate = qa_vec_scale(qa_vec_sub(r->destination, closed ? r->dest1 : r->dest2), 1 / e->speed);
    r->phase = closed ? 6 : 7;
    r->end_time = g->time + e->speed;
    r->last_time = g->time;
    if (!noise(g, id, 2, error))
        return false;
    e = rotation(g, id);
    return !e || q1_map_schedule(g, e, .01, Q1_MAP_ROTATE_DOOR_TICK, error);
}
bool q1_map_rotation_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, bool blocked,
                            qa_error *error) {
    if (e->map->kind != Q1_MAP_MOVEWALL || (blocked && e->physics.solid != QA_PHYSICS_BRUSH))
        return true;
    qa_actor_id id = e->id, owner_id = e->owner;
    q1_actor *owner = rotation(g, owner_id);
    if (!owner || g->time < owner->map->cooldown)
        return true;
    float damage = e->damage != 0 ? e->damage : owner->damage;
    if (blocked) {
        owner->map->cooldown = g->time + .5;
        if (owner->map->kind == Q1_MAP_ROTATE_DOOR && !reverse_group(g, owner_id, error))
            return false;
    }
    if (!q1_alive(g, id) || !q1_alive(g, owner_id))
        return true;
    if (damage != 0 && !q1_damage(g, other, id, owner_id, damage, QA_Q1_WEAPON_COUNT, error))
        return false;
    owner = rotation(g, owner_id);
    if (damage != 0 && owner)
        owner->map->cooldown = g->time + .5;
    return true;
}
static bool clock_tick(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    double pos = (g->time + e->map->counter_value) / e->count;
    float angle = (float)(360 * (pos - floor(pos)));
    qa_vec3 movedir = e->map->movedir;
    bool fire = q1_map_text(g, e->map->event) && e->map->pending.rotation.last_time > angle;
    if (fire) {
        qa_authored_target fields;
        if (!qa_targets_read(g->maps->options.targets, id, &fields))
            return q1_map_fail(error, "Hipnotic clock target owner is unavailable");
        fields.target = e->map->event;
        fields.message = (qa_string_id){0};
        qa_target_use use = {.source = id, .activator = e->activator,
                             .dialect = g->options.quakeworld ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE,
                             .fields = fields, .time_ns = g->time_ns};
        if (!qa_targets_use_request(g->maps->options.targets, &use, error))
            return false;
    }
    if (!rotation(g, id))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!rotation(g, id))
        return true;
    body.angles = qa_vec_scale(movedir, angle);
    if (!publish(g, id, &body, false, error) ||
        !transform_targets(g, id, TARGET_FINAL, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    e->map->pending.rotation.last_time = angle;
    return q1_map_schedule(g, e, 1, Q1_MAP_CLOCK_TICK, error);
}
bool q1_map_rotation_think(qa_q1_game *g, q1_actor *e, q1_map_action action, qa_error *error) {
    qa_actor_id id = e->id;
    if (action == Q1_MAP_ROTATE_TRAIN_TICK)
        return train_tick(g, id, error);
    if (action == Q1_MAP_ROTATE_FIRST || action == Q1_MAP_CLOCK_FIRST) {
        if (!link_targets(g, id, error))
            return false;
        e = rotation(g, id);
        if (!e)
            return true;
        if (action == Q1_MAP_CLOCK_FIRST)
            return clock_tick(g, id, error);
        e->map->use_enabled = true;
        e->map->pending.rotation.phase = e->spawnflags & 2 ? 0 : 1;
        if (!(e->spawnflags & 2))
            return true;
        e->map->pending.rotation.last_time = g->time;
        return q1_map_schedule(g, e, .02, Q1_MAP_ROTATE_TICK, error);
    }
    if (action == Q1_MAP_ROTATE_TICK)
        return continuous(g, id, error);
    if (action == Q1_MAP_CLOCK_TICK)
        return clock_tick(g, id, error);
    if (action == Q1_MAP_MOVEWALL_TICK) {
        e->map->pending.rotation.last_time = g->time;
        return q1_map_schedule(g, e, .02, action, error);
    }
    q1_map_rotation *r = &e->map->pending.rotation;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    if (action == Q1_MAP_ROTATE_DOOR_TICK) {
        double elapsed = g->time - r->last_time;
        r->last_time = g->time;
        bool moving = g->time < r->end_time;
        body.angles = moving ? qa_vec_add(body.angles, qa_vec_scale(r->rate, (float)elapsed))
                             : r->destination;
        if (!publish(g, id, &body, false, error) ||
            !transform_targets(g, id, TARGET_ROTATE, error))
            return false;
        e = rotation(g, id);
        return !e || q1_map_schedule(g, e, .01,
            moving ? Q1_MAP_ROTATE_DOOR_TICK : Q1_MAP_ROTATE_DOOR_DONE, error);
    }
    if (action != Q1_MAP_ROTATE_DOOR_DONE)
        return q1_map_fail(error, "Invalid Hipnotic rotation callback");
    r->last_time = g->time;
    e->frame = 1 - e->frame;
    body.angles = r->destination;
    if (!publish(g, id, &body, false, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    if (e->map->pending.rotation.phase == 6)
        e->map->pending.rotation.phase = 5;
    else if (e->spawnflags & 1)
        return reverse_group(g, id, error);
    else
        e->map->pending.rotation.phase = 4;
    return noise(g, id, 3, error) && transform_targets(g, id, TARGET_FINAL, error);
}
bool q1_map_rotation_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    if (kind == Q1_MAP_ROTATE_INFO)
        return q1_map_schedule(g, e, 2, Q1_MAP_REMOVE, error);
    if (kind == Q1_MAP_ROTATE_PATH)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    q1_map_state *s = e->map;
    q1_map_rotation *r = &s->pending.rotation;
    r->rate = s->rotate;
    r->last_time = g->time;
    e->physics.solid = QA_PHYSICS_NOT_SOLID;
    e->physics.motion = QA_PHYSICS_STATIONARY;
    if (kind == Q1_MAP_ROTATE_OBJECT)
        return true;
    q1_map_action action = Q1_MAP_IDLE;
    double delay = .1;
    if (kind == Q1_MAP_ROTATE_ENTITY) {
        if (e->speed != 0)
            s->counter_value = 1 / e->speed;
        if (!isfinite(s->counter_value))
            return q1_map_fail(error, "Hipnotic rotation acceleration is too small");
        action = Q1_MAP_ROTATE_FIRST;
    } else if (kind == Q1_MAP_ROTATE_DOOR) {
        if (!q1_map_text(g, e->target))
            return q1_map_fail(error, "Hipnotic rotating door has no target");
        r->dest2 = body.angles;
        body.angles = qa_v3(0, 0, 0);
        e->speed = e->speed != 0 ? e->speed : 2;
        e->damage = e->damage != 0 ? fmaxf(0, e->damage) : 2;
        r->phase = 4;
        s->use_enabled = true;
        s->sounds = s->sounds ? s->sounds : 1;
        const char *start = s->sounds == 2 ? "doors/airdoor1.wav"
                           : s->sounds == 3 ? "doors/basesec1.wav" : "doors/winch2.wav";
        const char *stop = s->sounds == 2 ? "doors/airdoor2.wav"
                          : s->sounds == 3 ? "doors/basesec2.wav" : "doors/drclos4.wav";
        if (!qa_builtin_resource(&g->services, start, &s->noise[2], error) ||
            !qa_builtin_resource(&g->services, stop, &s->noise[3], error))
            return false;
    } else if (kind == Q1_MAP_MOVEWALL) {
        e->physics.motion = QA_PHYSICS_PUSH;
        e->physics.solid = e->spawnflags & 4 ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_BRUSH;
        s->touch_enabled = (e->spawnflags & 2) != 0;
        body.angles = qa_v3(0, 0, 0);
        if (!(e->spawnflags & 1))
            e->model = (qa_string_id){0};
        action = Q1_MAP_MOVEWALL_TICK;
        delay = .02;
    } else if (kind == Q1_MAP_CLOCK) {
        qa_vec3 direction = q1_map_direction(body.angles);
        s->movedir = qa_v3(-direction.y, -direction.z, -direction.x);
        body.angles = qa_v3(0, 0, 0);
        e->count = e->count != 0 ? e->count : 60;
        s->counter_value = s->counter_value * e->count / 12;
        action = Q1_MAP_CLOCK_FIRST;
    } else if (kind == Q1_MAP_ROTATE_TRAIN) {
        if (!q1_map_text(g, e->target))
            return q1_map_fail(error, "Hipnotic rotating train has no target");
        e->speed = e->speed != 0 ? e->speed : 100;
        e->physics.motion = QA_PHYSICS_STEP;
        s->use_enabled = true;
        const char *resources[] = {s->sounds == 1 ? "plats/train2.wav" : "misc/null.wav",
                                   s->sounds == 1 ? "plats/train1.wav" : "misc/null.wav"};
        for (unsigned i = 0; i < 2; ++i)
            if (!q1_map_text(g, s->noise[i]) &&
                !qa_builtin_resource(&g->services, resources[i], &s->noise[i], error))
                return false;
        r->phase = 3;
        r->next = ROTATE_TRAIN_FIND;
        r->end_time = g->time + .1;
        r->inverse_duration = 1;
        r->progress_start = .1;
        r->dest1 = body.origin;
        action = Q1_MAP_ROTATE_TRAIN_TICK;
    }
    if (!rotation(g, id))
        return true;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    if (!q1_link(g, e, error))
        return false;
    e = rotation(g, id);
    return !e || action == Q1_MAP_IDLE || q1_map_schedule(g, e, delay, action, error);
}

bool qa_q1_game_map_damage(qa_q1_game *g, qa_actor_id actor, float damage,
                            bool *handled, qa_error *error) {
    if (!g || !handled || !isfinite(damage))
        return q1_map_fail(error, "Invalid Q1 authored damage mutation");
    *handled = false;
    q1_actor *e = q1_entity(g, actor);
    if (!e || !e->native || !e->map ||
        (e->map->kind != Q1_MAP_HURT && e->map->kind != Q1_MAP_MOVEWALL))
        return true;
    e->damage = damage;
    *handled = true;
    return true;
}
static bool damage_targets(qa_q1_game *g, qa_actor_id id, float damage, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    q1_actor_snapshot *list;
    if (!target_snapshot(g, e->target, &list, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < list->shared.count && rotation(g, id); ++i) {
        qa_actor_id actor = list->shared.ids[i];
        qa_string_id classname = text_field(g, actor, "classname");
        if (!same_text(g, classname, "trigger_hurt") &&
            !same_text(g, classname, "func_movewall"))
            continue;
        bool handled;
        ok = qa_q1_game_map_damage(g, actor, damage, &handled, error);
        if (ok && !handled && q1_alive(g, actor))
            ok = g->maps->options.target_damage
                ? g->maps->options.target_damage(g->maps->options.context, actor, damage, error)
                : q1_map_fail(error, "Hipnotic train requires target damage owner");
    }
    list->borrowed = false;
    return ok;
}
typedef struct rotate_path {
    qa_actor_id id;
    qa_authored_target fields;
    qa_body_state body;
    qa_string_id noise, noise1, event;
    qa_vec3 rate;
    float speed, wait, damage;
    uint32_t flags;
} rotate_path;
static bool path_read(qa_q1_game *g, qa_actor_id id, rotate_path *path, qa_error *error) {
    *path = (rotate_path){.id = id};
    if (!qa_targets_read(g->maps->options.targets, id, &path->fields) ||
        !same_text(g, path->fields.classname, "path_rotate"))
        return q1_map_fail(error, "Hipnotic rotating train goal is not path_rotate");
    if (!qa_world_body_read(g->services.world, id, &path->body, error))
        return false;
    path->noise = text_field(g, id, "noise");
    path->noise1 = text_field(g, id, "noise1");
    path->event = text_field(g, id, "event");
    qa_targets_vector(g->maps->options.targets, id, "rotate", &path->rate);
    double value = 0;
    qa_targets_number(g->maps->options.targets, id, "spawnflags", &value);
    if (!isfinite(value) || value < 0 || value > UINT32_MAX || floor(value) != value)
        return q1_map_fail(error, "Invalid Hipnotic rotating path flags");
    path->flags = (uint32_t)value;
    value = 0;
    qa_targets_number(g->maps->options.targets, id, "speed", &value);
    if (!isfinite(value) || fabs(value) > FLT_MAX)
        return q1_map_fail(error, "Invalid Hipnotic rotating path speed");
    path->speed = (float)value;
    value = path->fields.wait_seconds;
    qa_targets_number(g->maps->options.targets, id, "wait", &value);
    if (!isfinite(value) || fabs(value) > FLT_MAX)
        return q1_map_fail(error, "Invalid Hipnotic rotating path wait");
    path->wait = (float)value;
    value = 0;
    qa_targets_number(g->maps->options.targets, id, "dmg", &value);
    if (!isfinite(value) || fabs(value) > FLT_MAX)
        return q1_map_fail(error, "Invalid Hipnotic rotating path damage");
    path->damage = (float)value;
    return (isfinite(path->speed) && isfinite(path->wait) && isfinite(path->damage) &&
            qa_vec_finite(path->rate)) || q1_map_fail(error, "Invalid Hipnotic rotating path fields");
}
static bool event_targets(qa_q1_game *g, qa_actor_id id, qa_string_id target,
                           qa_string_id message, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e || !q1_map_text(g, target))
        return true;
    qa_authored_target fields;
    if (!qa_targets_read(g->maps->options.targets, id, &fields))
        return q1_map_fail(error, "Hipnotic train target owner is unavailable");
    fields.target = target;
    fields.message = message;
    qa_target_use use = {.source = id, .activator = e->activator,
        .dialect = g->options.quakeworld ? QA_CLOCK_QUAKEWORLD : QA_CLOCK_NETQUAKE,
        .fields = fields, .time_ns = g->time_ns};
    return qa_targets_use_request(g->maps->options.targets, &use, error);
}
static bool train_find(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    if (!link_targets(g, id, error))
        return false;
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    qa_actor_id goal;
    if (!qa_targets_first(g->maps->options.targets, e->map->path, &goal))
        return q1_map_fail(error, "Hipnotic rotating train has no first path");
    rotate_path path;
    if (!path_read(g, goal, &path, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    q1_map_rotation *r = &e->map->pending.rotation;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    r->phase = 3;
    r->goal = goal;
    if (path.flags & 2) {
        body.angles = path.body.angles;
        r->final_angle = normalized(path.body.angles);
    }
    e->map->path = path.fields.target;
    body.origin = path.body.origin;
    r->next = ROTATE_TRAIN_NEXT;
    r->end_time = q1_map_text(g, e->targetname) ? 0 : r->last_time + .1;
    r->inverse_duration = 1;
    r->progress_start = g->time;
    r->dest1 = body.origin;
    r->dest2 = qa_v3(0, 0, 0);
    return publish(g, id, &body, true, error) &&
           transform_targets(g, id, TARGET_PLACE, error) &&
           transform_targets(g, id, TARGET_FINAL, error);
}
static bool train_stop(qa_q1_game *g, qa_actor_id id, bool wait, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    rotate_path path;
    if (!path_read(g, e->map->pending.rotation.goal, &path, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    q1_map_rotation *r = &e->map->pending.rotation;
    r->phase = wait ? 0 : 2;
    qa_string_id sound = q1_map_text(g, path.noise) ? path.noise : e->map->noise[0];
    if (q1_map_text(g, sound) && !q1_sound_resource(g, id, sound, 0, 1, 1, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    if (path.flags & 2) {
        r->rate = qa_v3(0, 0, 0);
        qa_vec3 angles = r->final_angle;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!rotation(g, id))
            return true;
        body.angles = angles;
        if (!publish(g, id, &body, false, error))
            return false;
    }
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    if (path.flags & 8)
        r->rate = qa_v3(0, 0, 0);
    if (wait)
        r->end_time = r->last_time + path.wait;
    else
        e->damage = 0;
    r->next = ROTATE_TRAIN_NEXT;
    return true;
}
static bool train_next(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    e->map->pending.rotation.phase = 4;
    qa_actor_id goal_id = e->map->pending.rotation.goal, next_id;
    qa_string_id next_name = e->map->path;
    if (!qa_targets_first(g->maps->options.targets, next_name, &next_id))
        return q1_map_fail(error, "Hipnotic rotating train has no next path");
    rotate_path current, next;
    if (!path_read(g, goal_id, &current, error) || !path_read(g, next_id, &next, error))
        return false;
    if (!q1_map_text(g, next.fields.target))
        return q1_map_fail(error, "Hipnotic rotating train path has no next target");
    e = rotation(g, id);
    if (!e)
        return true;
    if (q1_map_text(g, current.noise1))
        e->map->noise[1] = current.noise1;
    if (!noise(g, id, 1, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    q1_map_rotation *r = &e->map->pending.rotation;
    r->goal = next_id;
    e->map->path = next.fields.target;
    r->next = next.flags & 4 ? ROTATE_TRAIN_STOP : next.wait != 0 ? ROTATE_TRAIN_WAIT : ROTATE_TRAIN_NEXT;
    if (!event_targets(g, id, current.event, current.fields.message, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    if (current.flags & 2) {
        r->rate = qa_v3(0, 0, 0);
        qa_body_state body;
        qa_vec3 angles = r->final_angle;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        if (!rotation(g, id))
            return true;
        body.angles = angles;
        if (!publish(g, id, &body, false, error))
            return false;
    }
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    if (current.flags & 1)
        r->rate = current.rate;
    if (current.flags & 16)
        e->damage = current.damage;
    if ((current.flags & 64) && !damage_targets(g, id, current.damage, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    r = &e->map->pending.rotation;
    r->final_destination = next.body.origin;
    if (current.speed == -1) {
        body.origin = next.body.origin;
        r->end_time = r->last_time + .01;
        if (!publish(g, id, &body, true, error) ||
            !transform_targets(g, id, TARGET_PLACE, error))
            return false;
        e = rotation(g, id);
        if (!e)
            return true;
        r = &e->map->pending.rotation;
        if (next.flags & 2) {
            body.angles = next.body.angles;
            if (!publish(g, id, &body, false, error))
                return false;
            e = rotation(g, id);
            if (!e)
                return true;
            r = &e->map->pending.rotation;
        }
        r->inverse_duration = 1;
        r->progress_start = g->time;
        r->dest1 = next.body.origin;
        r->dest2 = qa_v3(0, 0, 0);
        return true;
    }
    r->phase = 1;
    qa_vec3 delta = qa_vec_sub(next.body.origin, body.origin);
    float distance = qa_vec_length(delta);
    if (distance == 0) {
        body.velocity = qa_v3(0, 0, 0);
        r->end_time = r->last_time + .1;
        r->inverse_duration = 1;
        r->progress_start = g->time;
        r->dest1 = body.origin;
        r->dest2 = qa_v3(0, 0, 0);
        return publish(g, id, &body, false, error);
    }
    if (!(current.flags & 32) && current.speed > 0)
        e->speed = current.speed;
    if (!(current.flags & 32) && e->speed == 0)
        return q1_map_fail(error, "Hipnotic rotating train has no speed");
    double travel = current.flags & 32 ? current.speed : distance / e->speed;
    if (travel < .1) {
        body.velocity = qa_v3(0, 0, 0);
        if (next.flags & 2)
            body.angles = next.body.angles;
        r->end_time = r->last_time + .1;
        return publish(g, id, &body, false, error);
    }
    r->inverse_duration = 1 / travel;
    if (next.flags & 2) {
        r->final_angle = normalized(next.body.angles);
        r->rate = qa_vec_scale(qa_vec_sub(next.body.angles, body.angles), (float)r->inverse_duration);
    }
    r->end_time = r->last_time + travel;
    body.velocity = qa_vec_scale(delta, (float)r->inverse_duration);
    r->progress_start = g->time;
    r->dest1 = body.origin;
    r->dest2 = delta;
    return publish(g, id, &body, false, error);
}
static bool train_action(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    switch (e->map->pending.rotation.next) {
    case ROTATE_TRAIN_FIND: return train_find(g, id, error);
    case ROTATE_TRAIN_NEXT: return train_next(g, id, error);
    case ROTATE_TRAIN_WAIT: return train_stop(g, id, true, error);
    case ROTATE_TRAIN_STOP: return train_stop(g, id, false, error);
    default: return q1_map_fail(error, "Invalid Hipnotic rotating train continuation");
    }
}
static bool train_tick(qa_q1_game *g, qa_actor_id id, qa_error *error) {
    q1_actor *e = rotation(g, id);
    if (!e)
        return true;
    q1_map_rotation *r = &e->map->pending.rotation;
    double elapsed = g->time - r->last_time;
    r->last_time = g->time;
    bool arrive = r->end_time != 0 && g->time >= r->end_time;
    bool finish_move = arrive && r->phase == 1;
    qa_vec3 destination = r->final_destination;
    qa_vec3 pos1 = r->dest1, delta = r->dest2;
    double fraction = fmin(1, (g->time - r->progress_start) * r->inverse_duration);
    if (arrive)
        r->end_time = 0;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!rotation(g, id))
        return true;
    if (finish_move) {
        body.origin = destination;
        body.velocity = qa_v3(0, 0, 0);
        if (!publish(g, id, &body, false, error))
            return false;
    } else if (!arrive) {
        body.origin = qa_vec_add(pos1, qa_vec_scale(delta, (float)fraction));
        if (!publish(g, id, &body, false, error))
            return false;
    }
    if (arrive && !train_action(g, id, error))
        return false;
    e = rotation(g, id);
    if (!e)
        return true;
    qa_vec3 rate = e->map->pending.rotation.rate;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    if (!rotation(g, id))
        return true;
    body.angles = normalized(qa_vec_add(body.angles, qa_vec_scale(rate, (float)elapsed)));
    if (!publish(g, id, &body, false, error) ||
        !transform_targets(g, id, TARGET_ROTATE, error))
        return false;
    e = rotation(g, id);
    return !e || q1_map_schedule(g, e, .02, Q1_MAP_ROTATE_TRAIN_TICK, error);
}
