#include "maps/internal.h"
#include "qa/game_q1_source_entities.h"
#include "qa/game_q1_source_observer.h"
#include "qa/game_q1_bots.h"

bool qa_q1_source_movement_flags_read(const qa_q1_game *game, qa_actor_id actor,
    uint32_t *out, bool *found, qa_error *error) {
    if (!game || game->destroy_pending || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Source movement flags require their retained native owner");
        return false;
    }
    const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), actor);
    if (!record) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Source movement flag actor retired");
        return false;
    }
    const q1_actor *entity = q1_entity_const(game, actor);
    *out = 0;
    *found = false;
    if (record->owner != game->options.provider || !entity || !entity->native) return true;
    if (game->wire && !record->has_source) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
            "Native movement flags lost their physical source identity");
        return false;
    }
    uint32_t flags = entity->kind == Q1_SOURCE_CTF_FLAG ? entity->state.source_flag.movement_flags :
        entity->kind == Q1_SOURCE_CTF_RUNE ? entity->state.source_rune.movement_flags :
        entity->source_movement_flags;
    flags &= ~(UINT32_C(1) | UINT32_C(2) | UINT32_C(8) | UINT32_C(32) |
        UINT32_C(512) | UINT32_C(1024));
    uint32_t physics = entity->physics.flags;
    if (physics & QA_PHYSICS_FLYING) flags |= 1;
    if (physics & QA_PHYSICS_SWIMMING) flags |= 2;
    if (physics & QA_PHYSICS_PLAYER) flags |= 8;
    if (physics & QA_PHYSICS_MONSTER) flags |= 32;
    if (physics & QA_PHYSICS_ONGROUND) flags |= 512;
    if (physics & QA_PHYSICS_PARTIAL_GROUND) flags |= 1024;
    *out = flags;
    *found = true;
    return true;
}

bool qa_q1_source_entity_first(const qa_q1_game *game, const char *name,
    qa_q1_source_entity *out, bool *found, qa_error *error) {
    if (!game || game->destroy_pending || game->continuation_pending || !game->wire ||
        !name || !*name || !out || !found) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source entity lookup requires its live native owner");
        return false;
    }
    size_t length = strlen(name);
    const q1_actor *first = NULL;
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        const q1_actor *entity = game->actors[i];
        if (!entity || !entity->active || !entity->native ||
            !qa_actors_get(qa_session_actors(game->services.session), entity->id)) continue;
        qa_bytes classname = qa_strings_text(qa_session_strings(game->services.session), entity->classname);
        if (classname.size != length || memcmp(classname.data, name, length)) continue;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), entity->id);
        if (!record || record->owner != game->options.provider || !record->has_source) {
            qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                "Native source entity lost its genuine creation ordinal");
            return false;
        }
        if (!first || record->source_slot < ordinal) {
            first = entity;
            ordinal = record->source_slot;
        } else if (record->source_slot == ordinal) {
            qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot, "Native source entity ordinal is duplicated");
            return false;
        }
    }
    *found = first != NULL;
    *out = first ? (qa_q1_source_entity){.actor = first->id, .owner = first->owner,
        .classname = first->classname, .count = first->count, .ordinal = ordinal} : (qa_q1_source_entity){0};
    return true;
}

static bool observer_door(qa_q1_game *game, q1_actor *door, qa_body_state *body,
                          bool *written, qa_error *error) {
    q1_door_group *group = door->state.map->movement.group;
    qa_actor_id master_id = group && group->count ? group->members[0] : door->owner;
    q1_actor *master = q1_entity(game, master_id);
    if (!master || master->kind != Q1_MAP || !master->state.map ||
        master->state.map->movement.position != Q1_MAP_BOTTOM) return true;
    group = master->state.map->movement.group;
    size_t count = group && group->count ? group->count : 1;
    qa_vec3 low = qa_v3(INFINITY, INFINITY, INFINITY);
    qa_vec3 high = qa_v3(-INFINITY, -INFINITY, -INFINITY);
    for (size_t i = 0; i < count; ++i) {
        qa_actor_id actor = group && group->count ? group->members[i] : master->id;
        qa_body_state member;
        if (!qa_world_body_read(game->services.world, actor, &member, error)) return false;
        qa_vec3 min = qa_vec_add(member.origin, member.bounds.mins);
        qa_vec3 max = qa_vec_add(member.origin, member.bounds.maxs);
        low.x = fminf(low.x, min.x); low.y = fminf(low.y, min.y); low.z = fminf(low.z, min.z);
        high.x = fmaxf(high.x, max.x); high.y = fmaxf(high.y, max.y); high.z = fmaxf(high.z, max.z);
    }
    qa_vec3 min = qa_vec_add(body->origin, body->bounds.mins);
    qa_vec3 max = qa_vec_add(body->origin, body->bounds.maxs);
    bool x = low.x + 15 < min.x && max.x < high.x - 15;
    bool y = low.y + 15 < min.y && max.y < high.y - 15;
    bool z = low.z + 15 < min.z && max.z < high.z - 15;
    qa_vec3 direction = qa_v3(0, 0, 0), origin = body->origin;
    if (x && y) {
        if (origin.z < low.z) { direction.z = 1; origin.z = high.z + 25; }
        else if (origin.z > high.z) { direction.z = -1; origin.z = low.z - 25; }
    } else if (x && z) {
        if (origin.y < low.y) { direction.y = 1; origin.y = high.y + 25; }
        else if (origin.y > high.y) { direction.y = -1; origin.y = low.y - 25; }
    } else if (y && z) {
        if (origin.x < low.x) { direction.x = 1; origin.x = high.x + 25; }
        else if (origin.x > high.x) { direction.x = -1; origin.x = low.x - 25; }
    }
    if (qa_vec_dot(direction, qa_vec_normalize(body->velocity)) >= .5f) {
        body->origin = origin;
        *written = true;
    }
    return true;
}

