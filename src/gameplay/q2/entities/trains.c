#include "internal.h"

static bool destination(qa_q2_game *g, q2_actor *a, qa_actor_id corner, qa_vec3 *out, qa_error *e) {
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
        !qa_world_body_read(g->services.world, corner, &target, e))
        return false;
    q2_entity_state *s = a->entity;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    *out = rr && (s->spawnflags & 32) ? target.origin : qa_vec_sub(target.origin, body.bounds.mins);
    if (rr && !(s->spawnflags & 32) && (s->spawnflags & 16))
        *out = qa_vec_sub(*out, qa_v3(1, 1, 1));
    return true;
}
static bool target_fields(qa_q2_game *g, qa_actor_id id, qa_string_id *target, uint32_t *flags) {
    q2_actor *a = q2_ent(g, id);
    if (a) {
        *target = a->entity->target;
        *flags = a->entity->spawnflags;
        return true;
    }
    qa_authored_target fields;
    if (g->entity_runtime->services.targets &&
        qa_targets_read(g->entity_runtime->services.targets, id, &fields)) {
        *target = fields.target;
        *flags = q2_actor_field_flags(g, id, "spawnflags");
        return true;
    }
    return false;
}
bool q2_train_next(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    q2_mover *m = s->mover;
    bool teleported = false;
    for (;;) {
        if (!s->target)
            return q2_entity_sound(g, a, q2_field_text(g, s, "noise"), 2, 1, 3, -1, e);
        qa_actor_id id;
        if (!q2_entity_pick(g, s->target, &id))
            return true;
        uint32_t flags;
        if (!target_fields(g, id, &s->target, &flags))
            return true;
        qa_vec3 goal;
        if (!destination(g, a, id, &goal, e))
            return false;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        if (flags & 1) {
            if (teleported)
                return true;
            teleported = true;
            body.origin = goal;
            if (!q2_entity_body(g, a, &body, true, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            if (!q2_projectile_event(g, a->id, QA_BUILTIN_TELEPORT, "q2:other-teleport", 0, goal,
                                     qa_v3(0, 0, 0), e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            continue;
        }
        m->destination = id;
        q2_actor *target = q2_ent(g, id);
        float speed = target ? target->entity->speed : q2_actor_field_float(g, id, "speed", 0);
        if (g->options.edition == QA_Q2_RERELEASE && speed != 0) {
            s->speed = speed;
            float accel = target ? target->entity->accel : q2_actor_field_float(g, id, "accel", 0);
            float decel = target ? target->entity->decel : q2_actor_field_float(g, id, "decel", 0);
            s->accel = accel != 0 ? accel : s->speed;
            s->decel = decel != 0 ? decel : s->speed;
        }
        if (!q2_entity_sound(g, a, q2_field_text(g, s, "noise"), 2, 1, 3, 1, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->spawnflags |= 1;
        qa_vec3 delta = qa_vec_sub(goal, body.origin);
        if (!q2_move_start(g, a, goal, false, Q2MD_TRAIN_WAIT, e))
            return false;
        if (g->options.edition == QA_Q2_RERELEASE && (s->spawnflags & 8))
            for (q2_actor *part = q2_ent(g, qa_actor_reference_resolve(actors, s->team_next)); part;) {
                q2_entity_state *p = part->entity;
                qa_actor_reference next = p->team_next;
                if (!qa_world_body_read(g->services.world, part->id, &body, e))
                    return false;
                p->speed = s->speed;
                p->accel = s->accel;
                p->decel = s->decel;
                part->physics.motion = QA_PHYSICS_PUSH;
                if (!q2_move_start(g, part, qa_vec_add(body.origin, delta), false, Q2MD_NONE, e))
                    return false;
                if (!q2_actor_live(g, a->id))
                    return true;
                part = q2_ent(g, qa_actor_reference_resolve(actors, next));
            }
        return true;
    }
}
bool q2_train_wait(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_actor_id destination = s->mover->destination;
    q2_actor *target = q2_ent(g, destination);
    qa_authored_target authored;
    if (!qa_targets_read(g->entity_runtime->services.targets, destination, &authored))
        return true;
    qa_string_id path = q2_actor_field(g, destination, "pathtarget");
    if (path) {
        bool okay;
        if (target) {
            qa_string_id saved = target->entity->target;
            target->entity->target = path;
            okay = q2_entity_targets(g, target, s->activator, false, e);
            if (q2_actor_live(g, destination))
                target->entity->target = saved;
        } else {
            authored.target = path;
            okay = qa_targets_use_request(
                g->entity_runtime->services.targets,
                &(qa_target_use){.source = destination,
                                 .activator = s->activator,
                                 .fields = authored,
                                 .dialect = g->options.edition == QA_Q2_CLASSIC
                                                ? QA_CLOCK_Q2_CLASSIC
                                                : QA_CLOCK_Q2_RERELEASE,
                                 .time_ns = g->now_ns},
                e);
        }
        if (!okay)
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    if (!q2_actor_live(g, destination))
        return true;
    float wait = target ? target->entity->wait : authored.wait_seconds;
    if (wait == 0)
        return q2_train_next(g, a, e);
    if (wait > 0)
        q2_entity_schedule(g, a, Q2ET_TRAIN_NEXT, wait);
    else if (s->spawnflags & 2) {
        if (g->options.edition == QA_Q2_RERELEASE)
            s->mover->destination = (qa_actor_id){0};
        else if (!q2_train_next(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        s->spawnflags &= ~1u;
        s->mover->moving = false;
        q2_entity_schedule(g, a, Q2ET_NONE, 0);
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        b.velocity = qa_v3(0, 0, 0);
        if (!q2_entity_body(g, a, &b, false, e))
            return false;
    }
    return q2_entity_sound(g, a, q2_field_text(g, s, "noise"), 2, 1, 3, -1, e);
}
bool q2_train_find(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_actor_id id;
    if (!q2_entity_pick(g, s->target, &id))
        return true;
    uint32_t flags;
    if (!target_fields(g, id, &s->target, &flags))
        return true;
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e) ||
        !destination(g, a, id, &b.origin, e) || !q2_entity_body(g, a, &b, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!s->targetname)
        s->spawnflags |= 1;
    if (s->spawnflags & 1) {
        s->activator = a->id;
        q2_entity_schedule(g, a, Q2ET_TRAIN_NEXT, (float)g->frame_ns / Q2_NS);
    }
    return true;
}
bool q2_train_resume(qa_q2_game *g, q2_actor *a, qa_actor_id corner, qa_error *e) {
    qa_vec3 goal;
    if (!destination(g, a, corner, &goal, e))
        return false;
    a->entity->mover->destination = corner;
    a->entity->spawnflags |= 1;
    return q2_move_start(g, a, goal, false, Q2MD_TRAIN_WAIT, e);
}
bool q2_train_use(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    q2_entity_state *s = a->entity;
    s->activator = activator;
    if (s->mover->ship && !s->visual.visible) {
        s->visual.visible = true;
        if (!q2_entity_show(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    if (s->spawnflags & 1) {
        if (!(s->spawnflags & 2))
            return true;
        s->spawnflags &= ~1u;
        s->mover->moving = false;
        q2_entity_schedule(g, a, Q2ET_NONE, 0);
        qa_body_state b;
        if (!qa_world_body_read(g->services.world, a->id, &b, e))
            return false;
        b.velocity = qa_v3(0, 0, 0);
        return q2_entity_body(g, a, &b, false, e);
    }
    return q2_actor_live(g, s->mover->destination) ? q2_train_resume(g, a, s->mover->destination, e)
                                                   : q2_train_next(g, a, e);
}
bool q2_train_spawn(qa_q2_game *g, q2_actor *a, qa_error *e) {
    const qa_actor_registry *actors = qa_session_actors(g->services.session);
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    s->mover->ship = strcmp(name, "func_train") != 0;
    bool crash = !strcmp(name, "misc_crashviper") || !strcmp(name, "misc_transport");
    if (s->mover->ship && !s->target)
        return qa_session_release(g->services.session, a->id, e);
    s->team_master = qa_actor_reference_from_actor(actors, g->options.owner, a->id);
    qa_body_state b;
    if (!qa_world_body_read(g->services.world, a->id, &b, e))
        return false;
    if (s->mover->ship) {
        if (!qa_builtin_resource(
                &g->services,
                !strcmp(name, "misc_transport")     ? "models/objects/ship/tris.md2"
                : !strcmp(name, "misc_crashviper")  ? "models/ships/bigviper/tris.md2"
                : !strcmp(name, "misc_strogg_ship") ? "models/ships/strogg1/tris.md2"
                                                    : "models/ships/viper/tris.md2",
                &s->visual.models[0], e))
            return false;
        if (!crash)
            b.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 32}};
        s->visual.visible = crash;
    } else
        s->visual.visible = true;
    if (!crash)
        b.angles = qa_v3(0, 0, 0);
    a->physics.motion = QA_PHYSICS_PUSH;
    if (s->speed == 0)
        s->speed = s->mover->ship && !crash ? 300 : 100;
    s->accel = s->decel = s->speed;
    s->damage = (s->spawnflags & 4) ? 0 : s->damage != 0 ? s->damage : 100;
    if (crash)
        s->damage = 0;
    if (!strcmp(name, "misc_transport"))
        s->spawnflags |= 1;
    s->usable = true;
    if (!q2_entity_body(g, a, &b, false, e) ||
        !q2_entity_solid(g, a, s->mover->ship && !crash ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_BRUSH,
                         e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    q2_entity_schedule(g, a, Q2ET_TRAIN_FIND, (float)g->frame_ns / Q2_NS);
    return q2_entity_show(g, a, e);
}
