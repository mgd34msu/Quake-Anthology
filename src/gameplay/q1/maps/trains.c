#include "internal.h"
#include <float.h>

static q1_actor *train(qa_q1_game *g, qa_actor_id id) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map &&
                   (entity->map->kind == Q1_MAP_TRAIN || entity->map->kind == Q1_MAP_TRAIN2)
               ? entity
               : NULL;
}
static bool train_sound(qa_q1_game *g, q1_actor *entity, bool moving, qa_error *error) {
    const char *path = NULL;
    if (entity->map->kind == Q1_MAP_TRAIN2)
        path = qa_strings_cstr(qa_session_strings(g->services.session),
                               entity->map->noise[moving ? 1 : 0]);
    if (!path || !*path)
        path = entity->map->sounds == 1 ? moving ? "plats/train1.wav" : "plats/train2.wav"
                                        : "misc/null.wav";
    return q1_sound(g, entity->id, path, 2, 1, error);
}
bool q1_map_train_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    bool hipnotic = entity->map->kind == Q1_MAP_TRAIN2;
    bool teleport = q1_classnamed(g, entity->id, "misc_teleporttrain");
    if (!q1_map_text(g, entity->target))
        return q1_map_fail(error, "Q1 train has no target");
    if (!teleport && !entity->map->has_inline_model)
        return q1_map_fail(error, "Q1 train has no brush model");
    entity->speed = entity->speed ? entity->speed : 100;
    entity->damage = entity->damage ? entity->damage : 2;
    entity->physics.motion = QA_PHYSICS_PUSH;
    entity->physics.solid = teleport ? QA_PHYSICS_NOT_SOLID : QA_PHYSICS_BRUSH;
    entity->map->use_enabled = true;
    qa_actor_id id = entity->id;
    if (hipnotic) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = train(g, id);
        if (!entity)
            return true;
        entity->map->mangle = body.angles;
        entity->map->pending.mover.next_speed = 1;
        body.angles = qa_v3(0, 0, 0);
        for (unsigned i = 0; i < 2; ++i)
            if (!q1_map_text(g, entity->map->noise[i]) &&
                !qa_builtin_resource(&g->services,
                                     entity->map->sounds == 1
                                         ? i ? "plats/train1.wav" : "plats/train2.wav"
                                         : "misc/null.wav",
                                     &entity->map->noise[i], error))
                return false;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = train(g, id);
        if (!entity)
            return true;
    } else if (teleport) {
        if (!q1_model(g, entity, "progs/teleport.mdl", error))
            return false;
        entity->physics.angular_velocity = qa_v3(100, 200, 300);
    } else if (!qa_builtin_resource(&g->services, "train", &entity->classname, error))
        return false;
    return q1_map_schedule(g, entity, .1, Q1_MAP_TRAIN_FIND, error) && q1_link(g, entity, error);
}

