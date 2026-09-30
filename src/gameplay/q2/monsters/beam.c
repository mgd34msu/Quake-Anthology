#include "internal.h"

static qa_vec3 beam_angles(qa_vec3 direction) {
  return qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) *
                   57.29577951308232f,
               atan2f(direction.y, direction.x) * 57.29577951308232f, 0.0f);
}

static bool controller_live(qa_q2_game *game, q2_actor *actor,
                            const struct qa_q2_monster *expected,
                            q2m_controller_kind kind) {
  return actor != NULL && actor->monster == expected && expected != NULL &&
         actor->monster->controller_kind == kind &&
         q2_actor_live(game, actor->id);
}

static bool beam_event(qa_q2_game *game, q2_actor *actor, qa_vec3 start,
                       qa_vec3 end, qa_error *error) {
  qa_builtin_event event = {
      .kind = QA_BUILTIN_BEAM,
      .family = QA_GAME_Q2,
      .provider = game->options.owner,
      .actor = actor->id,
      .other = actor->monster->controller_target,
      .time_ns = game->now_ns,
      .origin = start,
      .end = end,
      .direction = qa_vec_normalize(qa_vec_sub(end, start)),
      .value = 2.0f,
      .code = (int32_t)(actor->monster->controller_medic ? UINT32_C(0xf3f3f1f1)
                                                         : UINT32_C(0xf2f2f0f0)),
  };
  return qa_builtin_emit(&game->services, &event, error);
}

static bool spark_event(qa_q2_game *game, q2_actor *actor,
                        const qa_trace_result *trace, qa_error *error) {
  qa_builtin_event event = {
      .kind = QA_BUILTIN_IMPACT,
      .family = QA_GAME_Q2,
      .provider = game->options.owner,
      .actor = actor->id,
      .time_ns = game->now_ns,
      .origin = trace->end,
      .direction = trace->contact ? trace->contact_plane.normal
                                  : qa_v3(0.0f, 0.0f, 0.0f),
      .count = 10,
      .code = actor->monster->controller_medic ? 0xf1 : 0xf0,
  };
  if (!qa_builtin_resource(&game->services, "q2:laser-sparks",
                           &event.resource, error))
    return false;
  return qa_builtin_emit(&game->services, &event, error);
}

static bool beam_damage(qa_q2_game *game, q2_actor *actor, qa_actor_id target,
                        qa_vec3 point, qa_vec3 normal, qa_error *error) {
  struct qa_q2_monster *beam = actor->monster;
  qa_combat_state combat;
  qa_error ignored = {0};
  if (!qa_combat_read(game->services.combat, target, &combat, &ignored))
    return true;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
      !q2_actor_live(game, target) || !combat.can_take_damage ||
      qa_actor_id_equal(target, beam->controller_owner))
    return true;
  qa_builtin_actor_traits traits = {0};
  if (game->services.actor_traits != NULL)
    game->services.actor_traits(game->services.context, target, &traits);
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  if (traits.laser_immune || traits.no_source_friendly_fire)
    return true;

  qa_attack attack = {
      .attacker = beam->controller_owner,
      .inflictor = actor->id,
      .combat_provider = game->options.owner,
      .cause = qa_q2_damage_cause(game->options.edition, game->options.product,
                                  30, 4u),
  };
  if (!q2_damage(game, &attack, target, beam->controller_damage,
                 (float)game->options.skill, beam->controller_direction, point,
                 normal, false, error))
    return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
      beam->controller_damage >= 0.0f ||
      !traits.player)
    return true;
  if (!qa_combat_read(game->services.combat, target, &combat, &ignored) ||
      combat.health <= 100.0f)
    return true;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
      !q2_actor_live(game, target))
    return true;
  return qa_combat_set_health(game->services.combat, target,
                              combat.health + beam->controller_damage, error);
}

