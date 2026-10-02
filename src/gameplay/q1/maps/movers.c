#include "internal.h"
#include "qa/game_q1_wire.h"
#include <stdio.h>

bool q1_map_is_mover(q1_map_kind kind) {
    return kind >= Q1_MAP_DOOR && kind <= Q1_MAP_PLAT_TRIGGER;
}
bool q1_map_move(qa_q1_game *g, q1_actor *entity, qa_vec3 destination, q1_map_action done,
                 qa_error *error) {
    qa_actor_id id = entity->id;
    float speed = entity->speed;
    if (!(speed > 0))
        return q1_map_fail(error, "Q1 mover speed must be positive");
    q1_map_cancel(g, entity);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    qa_vec3 delta = qa_vec_sub(destination, body.origin);
    double duration = qa_vec_length(delta) / (double)speed;
    if (!qa_vec_finite(destination) || !isfinite(duration))
        return q1_map_fail(error, "invalid Q1 mover destination or speed");
    body.velocity = duration < .1 ? qa_v3(0, 0, 0) : qa_vec_scale(delta, (float)(1 / duration));
    q1_map_movement *move = &entity->map->pending.mover;
    move->destination = destination;
    move->done = done;
    move->moving = true;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    return !entity || !entity->map ||
           q1_map_schedule(g, entity, fmax(.1, duration), Q1_MAP_MOVE_DONE, error);
}
static const char *door_sound(const q1_actor *entity, bool moving) {
    static const char *const sounds[][2] = {{"misc/null.wav", "misc/null.wav"},
                                            {"doors/drclos4.wav", "doors/doormv1.wav"},
                                            {"doors/hydro2.wav", "doors/hydro1.wav"},
                                            {"doors/stndr2.wav", "doors/stndr1.wav"},
                                            {"doors/ddoor2.wav", "doors/ddoor1.wav"}};
    unsigned selection = (unsigned)entity->map->sounds;
    return sounds[selection < 5 ? selection : 0][moving];
}
static const char *secret_sound(const q1_actor *entity, bool moving) {
    return entity->map->sounds == 1   ? (moving ? "doors/winch2.wav" : "doors/drclos4.wav")
           : entity->map->sounds == 2 ? (moving ? "doors/airdoor1.wav" : "doors/airdoor2.wav")
                                      : (moving ? "doors/basesec1.wav" : "doors/basesec2.wav");
}
static const char *plat_sound(const q1_actor *entity, bool moving) {
    return entity->map->sounds == 1 ? (moving ? "plats/plat1.wav" : "plats/plat2.wav")
                                    : (moving ? "plats/medplat1.wav" : "plats/medplat2.wav");
}
static bool rearm(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    return entity->max_health <= 0 ||
           (qa_combat_set_health(g->services.combat, entity->id, entity->max_health, error) &&
            (!q1_alive(g, entity->id) || q1_map_damageable(g, entity, true, error)));
}
static q1_actor *door_master(qa_q1_game *g, q1_actor *entity) {
    const q1_door_group *group = entity->map->pending.mover.group;
    return group ? q1_entity(g, group->members[0]) : entity;
}
bool q1_map_door_down(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!q1_sound(g, entity->id, door_sound(entity, true), 2, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    entity->map->pending.mover.position = Q1_MAP_DOWN;
    return rearm(g, entity, error) &&
           (!q1_alive(g, entity->id) ||
            q1_map_move(g, entity, entity->map->pending.mover.pos1, Q1_MAP_DOOR_BOTTOM, error));
}
static bool door_up(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_movement *move = &entity->map->pending.mover;
    if (move->position == Q1_MAP_UP)
        return true;
    if (move->position == Q1_MAP_TOP)
        return entity->wait < 0 || (entity->spawnflags & 32) ||
               q1_map_schedule(g, entity, entity->wait, Q1_MAP_DOOR_DOWN, error);
    if (!q1_sound(g, entity->id, door_sound(entity, true), 2, 1, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    move->position = Q1_MAP_UP;
    qa_actor_id id = entity->id;
    if (!q1_map_move(g, entity, move->pos2, Q1_MAP_DOOR_TOP, error))
        return false;
    entity = q1_entity(g, id);
    return !entity || !entity->map || q1_map_targets(g, entity, entity->activator, error);
}
static bool door_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    q1_actor *master = door_master(g, entity);
    if (!master || !master->map)
        return true;
    q1_map_movement *move = &master->map->pending.mover;
    bool down =
        (master->spawnflags & 32) && (move->position == Q1_MAP_UP || move->position == Q1_MAP_TOP);
    const q1_door_group *group = move->group;
    size_t count = group ? group->count : 1;
    qa_actor_id single = master->id;
    for (size_t i = 0; i < count; ++i) {
        q1_actor *door = q1_entity(g, group ? group->members[i] : single);
        if (!door || !door->map)
            continue;
        door->message = QA_STRING_NONE;
        door->activator = activator;
        if (!(down ? q1_map_door_down(g, door, error) : door_up(g, door, error)))
            return false;
    }
    return true;
}
bool q1_map_addon_relay_mover(qa_q1_game *g, q1_actor *e, bool close,
                             qa_actor_id activator, qa_error *error) {
    if (e->map->kind == Q1_MAP_DOOR)
        return close ? q1_map_door_down(g, e, error) : door_up(g, e, error);
    if (e->map->kind == Q1_MAP_BUTTON)
        return close ? q1_map_mover_think(g, e, Q1_MAP_BUTTON_RETURN, error)
                     : q1_map_mover_use(g, e, activator, error);
    return true;
}
bool q1_map_addon_egg_mover(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    e = q1_entity(g, id);
    if (!e || !e->map || e->map->kind != Q1_MAP_DOOR)
        return true;
    q1_map_movement *move = &e->map->pending.mover;
    e->map->movedir = e->map->has_dest2 ? e->map->dest2 : move->dest2;
    move->pos1 = body.origin;
    move->pos2 = qa_vec_add(body.origin, e->map->movedir);
    move->position = Q1_MAP_BOTTOM;
    e->speed = 500;
    e->map->sounds = 1;
    if (!qa_builtin_resource(&g->services, "doors/drclos4.wav", &e->map->noise[1], error) ||
        !qa_builtin_resource(&g->services, "doors/doormv1.wav", &e->map->noise[2], error))
        return false;
    return door_up(g, e, error);
}
bool qa_q1_game_map_egg_mover(qa_q1_game *g, qa_actor_id actor, bool *handled,
                             qa_error *error) {
    if (!g || !handled)
        return q1_map_fail(error, "Invalid Q1 egg mover operation");
    *handled = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_actor *e = q1_entity(g, actor);
    bool ok = true;
    if (e && e->native && e->map && e->map->kind == Q1_MAP_DOOR) {
        *handled = true;
        ok = q1_map_addon_egg_mover(g, e, error);
    }
    if (ok && !qa_q1_game_operation_live(&operation)) {
        q1_map_fail(error, "Q1 source retired during egg mover operation");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
bool qa_q1_game_map_relay_mover(qa_q1_game *g, qa_actor_id actor, bool close,
                               qa_actor_id activator, bool *handled, qa_error *error) {
    if (!g || !handled)
        return q1_map_fail(error, "Invalid Q1 directed mover operation");
    *handled = false;
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    q1_actor *e = q1_entity(g, actor);
    bool ok = true;
    if (e && e->native && e->map &&
        (e->map->kind == Q1_MAP_DOOR || e->map->kind == Q1_MAP_BUTTON)) {
        *handled = true;
        ok = q1_map_addon_relay_mover(g, e, close, activator, error);
    }
    if (ok && !qa_q1_game_operation_live(&operation)) {
        q1_map_fail(error, "Q1 source retired during directed mover operation");
        ok = false;
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
static bool button_fire(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    static const char *const sounds[] = {"buttons/airbut1.wav", "buttons/switch21.wav",
                                         "buttons/switch02.wav", "buttons/switch04.wav"};
    q1_map_movement *move = &entity->map->pending.mover;
    if (move->position == Q1_MAP_UP || move->position == Q1_MAP_TOP)
        return true;
    entity->activator = activator;
    move->position = Q1_MAP_UP;
    unsigned sound = (unsigned)entity->map->sounds;
    return q1_sound(g, entity->id, sounds[sound < 4 ? sound : 0], 2, 1, error) &&
           (!q1_alive(g, entity->id) ||
            q1_map_move(g, entity, move->pos2, Q1_MAP_BUTTON_TOP, error));
}
static bool secret_shootable(qa_q1_game *g, q1_actor *entity) {
    return !q1_map_text(g, entity->targetname) || (entity->spawnflags & 16);
}
static bool secret_fire(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    if (!qa_combat_set_health(g->services.combat, entity->id, 10000, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    q1_map_state *state = entity->map;
    q1_map_movement *move = &state->pending.mover;
    if (move->position != Q1_MAP_BOTTOM || move->moving)
        return true;
    entity->message = QA_STRING_NONE;
    if (!q1_map_targets(g, entity, activator, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (!q1_map_damageable(g, entity, false, error))
        return false;
    move->position = Q1_MAP_UP;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 forward, right, up, size = qa_vec_sub(body.bounds.maxs, body.bounds.mins);
    qa_builtin_angle_vectors(state->mangle, &forward, &right, &up);
    float width =
        state->width ? state->width : fabsf(qa_vec_dot(entity->spawnflags & 4 ? up : right, size));
    float length = state->length ? state->length : fabsf(qa_vec_dot(forward, size));
    qa_vec3 side = entity->spawnflags & 4
                       ? qa_vec_scale(up, -width)
                       : qa_vec_scale(right, width * (1 - (int)(entity->spawnflags & 2)));
    move->dest1 = qa_vec_add(move->pos1, side);
    move->dest2 = qa_vec_add(move->dest1, qa_vec_scale(forward, length));
    if (!q1_sound(g, entity->id,
                  state->sounds == 1 ? "doors/latch2.wav" : secret_sound(entity, false), 2, 1,
                  error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    return q1_sound(g, entity->id, secret_sound(entity, true), 2, 1, error) &&
           (!q1_alive(g, entity->id) ||
            q1_map_move(g, entity, move->dest1, Q1_MAP_SECRET_FIRST, error));
}
static bool plat_move(qa_q1_game *g, q1_actor *entity, bool up, qa_error *error) {
    q1_map_movement *move = &entity->map->pending.mover;
    move->position = up ? Q1_MAP_UP : Q1_MAP_DOWN;
    return q1_sound(g, entity->id, plat_sound(entity, true), 2, 1, error) &&
           (!q1_alive(g, entity->id) ||
            q1_map_move(g, entity, up ? move->pos1 : move->pos2,
                        up ? Q1_MAP_PLAT_TOP : Q1_MAP_PLAT_BOTTOM, error));
}
static bool helper_trigger(qa_q1_game *g, q1_actor *owner, q1_map_kind kind, qa_bounds bounds,
                           qa_error *error) {
    qa_actor_id owner_id = owner->id;
    const char *name = kind == Q1_MAP_DOOR_TRIGGER         ? "door_trigger"
                       : kind == Q1_MAP_ROGUE_PLAT_TRIGGER ? "rogue_plat2_trigger"
                                                           : "plat_trigger";
    q1_actor *trigger;
    if (!q1_create(g, name, Q1_MAP, owner_id, &trigger, error))
        return false;
    qa_actor_id id = trigger->id;
    if (!q1_map_allocate(g, trigger, error))
        goto fail;
    trigger->map->kind = kind;
    trigger->map->touch_enabled = true;
    trigger->physics.solid = QA_PHYSICS_TRIGGER;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        goto fail;
    trigger = q1_entity(g, id);
    if (!trigger)
        return true;
    body.bounds = bounds;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        goto fail;
    trigger = q1_entity(g, id);
    if (trigger && !q1_link(g, trigger, error))
        goto fail;
    if (!q1_alive(g, owner_id))
        (void)qa_session_release(g->services.session, id, NULL);
    return true;
fail:
    (void)qa_session_release(g->services.session, id, NULL);
    return false;
}
bool q1_map_plat_trigger(qa_q1_game *g, q1_actor *owner, q1_map_kind kind, qa_bounds bounds,
                         float height, qa_error *error) {
    qa_bounds trigger = {qa_vec_add(bounds.mins, qa_v3(25, 25, 0)),
                         qa_vec_add(bounds.maxs, qa_v3(-25, -25, 8))};
    trigger.mins.z = trigger.maxs.z - height - 8;
    if (owner->spawnflags & 1)
        trigger.maxs.z = trigger.mins.z + 8;
    if (bounds.maxs.x - bounds.mins.x <= 50) {
        trigger.mins.x = (bounds.mins.x + bounds.maxs.x) * .5f;
        trigger.maxs.x = trigger.mins.x + 1;
    }
    if (bounds.maxs.y - bounds.mins.y <= 50) {
        trigger.mins.y = (bounds.mins.y + bounds.maxs.y) * .5f;
        trigger.maxs.y = trigger.mins.y + 1;
    }
    return helper_trigger(g, owner, kind, trigger, error);
}
bool q1_map_mover_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    if (g->options.program == QA_Q1_MG3 && q1_classnamed(g, id, "func_axe_button")) {
        entity->max_health = 1;
        if (!qa_combat_set_health(g->services.combat, id, 1, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
    }
    q1_map_state *state = entity->map;
    if (state->kind == Q1_MAP_TRAIN || state->kind == Q1_MAP_TRAIN2)
        return q1_map_train_spawn(g, entity, error);
    if (!state->has_inline_model)
        return q1_map_fail(error, "Q1 brush mover has no inline model");
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    if (!entity || !entity->map)
        return true;
    state = entity->map;
    qa_vec3 size = qa_vec_sub(body.bounds.maxs, body.bounds.mins);
    q1_map_movement *move = &state->pending.mover;
    move->pos1 = body.origin;
    entity->physics.motion = QA_PHYSICS_PUSH;
    entity->physics.solid = QA_PHYSICS_BRUSH;
    state->use_enabled = true;
    state->movedir = q1_map_direction(body.angles);
    if (state->kind == Q1_MAP_SECRET_DOOR)
        state->mangle = body.angles;
    body.angles = qa_v3(0, 0, 0);
    switch (state->kind) {
    case Q1_MAP_DOOR: {
        entity->speed = entity->speed ? entity->speed : 100;
        entity->wait = entity->spawnflags & 24 ? -1 : entity->wait ? entity->wait : 3;
        entity->damage = entity->damage ? entity->damage : 2;
        qa_vec3 absolute =
            qa_v3(fabsf(state->movedir.x), fabsf(state->movedir.y), fabsf(state->movedir.z));
        float distance = g->options.edition == QA_Q1_RERELEASE
                             ? qa_vec_dot(absolute, size)
                             : fabsf(qa_vec_dot(state->movedir, size));
        move->pos2 = qa_vec_add(
            move->pos1, qa_vec_scale(state->movedir, distance - (state->lip ? state->lip : 8)));
        if (entity->spawnflags & 1) {
            qa_vec3 closed = move->pos1;
            move->pos1 = move->pos2;
            move->pos2 = closed;
            body.origin = move->pos1;
        }
        state->touch_enabled = true;
        if (entity->max_health > 0 && !q1_map_damageable(g, entity, true, error))
            return false;
        break;
    }
    case Q1_MAP_BUTTON:
        entity->speed = entity->speed ? entity->speed : 40;
        entity->wait = entity->wait ? entity->wait : 1;
        move->pos2 = qa_vec_add(
            move->pos1, qa_vec_scale(state->movedir, fabsf(qa_vec_dot(state->movedir, size)) -
                                                         (state->lip ? state->lip : 4)));
        state->touch_enabled = entity->max_health <= 0;
        if (entity->max_health > 0 && !q1_map_damageable(g, entity, true, error))
            return false;
        break;
    case Q1_MAP_SECRET_DOOR:
        entity->speed = 50;
        entity->wait = entity->wait ? entity->wait : 5;
        entity->damage = entity->damage ? entity->damage : 2;
        state->sounds = state->sounds ? state->sounds : 3;
        state->touch_enabled = true;
        if (!q1_map_damageable(g, entity, secret_shootable(g, entity), error) ||
            !qa_combat_set_health(g->services.combat, entity->id, 10000, error))
            return false;
        break;
    case Q1_MAP_PLAT: {
        entity->speed = entity->speed ? entity->speed : 150;
        move->pos2 =
            qa_vec_add(move->pos1, qa_v3(0, 0, -(state->height ? state->height : size.z - 8)));
        move->activated = !q1_map_text(g, entity->targetname);
        move->position = move->activated ? Q1_MAP_BOTTOM : Q1_MAP_UP;
        if (move->activated)
            body.origin = move->pos2;
        if (!q1_map_plat_trigger(g, entity, Q1_MAP_PLAT_TRIGGER, body.bounds,
                                 move->pos1.z - move->pos2.z, error))
            return false;
        break;
    }
    default:
        return q1_map_fail(error, "invalid Q1 authored mover kind");
    }
    if (!q1_alive(g, id))
        return true;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = q1_entity(g, id);
    return !entity || q1_link(g, entity, error);
}
bool qa_q1_game_maps_finish(qa_q1_game *g, qa_error *error) {
    if (!g || !g->maps)
        return q1_map_fail(error, "Q1 map services are not bound");
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    size_t count = 0;
    for (size_t i = 0; i < snapshot->count; ++i) {
        q1_actor *entity = q1_entity(g, snapshot->actors[i]);
        if (entity && entity->map && entity->map->kind == Q1_MAP_DOOR)
            snapshot->actors[count++] = entity->id;
    }
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        q1_actor *master = q1_entity(g, snapshot->actors[i]);
        if (!master || master->map->pending.mover.group)
            continue;
        q1_door_group *group = calloc(1, sizeof(*group));
        if (!group) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q1 door group");
            ok = false;
            break;
        }
        group->next = g->maps->door_groups;
        g->maps->door_groups = group;
        qa_body_state body;
        if (!(ok = qa_world_body_read(g->services.world, master->id, &body, error)))
            break;
        qa_bounds bounds = body.bounds, previous = body.bounds;
        size_t capacity = 0;
        for (size_t j = i; j < count; ++j) {
            q1_actor *candidate = q1_entity(g, snapshot->actors[j]);
            if (!candidate)
                continue;
            if (j != i) {
                if (master->spawnflags & 4)
                    break;
                if (!(ok = qa_world_body_read(g->services.world, candidate->id, &body, error)))
                    break;
                if (!qa_bounds_overlap(previous, body.bounds))
                    continue;
                if (candidate->map->pending.mover.group) {
                    ok = q1_map_fail(error, "cross connected Q1 doors");
                    break;
                }
            }
            if (group->count == capacity) {
                size_t next = capacity ? capacity * 2 : 4;
                if (next < capacity || next > SIZE_MAX / sizeof(*group->members)) {
                    ok = q1_map_fail(error, "Q1 door group capacity overflow");
                    break;
                }
                qa_actor_id *members = realloc(group->members, next * sizeof(*members));
                if (!members) {
                    qa_error_set(error, QA_ERROR_MEMORY, 0, "growing Q1 door group");
                    ok = false;
                    break;
                }
                group->members = members;
                capacity = next;
            }
            group->members[group->count++] = candidate->id;
            candidate->map->pending.mover.group = group;
            previous = body.bounds;
            bounds = qa_bounds_union(bounds, body.bounds);
            if (j == i)
                continue;
            if (q1_map_text(g, candidate->targetname)) {
                master->targetname = candidate->targetname;
                qa_targets_changed(g->maps->options.targets);
            }
            if (q1_map_text(g, candidate->message))
                master->message = candidate->message;
            if (candidate->max_health) {
                master->max_health = candidate->max_health;
                if (!(ok = qa_combat_set_health(g->services.combat, master->id,
                                                candidate->max_health, error)))
                    break;
            }
        }
        if (!ok || !q1_alive(g, master->id))
            continue;
        if ((master->spawnflags & 28) || master->max_health > 0 ||
            q1_map_text(g, master->targetname))
            continue;
        bounds.mins = qa_vec_sub(bounds.mins, qa_v3(60, 60, 8));
        bounds.maxs = qa_vec_add(bounds.maxs, qa_v3(60, 60, 8));
        ok = helper_trigger(g, master, Q1_MAP_DOOR_TRIGGER, bounds, error);
    }
    snapshot->borrowed = false;
    return ok && qa_q1_wire_freeze(g, error);
}
bool q1_map_mover_use(qa_q1_game *g, q1_actor *entity, qa_actor_id activator, qa_error *error) {
    switch (entity->map->kind) {
    case Q1_MAP_DOOR:
        return door_use(g, entity, activator, error);
    case Q1_MAP_BUTTON:
        return button_fire(g, entity, activator, error);
    case Q1_MAP_SECRET_DOOR:
        return secret_fire(g, entity, activator, error);
    case Q1_MAP_PLAT:
        if (entity->map->pending.mover.activated)
            return true;
        entity->map->pending.mover.activated = true;
        return plat_move(g, entity, false, error);
    case Q1_MAP_TRAIN:
    case Q1_MAP_TRAIN2:
        return q1_map_train_use(g, entity, activator, error);
    default:
        return true;
    }
}
static bool horde_key(qa_q1_game *g, qa_actor_id actor, bool gold, bool *has,
                       qa_error *error) {
    *has = false;
    if (!qa_inventory_has(g->services.inventory, actor))
        return true;
    qa_item_id item;
    if (!qa_builtin_resource(&g->services, gold ? "q1:key/gold" : "q1:key/silver", &item, error))
        return false;
    qa_inventory_entry entry;
    qa_error missing = {0};
    if (!qa_inventory_entry_read(g->services.inventory, actor, item, &entry, &missing)) {
        if (missing.code == QA_ERROR_NOT_FOUND)
            return true;
        if (error)
            *error = missing;
        return false;
    }
    *has = entry.count != 0;
    return true;
}
static bool horde_door_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                              qa_error *error) {
    qa_actor_id id = entity->id;
    q1_actor *master = door_master(g, entity);
    if (!master || !master->map || !q1_map_player(g, other) || master->map->cooldown > g->time)
        return true;
    qa_actor_id master_id = master->id;
    master->map->cooldown = g->time + 2;
    const char *message = qa_strings_cstr(qa_session_strings(g->services.session), master->message);
    if (message && *message) {
        if (!q1_message(g, other, message, error))
            return false;
        if (!q1_alive(g, other) || !q1_alive(g, id) || !q1_alive(g, master_id))
            return true;
        if (!q1_sound(g, other, "misc/talk.wav", 2, 1, error))
            return false;
    }
    entity = q1_entity(g, id);
    master = q1_entity(g, master_id);
    if (!entity || !entity->map || !master || !master->map || !q1_alive(g, other))
        return true;
    bool silver = (entity->spawnflags & 16) != 0, gold = (entity->spawnflags & 8) != 0;
    if (!silver && !gold)
        return true;
    bool has_silver = false, has_gold = false;
    if ((silver && !horde_key(g, other, false, &has_silver, error)) ||
        (gold && !horde_key(g, other, true, &has_gold, error)))
        return false;
    if (!q1_alive(g, id) || !q1_alive(g, master_id) || !q1_alive(g, other))
        return true;
    if ((silver && !has_silver) || (gold && !has_gold)) {
        if (!q1_sound(g, id, g->options.world_type == 2 ? "doors/basetry.wav"
                           : g->options.world_type == 1 ? "doors/runetry.wav"
                                                        : "doors/medtry.wav", 2, 1, error))
            return false;
        if (silver == gold || !q1_alive(g, other))
            return true;
        char text[64];
        snprintf(text, sizeof(text), "$qc_need_%s_%s", silver ? "silver" : "gold",
                 g->options.world_type == 2 ? "keycard"
                 : g->options.world_type == 1 ? "runekey" : "key");
        return q1_message(g, other, text, error);
    }
    if (!g->maps->options.horde_keys)
        return q1_map_fail(error, "Q1 Horde door requires the selected shared key owner");
    if (!g->maps->options.horde_keys(g->maps->options.context, !silver, -1, error))
        return false;
    master = q1_entity(g, master_id);
    if (!master || !master->map || !q1_alive(g, other))
        return true;
    const q1_door_group *group = master->map->pending.mover.group;
    if (group) {
        for (size_t i = 0; i < group->count; ++i) {
            q1_actor *door = q1_entity(g, group->members[i]);
            if (door && door->map)
                door->map->touch_enabled = false;
        }
    } else
        master->map->touch_enabled = false;
    return door_use(g, master, other, error);
}
static bool door_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (q1_map_horde_present(g))
        return horde_door_touch(g, entity, other, error);
    q1_actor *master = door_master(g, entity);
    if (!master || !q1_map_player(g, other) || master->map->cooldown > g->time)
        return true;
    master->map->cooldown = g->time + 2;
    const char *message = qa_strings_cstr(qa_session_strings(g->services.session), master->message);
    if (message && *message && !q1_message(g, other, message, error))
        return false;
    if (!q1_alive(g, entity->id) || !q1_alive(g, master->id) || !q1_alive(g, other))
        return true;
    const char *key = entity->spawnflags & 8    ? "q1:key/gold"
                      : entity->spawnflags & 16 ? "q1:key/silver"
                                                : NULL;
    if (!key || !qa_inventory_has(g->services.inventory, other))
        return true;
    qa_item_id item;
    bool consumed;
    if (!qa_builtin_resource(&g->services, key, &item, error) ||
        !qa_inventory_consume(g->services.inventory, other, item, 1, &consumed, error))
        return false;
    if (!q1_alive(g, entity->id) || !q1_alive(g, master->id) || !q1_alive(g, other))
        return true;
    if (!consumed) {
        char text[64];
        snprintf(text, sizeof(text), "$qc_need_%s_%s", entity->spawnflags & 8 ? "gold" : "silver",
                 g->options.world_type == 2   ? "keycard"
                 : g->options.world_type == 1 ? "runekey"
                                              : "key");
        if (!q1_message(g, other, text, error))
            return false;
        return !q1_alive(g, entity->id) ||
               q1_sound(g, entity->id,
                        g->options.world_type == 2   ? "doors/basetry.wav"
                        : g->options.world_type == 1 ? "doors/runetry.wav"
                                                     : "doors/medtry.wav",
                        2, 1, error);
    }
    const q1_door_group *group = master->map->pending.mover.group;
    if (group) {
        for (size_t i = 0; i < group->count; ++i) {
            q1_actor *door = q1_entity(g, group->members[i]);
            if (door && door->map)
                door->map->touch_enabled = false;
        }
    } else
        master->map->touch_enabled = false;
    return q1_sound(g, entity->id,
                    g->options.world_type == 2   ? "doors/baseuse.wav"
                    : g->options.world_type == 1 ? "doors/runeuse.wav"
                                                 : "doors/meduse.wav",
                    3, 1, error) &&
           (!q1_alive(g, master->id) || door_use(g, master, other, error));
}
bool q1_map_mover_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    q1_map_state *state = entity->map;
    switch (state->kind) {
    case Q1_MAP_DOOR:
        return door_touch(g, entity, other, error);
    case Q1_MAP_BUTTON:
        return !q1_map_player(g, other) || button_fire(g, entity, other, error);
    case Q1_MAP_SECRET_DOOR: {
        if (!q1_map_player(g, other) || state->cooldown > g->time)
            return true;
        state->cooldown = g->time + 2;
        const char *message =
            qa_strings_cstr(qa_session_strings(g->services.session), entity->message);
        return !message || !*message || q1_message(g, other, message, error);
    }
    case Q1_MAP_DOOR_TRIGGER: {
        if (q1_health(g, other) <= 0 || state->cooldown > g->time)
            return true;
        q1_actor *master = q1_entity(g, entity->owner);
        if (!master || !master->map)
            return true;
        state->cooldown = g->time + 1;
        return door_use(g, master, other, error);
    }
    case Q1_MAP_PLAT_TRIGGER: {
        if (!q1_map_player(g, other) || q1_health(g, other) <= 0)
            return true;
        q1_actor *plat = q1_entity(g, entity->owner);
        if (!plat || !plat->map)
            return true;
        if (plat->map->pending.mover.position == Q1_MAP_BOTTOM)
            return plat_move(g, plat, true, error);
        return plat->map->pending.mover.position != Q1_MAP_TOP ||
               q1_map_schedule(g, plat, 1, Q1_MAP_PLAT_DOWN, error);
    }
    default:
        return true;
    }
}
bool q1_map_mover_blocked(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    q1_map_kind kind = entity->map->kind;
    if (kind != Q1_MAP_DOOR && kind != Q1_MAP_PLAT && kind != Q1_MAP_SECRET_DOOR &&
        kind != Q1_MAP_TRAIN && kind != Q1_MAP_TRAIN2)
        return true;
    if (kind == Q1_MAP_SECRET_DOOR || kind == Q1_MAP_TRAIN || kind == Q1_MAP_TRAIN2) {
        if (entity->map->cooldown > g->time)
            return true;
        entity->map->cooldown = g->time + .5;
    }
    qa_string_id crush;
    if (!qa_builtin_resource(&g->services, "crush", &crush, error) ||
        !q1_damage_typed(g, other, entity->id, entity->id, kind == Q1_MAP_PLAT ? 1 : entity->damage,
                         QA_Q1_WEAPON_COUNT, QA_Q1_ARMOR_NORMAL, crush, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (kind == Q1_MAP_PLAT)
        return plat_move(g, entity, entity->map->pending.mover.position != Q1_MAP_UP, error);
    if (kind == Q1_MAP_DOOR && entity->wait >= 0)
        return entity->map->pending.mover.position == Q1_MAP_DOWN
                   ? door_up(g, entity, error)
                   : q1_map_door_down(g, entity, error);
    return true;
}
bool q1_map_mover_reaction(qa_q1_game *g, q1_actor *entity, const qa_damage_outcome *outcome,
                           qa_error *error) {
    qa_actor_id attacker = outcome->request.attack.attacker;
    if (entity->map->kind == Q1_MAP_SECRET_DOOR && (outcome->result.reaction == QA_REACTION_PAIN ||
                                                    outcome->result.reaction == QA_REACTION_DEATH))
        return secret_fire(g, entity, attacker, error);
    if (outcome->result.reaction != QA_REACTION_DEATH)
        return true;
    q1_actor *target = entity->map->kind == Q1_MAP_DOOR ? door_master(g, entity) : entity;
    if (!target || !target->map ||
        (target->map->kind != Q1_MAP_DOOR && target->map->kind != Q1_MAP_BUTTON))
        return true;
    return qa_combat_set_health(g->services.combat, target->id, target->max_health, error) &&
           (!q1_alive(g, target->id) || (q1_map_damageable(g, target, false, error) &&
                                         q1_map_mover_use(g, target, attacker, error)));
}
bool q1_map_mover_think(qa_q1_game *g, q1_actor *entity, q1_map_action action, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_movement *move = &entity->map->pending.mover;
    if (action == Q1_MAP_MOVE_DONE) {
        if (!move->moving)
            return q1_map_fail(error, "Q1 mover completion has no destination");
        qa_vec3 destination = move->destination;
        action = move->done;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        body.origin = destination;
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        if (!q1_link(g, entity, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        body.velocity = qa_v3(0, 0, 0);
        if (!qa_world_body_write(g->services.world, id, &body, error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map)
            return true;
        move = &entity->map->pending.mover;
        entity->next_think = -1;
        move->moving = false;
    }
    if (action >= Q1_MAP_ROGUE_PLAT_UP && action <= Q1_MAP_ELEVATOR_BUTTON_DONE)
        return q1_map_rogue_plat_think(g, entity, action, error);
    switch (action) {
    case Q1_MAP_DOOR_DOWN:
        return q1_map_door_down(g, entity, error);
    case Q1_MAP_DOOR_BOTTOM:
        move->position = Q1_MAP_BOTTOM;
        return q1_sound(g, entity->id, door_sound(entity, false), 2, 1, error);
    case Q1_MAP_DOOR_TOP:
        move->position = Q1_MAP_TOP;
        return q1_sound(g, entity->id, door_sound(entity, false), 2, 1, error) &&
               (!q1_alive(g, entity->id) || entity->wait < 0 || (entity->spawnflags & 32) ||
                q1_map_schedule(g, entity, entity->wait, Q1_MAP_DOOR_DOWN, error));
    case Q1_MAP_BUTTON_TOP:
        move->position = Q1_MAP_TOP;
        if (entity->wait >= 0 &&
            !q1_map_schedule(g, entity, entity->wait, Q1_MAP_BUTTON_RETURN, error))
            return false;
        if (!q1_map_targets(g, entity, entity->activator, error))
            return false;
        if (q1_alive(g, entity->id))
            entity->frame = 1;
        return true;
    case Q1_MAP_BUTTON_RETURN:
        move->position = Q1_MAP_DOWN;
        entity->frame = 0;
        if (entity->max_health > 0 && !q1_map_damageable(g, entity, true, error))
            return false;
        entity = q1_entity(g, id);
        return !entity || !entity->map ||
               q1_map_move(g, entity, entity->map->pending.mover.pos1, Q1_MAP_BUTTON_BOTTOM, error);
    case Q1_MAP_BUTTON_BOTTOM:
        move->position = Q1_MAP_BOTTOM;
        return true;
    case Q1_MAP_SECRET_FIRST:
    case Q1_MAP_SECRET_LAST_WAIT:
        return q1_sound(g, entity->id, secret_sound(entity, false), 2, 1, error) &&
               (!q1_alive(g, entity->id) ||
                q1_map_schedule(g, entity, 1,
                                action == Q1_MAP_SECRET_FIRST ? Q1_MAP_SECRET_SECOND
                                                              : Q1_MAP_SECRET_LAST,
                                error));
    case Q1_MAP_SECRET_SECOND:
    case Q1_MAP_SECRET_RETURN:
    case Q1_MAP_SECRET_LAST: {
        if (action == Q1_MAP_SECRET_RETURN)
            move->position = Q1_MAP_DOWN;
        qa_vec3 destination = action == Q1_MAP_SECRET_SECOND   ? move->dest2
                              : action == Q1_MAP_SECRET_RETURN ? move->dest1
                                                               : move->pos1;
        q1_map_action done = action == Q1_MAP_SECRET_SECOND   ? Q1_MAP_SECRET_TOP
                             : action == Q1_MAP_SECRET_RETURN ? Q1_MAP_SECRET_LAST_WAIT
                                                              : Q1_MAP_SECRET_BOTTOM;
        return q1_sound(g, entity->id, secret_sound(entity, true), 2, 1, error) &&
               (!q1_alive(g, entity->id) || q1_map_move(g, entity, destination, done, error));
    }
    case Q1_MAP_SECRET_TOP:
        move->position = Q1_MAP_TOP;
        return q1_sound(g, entity->id, secret_sound(entity, false), 2, 1, error) &&
               (!q1_alive(g, entity->id) || (entity->spawnflags & 1) ||
                q1_map_schedule(g, entity, entity->wait, Q1_MAP_SECRET_RETURN, error));
    case Q1_MAP_SECRET_BOTTOM:
        move->position = Q1_MAP_BOTTOM;
        return q1_map_damageable(g, entity, secret_shootable(g, entity), error) &&
               qa_combat_set_health(g->services.combat, entity->id, 10000, error) &&
               (!q1_alive(g, entity->id) ||
                q1_sound(g, entity->id, secret_sound(entity, false), 2, 1, error));
    case Q1_MAP_PLAT_DOWN:
        return plat_move(g, entity, false, error);
    case Q1_MAP_PLAT_BOTTOM:
        move->position = Q1_MAP_BOTTOM;
        return q1_sound(g, entity->id, plat_sound(entity, false), 2, 1, error);
    case Q1_MAP_PLAT_TOP:
        move->position = Q1_MAP_TOP;
        return q1_sound(g, entity->id, plat_sound(entity, false), 2, 1, error) &&
               (!q1_alive(g, entity->id) || q1_map_schedule(g, entity, 3, Q1_MAP_PLAT_DOWN, error));
    case Q1_MAP_TRAIN_FIND:
    case Q1_MAP_TRAIN_NEXT:
    case Q1_MAP_TRAIN_WAIT:
        return q1_map_train_think(g, entity, action, error);
    default:
        return q1_map_fail(error, "unknown Q1 mover continuation");
    }
}
