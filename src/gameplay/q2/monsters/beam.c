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

static bool spawn_beam(q2m_context *context, qa_actor_id target,
                        qa_vec3 origin, qa_vec3 direction, float damage,
                        bool medic, q2m_controller_kind kind, int channel,
                        q2_actor **out, qa_error *error) {
  if (out) *out = NULL;
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
  beam->controller_kind = kind;
  beam->count = channel;
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
  if (out) *out = actor;
  return true;
}

bool q2m_spawn_monster_beam(q2m_context *context, qa_actor_id target,
                            qa_vec3 origin, qa_vec3 direction, float damage,
                            bool medic, qa_error *error) {
  return spawn_beam(context, target, origin, direction, damage, medic,
                     Q2M_CONTROLLER_BEAM, 0, NULL, error);
}

static bool guardian_trace(qa_q2_game *game, q2_actor *actor, bool damage,
                            qa_error *error) {
  struct qa_q2_monster *beam = actor->monster;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, actor->id, &body, error)) return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
  qa_trace_query query = {.start = body.origin,
      .end = qa_vec_add(body.origin, qa_vec_scale(beam->controller_direction, 2048.0f)),
      .pass_actor = actor->id, .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
  query.policy.contents_mask = UINT32_C(0x46000001);
  qa_actor_id excluded[16];
  size_t count = 0;
  qa_trace_result trace;
  for (;;) {
    if (!qa_world_trace_excluding(game->services.world, &query, excluded, count, &trace, error))
      return false;
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
    if (trace.hit == QA_TRACE_HIT_NONE || trace.fraction == 1.0f) break;
    qa_builtin_actor_traits traits = {0};
    if (trace.hit == QA_TRACE_HIT_ACTOR && game->services.actor_traits)
      game->services.actor_traits(game->services.context, trace.actor, &traits);
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
    if (damage && trace.hit == QA_TRACE_HIT_ACTOR && !traits.laser_immune &&
        !qa_actor_id_equal(trace.actor, beam->controller_owner)) {
      qa_combat_state combat;
      qa_error ignored = {0};
      bool can_damage = qa_combat_read(game->services.combat, trace.actor, &combat, &ignored) &&
                        combat.can_take_damage;
      if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
      if (can_damage && q2_actor_live(game, trace.actor)) {
        qa_attack attack = {.attacker = beam->controller_owner, .inflictor = actor->id,
            .combat_provider = game->options.owner,
            .cause = qa_q2_damage_cause(game->options.edition, game->options.product, 30, 4u)};
        if (!q2_damage(game, &attack, trace.actor, beam->controller_damage,
            (float)game->options.skill, beam->controller_direction, trace.end,
            qa_v3(0, 0, 0), false, error)) return false;
        if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
      }
    }
    if (!traits.monster && !traits.player) {
      if (damage && !spark_event(game, actor, &trace, error)) return false;
      break;
    }
    if (count == sizeof(excluded) / sizeof(excluded[0])) break;
    excluded[count++] = trace.actor;
  }
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
  qa_vec3 end = trace.end;
  if (trace.contact) end = qa_vec_add(end, trace.contact_plane.normal);
  return qa_world_link(game->services.world, actor->id, NULL, error) &&
      (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM) ||
       beam_event(game, actor, body.origin, end, error));
}