static bool beam_fire(qa_q2_game *game, q2_actor *actor, qa_error *error) {
  struct qa_q2_monster *beam = actor->monster;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, actor->id, &body, error))
    return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  qa_vec3 start = body.origin;
  qa_vec3 end = qa_vec_add(start, qa_vec_scale(beam->controller_direction,
                                               2048.0f));
  qa_vec3 endpoint = end;
  qa_actor_id ignored_actor = actor->id;
  for (size_t hit_count = 0; hit_count <= game->capacity; ++hit_count) {
    qa_trace_query query = {
        .start = start,
        .end = end,
        .pass_actor = ignored_actor,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    query.policy.contents_mask = UINT32_C(0x06000001);
    qa_trace_result trace;
    if (!qa_world_trace(game->services.world, &query, &trace, error))
      return false;
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
    endpoint = trace.end;
    if (trace.hit != QA_TRACE_HIT_ACTOR) {
      if (trace.hit != QA_TRACE_HIT_NONE &&
          !spark_event(game, actor, &trace, error))
        return false;
      break;
    }

    qa_builtin_actor_traits traits = {0};
    if (game->services.actor_traits != NULL)
      game->services.actor_traits(game->services.context, trace.actor, &traits);
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
    if (!beam_damage(game, actor, trace.actor, trace.end,
                     trace.contact ? trace.contact_plane.normal
                                   : qa_v3(0.0f, 0.0f, 0.0f),
                     error))
      return false;
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
    if (!traits.monster && !traits.player) {
      if (!spark_event(game, actor, &trace, error))
        return false;
      break;
    }
    ignored_actor = trace.actor;
    start = trace.end;
  }
  return !controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
         beam_event(game, actor, body.origin, endpoint, error);
}

bool q2m_spawn_monster_beam(q2m_context *context, qa_actor_id target,
                            qa_vec3 origin, qa_vec3 direction, float damage,
                            bool medic, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  qa_actor_definition definition;
  if (!qa_builtin_resource(&context->game->services, "dabeam", &definition,
                           error))
    return false;

  qa_vec3 aim = direction;
  qa_body_state target_body;
  qa_error ignored = {0};
  if (target.registry != 0 &&
      qa_world_body_read(context->game->services.world, target, &target_body,
                         &ignored)) {
    qa_vec3 center = qa_vec_add(
        target_body.origin,
        qa_vec_scale(qa_vec_add(target_body.bounds.mins,
                                target_body.bounds.maxs),
                     0.5f));
    if (medic)
      center.x +=
          (float)(sin((double)context->game->now_ns / 1e9) * 8.0);
    aim = qa_vec_normalize(qa_vec_sub(center, origin));
  }
  if (!q2m_alive(context))
    return true;
  qa_body_state body = {
      .origin = origin,
      .angles = beam_angles(aim),
      .bounds = {.mins = {-8.0f, -8.0f, -8.0f},
                 .maxs = {8.0f, 8.0f, 8.0f}},
  };
  qa_builtin_spawn spawn = {
      .owner = context->game->options.owner,
      .definition = definition,
      .body = body,
      .link = false,
  };
  qa_actor_id id;
  if (!qa_builtin_spawn_actor(&context->game->services, &spawn, &id, error))
    return false;
  if (!q2m_alive(context))
    return !q2_actor_live(context->game, id) ||
           qa_session_release(context->game->services.session, id, error);
  q2_actor *actor = q2_actor_get(context->game, id, true, error);
  if (actor == NULL) {
    qa_session_release(context->game->services.session, id, NULL);
    return false;
  }
  struct qa_q2_monster *beam = calloc(1, sizeof(*beam));
  if (beam == NULL) {
    qa_session_release(context->game->services.session, id, NULL);
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Unable to allocate Q2 monster beam state");
    return false;
  }
  beam->controller_kind = Q2M_CONTROLLER_BEAM;
  beam->controller_owner = context->actor->id;
  beam->controller_target = target;
  beam->controller_direction = aim;
  beam->controller_damage = damage;
  beam->controller_ns = q2m_after(context->game->now_ns, 0.1);
  beam->controller_medic = medic;
  beam->initialized = true;
  actor->monster = beam;
  actor->physics_bound = true;
  actor->physics = qa_physics_properties_default(QA_COLLISION_Q2);
  actor->physics.motion = QA_PHYSICS_STATIONARY;
  actor->physics.solid = QA_PHYSICS_NOT_SOLID;
  actor->physics.clip_mask = 0;
  return true;
}

