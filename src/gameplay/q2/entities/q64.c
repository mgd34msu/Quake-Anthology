#include "internal.h"

static qa_vec3 angles_for(qa_vec3 direction) {
    float planar = hypotf(direction.x, direction.y);
    return qa_v3(planar == 0 ? (direction.z > 0 ? -90 : 90)
                             : -atan2f(direction.z, planar) * 57.29577951308232f,
                 planar == 0 ? 0 : atan2f(direction.y, direction.x) * 57.29577951308232f, 0);
}
static float anglemod(float angle) {
    float units = fmodf(truncf(angle * (65536.0f / 360)), 65536);
    if (units < 0)
        units += 65536;
    return units * (360.0f / 65536);
}
static float turn(float current, float ideal, float speed) {
    current = anglemod(current);
    float move = ideal - current;
    if (ideal > current && move >= 180)
        move -= 360;
    else if (ideal <= current && move <= -180)
        move += 360;
    return anglemod(current + q2_clamp(move, -speed, speed));
}
static float step(float current, float wanted, float amount) {
    return current < wanted ? fminf(wanted, current + amount) : fmaxf(wanted, current - amount);
}
static bool spinning(qa_q2_game *g, q2_actor *a, qa_error *e) {
    (void)e;
    q2_entity_state *s = a->entity;
    if (s->timestamp_ns <= g->now_ns) {
        s->timestamp_ns = q2_deadline(g->now_ns, q2_item_seconds(1 + q2_random(g) * 5));
        float x = s->decel + q2_random(g) * (s->speed - s->decel),
              y = s->decel + q2_random(g) * (s->speed - s->decel),
              z = s->decel + q2_random(g) * (s->speed - s->decel);
        if (q2_random(g) < .5f)
            x = -x;
        if (q2_random(g) < .5f)
            y = -y;
        if (q2_random(g) < .5f)
            z = -z;
        s->direction = qa_v3(x, y, z);
    }
    qa_vec3 velocity = a->physics.angular_velocity;
    a->physics.angular_velocity = qa_v3(step(velocity.x, s->direction.x, s->accel),
                                        step(velocity.y, s->direction.y, s->accel),
                                        step(velocity.z, s->direction.z, s->accel));
    a->physics.motion = QA_PHYSICS_PUSH;
    return q2_entity_schedule(g, a, Q2ET_SPINNING, (float)g->frame_ns / Q2_NS);
}
static bool eye(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_q64 *v = s->q64;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    q2_trace_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    qa_actor_id closest = {0};
    float best = INFINITY;
    bool okay = false;
    for (size_t i = 0; i < players->snapshot.count; i++) {
        qa_actor_id id = players->snapshot.ids[i];
        if (!q2_actor_live(g, id))
            continue;
        q2_actor *p = q2_actor_get(g, id, false, NULL);
        if (p && p->client && !p->client->info.connected)
            continue;
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, id, &target, e))
            goto out;
        qa_vec3 direction = qa_vec_sub(target.origin, body.origin);
        float distance = qa_vec_length(direction);
        if (qa_vec_dot(qa_vec_normalize(direction), s->direction) < v->vision_cone ||
            distance >= s->random || distance >= best)
            continue;
        closest = id;
        best = distance;
    }
    s->enemy = closest;
    qa_vec3 wanted = body.angles;
    if (closest.registry) {
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, closest, &target, e))
            goto out;
        if (!(s->spawnflags & 0x20000)) {
            if (!q2_entity_targets(g, a, closest, false, e))
                goto out;
            if (!q2_actor_live(g, a->id)) {
                okay = true;
                goto out;
            }
            s->spawnflags |= 0x20000;
        }
        qa_vec3 forward, right, up;
        qa_builtin_angle_vectors(body.angles, &forward, &right, &up);
        qa_vec3 point =
            qa_vec_add(body.origin, qa_vec_add(qa_vec_add(qa_vec_scale(forward, v->eye_position.x),
                                                          qa_vec_scale(right, v->eye_position.y)),
                                               qa_vec_scale(up, v->eye_position.z)));
        wanted = angles_for(qa_vec_normalize(qa_vec_sub(target.origin, point)));
        s->visual.frame = 2;
        s->timestamp_ns = q2_deadline(g->now_ns, q2_item_seconds(s->wait));
    } else if (s->timestamp_ns <= g->now_ns) {
        wanted = v->neutral;
        s->visual.frame = 0;
    }
    body.angles = qa_v3(turn(body.angles.x, wanted.x, s->speed),
                        turn(body.angles.y, wanted.y, s->speed), body.angles.z);
    okay = q2_entity_body(g, a, &body, true, e) &&
           (!q2_actor_live(g, a->id) ||
            (q2_entity_show(g, a, e) &&
             (!q2_actor_live(g, a->id) ||
              q2_entity_schedule(g, a, Q2ET_EYE, (float)g->frame_ns / Q2_NS))));
