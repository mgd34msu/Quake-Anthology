#include "internal.h"

static bool rotating_accelerated(const qa_q2_game *g, const q2_entity_state *s) {
    uint32_t flag = g->options.edition == QA_Q2_RERELEASE ? 0x10000u :
        g->options.product == QA_Q2_ROGUE ? 8192u : 0;
    return (s->spawnflags & flag) != 0;
}
static bool rotate_speed(qa_q2_game *g, q2_actor *a, bool decelerating, qa_error *e) {
    q2_entity_state *s = a->entity;
    float speed = qa_vec_length(a->physics.angular_velocity);
    if (decelerating ? speed <= s->decel : speed >= s->speed - s->accel) {
        a->physics.angular_velocity = decelerating ? qa_v3(0, 0, 0) :
            qa_vec_scale(s->direction, s->speed);
        if (!q2_entity_targets(g, a, a->id, false, e)) return false;
        if (decelerating && q2_actor_live(g, a->id)) s->touchable = false;
        return true;
    }
    speed += decelerating ? -s->decel : s->accel;
    a->physics.angular_velocity = qa_vec_scale(s->direction, speed);
    return q2_entity_schedule(g, a, decelerating ? Q2ET_ROTATE_DECEL : Q2ET_ROTATE_ACCEL,
        (float)g->frame_ns / Q2_NS);
}
static bool rotate_use(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    bool stop = qa_vec_length(a->physics.angular_velocity) != 0;
    s->loop_sound = stop ? 0 : s->noise;
    if (rotating_accelerated(g, s)) {
        if (!rotate_speed(g, a, stop, e)) return false;
    } else {
        a->physics.angular_velocity = stop ? qa_v3(0, 0, 0) : qa_vec_scale(s->direction, s->speed);
        if (!q2_entity_targets(g, a, a->id, false, e)) return false;
        if (stop && q2_actor_live(g, a->id)) s->touchable = false;
    }
    if (!q2_actor_live(g, a->id)) return true;
    if (!stop && (s->spawnflags & 16)) s->touchable = true;
    return q2_entity_show(g, a, e);
}
bool q2_mover_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    if (!strcmp(name, "func_door") || !strcmp(name, "func_door_rotating"))
        s->kind = Q2E_DOOR;
    else if (!strcmp(name, "func_button"))
        s->kind = Q2E_BUTTON;
    else if (!strcmp(name, "func_water"))
        s->kind = Q2E_WATER;
    else if (!strcmp(name, "func_train") || !strcmp(name, "misc_viper") ||
             !strcmp(name, "misc_strogg_ship") || !strcmp(name, "misc_crashviper") ||
             !strcmp(name, "misc_transport"))
        s->kind = Q2E_TRAIN;
    else if (!strcmp(name, "func_rotating"))
        s->kind = Q2E_ROTATING;
    else if (!strcmp(name, "path_corner"))
        s->kind = Q2E_PATH;
    else if (!strcmp(name, "point_combat"))
        s->kind = Q2E_COMBAT_POINT;
    else
        return q2_brush_spawn(g, a, handled, e);
    if (s->kind == Q2E_PATH || s->kind == Q2E_COMBAT_POINT) {
        if (s->kind == Q2E_PATH && !s->targetname)
            return qa_session_release(g->services.session, a->id, e);
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        b.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
        s->touchable = true;
        s->visual.visible = false;
        return q2_entity_body(g, a, &b, false, e) && q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e);
    }
    if (s->kind == Q2E_ROTATING) {
        s->direction = (s->spawnflags & 4)   ? qa_v3(0, 0, 1)
                       : (s->spawnflags & 8) ? qa_v3(1, 0, 0)
                                             : qa_v3(0, 1, 0);
        if (s->spawnflags & 2)
            s->direction = qa_vec_scale(s->direction, -1);
        if (s->speed == 0)
            s->speed = 100;
        if (g->options.edition == QA_Q2_RERELEASE ? !*q2_field_text(g, s, g->field_keys[QA_TARGET_KEY_DMG]) : s->damage == 0)
            s->damage = 2;
        if (g->options.edition == QA_Q2_CLASSIC) s->noise = 0;
        a->physics.motion = (s->spawnflags & 32) ? QA_PHYSICS_STOP : QA_PHYSICS_PUSH;
        s->usable = true;
        s->visual.visible = true;
        if (s->spawnflags & 64)
            s->visual.effects |= 0x1000;
        if (s->spawnflags & 128)
            s->visual.effects |= 0x2000;
        if (!q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if ((s->spawnflags & 1) && !rotate_use(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id)) return true;
        if (rotating_accelerated(g, s)) {
            s->accel = s->accel == 0 ? 1 : fminf(s->accel, s->speed);
            s->decel = s->decel == 0 ? 1 : fminf(s->decel, s->speed);
        }
        return q2_entity_show(g, a, e);
    }
    s->mover = calloc(1, sizeof(*s->mover));
    if (!s->mover) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 mover");
        return false;
    }
    return s->kind == Q2E_TRAIN ? q2_train_spawn(g, a, e) : q2_door_spawn(g, a, e);
}
bool q2_mover_use(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_actor_id activator,
                  bool *handled, qa_error *e) {
    *handled = true;
    switch (a->entity->kind) {
    case Q2E_DOOR:
    case Q2E_BUTTON:
    case Q2E_WATER:
        return q2_door_use(g, a, activator, e);
    case Q2E_TRAIN:
        return q2_train_use(g, a, activator, e);
    case Q2E_ROTATING:
        return rotate_use(g, a, e);
    default:
        return q2_brush_use(g, a, other, activator, handled, e);
    }
}
bool q2_mover_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, bool *handled, qa_error *e) {
    *handled = true;
    switch (think) {
    case Q2ET_ROTATE_ACCEL:
    case Q2ET_ROTATE_DECEL:
        return rotate_speed(g, a, think == Q2ET_ROTATE_DECEL, e);
    case Q2ET_DOOR_PREPARE:
        return q2_door_prepare(g, a, e);
    case Q2ET_DOOR_DOWN:
        return q2_door_down(g, a, e);
    case Q2ET_SMART_WATER:
        return q2_door_smart_water(g, a, e);
    case Q2ET_TRAIN_FIND:
        return q2_train_find(g, a, e);
    case Q2ET_TRAIN_NEXT:
        return q2_train_next(g, a, e);
    default:
        return q2_brush_think(g, a, think, handled, e);
    }
}
bool q2_move_finished(qa_q2_game *g, q2_actor *a, q2_move_done done, qa_error *e) {
    switch (done) {
    case Q2MD_NONE:
        return true;
    case Q2MD_DOOR_TOP:
    case Q2MD_DOOR_BOTTOM:
        return q2_door_finished(g, a, done == Q2MD_DOOR_TOP, e);
    case Q2MD_TRAIN_WAIT:
        return q2_train_wait(g, a, e);
    case Q2MD_PLAT_TOP:
    case Q2MD_PLAT_BOTTOM:
    case Q2MD_SECRET_NEXT:
        return q2_brush_finished(g, a, done, e);
    }
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Unknown Q2 mover completion");
    return false;
}
bool q2_mover_touch(qa_q2_game *g, q2_actor *a, const qa_touch_contact *contact, bool *handled,
                    qa_error *e) {
    *handled = true;
    switch (a->entity->kind) {
    case Q2E_DOOR:
    case Q2E_BUTTON:
    case Q2E_WATER:
    case Q2E_DOOR_TRIGGER:
        return q2_door_touch(g, a, contact->other, e);
    case Q2E_PATH:
    case Q2E_COMBAT_POINT:
        return q2_route_touch(g, a, contact->other, e);
    case Q2E_ROTATING:
        return qa_vec_length(a->physics.angular_velocity) == 0 ||
               !q2_target_damageable(g, contact->other) ||
               q2_entity_damage(g, a, contact->other, a->id, a->entity->damage, 1, 20, 0, e);
    default:
        return q2_brush_touch(g, a, contact, handled, e);
    }
}
bool q2_mover_blocked(qa_q2_game *g, q2_actor *a, qa_actor_id obstacle, qa_error *e) {
    q2_entity_state *s = a->entity;
    switch (s->kind) {
    case Q2E_WATER:
        return g->options.edition != QA_Q2_RERELEASE || !(s->spawnflags & 2) ||
               q2_door_blocked(g, a, obstacle, e);
    case Q2E_DOOR:
    case Q2E_BUTTON:
        return q2_door_blocked(g, a, obstacle, e);
    case Q2E_TRAIN:
        if (s->damage == 0 || s->debounce_ns > g->now_ns)
            return true;
        s->debounce_ns = q2_deadline(g->now_ns, 500 * Q2_MS);
        return !q2_target_damageable(g, obstacle) ||
               q2_entity_damage(g, a, obstacle, a->id, s->damage, 1, 20, 0, e);
    case Q2E_ROTATING:
        if (g->options.edition == QA_Q2_RERELEASE) {
            if (s->damage == 0 || g->now_ns < s->debounce_ns) return true;
            s->debounce_ns = q2_deadline(g->now_ns, 100 * Q2_MS);
        }
        return !q2_target_damageable(g, obstacle) ||
               q2_entity_damage(g, a, obstacle, a->id, s->damage, 1, 20, 0, e);
    case Q2E_PLAT:
    case Q2E_SECRET_DOOR:
        return q2_brush_blocked(g, a, obstacle, e);
    default:
        return true;
    }
}
bool q2_mover_reaction(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *o, qa_error *e) {
    switch (a->entity->kind) {
    case Q2E_DOOR:
    case Q2E_BUTTON:
        return q2_door_reaction(g, a, o, e);
    case Q2E_SECRET_DOOR:
        return q2_brush_reaction(g, a, o, e);
    default:
        return true;
    }
}