bool q2m_spawn_boss_exploder(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  qa_actor_definition definition;
  if (!qa_builtin_resource(&context->game->services, "boss_exploder",
                           &definition, error))
    return false;
  qa_builtin_spawn spawn = {
      .owner = context->game->options.owner,
      .definition = definition,
      .body = {.origin = context->body.origin},
      .link = false,
  };
  qa_actor_id id;
  if (!qa_builtin_spawn_actor(&context->game->services, &spawn, &id, error))
    return false;
  if (!q2m_alive(context))
    return !q2_actor_live(context->game, id) ||
           qa_session_release(context->game->services.session, id, error);
  q2_actor *actor = q2_actor_get(context->game, id, true, error);
  if (actor == NULL) {
    qa_session_release(context->game->services.session, id, NULL);
    return false;
  }
  struct qa_q2_monster *controller = calloc(1, sizeof(*controller));
  if (controller == NULL) {
    qa_session_release(context->game->services.session, id, NULL);
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Unable to allocate Q2 boss explosion controller");
    return false;
  }
  controller->controller_kind = Q2M_CONTROLLER_BOSS_EXPLODER;
  controller->controller_owner = context->actor->id;
  controller->controller_ns =
      q2m_after(context->game->now_ns,
                0.075 + q2m_random(context->game) * 0.175);
  controller->initialized = true;
  actor->monster = controller;
  actor->physics_bound = true;
  actor->physics = qa_physics_properties_default(QA_COLLISION_Q2);
  actor->physics.motion = QA_PHYSICS_STATIONARY;
  actor->physics.solid = QA_PHYSICS_NOT_SOLID;
  actor->physics.clip_mask = 0;
  return true;
}

bool q2m_schedule_makron_spawn(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  qa_actor_definition definition;
  if (!qa_builtin_resource(&context->game->services, "monster_makron",
                           &definition, error))
    return false;
  qa_builtin_spawn spawn = {
      .owner = context->game->options.owner,
      .definition = definition,
      .body = {.origin = context->body.origin, .angles = context->body.angles},
      .link = false,
  };
  qa_actor_id id;
  if (!qa_builtin_spawn_actor(&context->game->services, &spawn, &id, error))
    return false;
  if (!q2m_alive(context))
    return !q2_actor_live(context->game, id) ||
           qa_session_release(context->game->services.session, id, error);
  q2_actor *actor = q2_actor_get(context->game, id, true, error);
  if (actor == NULL) {
    qa_session_release(context->game->services.session, id, NULL);
    return false;
  }
  struct qa_q2_monster *controller = calloc(1, sizeof(*controller));
  if (controller == NULL) {
    qa_session_release(context->game->services.session, id, NULL);
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Unable to allocate Q2 Makron spawn controller");
    return false;
  }
  controller->controller_kind = Q2M_CONTROLLER_MAKRON_SPAWN;
  controller->controller_owner = context->actor->id;
  controller->controller_ns = q2m_after(context->game->now_ns, 0.8);
  controller->initialized = true;
  actor->monster = controller;
  actor->physics_bound = true;
  actor->physics = qa_physics_properties_default(QA_COLLISION_Q2);
  actor->physics.motion = QA_PHYSICS_STATIONARY;
  actor->physics.solid = QA_PHYSICS_NOT_SOLID;
  actor->physics.clip_mask = 0;
  return true;
}