static bool corner_fields(qa_q1_game *g, qa_actor_id id, qa_authored_target *fields,
                          qa_error *error) {
    if (!qa_targets_read(g->maps->options.targets, id, fields))
        return q1_map_fail(error, "Q1 train target is unavailable");
    if (!isfinite(fields->wait_seconds))
        return q1_map_fail(error, "Q1 train target has invalid wait");
    return true;
}
static bool corner_speed(qa_q1_game *g, qa_actor_id id, float *speed, qa_error *error) {
    double value = 0;
    qa_targets_number(g->maps->options.targets, id, "speed", &value);
    if (!isfinite(value) || fabs(value) > FLT_MAX)
        return q1_map_fail(error, "Q1 train target has invalid speed");
    *speed = (float)value;
    return true;
}
static bool departed_event(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id, goal = entity->map->pending.mover.goal;
    qa_target_field field;
    qa_authored_target fields;
    if (!qa_targets_field(g->maps->options.targets, goal, "event", &field) ||
        field.kind != QA_TARGET_FIELD_TEXT || !q1_map_text(g, field.value.text) ||
        !qa_targets_read(g->maps->options.targets, goal, &fields))
        return true;
    qa_string_id prior_target = entity->target, prior_message = entity->message;
    entity->target = field.value.text;
    entity->message = fields.message;
    bool ok = q1_map_targets(g, entity, entity->activator, error);
    entity = train(g, id);
    if (entity) {
        entity->target = prior_target;
        entity->message = prior_message;
    }
    return ok;
}
static bool relocate(qa_q1_game *g, qa_actor_id id, qa_body_state *body, qa_vec3 destination,
                     qa_error *error) {
    body->origin = destination;
    if (!qa_world_body_write(g->services.world, id, body, error))
        return false;
    q1_actor *entity = train(g, id);
    return !entity || q1_link(g, entity, error);
}
bool q1_map_train_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    if (entity->map->kind == Q1_MAP_TRAIN) {
        if (entity->map->pending.mover.position != Q1_MAP_TOP ||
            entity->map->pending.mover.activated)
            return true;
    } else {
        qa_actor_id id = entity->id;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = train(g, id);
        if (!entity || body.velocity.x || body.velocity.y || body.velocity.z)
            return true;
        entity->activator = activator;
    }
    return q1_map_train_think(g, entity, Q1_MAP_TRAIN_NEXT, error);
}
bool q1_map_train_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    qa_actor_id id = entity->id;
    bool hipnotic = entity->map->kind == Q1_MAP_TRAIN2;
    if (action == Q1_MAP_TRAIN_WAIT) {
        if (entity->wait && !train_sound(g, entity, false, error))
            return false;
        entity = train(g, id);
        if (!entity || (hipnotic && entity->wait == -1))
            return true;
        double delay = entity->wait ? entity->wait : .1;
        if (hipnotic)
            entity->wait = 0;
        return q1_map_schedule(g, entity, delay, Q1_MAP_TRAIN_NEXT, error);
    }
    if (action != Q1_MAP_TRAIN_FIND && action != Q1_MAP_TRAIN_NEXT)
        return q1_map_fail(error, "invalid Q1 train continuation");
    float speed = entity->map->pending.mover.next_speed;
    if (action == Q1_MAP_TRAIN_NEXT)
        entity->map->pending.mover.activated = true;
    qa_actor_id node_id;
    qa_authored_target fields;
    if (!qa_targets_first(g->maps->options.targets, entity->target, &node_id))
        return q1_map_fail(error, "Q1 train target not found");
    if (!corner_fields(g, node_id, &fields, error))
        return false;
    entity->target = fields.target;
    if (hipnotic && !corner_speed(g, node_id, &entity->map->pending.mover.next_speed, error))
        return false;
    if (action == Q1_MAP_TRAIN_NEXT) {
        if (!q1_map_text(g, entity->target))
            return q1_map_fail(error, "Q1 train path has no next target");
        if (!hipnotic)
            entity->wait = fields.wait_seconds;
        if (!train_sound(g, entity, true, error))
            return false;
        entity = train(g, id);
        if (!entity)
            return true;
        if (hipnotic) {
            if (!corner_fields(g, node_id, &fields, error))
                return false;
            entity->wait = fields.wait_seconds;
            if (!departed_event(g, entity, error))
                return false;
            entity = train(g, id);
            if (!entity)
                return true;
        }
    }
    if (hipnotic)
        entity->map->pending.mover.goal = node_id;
    q1_map_action done = hipnotic && !entity->wait ? Q1_MAP_TRAIN_NEXT : Q1_MAP_TRAIN_WAIT;
    qa_body_state node, body;
    if (!qa_world_body_read(g->services.world, node_id, &node, error))
        return false;
    if (!train(g, id))
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = train(g, id);
    if (!entity)
        return true;
    qa_vec3 destination = qa_vec_sub(node.origin, body.bounds.mins);
    if (action == Q1_MAP_TRAIN_FIND || (hipnotic && speed == -1)) {
        if (action == Q1_MAP_TRAIN_FIND)
            entity->map->pending.mover.position = Q1_MAP_TOP;
        if (!relocate(g, id, &body, destination, error))
            return false;
        entity = train(g, id);
        if (!entity)
            return true;
        if (action == Q1_MAP_TRAIN_FIND)
            return q1_map_text(g, entity->targetname) ||
                   q1_map_schedule(g, entity, .1, Q1_MAP_TRAIN_NEXT, error);
        return q1_map_schedule(g, entity, .01, done, error);
    }
    if (hipnotic && speed > 0)
        entity->speed = speed;
    return q1_map_move(g, entity, destination, done, error);
}
