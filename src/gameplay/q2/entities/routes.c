#include "internal.h"
#include "qa/game_q2_monsters.h"

bool qa_q2_entity_set_target(qa_q2_game *g, qa_actor_id id, qa_string_id name, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || (!a->entity && !a->item)) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 target owner is missing");
        return false;
    }
    if (a->item)
        a->item->spawn.target = name;
    else
        a->entity->target = name;
    return true;
}
bool qa_q2_entity_set_targetname(qa_q2_game *g, qa_actor_id id, qa_string_id name, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->entity) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 target name owner is missing");
        return false;
    }
    qa_string_id previous = a->entity->targetname;
    a->entity->targetname = name;
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (s->targets)
        qa_targets_changed(s->targets, id);
    return !s->target_name_changed || s->target_name_changed(s->context, id, previous, name, e);
}
static bool fields(qa_q2_game *g, qa_actor_id id, qa_string_id *name, qa_string_id *target,
                   uint32_t *flags) {
    q2_actor *a = q2_ent(g, id);
    if (a) {
        *name = a->entity->targetname;
        *target = a->entity->target;
        *flags = a->entity->spawnflags;
        return true;
    }
    qa_authored_target source;
    qa_targets *targets = g->entity_runtime->services.targets;
    if (targets && qa_targets_read(targets, id, &source)) {
        *name = source.targetname;
        *target = source.target;
        *flags = 0;
        return true;
    }
    return false;
}
static bool path_targets(qa_q2_game *g, q2_actor *a, qa_actor_id activator, qa_error *e) {
    q2_entity_state *s = a->entity;
    qa_string_id target = q2_field_id(s, g->field_keys[QA_TARGET_KEY_PATHTARGET]);
    if (!target)
        return true;
    qa_string_id saved = s->target;
    s->target = target;
    bool okay = q2_entity_targets(g, a, activator, false, e);
    if (q2_actor_live(g, a->id))
        s->target = saved;
    return okay;
}
static qa_actor_id external_activator(qa_q2_game *g, qa_actor_id actor,
                                      const qa_q2_path_follower *follower) {
    qa_actor_id ids[] = {follower->enemy, follower->old_enemy, follower->activator};
    if (g->services.actor_traits)
        for (size_t i = 0; i < 3; i++) {
            qa_builtin_actor_traits traits = {0};
            if (q2_actor_live(g, ids[i]) &&
                g->services.actor_traits(g->services.context, ids[i], &traits) && traits.player)
                return ids[i];
        }
    return actor;
}
bool q2_route_touch(qa_q2_game *g, q2_actor *a, qa_actor_id actor, qa_error *e) {
    bool combat = a->entity->kind == Q2E_COMBAT_POINT;
    q2_actor *monster = q2_actor_get(g, actor, false, NULL);
    qa_q2_entity_services *services = &g->entity_runtime->services;
    bool native = monster && monster->monster && !qa_targets_monster(services->targets, actor);
    qa_q2_path_follower follower = {0};
    bool eligible = false;
    if (native) {
        if (!qa_q2_monster_route_contact(g, actor, a->id, combat, &eligible) || !eligible)
            return true;
    } else {
        if (!services->path_follower ||
            !services->path_follower(services->context, actor, &follower) ||
            !qa_actor_id_equal(follower.move_target, a->id) || (!combat && follower.enemy.registry))
            return true;
        if (!services->path_advance) {
            qa_error_set(e, QA_ERROR_UNSUPPORTED, 0,
                         "Q2 route requires selected follower continuation");
            return false;
        }
    }
    q2_entity_state *s = a->entity;
    if (!combat && !path_targets(g, a, actor, e))
        return false;
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, actor))
        return true;
    qa_actor_id next = {0};
    qa_string_id next_name = 0, next_target = 0;
    uint32_t next_flags = 0;
    bool had_target = s->target != 0;
    if (s->target)
        q2_entity_pick(g, s->target, &next);
    if (next.registry)
        fields(g, next, &next_name, &next_target, &next_flags);
    if (!combat && next.registry && (next_flags & 1)) {
        qa_body_state b, to;
        if (!qa_world_body_read(g->services.world, actor, &b, e) ||
            !qa_world_body_read(g->services.world, next, &to, e))
            return false;
        b.origin = to.origin;
        b.origin.z += to.bounds.mins.z - b.bounds.mins.z;
        if (!qa_world_body_write(g->services.world, actor, &b, e) ||
            !qa_world_link(g->services.world, actor, NULL, e))
            return false;
        if (!q2_actor_live(g, a->id) || !q2_actor_live(g, actor))
            return true;
        if (!q2_projectile_event(g, actor, QA_BUILTIN_TELEPORT, g->runtime_names[Q2_NAME_RESOURCE_EMPTY], 7, b.origin, qa_v3(0, 0, 0), e))
            return false;
        next = (qa_actor_id){0};
        q2_entity_pick(g, next_target, &next);
        next_name = 0;
        if (next.registry)
            fields(g, next, &next_name, &next_target, &next_flags);
    }
    if (!q2_actor_live(g, a->id) || !q2_actor_live(g, actor))
        return true;
    bool accepted = false, finished = false;
    if (combat) {
        qa_string_id source_target = s->target;
        if (native) {
            q2_actor *authored = q2_ent(g, actor);
            if (had_target && authored)
                authored->entity->target = source_target;
            if (!qa_q2_monster_touch_combat_point(
                    g, actor,
                    &(qa_q2_monster_combat_point){.corner = a->id,
                                                  .next = next,
                                                  .has_target = had_target,
                                                  .hold_if_walking = (s->spawnflags & 1) != 0},
                    &accepted, &finished, e))
                return false;
            if (!accepted)
                return true;
            authored = q2_ent(g, actor);
            if (finished && authored)
                authored->entity->target = 0;
        } else {
            finished = !next.registry;
            qa_q2_path_advance change = {
                .goal = next,
                .move_target = next.registry ? next : a->id,
                .target = source_target,
                .set_target = had_target,
                .hold = !had_target && (s->spawnflags & 1) && follower.walking,
                .finish = finished,
                .pause_until_ns = (!had_target && (s->spawnflags & 1) && follower.walking)
                                      ? q2_deadline(g->now_ns, UINT64_C(100000000) * Q2_NS)
                                      : 0};
            if (!services->path_advance(services->context, actor, &change, e))
                return false;
        }
        if (!q2_actor_live(g, a->id) || !q2_actor_live(g, actor))
            return true;
        if (had_target)
            s->target = 0;
        qa_actor_id activator = actor;
        if (native)
            qa_q2_monster_path_activator(g, actor, &activator);
        else if (services->path_follower(services->context, actor, &follower))
            activator = external_activator(g, actor, &follower);
        return path_targets(g, a, activator, e);
    }
    uint64_t pause = s->wait != 0     ? q2_deadline(g->now_ns, q2_item_seconds(s->wait))
                     : !next.registry ? q2_deadline(g->now_ns, UINT64_C(100000000) * Q2_NS)
                                      : 0;
    if (native)
        return qa_q2_monster_touch_path_corner(
            g, actor,
            &(qa_q2_monster_path_corner){.corner = a->id, .next = next, .pause_until_ns = pause},
            &accepted, e);
    return services->path_advance(services->context, actor,
                                  &(qa_q2_path_advance){.goal = next,
                                                        .move_target = next,
                                                        .target = next_name,
                                                        .set_target = true,
                                                        .pause_until_ns = pause},
                                  e);
}