static bool boss_exploder_tick(qa_q2_game *game, q2_actor *actor,
                               qa_error *error) {
  struct qa_q2_monster *controller = actor->monster;
  if (game->now_ns < controller->controller_ns)
    return true;
  qa_actor_id owner_id = controller->controller_owner;
  if (!q2_actor_live(game, owner_id) || owner_id.slot >= game->capacity) {
    return qa_session_release(game->services.session, actor->id, error);
  }
  q2_actor *owner = game->actors[owner_id.slot];
  if (owner == NULL || !qa_actor_id_equal(owner->id, owner_id) ||
      owner->monster == NULL || owner->projectile.kind != Q2_PROJECTILE_NONE ||
      owner->monster->definition == NULL || owner->monster->gibbed) {
    return qa_session_release(game->services.session, actor->id, error);
  }
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, owner_id, &body, error))
    return false;
  if (!controller_live(game, actor, controller, Q2M_CONTROLLER_BOSS_EXPLODER) ||
      !q2_actor_live(game, owner_id) || !owner->monster ||
      !owner->monster->definition || owner->projectile.kind != Q2_PROJECTILE_NONE ||
      owner->monster->gibbed)
    return true;
  qa_vec3 span = qa_vec_sub(body.bounds.maxs, body.bounds.mins);
  qa_vec3 origin = qa_vec_add(
      qa_vec_add(body.origin, body.bounds.mins),
      qa_v3(q2_random(game) * span.x, q2_random(game) * span.y,
            q2_random(game) * span.z));
  qa_builtin_event event = {
      .kind = QA_BUILTIN_EXPLOSION,
      .family = QA_GAME_Q2,
      .provider = game->options.owner,
      .actor = actor->id,
      .other = owner_id,
      .time_ns = game->now_ns,
      .origin = origin,
      .end = origin,
      .value = 1.0f,
      .code = controller->count,
  };
  const char *effect = controller->count % 3 == 0 ? "q2:explosion1"
                                                   : "q2:explosion1-nl";
  if (!qa_builtin_resource(&game->services, effect, &event.resource, error) ||
      !qa_builtin_emit(&game->services, &event, error))
    return false;
  if (!controller_live(game, actor, controller, Q2M_CONTROLLER_BOSS_EXPLODER))
    return true;
  ++controller->count;
  controller->controller_ns =
      q2m_after(game->now_ns, 0.05 + q2_random(game) * 0.15);
  return true;
}

static bool makron_spawn_tick(qa_q2_game *game, q2_actor *actor,
                              qa_error *error) {
  struct qa_q2_monster *controller = actor->monster;
  if (game->now_ns < controller->controller_ns)
    return true;

  qa_actor_id child = actor->id;
  actor->monster = NULL;
  actor->physics_bound = false;
  free(controller);
  qa_q2_monster_spawn_options options = {
      .classname = "monster_makron",
      .scale = 1.0f,
      .health_multiplier = 1.0f,
  };
  if (!qa_q2_monster_spawn(game, child, &options, error)) {
    qa_error original = error != NULL ? *error : (qa_error){0};
    qa_error ignored = {0};
    if (q2_actor_live(game, child))
      qa_session_release(game->services.session, child, &ignored);
    if (error != NULL)
      *error = original;
    return false;
  }
  if (!q2_actor_live(game, child) || child.slot >= game->capacity)
    return true;
  q2_actor *spawned = game->actors[child.slot];
  if (spawned == NULL || !qa_actor_id_equal(spawned->id, child) ||
      spawned->monster == NULL)
    return true;
  q2m_context resumed = {
      .game = game, .actor = spawned, .monster = spawned->monster};
  if (!q2m_refresh(&resumed, error))
    return false;
  qa_actor_id player = q2m_current_sight_client(game);
  qa_body_state target;
  qa_error ignored = {0};
  if (player.registry == 0 ||
      !qa_world_body_read(game->services.world, player, &target, &ignored))
    return true;
  qa_vec3 difference = qa_vec_sub(target.origin, resumed.body.origin);
  qa_vec3 direction = qa_vec_normalize(difference);
  resumed.body.angles.y = qa_builtin_angle_mod(
      atan2f(difference.y, difference.x) * 57.29577951308232f);
  resumed.body.velocity = qa_vec_scale(direction, 400.0f);
  resumed.body.velocity.z = 200.0f;
  resumed.body.ground = (qa_actor_id){0};
  return q2m_write_body(&resumed, true, error);
}

bool q2m_controller_tick(qa_q2_game *game, q2_actor *actor,
                         qa_error *error) {
  if (actor == NULL || actor->monster == NULL || !q2_actor_live(game, actor->id))
    return true;
  struct qa_q2_monster *controller = actor->monster;
  if (controller->controller_kind == Q2M_CONTROLLER_BOSS_EXPLODER)
    return boss_exploder_tick(game, actor, error);
  if (controller->controller_kind == Q2M_CONTROLLER_MAKRON_SPAWN)
    return makron_spawn_tick(game, actor, error);
  if (!controller_live(game, actor, controller, Q2M_CONTROLLER_BEAM))
    return true;
  if (game->now_ns < controller->controller_ns)
    return true;
  if (!controller->controller_fired) {
    controller->controller_fired = true;
    controller->controller_ns = q2m_after(game->now_ns, 0.1);
    return beam_fire(game, actor, error);
  }
  return qa_session_release(game->services.session, actor->id, error);
}
