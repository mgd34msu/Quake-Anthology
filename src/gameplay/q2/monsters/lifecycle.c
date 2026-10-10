#include "internal.h"
#include "reinforcements.h"
#include "../entities/internal.h"
#include "qa/game_q2_items.h"
#include "../items/internal.h"

bool q2m_show(q2m_context *context, qa_error *error) {
    if (!q2m_alive(context))
        return true;
    struct qa_q2_monster *monster = context->monster;
    qa_entity_visual visual = {.models = {monster->model},
                           .frame = monster->frame,
                           .old_frame = monster->old_frame,
                           .skin = monster->skin,
                           .effects = context->actor->extra_effects,
                           .render_flags = monster->render_flags,
                           .scale = monster->entity_scale,
                           .alpha = context->actor->alpha,
                           .visible = monster->visible};
    return q2_publish_visual(context->game, context->actor->id, &visual, error);
}

bool q2m_health_target(q2m_context *context, qa_error *error) {
    q2_entity_state *entity = context->actor->entity;
    if (!entity)
        return true;
    qa_string_id health_target = q2_field_id(context->game, entity, "healthtarget");
    if (!health_target)
        return true;
    qa_string_id saved = entity->target;
    qa_actor_id actor = context->actor->id;
    entity->target = health_target;
    bool ok = q2_entity_targets(context->game, context->actor, context->monster->enemy, false, error);
    q2_actor *current = q2_actor_get(context->game, actor, false, NULL);
    if (current == context->actor && current->entity == entity)
        entity->target = saved;
    return ok;
}

static uint64_t interval(const qa_q2_game *game) {
    return game->options.edition == QA_Q2_CLASSIC ? Q2M_TENTH : game->frame_ns;
}

static bool counted(const q2m_context *context) {
    const struct qa_q2_monster *monster = context->monster;
    return !monster->good_guy && !monster->do_not_count &&
           (context->game->options.edition == QA_Q2_CLASSIC ||
            (monster->spawnflags & UINT32_C(65536)) == 0);
}

static bool traits(q2m_context *context, qa_actor_id actor, qa_builtin_actor_traits *out) {
    *out = (qa_builtin_actor_traits){0};
    return q2_actor_live(context->game, actor) && context->game->services.actor_traits &&
           context->game->services.actor_traits(context->game->services.context, actor, out);
}

bool q2m_lifecycle_admitted(q2m_context *context, bool automatic, bool reviving, qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    if (!monster->good_guy && (monster->spawnflags & 4u))
        monster->spawnflags = (monster->spawnflags & ~4u) | 1u;
    if (context->actor->entity) {
        context->actor->entity->spawnflags = monster->spawnflags;
        monster->combat_target = q2_field_id(context->game, context->actor->entity, "combattarget");
    }
    monster->start_phase = automatic ? Q2M_START_PENDING : Q2M_START_MANUAL;
    monster->start_due_ns = q2_deadline(context->game->now_ns, interval(context->game));
    if (!counted(context) || (reviving && context->game->options.edition == QA_Q2_RERELEASE))
        return true;
    qa_monster_mission mission;
    bool present;
    if (!q2m_mission(context, &mission, &present, error))
        return false;
    if (!q2m_alive(context))
        return true;
    return present ? mission.spawned(mission.context, context->actor->id, error)
                   : q2m_count(context, QA_Q2_MONSTER_COUNT_TOTAL, error);
}