static bool guardian_update(qa_q2_game *game, q2_actor *actor, qa_error *error) {
  struct qa_q2_monster *beam = actor->monster;
  q2_actor *owner = q2_actor_get(game, beam->controller_owner, false, NULL);
  if (!owner || !owner->monster || !owner->monster->definition) return true;
  q2m_context context = {.game = game, .actor = owner, .monster = owner->monster};
  if (!q2m_refresh(&context, error)) return false;
  if (!q2m_alive(&context) ||
      !controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
  qa_actor_id enemy_id = context.monster->enemy;
  if (!q2_actor_live(game, enemy_id)) return true;
  qa_body_state enemy;
  qa_error body_error = {0};
  bool read = qa_world_body_read(game->services.world, enemy_id, &enemy, &body_error);
  if (!q2m_alive(&context) ||
      !controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM) ||
      !q2_actor_live(game, enemy_id) ||
      !qa_actor_id_equal(context.monster->enemy, enemy_id)) return true;
  if (!read) { if (error) *error = body_error; return false; }
  qa_vec3 offset = context.monster->frame & 1 ? qa_v3(125, -70, 60) : qa_v3(112, -62, 60);
  qa_vec3 origin = q2m_project_offset(&context, offset);
  qa_vec3 target = qa_vec_add(enemy.origin, enemy.bounds.mins);
  qa_vec3 size = qa_vec_sub(enemy.bounds.maxs, enemy.bounds.mins);
  float high = nextafterf(1.0f, 0.0f);
  target.x += fminf(high, q2_rerelease_float(game, 0, 1)) * size.x;
  target.y += fminf(high, q2_rerelease_float(game, 0, 1)) * size.y;
  target.z += fminf(high, q2_rerelease_float(game, 0, 1)) * size.z;
  qa_vec3 direction = qa_vec_normalize(qa_vec_sub(target, origin));
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, actor->id, &body, error)) return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
  body.origin = origin;
  body.angles = beam_angles(direction);
  beam->controller_direction = direction;
  beam->controller_target = context.monster->enemy;
  return qa_world_body_write(game->services.world, actor->id, &body, error) &&
      (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM) ||
       (qa_world_link(game->services.world, actor->id, NULL, error) &&
        (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM) ||
         guardian_trace(game, actor, false, error))));
}

bool q2m_guardian_beam(q2m_context *context, qa_error *error) {
  qa_q2_game *game = context->game;
  qa_actor_id enemy = context->monster->enemy;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy)) return true;
  int channel = context->monster->frame & 1;
  q2_actor *actor = NULL;
  bool fresh = false;
  for (q2_actor *candidate = game->first_actor; candidate; candidate = candidate->live_next)
    if (candidate->monster && candidate->monster->controller_kind == Q2M_CONTROLLER_GUARDIAN_BEAM &&
        candidate->monster->count == channel &&
        qa_actor_id_equal(candidate->monster->controller_owner, context->actor->id)) {
      actor = candidate;
      break;
    }
  if (!actor) {
    if (!spawn_beam(context, (qa_actor_id){0}, context->body.origin, qa_v3(1, 0, 0),
        25.0f, context->monster->medic, Q2M_CONTROLLER_GUARDIAN_BEAM, channel, &actor, error))
      return false;
    if (!actor || !q2m_alive(context)) return true;
    fresh = true;
    qa_string_id sound;
    if (!qa_builtin_resource(&game->services, "misc/lasfly.wav", &sound, error)) goto failed;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q2,
        .provider = game->options.owner, .actor = actor->id, .resource = sound,
        .time_ns = game->now_ns, .origin = context->body.origin,
        .volume = 1.0f, .attenuation = 1.0f, .flags = 1u};
    if (!qa_builtin_emit(&game->services, &event, error)) goto failed;
    if (!q2_actor_live(game, actor->id)) return true;
    if (!q2m_alive(context)) return qa_session_release(game->services.session, actor->id, error);
  }
  struct qa_q2_monster *beam = actor->monster;
  beam->controller_ns = q2_deadline(game->now_ns, 200 * Q2_MS);
  if (!guardian_update(game, actor, error)) goto failed;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_GUARDIAN_BEAM)) return true;
  if (fresh && !q2m_alive(context)) return qa_session_release(game->services.session, actor->id, error);
  if (!q2m_alive(context) || !q2_actor_live(game, enemy) ||
      !qa_actor_id_equal(context->monster->enemy, enemy)) return true;
  if (!guardian_trace(game, actor, true, error)) goto failed;
  return true;
failed:
  if (fresh && q2_actor_live(game, actor->id))
    qa_session_release(game->services.session, actor->id, NULL);
  return false;
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
  if (controller->controller_kind == Q2M_CONTROLLER_GUARDIAN_BEAM) {
    if (game->now_ns >= controller->controller_ns)
      return qa_session_release(game->services.session, actor->id, error);
    return true;
  }
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

bool q2m_controller_postthink(qa_q2_game *game, q2_actor *actor, qa_error *error) {
  return !actor->monster || actor->monster->controller_kind != Q2M_CONTROLLER_GUARDIAN_BEAM ||
      !q2_actor_live(game, actor->id) || guardian_update(game, actor, error);
}