bool qa_q1_source_observer_body(qa_q1_game *game, qa_actor_id actor, const qa_q1_input *input,
    qa_body_state *projected_body, qa_body_state *out, bool *passage_written,
    bool *teleported, qa_vec3 *view_angles, double *until, qa_error *error) {
    qa_q1_source_client_view client;
    if (!game || game->destroy_pending || game->continuation_pending || !game->wire ||
        game->options.program != QA_Q1_CTF || !input || !projected_body || !out ||
        !passage_written || !teleported ||
        !view_angles || !until || !qa_q1_source_client_read(game, actor, &client) ||
        !client.observer || !q1_alive(game, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Observer projection lost its actual CTF client");
        return false;
    }
    qa_body_state body;
    if (!qa_world_body_read(game->services.world, actor, &body, error)) return false;
    qa_vec3 forward;
    qa_builtin_angle_vectors(input->view_angles, &forward, NULL, NULL);
    qa_vec3 horizontal = qa_v3(forward.x, forward.y, 0);
    float cosine = qa_vec_length(horizontal), inverse = cosine == 0 ? 0 : 1 / cosine;
    qa_vec3 facing = qa_vec_scale(horizontal, inverse);
    qa_vec3 velocity = qa_v3(body.velocity.x, body.velocity.y, 0);
    float along = qa_vec_dot(facing, velocity);
    qa_vec3 parallel = qa_vec_scale(facing, along), strafe = qa_vec_sub(velocity, parallel);
    qa_vec3 projected = qa_vec_scale(forward,
        (along < 0 ? -qa_vec_length(parallel) : qa_vec_length(parallel)) * inverse);
    projected.z += body.velocity.z * .75f;
    float speed = qa_vec_length(projected), maximum = 320 - 100 * forward.z;
    if (speed > maximum) projected = qa_vec_scale(projected, maximum / speed);
    if (fabsf(body.angles.x) == 30) projected.z = -projected.z;
    body.velocity = qa_vec_add(projected, strafe);
    *projected_body = body;
    *passage_written = false;
    *teleported = false;
    *view_angles = input->view_angles;
    *until = input->teleport_until;
    q1_actor *near = NULL;
    uint32_t ordinal = 0;
    for (uint32_t i = 0; i < game->capacity; ++i) {
        q1_actor *entity = game->actors[i];
        if (!entity || !entity->active || !entity->native || entity->kind != Q1_MAP ||
            !entity->state.map || !q1_alive(game, entity->id) ||
            (!q1_classnamed(game, entity->id, "func_door") &&
             !q1_classnamed(game, entity->id, "trigger_teleport"))) continue;
        qa_body_state target;
        if (!qa_world_body_read(game->services.world, entity->id, &target, error)) return false;
        qa_vec3 center = qa_vec_add(target.origin,
            qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), .5f));
        if (qa_vec_length(qa_vec_sub(center, body.origin)) > 75) continue;
        const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), entity->id);
        if (!record || !record->has_source || record->owner != game->options.provider) {
            qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot, "Observer geometry lost its source ordinal");
            return false;
        }
        if (!near || record->source_slot < ordinal) { near = entity; ordinal = record->source_slot; }
    }
    if (near && q1_classnamed(game, near->id, "func_door")) {
        if (!observer_door(game, near, &body, passage_written, error)) return false;
    } else if (near) {
        qa_body_state target;
        if (!qa_world_body_read(game->services.world, near->id, &target, error)) return false;
        qa_vec3 direction = qa_vec_sub(qa_vec_add(target.origin,
            qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), .5f)), body.origin);
        if (qa_vec_dot(direction, body.velocity) > .1f && near->target) {
            q1_actor *destination = NULL;
            for (uint32_t i = 0; i < game->capacity; ++i) {
                q1_actor *entity = game->actors[i];
                if (!entity || !entity->active || !entity->native ||
                    entity->targetname != near->target || !q1_alive(game, entity->id)) continue;
                const qa_actor_record *record = qa_actors_get(qa_session_actors(game->services.session), entity->id);
                if (!record || !record->has_source || record->owner != game->options.provider) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot, "Observer target lost its source ordinal");
                    return false;
                }
                if (!destination || record->source_slot < ordinal) {
                    destination = entity; ordinal = record->source_slot;
                }
            }
            if (destination) {
                if (destination->kind != Q1_MAP || !destination->state.map) {
                    qa_error_set(error, QA_ERROR_UNSUPPORTED, destination->id.slot,
                        "Observer destination has no retained native source mangle");
                    return false;
                }
                if (!qa_world_body_read(game->services.world, destination->id, &target, error)) return false;
                body.origin = target.origin;
                *view_angles = destination->state.map->mangle;
                qa_builtin_angle_vectors(*view_angles, &forward, NULL, NULL);
                body.velocity = qa_vec_scale(forward, 300);
                *until = game->time + .7;
                *teleported = true;
                *passage_written = true;
            }
        }
    }
    if (!qa_q1_source_client_read(game, actor, &client) || !client.observer || !q1_alive(game, actor)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Observer projection changed its source client");
        return false;
    }
    *out = body;
    return true;
}
