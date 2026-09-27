#include "internal.h"
#include "qa/game_q2_monsters.h"

static float normalize_angle(float a) {
    float v = fmodf(a, 360);
    return v < 0 ? v + 360 : v == 0 && a > 0 ? 360 : v;
}
static float short_angle(float a) { return a < -180 ? a + 360 : a > 180 ? a - 360 : a; }
static qa_vec3 vector_angles(qa_vec3 direction) {
    return qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) * 57.29577951308232f,
                 atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
}
static float snap(float value) { return truncf(value * 8 + (value > 0 ? .5f : -.5f)) * .125f; }
static q2_actor *master(qa_q2_game *g, q2_actor *a) {
    q2_actor *root = q2_ent(g, a->entity->mover->master);
    return root ? root : a;
}
static bool fire(qa_q2_game *g, q2_actor *a, q2_actor *driver, const qa_body_state *body,
                 qa_error *e) {
    q2_turret *s = a->entity->turret;
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(body->angles, &forward, &right, &up);
    qa_vec3 start = qa_vec_add(
        body->origin,
        qa_vec_add(qa_vec_add(qa_vec_scale(forward, s->muzzle.x), qa_vec_scale(right, s->muzzle.y)),
                   qa_vec_scale(up, s->muzzle.z)));
    float damage = truncf(100 + q2_random(g) * 50), speed = 550 + 50 * g->options.skill;
    if (!q2_fire_actor_rocket(g, driver->id, driver->id, start, forward, damage, speed, damage, 150,
                              8, 9, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_string_id sound;
    if (!qa_builtin_resource(&g->services, "weapons/rocklf1a.wav", &sound, e))
        return false;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = a->id,
                                               .origin = start,
                                               .resource = sound,
                                               .channel = 1,
                                               .volume = 1,
                                               .attenuation = 1,
                                               .time_ns = g->now_ns},
                           e);
}
static bool breach_tick(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_turret *t = s->turret;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    float frame = (float)g->frame_ns / Q2_NS, pitch = normalize_angle(t->goal.x),
          yaw = normalize_angle(t->goal.y);
    if (pitch > 180)
        pitch -= 360;
    pitch = q2_clamp(pitch, t->pitch_min, t->pitch_max);
    if (yaw < t->yaw_min || yaw > t->yaw_max) {
        float min = fabsf(short_angle(fabsf(t->yaw_min - yaw))),
              max = fabsf(short_angle(fabsf(t->yaw_max - yaw)));
        yaw = min < max ? t->yaw_min : t->yaw_max;
    }
    t->goal.x = pitch;
    t->goal.y = yaw;
    a->physics.angular_velocity =
        qa_v3(q2_clamp(short_angle(pitch - normalize_angle(body.angles.x)), -s->speed * frame,
                       s->speed * frame) /
                  frame,
              q2_clamp(short_angle(yaw - normalize_angle(body.angles.y)), -s->speed * frame,
                       s->speed * frame) /
                  frame,
              0);
    a->physics.motion = QA_PHYSICS_PUSH;
    q2_actor *root = master(g, a);
    for (q2_actor *part = root; part && part->entity && part->entity->mover;
         part = q2_ent(g, part->entity->mover->next)) {
        part->physics.angular_velocity.y = a->physics.angular_velocity.y;
        part->physics.motion = QA_PHYSICS_PUSH;
    }
    q2_actor *driver = q2_ent(g, s->owner);
    if (driver && driver->entity->kind == Q2E_TURRET_DRIVER && driver->entity->turret) {
        q2_turret *d = driver->entity->turret;
        qa_body_state rider;
        if (!qa_world_body_read(g->services.world, driver->id, &rider, e))
            return false;
        float angle = (body.angles.y + d->yaw_offset) * .01745329251994329577f;
        qa_vec3 target =
            qa_v3(snap(body.origin.x + cosf(angle) * d->radius),
                  snap(body.origin.y + sinf(angle) * d->radius),
                  snap(body.origin.z + d->radius * tanf(body.angles.x * .01745329251994329577f) +
                       d->height));
        driver->physics.angular_velocity.x = a->physics.angular_velocity.x;
        driver->physics.angular_velocity.y = a->physics.angular_velocity.y;
        rider.velocity = qa_vec_scale(qa_vec_sub(target, rider.origin), 1 / frame);
        driver->physics.motion = QA_PHYSICS_PUSH;
        if (!q2_entity_body(g, driver, &rider, false, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        if ((s->spawnflags & 65536) && q2_actor_live(g, driver->id)) {
            if (!fire(g, a, driver, &body, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            s->spawnflags &= ~65536u;
        }
    }
    return q2_entity_schedule(g, a, Q2ET_TURRET, frame);
}
static bool driver_link(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_turret *t = s->turret;
    qa_actor_id found;
    if (!q2_entity_pick(g, s->target, &found))
        return true;
    q2_actor *breach = q2_ent(g, found);
    if (!breach || breach->entity->kind != Q2E_TURRET_BREACH)
        return true;
    t->breach = found;
    breach->entity->owner = a->id;
    q2_actor *root = master(g, breach), *last = root;
    while (last->entity->mover && q2_actor_live(g, last->entity->mover->next)) {
        q2_actor *next = q2_ent(g, last->entity->mover->next);
        if (!next || !next->entity->mover)
            break;
        last = next;
    }
    root->entity->owner = a->id;
    root->entity->mover->master = root->id;
    last->entity->mover->next = a->id;
    s->mover->master = root->id;
    s->mover->next = (qa_actor_id){0};
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
        !qa_world_body_read(g->services.world, found, &target, e))
        return false;
    qa_vec3 delta = qa_vec_sub(body.origin, target.origin);
    t->radius = hypotf(delta.x, delta.y);
    t->yaw_offset = normalize_angle(vector_angles(delta).y);
    t->height = delta.z;
    body.angles = target.angles;
    a->physics.flags |= QA_PHYSICS_TEAM_SLAVE;
    return q2_entity_body(g, a, &body, false, e) &&
           q2_entity_schedule(g, a, Q2ET_TURRET_DRIVER, (float)g->frame_ns / Q2_NS);
}
bool q2_turret_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_turret *t = s->turret;
    if (!t) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Missing Q2 turret continuation");
        return false;
    }
    if (think == Q2ET_TURRET_LINK)
        return driver_link(g, a, e);
    if (think == Q2ET_TURRET_INIT) {
        qa_actor_id muzzle;
        if (q2_entity_pick(g, s->target, &muzzle)) {
            qa_body_state from, to;
            if (!qa_world_body_read(g->services.world, a->id, &from, e) ||
                !qa_world_body_read(g->services.world, muzzle, &to, e))
                return false;
            t->muzzle = qa_vec_sub(to.origin, from.origin);
            if (!qa_session_release(g->services.session, muzzle, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
        master(g, a)->entity->damage = s->damage;
        return breach_tick(g, a, e);
    }
    if (think == Q2ET_TURRET)
        return breach_tick(g, a, e);
    q2_actor *breach = q2_ent(g, t->breach);
    if (!breach || !breach->entity->turret)
        return true;
    q2_entity_schedule(g, a, Q2ET_TURRET_DRIVER, (float)g->frame_ns / Q2_NS);
    qa_actor_id enemy;
    bool ready;
    if (!qa_q2_monster_turret_aim(g, a->id, &enemy, &ready, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, breach->id) || !q2_actor_live(g, enemy))
        return true;
    qa_body_state from, to;
    if (!qa_world_body_read(g->services.world, breach->id, &from, e) ||
        !qa_world_body_read(g->services.world, enemy, &to, e))
        return false;
    qa_builtin_actor_traits traits = {.view_height = 22};
    if (g->services.actor_traits)
        g->services.actor_traits(g->services.context, enemy, &traits);
    to.origin.z += traits.view_height;
    breach->entity->turret->goal = vector_angles(qa_vec_sub(to.origin, from.origin));
    if (ready)
        breach->entity->spawnflags |= 65536;
    return true;
}
bool q2_turret_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    if (!strcmp(name, "turret_base"))
        s->kind = Q2E_TURRET_BASE;
    else if (!strcmp(name, "turret_breach"))
        s->kind = Q2E_TURRET_BREACH;
    else if (!strcmp(name, "turret_driver"))
        s->kind = Q2E_TURRET_DRIVER;
    else {
        *handled = false;
        return true;
    }
    if (s->kind == Q2E_TURRET_DRIVER && g->options.deathmatch)
        return qa_session_release(g->services.session, a->id, e);
    s->mover = calloc(1, sizeof(*s->mover));
    if (!s->mover) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 turret team");
        return false;
    }
    s->mover->master = a->id;
    if (s->kind != Q2E_TURRET_BASE) {
        s->turret = calloc(1, sizeof(*s->turret));
        if (!s->turret) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 turret");
            return false;
        }
    }
    if (s->kind == Q2E_TURRET_DRIVER) {
        if (!qa_q2_monster_turret_admit(g, a->id, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        q2_entity_schedule(g, a, Q2ET_TURRET_LINK, (float)g->frame_ns / Q2_NS);
        a->physics.motion = QA_PHYSICS_PUSH;
        return q2_entity_solid(g, a, QA_PHYSICS_BOX, e);
    }
    a->physics.motion = QA_PHYSICS_PUSH;
    s->visual.visible = true;
    if (s->kind == Q2E_TURRET_BREACH) {
        if (!s->speed)
            s->speed = 50;
        if (!s->damage)
            s->damage = 10;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        q2_turret *t = s->turret;
        t->goal = qa_v3(0, body.angles.y, 0);
        float minimum = q2_field_float(g, s, "minpitch", -30),
              maximum = q2_field_float(g, s, "maxpitch", 30);
        t->pitch_max = -(minimum ? minimum : -30);
        t->pitch_min = -(maximum ? maximum : 30);
        t->yaw_min = q2_field_float(g, s, "minyaw", 0);
        t->yaw_max = q2_field_float(g, s, "maxyaw", 360);
        if (!t->yaw_max)
            t->yaw_max = 360;
        q2_entity_schedule(g, a, Q2ET_TURRET_INIT, (float)g->frame_ns / Q2_NS);
    }
    return q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e) &&
           (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e));
}
bool q2_turret_blocked(qa_q2_game *g, q2_actor *a, qa_actor_id other, qa_error *e) {
    if (!q2_target_damageable(g, other))
        return true;
    q2_actor *root = master(g, a);
    return q2_entity_damage(g, a, other,
                            root->entity->owner.registry ? root->entity->owner : root->id,
                            root->entity->damage, 10, 20, 0, e);
}
bool qa_q2_turret_driver_detach(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    q2_actor *a = q2_ent(g, id);
    if (!a || a->entity->kind != Q2E_TURRET_DRIVER || !a->entity->turret)
        return true;
    q2_entity_state *s = a->entity;
    q2_actor *breach = q2_ent(g, s->turret->breach);
    if (breach && breach->entity->turret) {
        breach->entity->turret->goal.x = 0;
        breach->entity->owner = (qa_actor_id){0};
        q2_actor *root = master(g, breach);
        root->entity->owner = (qa_actor_id){0};
        for (q2_actor *m = root; m && m->entity && m->entity->mover;
             m = q2_ent(g, m->entity->mover->next))
            if (qa_actor_id_equal(m->entity->mover->next, id)) {
                m->entity->mover->next = s->mover->next;
                break;
            }
    }
    s->turret->breach = (qa_actor_id){0};
    s->mover->master = s->mover->next = (qa_actor_id){0};
    a->physics.flags &= ~QA_PHYSICS_TEAM_SLAVE;
    a->physics.angular_velocity = qa_v3(0, 0, 0);
    a->physics.motion = QA_PHYSICS_STEP;
    q2_entity_schedule(g, a, Q2ET_NONE, 0);
    return qa_q2_monster_turret_release(g, id, e);
}