static bool drop_to_floor(q2m_context *context, qa_error *error) {
    qa_trace_query query = {.start = context->body.origin,
                           .end = context->body.origin,
                           .shape = {.kind = QA_SHAPE_BOX, .bounds = context->body.bounds},
                           .pass_actor = context->actor->id,
                           .policy = qa_collision_default_policy(QA_GAME_Q2)};
    query.policy.contents_mask = qa_collision_contents_mask(Q2M_MONSTER_MASK |
        (context->game->options.edition == QA_Q2_RERELEASE ? Q2_PLAYER_CONTENTS : 0), QA_GAME_Q2);
    qa_trace_result hit;
    bool offset = context->game->options.edition == QA_Q2_CLASSIC;
    if (!offset) {
        if (!qa_world_trace(context->game->services.world, &query, &hit, error))
            return false;
        if (!q2m_alive(context))
            return true;
        offset = hit.start_solid;
    }
    float direction = context->actor->physics.gravity_direction.z > 0 ? 1.0f : -1.0f;
    if (offset)
        context->body.origin.z -= direction;
    if (!q2m_write_body(context, false, error))
        return false;
    if (!q2m_alive(context))
        return true;
    query.start = query.end = context->body.origin;
    query.end.z += direction * 256.0f;
    if (!qa_world_trace(context->game->services.world, &query, &hit, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (hit.fraction == 1.0f || hit.all_solid)
        return true;
    context->body.origin = hit.end;
    if (!q2m_write_body(context, true, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!qa_physics_check_ground(context->game->services.physics, context->actor->id, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!qa_physics_categorize_water(context->game->services.physics, context->actor->id, error))
        return false;
    if (!q2m_alive(context))
        return true;
    context->monster->water_level = context->actor->physics.water_level;
    context->monster->water_type = context->actor->physics.water_type;
    return q2m_refresh(context, error);
}

static bool set_route(q2m_context *context, qa_actor_id goal, bool spawn_dead, qa_error *error) {
    qa_body_state target;
    if (goal.registry) {
        if (!qa_world_body_read(context->game->services.world, goal, &target, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!q2m_refresh(context, error))
            return false;
        if (!q2m_alive(context))
            return true;
    }
    struct qa_q2_monster *monster = context->monster;
    monster->move_target = monster->goal = goal;
    context->actor->physics.goal = qa_actor_reference_lifetime(goal);
    if (goal.registry == 0) {
        monster->pause_ns = q2m_after(context->game->now_ns, 100000000.0);
        return spawn_dead || q2m_set_move(context, monster->definition->stand_move, false, error);
    }
    qa_vec3 angles = q2m_vector_angles(qa_vec_sub(target.origin, context->body.origin));
    monster->ideal_yaw = context->body.angles.y = angles.y;
    if (!q2m_write_body(context, false, error))
        return false;
    return !q2m_alive(context) || spawn_dead ||
           q2m_set_move(context, monster->definition->walk_move, false, error);
}

static bool authored_route(q2m_context *context, bool spawn_dead, qa_error *error) {
    q2_entity_state *entity = context->actor->entity;
    if (!entity || !entity->target)
        return set_route(context, (qa_actor_id){0}, spawn_dead, error);
    qa_targets *targets = context->game->entity_runtime->services.targets;
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    while (qa_targets_next(targets, entity->target, &cursor, &id)) {
        qa_authored_target target;
        if (!qa_targets_read(targets, id, &target))
            continue;
        const char *classname =
            qa_strings_cstr(qa_session_strings(context->game->services.session), target.classname);
        if (classname && !strcmp(classname, "point_combat")) {
            context->monster->combat_target = entity->target;
            entity->target = 0;
            return set_route(context, (qa_actor_id){0}, spawn_dead, error);
        }
    }
    if (!q2_entity_pick(context->game, entity->target, &id)) {
        entity->target = 0;
        return set_route(context, (qa_actor_id){0}, spawn_dead, error);
    }
    qa_authored_target target;
    const char *classname = qa_targets_read(targets, id, &target)
                                ? qa_strings_cstr(qa_session_strings(context->game->services.session),
                                                  target.classname)
                                : NULL;
    if (!classname || strcmp(classname, "path_corner"))
        return set_route(context, (qa_actor_id){0}, spawn_dead, error);
    entity->target = 0;
    return set_route(context, id, spawn_dead, error);
}

static bool dead_at_spawn(q2m_context *context, qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    qa_vec3 origin = context->body.origin;
    monster->death_notified = true;
    if (!qa_combat_set_health(context->game->services.combat, context->actor->id, 0, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!q2m_refresh(context, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!q2m_die(context, error))
        return false;
    if (!q2m_alive(context) || monster->gibbed)
        return true;
    const q2m_move *move = monster->next_move ? monster->next_move : monster->move;
    monster->move = move;
    monster->next_move = NULL;
    for (int frame = move->first_frame; frame < move->last_frame; frame++) {
        monster->frame = frame;
        const q2m_frame *entry = q2m_frame_at(monster, move, frame, error);
        if (!entry)
            return false;
        for (uint16_t i = 0; i < entry->action_count; i++) {
            const q2m_frame_action *action =
                &monster->move_set->actions[entry->action_first + i];
            if (action->callback) {
                if (!q2m_callback_call(context, action->callback, error))
                    return false;
            } else if (action->next_frame != INT_MIN) {
                monster->next_frame = action->next_frame == INT_MAX ? frame + 1 : action->next_frame;
            }
            if (!q2m_alive(context))
                return true;
        }
    }
    if (move->end && !q2m_callback_call(context, move->end, error))
        return false;
    if (!q2m_alive(context))
        return true;
    monster->frame = move->last_frame;
    if (!q2m_refresh(context, error))
        return false;
    if (!q2m_alive(context))
        return true;
    context->body.origin = origin;
    monster->start_phase = Q2M_START_ACTIVE;
    return q2m_write_body(context, true, error);
}

static bool start(q2m_context *context, qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool spawn_dead = rerelease && (monster->spawnflags & UINT32_C(65536));
    qa_monster_mission mission;
    bool present;
    if (!q2m_mission(context, &mission, &present, error)) return false;
    qa_monster_activation activation = {.active = true};
    if (present && mission.active && !mission.active(mission.context, context->actor->id, &activation, error)) return false;
    if (!q2m_alive(context)) return true;
    if (activation.active && !(monster->spawnflags & 2u) && monster->definition->locomotion == Q2M_WALK &&
        context->game->now_ns < Q2M_SECOND &&
        (!rerelease || !(monster->spawnflags & UINT32_C(262144)))) {
        if (!drop_to_floor(context, error))
            return false;
        if (!q2m_alive(context))
            return true;
    }
    if (context->combat.health <= 0)
        return true;
    if (!q2m_alive(context))
        return true;
    if (present) {
        qa_actor_id goal = {0};
        if (!mission.route(mission.context, context->actor->id, &goal, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!set_route(context, goal, spawn_dead, error))
            return false;
    } else if (!authored_route(context, spawn_dead, error)) {
        return false;
    }
    if (!q2m_alive(context))
        return true;
    if (spawn_dead)
        return dead_at_spawn(context, error);
    if (monster->spawnflags & 2u) {
        monster->start_phase = Q2M_START_DORMANT;
        monster->triggered = true;
        monster->visible = false;
        context->actor->physics.solid = QA_PHYSICS_NOT_SOLID;
        context->actor->physics.motion = QA_PHYSICS_STATIONARY;
        if (!q2m_damageable(context, false, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (!qa_world_set_collision(context->game->services.world, context->actor->id, NULL, error))
            return false;
        return q2m_link(context, error);
    }
    monster->start_phase = Q2M_START_ACTIVE;
    monster->start_due_ns = q2_deadline(context->game->now_ns, interval(context->game));
    return !present || mission.started(mission.context, context->actor->id, error);
}

static bool activate_body(q2m_context *context, qa_error *error) {
    context->monster->visible = true;
    context->monster->air_ns = q2m_after(context->game->now_ns, 12);
    context->actor->physics.solid = QA_PHYSICS_BOX;
    context->actor->physics.motion = QA_PHYSICS_STEP;
    qa_actor_collision collision = {.family = QA_GAME_Q2, .shape = QA_SHAPE_BOX,
        .contents = qa_collision_q2_source_contents(2, 4, context->game->options.edition == QA_Q2_RERELEASE),
        .role = QA_COLLISION_SOLID, .monster = true};
    return q2m_damageable(context, true, error) && (!q2m_alive(context) ||
        (qa_world_set_collision(context->game->services.world, context->actor->id, &collision, error) &&
        q2m_link(context, error)));
}

bool qa_q2_monster_activate(qa_q2_game *game, qa_actor_id actor, qa_error *error) {
    q2_actor *native = q2_actor_get(game, actor, false, NULL);
    if (!native || !native->monster) return false;
    q2m_context context = {.game = game, .actor = native, .monster = native->monster};
    return q2m_refresh(&context, error) && activate_body(&context, error);
}

static bool trigger_spawn(q2m_context *context, qa_error *error) {
    context->body.origin.z += 1;
    if (!q2m_write_body(context, false, error)) return false;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    if (rerelease && !activate_body(context, error)) return false;
    if (!q2m_alive(context)) return true;
    bool clear;
    if (!qa_q2_entities_killbox(context->game, context->actor->id, context->actor->id, &clear, error)) return false;
    if (!q2m_alive(context)) return true;
    struct qa_q2_monster *monster = context->monster;
    monster->spawnflags &= ~2u;
    monster->triggered = false;
    if (context->actor->entity)
        context->actor->entity->spawnflags = monster->spawnflags;
    if ((!rerelease && !activate_body(context, error)) || (q2m_alive(context) && !start(context, error))) return false;
    if (!q2m_alive(context))
        return true;
    qa_builtin_actor_traits target;
    bool acquire = monster->enemy.registry != 0 && !(monster->spawnflags & 1u) &&
                   traits(context, monster->enemy, &target) && !target.no_target;
    if (!q2m_alive(context))
        return true;
    if (acquire) {
        if (!q2m_found_target(context, monster->enemy, error))
            return false;
    } else {
        monster->enemy = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.enemy = (qa_actor_reference){0});
    }
    return !q2m_alive(context) || q2m_link(context, error);
}

static bool mission_turn(q2m_context *context, bool *active, qa_error *error) {
    qa_monster_mission mission;
    bool present;
    *active = true;
    if (!q2m_mission(context, &mission, &present, error)) return false;
    struct qa_q2_monster *monster = context->monster;
    if (!present || !mission.active || monster->start_phase != Q2M_START_ACTIVE || monster->dead) return true;
    qa_monster_activation activation;
    if (!mission.active(mission.context, context->actor->id, &activation, error)) return false;
    if (!q2m_alive(context)) { *active = false; return true; }
    if (!q2m_refresh(context, error)) return false;
    *active = activation.active;
    if (!*active) {
        monster->visible = false;
        context->actor->physics.solid = QA_PHYSICS_NOT_SOLID;
        context->actor->physics.motion = QA_PHYSICS_STATIONARY;
        return q2m_damageable(context, false, error) &&
            qa_world_set_collision(context->game->services.world, context->actor->id, NULL, error) &&
            q2m_link(context, error);
    }
    if (!monster->visible) {
        if (!activate_body(context, error) || (q2m_alive(context) && !q2m_refresh(context, error))) return false;
    }
    return !activation.activator.registry || q2m_lifecycle_use(context, activation.activator, error);
}
bool q2m_lifecycle_tick(q2m_context *context, bool *handled, qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    *handled = true;
    if (monster->start_phase == Q2M_START_DORMANT || monster->start_phase == Q2M_START_MANUAL ||
        context->game->now_ns < monster->start_due_ns)
        return true;
    bool active;
    if (monster->start_phase == Q2M_START_PENDING || monster->start_phase == Q2M_START_TRIGGER) {
        bool ok = monster->start_phase == Q2M_START_PENDING ? start(context, error)
                                                            : trigger_spawn(context, error);
        return ok && (!q2m_alive(context) || (mission_turn(context, &active, error) && q2m_show(context, error)));
    }
    if (!mission_turn(context, &active, error)) return false;
    if (!active) return !q2m_alive(context) || q2m_show(context, error);
    *handled = false;
    return true;
}

bool q2m_lifecycle_use(q2m_context *context, qa_actor_id activator, qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    qa_builtin_actor_traits target;
    if (monster->start_phase == Q2M_START_DORMANT) {
        monster->start_phase = Q2M_START_TRIGGER;
        monster->start_due_ns = q2_deadline(context->game->now_ns, interval(context->game));
        bool player = traits(context, activator, &target) && target.player;
        if (!q2m_alive(context))
            return true;
        if (player)
            monster->enemy = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.enemy = qa_actor_reference_lifetime(activator));
        return true;
    }
    qa_monster_mission mission;
    bool present, handled = false;
    if (!q2m_mission(context, &mission, &present, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (present && !mission.use(mission.context, context->actor->id, activator, &handled, error))
        return false;
    if (!q2m_alive(context) || handled || monster->enemy.registry || monster->dead)
        return true;
    bool eligible = traits(context, activator, &target) && !target.no_target;
    if (!q2m_alive(context) || !eligible)
        return true;
    q2_actor *native = q2_actor_get(context->game, activator, false, NULL);
    if (!target.player && !(native && native->monster && native->monster->good_guy))
        return true;
    return q2m_found_target(context, activator, error);
}

bool q2m_lifecycle_killed(q2m_context *context, qa_error *error) {
    struct qa_q2_monster *monster = context->monster;
    if (monster->dead || monster->death_notified)
        return true;
    q2_actor *commander = q2_actor_get(context->game, monster->commander, false, NULL);
    if (commander && commander->monster && commander->monster->definition) {
        struct qa_q2_monster *leader = commander->monster;
        q2m_species species = leader->definition->species;
        if (monster->spawned_by == Q2M_SPAWN_CARRIER && species == Q2M_CARRIER) {
            if (!q2m_summon_add(&leader->monster_slots, 1, error))
                return false;
        } else if (monster->spawned_by == Q2M_SPAWN_MEDIC && species == Q2M_MEDIC_COMMANDER) {
            bool ok = context->game->options.edition == QA_Q2_RERELEASE
                ? q2m_summon_subtract(&leader->monster_used, monster->monster_slots, error)
                : q2m_summon_add(&leader->monster_slots, 1, error);
            if (!ok)
                return false;
        } else if (monster->spawned_by == Q2M_SPAWN_WIDOW &&
                   (species == Q2M_WIDOW || species == Q2M_WIDOW2) && leader->monster_used > 0) {
            if (!q2m_summon_subtract(&leader->monster_used, 1, error))
                return false;
        }
    }
    monster->death_notified = true;
    qa_monster_mission mission;
    bool present;
    if (!q2m_mission(context, &mission, &present, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!present && counted(context) && !q2m_count(context, QA_Q2_MONSTER_COUNT_KILLED, error))
        return false;
    if (!q2m_alive(context))
        return true;
    qa_actor_id attacker = monster->last_attack.attacker;
    monster->enemy = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.enemy = qa_actor_reference_lifetime(attacker));
    qa_physics_motion motion = context->actor->physics.motion;
    if (context->game->options.edition == QA_Q2_CLASSIC &&
        (motion == QA_PHYSICS_PUSH || motion == QA_PHYSICS_STOP || motion == QA_PHYSICS_STATIONARY))
        return true;
    monster->touch_active = false;
    context->actor->physics.flags &= ~(uint32_t)(QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING);
    if (!present && context->actor->entity) {
        const char *drop = q2_field_text(context->game, context->actor->entity, "item");
        if (*drop) {
            qa_actor_id item;
            bool dropped;
            if (!qa_q2_item_drop_monster(context->game, context->actor->id, drop, &item, &dropped, error))
                return false;
            if (!q2m_alive(context))
                return true;
            if (dropped && context->game->options.edition == QA_Q2_RERELEASE) {
                qa_string_id item_target = q2_field_id(context->game, context->actor->entity, "itemtarget");
                if (item_target && !qa_q2_entity_set_target(context->game, item, item_target, error)) return false;
            }
        }
    }
    if (present)
        return mission.killed(mission.context, context->actor->id, attacker, error);
    q2_entity_state *entity = context->actor->entity;
    if (!entity)
        return true;
    qa_string_id death_target = q2_field_id(context->game, entity, "deathtarget");
    if (death_target)
        entity->target = death_target;
    if (entity->target && !q2_entity_targets(context->game, context->actor, attacker, false, error)) return false;
    if (!q2m_alive(context) || context->game->options.edition != QA_Q2_RERELEASE) return true;
    entity = context->actor->entity;
    qa_string_id health_target = q2_field_id(context->game, entity, "healthtarget");
    if (!health_target) return true;
    entity->target = health_target;
    return q2_entity_targets(context->game, context->actor, attacker, false, error);
}

bool q2m_lifecycle_route(q2m_context *context, bool found_target, bool *routed, qa_error *error) {
    *routed = false;
    qa_monster_mission mission;
    bool present;
    if (!q2m_mission(context, &mission, &present, error))
        return false;
    if (!q2m_alive(context))
        return true;
    struct qa_q2_monster *monster = context->monster;
    if (present) {
        if (found_target && !mission.found_target(mission.context, context->actor->id, error))
            return false;
        if (!q2m_alive(context))
            return true;
        qa_monster_combat_route route = {0};
        if (!mission.combat_route(mission.context, context->actor->id, &route, error))
            return false;
        if (!q2m_alive(context))
            return true;
        if (route.stand_ground)
            monster->stand_ground = true;
        if (route.goal.registry) {
            monster->combat_point = true;
            monster->goal = monster->move_target = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = qa_actor_reference_lifetime(route.goal));
            monster->pause_ns = 0;
            *routed = true;
            return !found_target || q2m_set_move(context, monster->definition->run_move, false, error);
        }
        if (!found_target && monster->combat_point) {
            monster->combat_point = false;
            monster->move_target = (qa_actor_id){0};
            monster->goal = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = qa_actor_reference_lifetime(monster->enemy));
        }
    }
    if (!found_target)
        return true;
    if (monster->combat_point) {
        *routed = true;
        return true;
    }
    qa_actor_id target;
    if (!monster->combat_target || !q2_entity_pick(context->game, monster->combat_target, &target))
        return true;
    monster->combat_target = 0;
    monster->combat_point = true;
    monster->move_target = monster->goal = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = qa_actor_reference_lifetime(target));
    monster->pause_ns = 0;
    if (context->game->options.edition == QA_Q2_CLASSIC) {
        if (!qa_targets_set_targetname(context->game->entity_runtime->services.targets,
                                       target, 0, error))
            return false;
        if (!q2m_alive(context))
            return true;
    }
    *routed = true;
    return q2m_set_move(context, monster->definition->run_move, false, error);
}