out:
    players->active = false;
    return okay;
}
static bool look_at(qa_q2_game *g, q2_actor *a, qa_vec3 origin, qa_vec3 *angles, qa_error *e) {
    qa_actor_id target;
    qa_string_id path = q2_field_id(g, a->entity, "pathtarget");
    if (!path || !q2_map_find(g, NULL, path, 0, &target))
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, target, &body, e))
        return false;
    *angles = angles_for(qa_vec_sub(body.origin, origin));
    return true;
}
static bool event(qa_q2_game *g, q2_actor *a, qa_error *e) {
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_ANIMATION,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = a->id,
                                               .code = 6,
                                               .time_ns = g->now_ns},
                           e);
}
static bool use_target(qa_q2_game *g, q2_actor *source, qa_actor_id target, qa_error *e) {
    if (q2_ent(g, target))
        return qa_q2_entity_use(g, target, source->id, source->entity->activator, e);
    qa_q2_entity_services *services = &g->entity_runtime->services;
    if (!services->invoke_use) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q64 camera requires selected target use");
        return false;
    }
    return services->invoke_use(services->context, target, source->id, source->entity->activator,
                                e);
}
static bool camera(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_q64 *v = s->q64;
    bool skip = false;
    if ((v->hackflags & 64) && g->now_ns > 2 * Q2_NS) {
        q2_trace_frame *players = q2_player_roster(g, e);
        if (!players)
            return false;
        bool okay = true;
        for (size_t i = 0; i < players->snapshot.count; i++) {
            qa_actor_id id = players->snapshot.ids[i];
            if (!q2_actor_live(g, id))
                continue;
            q2_actor *p = q2_actor_get(g, id, false, NULL);
            if (p && p->client && p->client->info.connected && p->client->buttons) {
                skip = true;
                break;
            }
            if (!p || !p->client) {
                qa_q2_player_services *services = &g->player_runtime->services;
                qa_q2_player_movement movement = {0};
                if (!services->movement) {
                    qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                                 "Q64 camera needs selected player controls");
                    okay = false;
                    break;
                }
                if (!services->movement(services->context, id, &movement, e)) {
                    okay = false;
                    break;
                }
                if (q2_actor_live(g, id) && movement.buttons) {
                    skip = true;
                    break;
                }
            }
        }
        players->active = false;
        if (!okay)
            return false;
    }
    qa_actor_id target = s->goal;
    qa_authored_target path;
    bool has_target = q2_actor_live(g, target) &&
                      qa_targets_read(g->entity_runtime->services.targets, target, &path);
    if (skip || !has_target) {
        if (s->killtarget) {
            if (q2_actor_live(g, s->enemy) && !qa_session_release(g->services.session, s->enemy, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
            g->player_runtime->intermission = false;
            g->player_runtime->camera_set = true;
            qa_targets *targets = g->entity_runtime->services.targets;
            if (!targets) {
                qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                             "Q64 camera requires shared target registry");
                return false;
            }
            qa_target_cursor cursor = {0};
            qa_actor_id id;
            while (qa_targets_next(targets, s->killtarget, &cursor, &id)) {
                if (!use_target(g, a, id, e))
                    return false;
                if (!q2_actor_live(g, a->id))
                    return true;
            }
            if (!qa_q2_players_finish_camera(g, e))
                return false;
        }
        return q2_entity_schedule(g, a, Q2ET_NONE, 0);
    }
    v->remaining -= v->speed * (float)g->frame_ns / Q2_NS * .8f;
    qa_body_state body, to;
    if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
        !qa_world_body_read(g->services.world, target, &to, e))
        return false;
    if (v->remaining <= 0) {
        if (q2_actor_field_flags(g, target, "hackflags") & 2) {
            q2_actor *dummy = q2_ent(g, s->enemy);
            if (dummy && dummy->entity->q64) {
                if (!event(g, dummy, e))
                    return false;
                if (!q2_actor_live(g, a->id))
                    return true;
                if (q2_actor_live(g, dummy->id)) {
                    dummy->entity->q64->fading = true;
                    dummy->entity->q64->fade_remaining = path.wait_seconds;
                    dummy->entity->q64->fade_duration = path.wait_seconds;
                }
            }
        }
        body.origin = to.origin;
        if (!q2_entity_body(g, a, &body, true, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        qa_actor_id next = {0};
        if (path.target)
            q2_entity_pick(g, path.target, &next);
        s->goal = next;
        if (next.registry) {
            v->speed = q2_actor_field_float(g, next, "speed", 0);
            if (!v->speed)
                v->speed = 55;
            if (!qa_world_body_read(g->services.world, next, &to, e))
                return false;
            v->distance = v->remaining = qa_vec_length(qa_vec_sub(to.origin, body.origin));
        }
        return q2_entity_schedule(g, a, Q2ET_CAMERA, path.wait_seconds);
    }
    float fraction = v->distance != 0 ? 1 - v->remaining / v->distance : 1;
    qa_vec3 origin =
        qa_vec_add(body.origin, qa_vec_scale(qa_vec_sub(to.origin, body.origin), fraction));
    q2_actor *dummy = q2_ent(g, s->enemy);
    if (dummy && dummy->entity->q64 && dummy->entity->q64->fading) {
        dummy->entity->visual.alpha = fmaxf(1.0f / 255, fraction);
        if (!q2_entity_show(g, dummy, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    if (!look_at(g, a, origin, &v->angles, e) ||
        !qa_q2_players_camera(g, origin, v->angles, false, e))
        return false;
    return !q2_actor_live(g, a->id) ||
           q2_entity_schedule(g, a, Q2ET_CAMERA, (float)g->frame_ns / Q2_NS);
}
static bool dummy(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_q64 *v = s->q64;
    if (!q2_actor_live(g, s->owner))
        return qa_session_release(g->services.session, a->id, e);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    q2_actor *owner = q2_actor_get(g, s->owner, false, NULL);
    if (owner && owner->client) {
        if (!q2_player_animate_reference(g, s->owner, &body, &s->visual, e))
            return false;
    } else {
        qa_q2_entity_services *services = &g->entity_runtime->services;
        if (!services->animate_reference) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                         "Q64 camera dummy requires selected character animation");
            return false;
        }
        if (!services->animate_reference(services->context, s->owner, &body, &s->visual, e))
            return false;
    }
    if (!q2_actor_live(g, a->id))
        return true;
    if (!q2_entity_show(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (v->fading) {
        v->fade_remaining = fmaxf(0, v->fade_remaining - .1f);
        s->visual.alpha =
            fmaxf(1.0f / 255, v->fade_duration == 0 ? 0 : v->fade_remaining / v->fade_duration);
        if (!q2_entity_show(g, a, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    return q2_entity_schedule(g, a, Q2ET_CAMERA_DUMMY, .1f);
}
bool q2_q64_use(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    q2_entity_state *s = a->entity;
    q2_q64 *v = s->q64;
    float music = q2_field_float(g, s, "sounds", 0);
    if (music != 0) {
        char text[32];
        snprintf(text, sizeof(text), "%.9g", music);
        qa_string_id track;
        if (!qa_builtin_resource(&g->services, text, &track, e) ||
            !q2_map_event(
                g, &(qa_q2_map_event){.kind = QA_Q2_MAP_MUSIC, .actor = a->id, .resource = track},
                e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
    }
    qa_actor_id target;
    if (!s->target || !q2_entity_pick(g, s->target, &target))
        return true;
    qa_body_state from, to;
    if (!qa_world_body_read(g->services.world, a->id, &from, e) ||
        !qa_world_body_read(g->services.world, target, &to, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    s->goal = target;
    s->activator = activator;
    qa_builtin_actor_traits traits = {0};
    bool player = q2_actor_live(g, activator) && g->services.actor_traits &&
                  g->services.actor_traits(g->services.context, activator, &traits) && traits.player;
    if (!q2_actor_live(g, a->id))
        return true;
    if (player) {
        qa_body_state body;
        qa_q2_visual visual;
        if (!qa_world_body_read(g->services.world, activator, &body, e) ||
            !qa_q2_entity_visual(g, activator, &visual, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        q2_actor *copy;
        if (!q2_entity_native_spawn(g, "target_camera_dummy", &body, Q2E_CAMERA_DUMMY, &copy, e))
            return false;
        qa_actor_id copy_id = copy->id;
        if (!q2_actor_live(g, a->id)) {
            return !q2_actor_live(g, copy_id) ||
                   qa_session_release(g->services.session, copy_id, e);
        }
        copy->entity->q64 = calloc(1, sizeof(*copy->entity->q64));
        if (!copy->entity->q64) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q64 camera dummy");
            (void)qa_session_release(g->services.session, copy_id, NULL);
            return false;
        }
        s->enemy = copy->id;
        copy->entity->owner = activator;
        copy->entity->visual = visual;
        copy->entity->visual.render_flags = 1;
        copy->entity->visual.visible = true;
        copy->physics.motion = QA_PHYSICS_STEP;
        q2_entity_schedule(g, copy, Q2ET_CAMERA_DUMMY, .1f);
        bool okay = q2_entity_solid(g, copy, QA_PHYSICS_BOX, e);
        if (okay && q2_actor_live(g, a->id) && q2_actor_live(g, copy_id))
            okay = q2_entity_show(g, copy, e);
        if (!okay) {
            if (q2_actor_live(g, copy_id))
                (void)qa_session_release(g->services.session, copy_id, NULL);
            return false;
        }
        if (!q2_actor_live(g, a->id))
            return !q2_actor_live(g, copy_id) ||
                   qa_session_release(g->services.session, copy_id, e);
        if (!q2_actor_live(g, copy_id))
            s->enemy = (qa_actor_id){0};
    }
    v->distance = v->remaining = qa_vec_length(qa_vec_sub(to.origin, from.origin));
    v->speed = s->speed;
    v->angles = qa_v3(0, 0, 0);
    if (!look_at(g, a, from.origin, &v->angles, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    q2_players *players = g->player_runtime;
    players->intermission = true;
    players->next_map = 0;
    players->intermission_ns = g->now_ns;
    players->exit = false;
    players->has_landmark = false;
    if (!qa_q2_players_camera(g, from.origin, v->angles, true, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if ((v->hackflags & 128) &&
        !q2_campaign_end_unit(g, a->id, q2_deadline(g->now_ns, 5 * Q2_NS), e))
        return false;
    return !q2_actor_live(g, a->id) || q2_entity_schedule(g, a, Q2ET_CAMERA, s->wait);
}
bool q2_q64_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    *handled = true;
    if (!strcmp(name, "target_camera"))
        s->kind = Q2E_CAMERA;
    else if (!strcmp(name, "func_eye"))
        s->kind = Q2E_EYE;
    else if (!strcmp(name, "func_spinning"))
        s->kind = Q2E_SPINNING;
    else {
        *handled = false;
        return true;
    }
    if (s->kind == Q2E_CAMERA && g->options.deathmatch)
        return qa_session_release(g->services.session, a->id, e);
    if (s->kind != Q2E_SPINNING) {
        s->q64 = calloc(1, sizeof(*s->q64));
        if (!s->q64) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q64 map continuation");
            return false;
        }
    }
    if (s->kind == Q2E_CAMERA) {
        s->q64->hackflags = q2_actor_field_flags(g, a->id, "hackflags");
        s->usable = true;
        s->visual.visible = false;
        return true;
    }
    a->physics.motion = QA_PHYSICS_PUSH;
    s->visual.visible = true;
    if (!q2_entity_solid(g, a, QA_PHYSICS_BRUSH, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (s->kind == Q2E_SPINNING) {
        if (!s->speed)
            s->speed = 100;
        if (!s->damage)
            s->damage = 2;
        return q2_entity_schedule(g, a, Q2ET_SPINNING, (float)g->frame_ns / Q2_NS);
    }
    s->random = q2_field_float(g, s, "radius", 512);
    if (!s->random)
        s->random = 512;
    s->speed = (s->speed ? s->speed : 45) * (float)g->frame_ns / Q2_NS;
    s->wait = 1;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    q2_q64 *v = s->q64;
    v->neutral = body.angles;
    v->eye_position = q2_field_vec(g, s, "eye_position", qa_v3(0, 0, 0));
    v->vision_cone = q2_field_float(g, s, "vision_cone", .5f);
    if (!v->vision_cone)
        v->vision_cone = .5f;
    if (q2_field_id(g, s, "pathtarget"))
        return q2_entity_schedule(g, a, Q2ET_EYE_SETUP, .1f);
    qa_vec3 forward, right, up;
    qa_builtin_angle_vectors(v->neutral, &forward, &right, &up);
    s->direction = forward;
    v->eye_position = qa_vec_add(qa_vec_add(qa_vec_scale(forward, v->eye_position.x),
                                            qa_vec_scale(right, v->eye_position.y)),
                                 qa_vec_scale(up, v->eye_position.z));
    return q2_entity_schedule(g, a, Q2ET_EYE, .1f);
}
bool q2_q64_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, qa_error *e) {
    if (think == Q2ET_SPINNING)
        return spinning(g, a, e);
    if (!a->entity->q64) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Missing Q64 map continuation");
        return false;
    }
    switch (think) {
    case Q2ET_EYE_SETUP: {
        qa_actor_id target;
        if (q2_entity_pick(g, q2_field_id(g, a->entity, "pathtarget"), &target)) {
            qa_body_state from, to;
            if (!qa_world_body_read(g->services.world, a->id, &from, e) ||
                !qa_world_body_read(g->services.world, target, &to, e))
                return false;
            a->entity->q64->eye_position = qa_vec_sub(to.origin, from.origin);
        }
        a->entity->direction = qa_vec_normalize(a->entity->q64->eye_position);
        return q2_entity_schedule(g, a, Q2ET_EYE, .1f);
    }
    case Q2ET_EYE:
        return eye(g, a, e);
    case Q2ET_CAMERA:
        return camera(g, a, e);
    case Q2ET_CAMERA_DUMMY:
        return dummy(g, a, e);
    default:
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q64 map continuation");
        return false;
    }
}
