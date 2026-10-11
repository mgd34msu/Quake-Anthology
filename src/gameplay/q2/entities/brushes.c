#include "qa/q2_sound.h"
#include "internal.h"

static bool is_second(q2_actor *a, qa_string_id name) {
    return a->entity->classname == name;
}
static bool platform_sound(qa_q2_game *g, q2_actor *a, bool start, qa_error *e) {
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2]);
    if (second && (a->physics.flags & QA_PHYSICS_TEAM_SLAVE))
        return true;
    if (!q2_entity_sound(g, a, start ? QA_Q2_SOUND_PLATS_PT1_STRT : QA_Q2_SOUND_PLATS_PT1_END, second ? 10 : 2,
                         1, 3, 0, e))
        return false;
    return !q2_actor_live(g, a->id) ||
           q2_entity_sound(g, a, QA_Q2_SOUND_PLATS_PT1_MID, second ? 0 : 2, 1, 3, start ? 1 : -1, e);
}
static bool platform_move(qa_q2_game *g, q2_actor *a, bool up, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    if (!platform_sound(g, a, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    m->phase = up ? 1 : 3;
    if (is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2])) {
        s->style = up ? 2 : 3;
        s->count |= 2;
        if (up) {
            qa_body_state b;
            qa_actor_id area;
            if (!qa_world_body_read(g->services.world, a->id, &b, e))
                return false;
            qa_bounds bounds = b.bounds;
            bounds.maxs.z = bounds.mins.z + 64;
            if (!qa_q2_spawn_bad_area(g, bounds, 0, a->id, &area, e))
                return false;
        }
    }
    return q2_move_start(g, a, up ? m->end : m->start, false, up ? Q2MD_PLAT_TOP : Q2MD_PLAT_BOTTOM,
                         e);
}
static bool platform_trigger(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2]);
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    float lip = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_LIP], 0);
    if (!second && lip == 0)
        lip = 8;
    qa_bounds box = {qa_vec_add(b.bounds.mins, qa_v3(25, 25, 0)),
                     qa_vec_sub(b.bounds.maxs, qa_v3(25, 25, 0))};
    box.mins.z = b.bounds.maxs.z + 8 - (m->end.z - m->start.z + lip);
    box.maxs.z = (s->spawnflags & 1) ? box.mins.z + 8 : b.bounds.maxs.z + 8;
    if (box.maxs.x <= box.mins.x) {
        box.mins.x = (b.bounds.mins.x + b.bounds.maxs.x) * .5f;
        box.maxs.x = box.mins.x + 1;
    }
    if (box.maxs.y <= box.mins.y) {
        box.mins.y = (b.bounds.mins.y + b.bounds.maxs.y) * .5f;
        box.maxs.y = box.mins.y + 1;
    }
    if (second) {
        box.mins.x -= 10;
        box.mins.y -= 10;
        box.maxs.x += 10;
        box.maxs.y += 10;
    }
    q2_actor *trigger;
    if (!q2_entity_native_spawn(g, second ? "plat2_trigger" : "plat_trigger",
                                &(qa_body_state){.bounds = box}, Q2E_PLAT_TRIGGER, &trigger, e))
        return false;
    trigger->entity->enemy = a->id;
    trigger->entity->touchable = true;
    s->goal = trigger->id;
    return q2_entity_solid(g, trigger, QA_PHYSICS_TRIGGER, e);
}
static bool platform_spawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2]);
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    float multiple = second && g->options.deathmatch ? 2 : 1;
    s->speed = (s->speed != 0 ? s->speed * .1f : 20) * multiple;
    s->accel = (s->accel != 0 ? s->accel * .1f : 5) * multiple;
    s->decel = (s->decel != 0 ? s->decel * .1f : 5) * multiple;
    if (s->damage == 0)
        s->damage = 2;
    float lip = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_LIP], 0);
    if (!second && lip == 0)
        lip = 8;
    float height = q2_field_float(g, s, g->field_keys[QA_TARGET_KEY_HEIGHT], 0);
    if (height == 0)
        height = b.bounds.maxs.z - b.bounds.mins.z - (second ? 0 : lip);
    m->end = b.origin;
    m->start = b.origin;
    m->start.z -= height - (second ? lip : 0);
    s->team_master = qa_actor_reference_from_actor(actors, g->options.owner, a->id);
    b.angles = qa_v3(0, 0, 0);
    s->usable = true;
    s->visual.visible = true;
    a->physics.motion = QA_PHYSICS_PUSH;
    bool locked =
        s->targetname && !(second && g->options.edition == QA_Q2_RERELEASE && (s->spawnflags & 8));
    if (second) {
        m->activated = !locked;
        s->debounce_ns = 2 * Q2_NS;
        s->count = 0;
        m->phase = 2;
        s->style = 0;
        if (!locked) {
            if (!platform_trigger(g, a, e))
                return false;
            if (!(s->spawnflags & 4)) {
                b.origin = m->start;
                m->phase = 0;
                s->style = 1;
            }
        }
    } else {
        m->phase = locked ? 1 : 0;
        if (!platform_trigger(g, a, e))
            return false;
        if (!locked)
            b.origin = m->start;
    }
    return q2_entity_body(g, a, &b, false, e) && q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e) &&
           (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
}
static bool platform_operate(qa_q2_game *g, q2_actor *trigger, qa_actor_id who, qa_error *e) {
    q2_actor *a = q2_ent(g, trigger->entity->enemy);
    if (!a || !a->entity->mover)
        return true;
    q2_entity_state *s = a->entity;
    if ((s->count & 2) || s->debounce_ns > g->now_ns)
        return true;
    qa_body_state b, other;
    if (!qa_world_body_read(g->services.world, trigger->id, &b, e) ||
        !qa_world_body_read(g->services.world, who, &other, e))
        return false;
    float lo = b.origin.z + b.bounds.mins.z, hi = b.origin.z + b.bounds.maxs.z,
          center = (lo + hi) * .5f;
    int state = s->style == 0 ? (((s->spawnflags & 32) ? center : hi) > other.origin.z ? 1 : 0)
                : other.origin.z > center ? 0
                                          : 1;
    s->count = 2;
    float pause = g->options.deathmatch ? .3f : .5f;
    if (s->style != state) {
        s->count |= 1;
        pause = .1f;
    }
    s->debounce_ns = q2_deadline(g->now_ns, 2 * Q2_NS);
    return q2_entity_schedule(g, a, s->style == 1 ? Q2ET_PLAT_UP : Q2ET_PLAT_DOWN, pause);
}
static bool platform_finished(qa_q2_game *g, q2_actor *a, bool top, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    s->mover->phase = top ? 2 : 0;
    if (!platform_sound(g, a, false, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2]))
        return !top || q2_entity_schedule(g, a, Q2ET_PLAT_DOWN, 3);
    s->style = top ? 0 : 1;
    q2_entity_think returning = top ? Q2ET_PLAT_DOWN : Q2ET_PLAT_UP;
    if (s->count & 1) {
        s->count = 4;
        if (!(s->spawnflags & 2))
            q2_entity_schedule(g, a, returning, 5);
        s->debounce_ns = q2_deadline(g->now_ns, g->options.deathmatch ? Q2_NS : 0);
    } else {
        s->count = 0;
        s->debounce_ns = q2_deadline(g->now_ns, 2 * Q2_NS);
        if (((s->spawnflags & 4) != 0) != top && !(s->spawnflags & 2))
            q2_entity_schedule(g, a, returning, 2);
    }
    if (!top)
        for (q2_actor *area = g->first_actor; area;) {
            q2_actor *next = area->live_next;
            if (area->projectile.kind == Q2_BAD_AREA &&
                qa_actor_id_equal(qa_actor_reference_resolve(actors, area->projectile.owner), a->id) &&
                !qa_session_release(g->services.session, area->id, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            area = next;
        }
    return q2_entity_targets(g, a, a->id, false, e);
}
static bool secret_spawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_DOOR_SECRET2]);
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(b.angles, &forward, &right, &up);
    qa_vec3 size = qa_vec_sub(b.bounds.maxs, b.bounds.mins);
    if (second) {
        if (b.angles.y != 0 && b.angles.y != 90 && b.angles.y != 180 && b.angles.y != 270)
            return qa_session_release(g->services.session, a->id, e);
        bool along = b.angles.y == 0 || b.angles.y == 180;
        forward =
            qa_vec_scale(forward, (along ? size.x : size.y) * ((s->spawnflags & 64) ? 1 : -1));
        right = qa_vec_scale(right, (along ? size.y : size.x) * ((s->spawnflags & 32) ? 1 : -1));
        m->start = b.origin;
        m->intermediate = qa_vec_add(b.origin, (s->spawnflags & 4) ? forward : right);
        m->end = qa_vec_add(m->intermediate, (s->spawnflags & 4) ? right : forward);
    } else {
        float width = fabsf(qa_vec_dot((s->spawnflags & 4) ? up : right, size)),
              length = fabsf(qa_vec_dot(forward, size));
        m->start = qa_v3(0, 0, 0);
        m->intermediate = qa_vec_add(
            b.origin,
            qa_vec_scale((s->spawnflags & 4) ? up : right,
                         (s->spawnflags & 4) ? -width : (1 - (float)(s->spawnflags & 2)) * width));
        m->end = qa_vec_add(m->intermediate, qa_vec_scale(forward, length));
    }
    b.angles = qa_v3(0, 0, 0);
    a->physics.motion = QA_PHYSICS_PUSH;
    s->team_master = qa_actor_reference_from_actor(actors, g->options.owner, a->id);
    if (s->damage == 0)
        s->damage = 2;
    if (s->wait == 0)
        s->wait = 5;
    s->speed = s->accel = s->decel = 50;
    m->activated = !s->targetname || (s->spawnflags & (second ? 16u : 1u));
    if (m->activated || (!second && s->health != 0)) {
        if (m->activated)
            s->health = second ? 1 : 0;
        if (!qa_combat_create_actor(
                g->services.combat, a->id,
                &(qa_combat_state){.health = s->health, .can_take_damage = true}, e))
            return false;
    }
    s->usable = true;
    s->touchable = second || (!m->activated && s->health == 0 && s->targetname && s->message);
    s->visual.visible = true;
    return q2_entity_body(g, a, &b, false, e) && q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e) &&
           (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
}
static bool secret_next(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_mover *m = a->entity->mover;
    if (m->stage == 1) {
        m->stage = 2;
        return q2_move_start(g, a, m->end, false, Q2MD_SECRET_NEXT, e);
    }
    if (m->stage == 3) {
        m->stage = 4;
        return q2_move_start(g, a, m->intermediate, false, Q2MD_SECRET_NEXT, e);
    }
    if (m->stage == 5) {
        m->stage = 6;
        return q2_move_start(g, a, m->start, false, Q2MD_SECRET_NEXT, e);
    }
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 secret door continuation");
    return false;
}
static bool secret_use(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_DOOR_SECRET2]);
    if (second && (a->physics.flags & QA_PHYSICS_TEAM_SLAVE))
        return true;
    if (!second) {
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        if (qa_vec_dot(qa_vec_sub(b.origin, s->mover->start),
                       qa_vec_sub(b.origin, s->mover->start)) != 0)
            return true;
    }
    for (q2_actor *part = a; part;) {
        q2_mover *m = q2_mover_state(part, e);
        if (!m)
            return false;
        qa_actor_reference next = part->entity->team_next;
        m->stage = 0;
        if (!q2_move_start(g, part, m->intermediate, false, Q2MD_SECRET_NEXT, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        part = second ? q2_ent(g, qa_actor_reference_resolve(actors, next)) : NULL;
    }
    return second || q2_mover_portals(g, a, true, e);
}
bool q2_brush_finished(qa_q2_game *g, q2_actor *a, q2_move_done done, qa_error *e) {
    if (done == Q2MD_PLAT_TOP || done == Q2MD_PLAT_BOTTOM)
        return platform_finished(g, a, done == Q2MD_PLAT_TOP, e);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_DOOR_SECRET2]);
    switch (m->stage) {
    case 0:
        m->stage = 1;
        return q2_entity_schedule(g, a, Q2ET_SECRET_NEXT, 1);
    case 2:
        m->stage = 3;
        return (second ? (s->spawnflags & 1) != 0 : s->wait == -1) ||
               q2_entity_schedule(g, a, Q2ET_SECRET_NEXT, s->wait);
    case 4:
        m->stage = 5;
        return q2_entity_schedule(g, a, Q2ET_SECRET_NEXT, 1);
    case 6:
        m->stage = 0;
        if (m->activated) {
            qa_combat_state c;
            if (!qa_combat_read_traits(g->services.combat, a->id, &c, e))
                return false;
            c.can_take_damage = true;
            if (!qa_combat_set_health(g->services.combat, a->id, second ? 1 : 0, e) ||
                !qa_combat_set_traits(g->services.combat, a->id, &c, e))
                return false;
        }
        return second || q2_mover_portals(g, a, false, e);
    default:
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 secret door endpoint");
        return false;
    }
}
bool qa_q2_force_wall_multicast_origin(qa_q2_game *g, qa_actor_id id, qa_vec3 *out) {
    q2_actor *a = g ? q2_ent(g, id) : NULL;
    if (!out || !a || a->entity->kind != Q2E_FORCEWALL)
        return false;
    *out = a->entity->multicast_origin;
    return true;
}
static bool force_think(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    if (s->wait == 0 && !q2_map_event(g,
                                  &(qa_q2_map_event){.kind = QA_Q2_MAP_FORCE_WALL,
                                                     .actor = a->id,
                                                     .origin = s->direction,
                                                     .direction = s->beam_end,
                                                     .style = s->style},
                                  e))
        return false;
    return !q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_FORCEWALL, .1f);
}
bool q2_brush_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    if (!strcmp(name, "func_plat") || !strcmp(name, "func_plat2"))
        s->kind = Q2E_PLAT;
    else if (!strcmp(name, "func_door_secret") || !strcmp(name, "func_door_secret2"))
        s->kind = Q2E_SECRET_DOOR;
    else if (!strcmp(name, "trigger_elevator"))
        s->kind = Q2E_ELEVATOR;
    else if (!strcmp(name, "func_conveyor"))
        s->kind = Q2E_CONVEYOR;
    else if (!strcmp(name, "func_killbox"))
        s->kind = Q2E_KILLBOX;
    else if (!strcmp(name, "func_object"))
        s->kind = Q2E_OBJECT;
    else if (!strcmp(name, "func_force_wall"))
        s->kind = Q2E_FORCEWALL;
    else {
        *handled = false;
        return true;
    }
    if (s->kind == Q2E_PLAT || s->kind == Q2E_SECRET_DOOR) {
        s->mover = calloc(1, sizeof(*s->mover));
        if (!s->mover) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 brush motion");
            return false;
        }
        return s->kind == Q2E_PLAT ? platform_spawn(g, a, e) : secret_spawn(g, a, e);
    }
    if (s->kind == Q2E_ELEVATOR)
        return q2_entity_schedule(g, a, Q2ET_ELEVATOR, (float)g->frame_ns / Q2_NS);
    s->usable = true;
    if (s->kind == Q2E_KILLBOX) {
        s->visual.visible = false;
        return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e);
    }
    if (s->kind == Q2E_CONVEYOR) {
        if (s->speed == 0)
            s->speed = 100;
        if (!(s->spawnflags & 1)) {
            s->count = (int)s->speed;
            s->speed = 0;
        }
        s->visual.visible = true;
        return q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    }
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    if (s->kind == Q2E_OBJECT) {
        a->physics.motion = QA_PHYSICS_PUSH;
        if (s->damage == 0)
            s->damage = 100;
        a->physics.clip_mask = 0x2010003;
        b.bounds.mins = qa_vec_add(b.bounds.mins, qa_v3(1, 1, 1));
        b.bounds.maxs = qa_vec_sub(b.bounds.maxs, qa_v3(1, 1, 1));
        s->visual.visible = s->spawnflags == 0;
        if (!s->spawnflags) {
            s->usable = false;
            q2_entity_schedule(g, a, Q2ET_OBJECT_FALL, 2 * (float)g->frame_ns / Q2_NS);
        }
        if (s->spawnflags & 2)
            s->visual.effects |= 0x1000;
        if (s->spawnflags & 4)
            s->visual.effects |= 0x2000;
        return q2_entity_body(g, a, &b, false, e) &&
               q2_entity_solid(g, a, s->visual.visible ? QA_PHYSICS_BRUSH : QA_PHYSICS_NOT_SOLID,
                               e) &&
               (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
    }
    qa_vec3 lo = qa_vec_add(b.origin, b.bounds.mins), hi = qa_vec_add(b.origin, b.bounds.maxs),
            mid = qa_vec_scale(qa_vec_add(lo, hi), .5f);
    bool x = hi.x - lo.x > hi.y - lo.y;
    s->multicast_origin = mid;
    s->direction = x ? qa_v3(lo.x, mid.y, hi.z) : qa_v3(mid.x, lo.y, hi.z);
    s->beam_end = x ? qa_v3(hi.x, mid.y, hi.z) : qa_v3(mid.x, hi.y, hi.z);
    if (!s->style)
        s->style = 208;
    s->wait = 1;
    s->visual.visible = false;
    if (s->spawnflags & 1)
        q2_entity_schedule(g, a, Q2ET_FORCEWALL, .1f);
    return q2_entity_solid(g, a, (s->spawnflags & 1) ? QA_PHYSICS_BRUSH : QA_PHYSICS_NOT_SOLID,
                           e) &&
           (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
}
bool q2_brush_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                  bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = true;
    switch (s->kind) {
    case Q2E_PLAT:
        if (!is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2]))
            return s->think != Q2ET_NONE || platform_move(g, a, false, e);
        if (!s->mover->activated) {
            s->mover->activated = true;
            return platform_trigger(g, a, e) && platform_move(g, a, false, e);
        }
        if (s->style > 1 || s->debounce_ns > g->now_ns || !q2_actor_live(g, activator))
            return true;
        {
            q2_actor *trigger = q2_ent(g, s->goal);
            return !trigger || platform_operate(g, trigger, activator, e);
        }
    case Q2E_SECRET_DOOR:
        return secret_use(g, a, e);
    case Q2E_ELEVATOR: {
        q2_actor *train = q2_ent(g, s->enemy), *caller = q2_ent(g, other);
        qa_actor_id corner;
        if (!train || train->entity->think != Q2ET_NONE || !caller ||
            !q2_entity_pick(g, q2_field_id(caller->entity, g->field_keys[QA_TARGET_KEY_PATHTARGET]), &corner))
            return true;
        return q2_train_resume(g, train, corner, e);
    }
    case Q2E_CONVEYOR:
        if (s->spawnflags & 1) {
            s->speed = 0;
            s->spawnflags &= ~1u;
        } else {
            s->speed = (float)s->count;
            s->spawnflags |= 1;
        }
        if (!(s->spawnflags & 2))
            s->count = 0;
        return true;
    case Q2E_KILLBOX: {
        bool clear, previous = g->player_runtime->deadly_killbox,
                    rr = g->options.edition == QA_Q2_RERELEASE;
        if (rr) {
            g->player_runtime->deadly_killbox = (s->spawnflags & 2) != 0;
            if (!q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e)) {
                g->player_runtime->deadly_killbox = previous;
                return false;
            }
        }
        bool okay = q2_killbox(g, a->id, a->id, false, rr && (s->spawnflags & 4), &clear, e);
        g->player_runtime->deadly_killbox = previous;
        if (rr && q2_actor_live(g, a->id)) {
            qa_error cleanup = {0};
            if (!q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, &cleanup) && okay) {
                if (e)
                    *e = cleanup;
                okay = false;
            }
        }
        return okay;
    }
    case Q2E_OBJECT: {
        s->visual.visible = true;
        s->usable = false;
        if (!q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e))
            return false;
        bool clear;
        if (!q2_killbox(g, a->id, a->id, false, false, &clear, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if (!q2_entity_show(g, a, e))
            return false;
        a->physics.motion = QA_PHYSICS_TOSS;
        s->touchable = true;
        return true;
    }
    case Q2E_FORCEWALL:
        if (s->wait == 0) {
            s->wait = 1;
            q2_entity_schedule(g, a, Q2ET_NONE, 0);
            return q2_entity_solid(g, a, QA_PHYSICS_NOT_SOLID, e);
        }
        s->wait = 0;
        q2_entity_schedule(g, a, Q2ET_FORCEWALL, .1f);
        if (!q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e))
            return false;
        {
            bool clear;
            return q2_killbox(g, a->id, a->id, false, false, &clear, e);
        }
    default:
        *handled = false;
        return true;
    }
}
bool q2_brush_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, bool *handled, qa_error *e) {
    *handled = true;
    switch (think) {
    case Q2ET_PLAT_UP:
    case Q2ET_PLAT_DOWN:
        return platform_move(g, a, think == Q2ET_PLAT_UP, e);
    case Q2ET_SECRET_NEXT:
        return secret_next(g, a, e);
    case Q2ET_FORCEWALL:
        return force_think(g, a, e);
    case Q2ET_OBJECT_FALL:
        a->physics.motion = QA_PHYSICS_TOSS;
        a->entity->touchable = true;
        return true;
    case Q2ET_ELEVATOR: {
        qa_actor_id train;
        if (!q2_entity_pick(g, a->entity->target, &train))
            return true;
        q2_actor *found = q2_ent(g, train);
        if (!found || found->entity->kind != Q2E_TRAIN || found->entity->mover->ship)
            return true;
        a->entity->enemy = train;
        a->entity->usable = true;
        return true;
    }
    default:
        *handled = false;
        return true;
    }
}
bool q2_brush_touch(qa_q2_game *g, q2_actor *a, const qa_touch_contact *contact, bool *handled,
                    qa_error *e) {
    q2_entity_state *s = a->entity;
    *handled = true;
    qa_builtin_actor_traits traits = {0};
    if (s->kind == Q2E_OBJECT)
        return !contact->has_plane || contact->plane.normal.z < 1 ||
               !q2_target_damageable(g, contact->other) ||
               q2_entity_damage(g, a, contact->other, a->id, s->damage, 1, 20, 0, e);
    if (s->kind != Q2E_PLAT_TRIGGER && s->kind != Q2E_SECRET_DOOR) {
        *handled = false;
        return true;
    }
    if (!g->services.actor_traits ||
        !g->services.actor_traits(g->services.context, contact->other, &traits))
        return true;
    if (s->kind == Q2E_PLAT_TRIGGER) {
        q2_actor *plat = q2_ent(g, s->enemy);
        if (!plat)
            return true;
        bool second = is_second(plat, g->runtime_names[Q2_NAME_FUNC_PLAT2]);
        if (!traits.player && (!second || !traits.monster))
            return true;
        qa_combat_state health;
        if (!qa_combat_read(g->services.combat, contact->other, &health, e))
            return false;
        if (health.health <= 0)
            return true;
        if (second)
            return platform_operate(g, a, contact->other, e);
        int phase = plat->entity->mover->phase;
        return phase == 0 ? platform_move(g, plat, true, e)
                          : phase != 2 || q2_entity_schedule(g, plat, Q2ET_PLAT_DOWN, 1);
    }
    if (!traits.player)
        return true;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_DOOR_SECRET2]);
    if (second) {
        qa_combat_state health;
        if (!qa_combat_read(g->services.combat, contact->other, &health, e))
            return false;
        if (health.health <= 0)
            return true;
    }
    if (s->timestamp_ns > g->now_ns)
        return true;
    s->timestamp_ns = q2_deadline(g->now_ns, (second ? 2u : 5u) * Q2_NS);
    if (second && !s->message)
        return true;
    if (!q2_entity_message(g, a, contact->other,
                           qa_strings_cstr(qa_session_strings(g->services.session), s->message), e))
        return false;
    return second || !q2_actor_live(g, a->id) ||
           q2_entity_sound(g, a, QA_Q2_SOUND_MISC_TALK1, 0, 1, 1, 0, e);
}
bool q2_brush_blocked(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_error *e) {
    q2_entity_state *s = a->entity;
    bool creature;
    if (!q2_target_creature(g, other, &creature, NULL, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->kind == Q2E_SECRET_DOOR && is_second(a, g->runtime_names[Q2_NAME_FUNC_DOOR_SECRET2]))
        return (a->physics.flags & QA_PHYSICS_TEAM_SLAVE) || !q2_target_damageable(g, other) ||
               q2_entity_damage(g, a, other, a->id, s->damage, 0, 20, 0, e);
    if (!creature) {
        if (q2_target_damageable(g, other) &&
            !q2_entity_damage(g, a, other, a->id, 100000, 1, 20, 0, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        q2_actor *victim = q2_actor_get(g, other, false, NULL);
        if (!victim)
            return true;
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, other, &b, e) ||
            !q2_projectile_event(g, other, QA_BUILTIN_EXPLOSION, "q2:explosion1", 1, b.origin,
                                 qa_v3(0, 0, 0), e))
            return false;
        return !q2_actor_live(g, other) || qa_session_release(g->services.session, other, e);
    }
    if (s->kind == Q2E_SECRET_DOOR) {
        if (s->debounce_ns > g->now_ns)
            return true;
        s->debounce_ns = q2_deadline(g->now_ns, 500 * Q2_MS);
    }
    if (s->kind == Q2E_PLAT && is_second(a, g->runtime_names[Q2_NAME_FUNC_PLAT2])) {
        qa_combat_state health;
        if (!qa_combat_read(g->services.combat, other, &health, e))
            return false;
        if (health.health < 1 && !q2_entity_damage(g, a, other, a->id, 100, 1, 20, 0, e))
            return false;
        if (!q2_actor_live(g, a->id) || !q2_actor_live(g, other))
            return true;
    }
    if (!q2_entity_damage(g, a, other, a->id, s->damage, 1, 20, 0, e))
        return false;
    if (!q2_actor_live(g, a->id) || s->kind != Q2E_PLAT)
        return true;
    return s->mover->phase == 1 ? platform_move(g, a, false, e)
                                : s->mover->phase != 3 || platform_move(g, a, true, e);
}
bool q2_brush_reaction(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *o, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    if (a->entity->kind != Q2E_SECRET_DOOR || o->result.reaction != QA_REACTION_DEATH)
        return true;
    qa_combat_state health;
    if (!qa_combat_read_traits(g->services.combat, a->id, &health, e))
        return false;
    health.can_take_damage = false;
    bool second = is_second(a, g->runtime_names[Q2_NAME_FUNC_DOOR_SECRET2]);
    if (second && !qa_combat_set_health(g->services.combat, a->id, a->entity->health, e))
        return false;
    if (!qa_combat_set_traits(g->services.combat, a->id, &health, e))
        return false;
    if (second && (a->physics.flags & QA_PHYSICS_TEAM_SLAVE)) {
        q2_actor *master = q2_ent(g, qa_actor_reference_resolve(actors, a->entity->team_master));
        if (master && qa_combat_read_traits(g->services.combat, master->id, &health, e) &&
            health.can_take_damage)
            return q2_brush_reaction(g, master, o, e);
    }
    return secret_use(g, a, e);
}
