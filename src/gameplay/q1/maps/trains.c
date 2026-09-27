#include "internal.h"

bool q1_map_train_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
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
    if (teleport) {
        if (!q1_model(g, entity, "progs/teleport.mdl", error))
            return false;
        entity->physics.angular_velocity = qa_v3(100, 200, 300);
    } else if (!qa_builtin_resource(&g->services, "train", &entity->classname, error))
        return false;
    return q1_map_schedule(g, entity, .1, Q1_MAP_TRAIN_FIND, error) && q1_link(g, entity, error);
}
static bool corner(qa_q1_game *g, q1_actor *entity, qa_authored_target *fields, qa_body_state *body,
                   qa_error *error) {
    qa_actor_id target;
    if (!qa_targets_first(g->maps->options.targets, entity->target, &target) ||
        !qa_targets_read(g->maps->options.targets, target, fields))
        return q1_map_fail(error, "Q1 train target not found");
    if (!isfinite(fields->wait_seconds))
        return q1_map_fail(error, "Q1 train target has invalid wait");
    return qa_world_body_read(g->services.world, target, body, error);
}
bool q1_map_train_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    if (action == Q1_MAP_TRAIN_WAIT) {
        if (entity->wait &&
            !q1_sound(g, entity->id,
                      entity->map->sounds == 1 ? "plats/train2.wav" : "misc/null.wav", 2, 1, error))
            return false;
        return !q1_alive(g, entity->id) ||
               q1_map_schedule(g, entity, entity->wait ? entity->wait : .1, Q1_MAP_TRAIN_NEXT,
                               error);
    }
    if (action != Q1_MAP_TRAIN_FIND && action != Q1_MAP_TRAIN_NEXT)
        return q1_map_fail(error, "invalid Q1 train continuation");
    qa_authored_target fields;
    qa_body_state node, body;
    if (action == Q1_MAP_TRAIN_NEXT)
        entity->map->pending.mover.activated = true;
    if (!corner(g, entity, &fields, &node, error) ||
        !qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    entity->target = fields.target;
    qa_vec3 destination = qa_vec_sub(node.origin, body.bounds.mins);
    if (action == Q1_MAP_TRAIN_FIND) {
        body.origin = destination;
        entity->map->pending.mover.position = Q1_MAP_TOP;
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               q1_link(g, entity, error) &&
               (!q1_alive(g, entity->id) || q1_map_text(g, entity->targetname) ||
                q1_map_schedule(g, entity, .1, Q1_MAP_TRAIN_NEXT, error));
    }
    if (!q1_map_text(g, entity->target))
        return q1_map_fail(error, "Q1 train path has no next target");
    entity->wait = fields.wait_seconds;
    return q1_sound(g, entity->id, entity->map->sounds == 1 ? "plats/train1.wav" : "misc/null.wav",
                    2, 1, error) &&
           (!q1_alive(g, entity->id) ||
            q1_map_move(g, entity, destination, Q1_MAP_TRAIN_WAIT, error));
}
