#include "internal.h"
#include "../entities/internal.h"
#include "qa/game_q2_wire.h"

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
      .flags = 1u,
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
      !q2_actor_live(game, target))
    return true;
  qa_builtin_actor_traits traits = {0};
  if (game->services.actor_traits != NULL)
    game->services.actor_traits(game->services.context, target, &traits);
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  if (beam->controller_damage < 0 && game->options.edition == QA_Q2_RERELEASE) {
    if (combat.health >= traits.max_health)
      return true;
    return qa_combat_set_health(game->services.combat, target,
        fminf(traits.max_health, combat.health - beam->controller_damage), error);
  }
  if (!combat.can_take_damage || qa_actor_id_equal(target, beam->controller_owner))
    return true;
  if (traits.laser_immune)
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

static bool beam_fire(qa_q2_game *game, q2_actor *actor, bool damage, qa_error *error) {
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
    if (game->options.edition == QA_Q2_RERELEASE)
      query.policy.contents_mask |= UINT32_C(0x40000000);
    qa_trace_result trace;
    if (!qa_world_trace(game->services.world, &query, &trace, error))
      return false;
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
    endpoint = trace.end;
    if (game->options.edition == QA_Q2_RERELEASE && trace.contact)
      endpoint = qa_vec_add(endpoint, trace.contact_plane.normal);
    if (trace.hit != QA_TRACE_HIT_ACTOR) {
      if (damage && trace.hit != QA_TRACE_HIT_NONE &&
          !spark_event(game, actor, &trace, error))
        return false;
      break;
    }

    qa_builtin_actor_traits traits = {0};
    if (game->services.actor_traits != NULL)
      game->services.actor_traits(game->services.context, trace.actor, &traits);
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
    if (damage && !beam_damage(game, actor, trace.actor, trace.end,
                     trace.contact ? trace.contact_plane.normal
                                   : qa_v3(0.0f, 0.0f, 0.0f),
                     error))
      return false;
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
    if (!traits.monster && !traits.player) {
      if (damage && !spark_event(game, actor, &trace, error))
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

static bool source_beam_pose(q2m_context *context, int channel, int flash,
                             qa_vec3 *origin, qa_vec3 *direction,
                             bool *available, qa_error *error) {
  *available = false;
  qa_actor_id target = context->monster->enemy;
  qa_body_state enemy;
  if (!q2_actor_live(context->game, target) ||
      !qa_world_body_read(context->game->services.world, target, &enemy, error))
    return !q2_actor_live(context->game, target);
  if (!q2m_alive(context))
    return true;
  q2m_species species = context->monster->definition->species;
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  if (species == Q2M_FIXBOT) {
    if (context->game->options.edition == QA_Q2_CLASSIC) {
      *direction = qa_vec_normalize(qa_vec_sub(enemy.origin, context->body.origin));
      *origin = qa_vec_add(context->body.origin, qa_vec_scale(*direction, 16));
      qa_vec3 point = qa_vec_add(enemy.origin,
          qa_vec_scale(qa_vec_add(enemy.bounds.mins, enemy.bounds.maxs), .5f));
      if (context->monster->medic)
        point.x += sinf((float)((double)context->game->now_ns / 1e9)) * 8;
      *direction = qa_vec_normalize(qa_vec_sub(point, *origin));
    } else {
      *origin = qa_vec_add(context->body.origin, qa_vec_scale(forward, 16));
      qa_vec3 point = qa_vec_add(enemy.origin,
          qa_vec_scale(qa_vec_add(enemy.bounds.mins, enemy.bounds.maxs), .5f));
      if (context->monster->medic)
        point.x += sinf((float)((double)context->game->now_ns / 1e9)) * 8;
      *direction = qa_vec_normalize(qa_vec_sub(point, context->body.origin));
    }
    *available = true;
    return true;
  }
  if (species == Q2M_BRAIN) {
    if (context->game->options.edition == QA_Q2_CLASSIC)
      qa_builtin_angle_vectors(q2m_vector_angles(qa_vec_sub(enemy.origin,
          context->body.origin)), &forward, &right, &up);
    static const qa_vec3 eyes[2][11] = {
      {{.746700f,.238370f,34.167690f},{-1.076390f,.238370f,33.386372f},
       {-1.335500f,5.334300f,32.177170f},{-.175360f,8.846370f,30.635479f},
       {-2.757590f,7.804610f,30.150860f},{-5.575090f,5.152840f,30.056160f},
       {-7.017550f,3.262470f,30.552521f},{-7.915740f,.638800f,33.176189f},
       {-3.915390f,8.285730f,33.976349f},{-.913540f,10.933030f,34.141811f},
       {-.369900f,8.923900f,34.189079f}},
      {{-3.364710f,.327750f,33.938381f},{-5.140450f,.493480f,32.659851f},
       {-5.341980f,5.646980f,31.277901f},{-4.134480f,9.277440f,29.925621f},
       {-6.598340f,6.815090f,29.322620f},{-8.610840f,2.529650f,29.251591f},
       {-9.231360f,.093280f,29.747959f},{-11.004110f,1.936930f,32.395260f},
       {-7.878310f,7.648190f,33.148151f},{-4.947370f,11.430050f,33.313610f},
       {-4.332820f,9.444570f,33.526340f}}};
    unsigned frame = (unsigned)context->monster->frame;
    if (frame >= 11 || (unsigned)channel >= 2)
      return true;
    qa_vec3 eye = eyes[channel][frame];
    *origin = qa_vec_add(context->body.origin,
        qa_vec_add(qa_vec_scale(right, eye.x),
            qa_vec_add(qa_vec_scale(forward, eye.y), qa_vec_scale(up, eye.z))));
    if (context->game->options.edition == QA_Q2_CLASSIC) {
      qa_vec3 point = qa_vec_add(enemy.origin,
          qa_vec_scale(qa_vec_add(enemy.bounds.mins, enemy.bounds.maxs), .5f));
      *direction = qa_vec_normalize(qa_vec_sub(point, *origin));
      *available = true;
      return true;
    }
  } else if (species == Q2M_SOLDIER_LASER) {
    qa_vec3 offset;
    if (!q2m_muzzle_offset(context, flash, &offset, error))
      return false;
    if (context->game->options.edition == QA_Q2_CLASSIC) {
      qa_builtin_angle_vectors(q2m_vector_angles(qa_vec_sub(enemy.origin,
          context->body.origin)), &forward, &right, &up);
      *origin = qa_vec_add(context->body.origin,
          qa_vec_add(qa_vec_scale(right, offset.x + (flash == 85 ? -14 : 2)),
              qa_vec_add(qa_vec_scale(up, offset.z + 8),
                         qa_vec_scale(forward, offset.y))));
      qa_vec3 center = qa_vec_add(enemy.origin,
          qa_vec_scale(qa_vec_add(enemy.bounds.mins, enemy.bounds.maxs), .5f));
      *direction = qa_vec_normalize(qa_vec_sub(center, *origin));
      *available = true;
      return true;
    }
    *origin = qa_vec_add(context->body.origin,
        qa_vec_add(qa_vec_scale(forward, offset.x),
            qa_vec_add(qa_vec_scale(right, offset.y), qa_vec_scale(up, offset.z + 6))));
    if (context->monster->dead) {
      *direction = forward;
      *available = true;
      return true;
    }
  } else {
    return true;
  }
  return q2m_predict_from(context, *origin, 0, false,
      q2_rerelease_float(context->game, .1f, .2f), NULL, direction, available, error);
}

static bool source_beam(q2m_context *context, float damage, int channel,
                         int flash, qa_error *error) {
  qa_vec3 origin, direction;
  bool available;
  if (!source_beam_pose(context, channel, flash, &origin, &direction,
                         &available, error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  qa_q2_game *game = context->game;
  bool rerelease = game->options.edition == QA_Q2_RERELEASE;
  q2_actor *actor = NULL;
  if (rerelease)
    for (q2_actor *candidate = game->first_actor; candidate; candidate = candidate->live_next)
      if (candidate->monster && candidate->monster->controller_kind == Q2M_CONTROLLER_BEAM &&
          candidate->monster->count == channel &&
          qa_actor_id_equal(candidate->monster->controller_owner, context->actor->id)) {
        actor = candidate;
        break;
      }
  bool fresh = actor == NULL;
  if (fresh && !spawn_beam(context, context->monster->enemy, origin, direction,
      damage, context->monster->medic, Q2M_CONTROLLER_BEAM, channel, &actor, error))
    return false;
  if (!actor || !q2m_alive(context))
    return true;
  struct qa_q2_monster *beam = actor->monster;
  beam->style = flash;
  beam->controller_target = context->monster->enemy;
  beam->controller_direction = direction;
  beam->controller_damage = damage;
  beam->controller_fired = rerelease;
  beam->controller_ns = q2m_after(game->now_ns, rerelease ? .2 : .1);
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, actor->id, &body, error))
    return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  body.origin = origin;
  body.angles = beam_angles(direction);
  if (!qa_world_body_write(game->services.world, actor->id, &body, error) ||
      !qa_world_link(game->services.world, actor->id, NULL, error))
    return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  if (fresh && rerelease) {
    qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q2,
      .provider = game->options.owner, .actor = actor->id,
      .time_ns = game->now_ns, .origin = origin, .volume = 1, .attenuation = 1,
      .flags = rerelease ? 1u : 0u};
    if (!qa_builtin_resource(&game->services, "misc/lasfly.wav", &sound.resource, error) ||
        !qa_builtin_emit(&game->services, &sound, error))
      return false;
    if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
      return true;
  }
  if (rerelease && q2m_alive(context) && context->monster->definition->species == Q2M_FIXBOT &&
      !beam_fire(game, actor, true, error))
    return false;
  return !rerelease || !controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
      beam_fire(game, actor, true, error);
}

bool q2m_soldier_laser_beam(q2m_context *context, int flash, qa_error *error) {
  return source_beam(context, 1, 0, flash, error);
}

bool q2m_brain_laser_beam(q2m_context *context, qa_error *error) {
  if (context->game->options.edition == QA_Q2_CLASSIC &&
      q2m_random(context->game) > .8f &&
      !q2m_sound(context, "misc/lasfly.wav", 0, 3, error))
    return false;
  if (!q2m_alive(context))
    return true;
  return source_beam(context, 1, 0, 0, error) &&
      (!q2m_alive(context) || source_beam(context, 1, 1, 0, error));
}

bool q2m_fixbot_laser_beam(q2m_context *context, qa_error *error) {
  if (context->game->options.edition == QA_Q2_CLASSIC &&
      !q2m_sound(context, "misc/lasfly.wav", 0, 3, error))
    return false;
  if (!q2m_alive(context))
    return true;
  return source_beam(context, -1, 0, 0, error);
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

bool q2m_spawn_makron_entity(q2m_context *context, qa_actor_id *child,
                            qa_error *error) {
  *child = (qa_actor_id){0};
  if (!q2m_alive(context))
    return true;
  qa_authored_target parent = {0};
  qa_q2_entity_authored(context->game, context->actor->id, &parent);
  qa_entity_property properties[] = {
      {.key = {(const uint8_t *)"classname", sizeof("classname") - 1},
       .value = {(const uint8_t *)"monster_makron", sizeof("monster_makron") - 1}},
      {.key = {(const uint8_t *)"target", sizeof("target") - 1},
       .value = qa_strings_text(
           qa_session_strings(context->game->services.session), parent.target)},
  };
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
  qa_q2_map_fields fields = {
      .properties = properties,
      .count = parent.target != 0 ? 2 : 1,
      .ordinal = UINT32_MAX,
  };
  bool handled;
  if (!qa_q2_entity_spawn(context->game, id, &fields, &handled, error)) {
    qa_error original = error != NULL ? *error : (qa_error){0};
    if (q2_actor_live(context->game, id))
      qa_session_release(context->game->services.session, id, NULL);
    if (error != NULL)
      *error = original;
    return false;
  }
  if (!q2m_alive(context))
    return !q2_actor_live(context->game, id) ||
           qa_session_release(context->game->services.session, id, error);
  if (q2_actor_live(context->game, id))
    *child = id;
  return true;
}

bool q2m_schedule_makron_spawn(q2m_context *context, qa_error *error) {
  qa_actor_id id;
  if (!q2m_spawn_makron_entity(context, &id, error))
    return false;
  if (id.registry == 0)
    return true;
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
      .scale = game->options.edition == QA_Q2_RERELEASE ? 0 : 1,
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
  resumed.body.ground = (qa_actor_reference){0};
  return q2m_write_body(&resumed, true, error);
}

static q2_actor *source_child(q2m_context *context, q2m_controller_kind kind, int channel) {
  for (q2_actor *a = context->game->first_actor; a; a = a->live_next)
    if (a->monster && a->monster->controller_kind == kind && a->monster->count == channel &&
        qa_actor_id_equal(a->monster->controller_owner, context->actor->id))
      return a;
  return NULL;
}

static bool create_source_child(q2m_context *context, const char *name,
    q2m_controller_kind kind, int channel, const qa_body_state *body,
    q2_actor **out, qa_error *error) {
  qa_q2_game *game = context->game;
  q2_actor *child;
  if (!q2_entity_native_spawn(game, name, body, Q2E_POINT, &child, error))
    return false;
  if (!q2m_alive(context)) {
    qa_session_release(game->services.session, child->id, NULL);
    *out = NULL;
    return true;
  }
  struct qa_q2_monster *controller = calloc(1, sizeof(*controller));
  if (!controller) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q2 Source child continuation");
    qa_session_release(game->services.session, child->id, NULL);
    return false;
  }
  controller->controller_kind = kind;
  controller->controller_owner = context->actor->id;
  controller->count = channel;
  controller->initialized = true;
  child->monster = controller;
  child->physics.motion = QA_PHYSICS_STATIONARY;
  child->physics.solid = QA_PHYSICS_NOT_SOLID;
  child->entity->owner = context->actor->id;
  *out = child;
  return true;
}

static bool show_source_beam(q2m_context *context, q2_actor *child,
    qa_vec3 start, qa_vec3 end, qa_error *error) {
  qa_q2_game *game = context->game;
  qa_actor_id id = child->id;
  struct qa_q2_monster *controller = child->monster;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, id, &body, error))
    return false;
  body.origin = start;
  child->entity->beam_end = end;
  if (!qa_world_body_write(game->services.world, id, &body, error) ||
      !qa_world_link(game->services.world, id, NULL, error))
    return false;
  if (!controller_live(game, child, controller, Q2M_CONTROLLER_VISUAL_CHILD))
    return true;
  return q2_entity_show(game, child, error);
}

bool q2m_source_visuals_release(q2m_context *context, qa_error *error) {
  for (q2_actor *child = context->game->first_actor, *next; child; child = next) {
    next = child->live_next;
    if (child->monster && child->monster->controller_kind == Q2M_CONTROLLER_VISUAL_CHILD &&
        qa_actor_id_equal(child->monster->controller_owner, context->actor->id) &&
        !qa_session_release(context->game->services.session, child->id, error)) return false;
  }
  return true;
}

bool q2m_shambler_lightning(q2m_context *context, bool windup, qa_error *error) {
  static const qa_vec3 left[] = {{44,36,25},{10,44,57},{-1,40,70},{-10,34,75},{7.4f,24,89}};
  static const qa_vec3 right[] = {{28,-38,25},{31,-7,70},{20,0,80},{16,1.2f,81},{27,-11,83}};
  q2_actor *child = source_child(context, Q2M_CONTROLLER_VISUAL_CHILD, 0);
  if (windup) {
    if (!q2m_sound(context, "shambler/sattck1.wav", 1, 1, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_body_state body = {.origin = context->body.origin};
    if (!create_source_child(context, "shambler_lightning", Q2M_CONTROLLER_VISUAL_CHILD,
        0, &body, &child, error))
      return false;
    if (!child)
      return true;
    if (!qa_builtin_resource(&context->game->services, "models/proj/lightning/tris.md2",
        &child->entity->visual.models[0], error)) {
      qa_session_release(context->game->services.session, child->id, NULL);
      return false;
    }
    child->entity->visual.render_flags = 128;
    child->entity->visual.visible = true;
  }
  int index = context->monster->frame - 65;
  if (index >= 5)
    return !child || qa_session_release(context->game->services.session, child->id, error);
  if (!child || index < 0) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Shambler hand lightning lacks its actual animation child");
    return false;
  }
  return show_source_beam(context, child, q2m_project_offset(context, left[index]),
      q2m_project_offset(context, right[index]), error);
}

bool q2m_turret_lasersight(q2m_context *context, qa_error *error) {
  if (context->game->options.edition != QA_Q2_RERELEASE ||
      (context->monster->spawnflags & (1u << 18)))
    return true;
  q2_actor *child = source_child(context, Q2M_CONTROLLER_VISUAL_CHILD, 1);
  if (!child) {
    qa_body_state body = {.origin = context->body.origin};
    if (!create_source_child(context, "turret_lasersight", Q2M_CONTROLLER_VISUAL_CHILD,
        1, &body, &child, error))
      return false;
    if (!child)
      return true;
    child->entity->visual.render_flags = 128;
    child->entity->visual.frame = 1;
    child->entity->visual.skin = (int32_t)UINT32_C(0xf0f0f0f0);
    child->entity->visual.visible = true;
  }
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  qa_trace_query query = {.start = context->body.origin,
    .end = qa_vec_add(context->body.origin, qa_vec_scale(forward, 8192)),
    .pass_actor = context->actor->id, .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
  query.policy.contents_mask = 3;
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context))
    return true;
  bool visible = false;
  if (context->monster->enemy.registry &&
      !q2m_visible(context, context->monster->enemy, &visible, error))
    return false;
  if (!q2m_alive(context))
    return true;
  uint32_t source_number;
  if (!qa_q2_wire_entity_number(context->game, context->actor->id, &source_number, error))
    return false;
  float range = visible ? 12 : 64;
  float time = (float)((double)context->game->now_ns / Q2M_SECOND);
  trace.end.x += sinf(time + (float)source_number) * range;
  trace.end.y += cosf((time - (float)source_number) * 3) * range;
  trace.end.z += sinf((time - (float)source_number) * 2.5f) * range;
  forward = qa_vec_normalize(qa_vec_sub(trace.end, context->body.origin));
  query.end = qa_vec_add(context->body.origin, qa_vec_scale(forward, 8192));
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  return !q2m_alive(context) || show_source_beam(context, child, context->body.origin, trace.end, error);
}

bool q2m_fixbot_goal(q2m_context *context, qa_vec3 origin, const qa_bounds *bounds,
    qa_actor_id *out, qa_error *error) {
  qa_body_state body = {.origin = origin};
  if (bounds) body.bounds = *bounds;
  q2_actor *child;
  if (!create_source_child(context, "bot_goal", Q2M_CONTROLLER_BOT_GOAL, 0, &body, &child, error))
    return false;
  *out = child ? child->id : (qa_actor_id){0};
  if (!child)
    return true;
  child->monster->controller_ns = context->game->now_ns + UINT64_C(1000000);
  return q2_entity_solid(context->game, child, QA_PHYSICS_BOX, error) &&
      (!q2_actor_live(context->game, *out) ||
       qa_world_link(context->game->services.world, *out, NULL, error));
}

void q2m_fixbot_goal_retire(q2m_context *context, qa_actor_id id) {
  q2_actor *goal = q2_actor_get(context->game, id, false, NULL);
  if (!goal || !goal->monster || goal->monster->controller_kind != Q2M_CONTROLLER_BOT_GOAL)
    return;
  goal->monster->controller_fired = true;
  goal->monster->controller_ns = q2m_after(context->game->now_ns, .1);
}

bool q2m_controller_tick(qa_q2_game *game, q2_actor *actor,
                         qa_error *error) {
  if (actor == NULL || actor->monster == NULL || !q2_actor_live(game, actor->id))
    return true;
  struct qa_q2_monster *controller = actor->monster;
  if (controller->controller_kind == Q2M_CONTROLLER_VISUAL_CHILD ||
      controller->controller_kind == Q2M_CONTROLLER_BOT_GOAL) {
    q2_actor *owner = q2_actor_get(game, controller->controller_owner, false, NULL);
    if (!owner || !owner->monster)
      return qa_session_release(game->services.session, actor->id, error);
    if (controller->controller_kind == Q2M_CONTROLLER_BOT_GOAL) {
      if (controller->controller_fired)
        return game->now_ns < controller->controller_ns ||
            qa_session_release(game->services.session, actor->id, error);
      if (game->options.edition == QA_Q2_RERELEASE &&
          !qa_actor_id_equal(owner->monster->goal, actor->id))
        return qa_session_release(game->services.session, actor->id, error);
    }
    return true;
  }
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
    return beam_fire(game, actor, true, error);
  }
  return qa_session_release(game->services.session, actor->id, error);
}

bool q2m_controller_postthink(qa_q2_game *game, q2_actor *actor, qa_error *error) {
  if (!actor->monster || !q2_actor_live(game, actor->id))
    return true;
  if (actor->monster->controller_kind == Q2M_CONTROLLER_GUARDIAN_BEAM)
    return guardian_update(game, actor, error);
  if (actor->monster->controller_kind != Q2M_CONTROLLER_BEAM ||
      game->options.edition != QA_Q2_RERELEASE)
    return true;
  struct qa_q2_monster *beam = actor->monster;
  q2_actor *owner = q2_actor_get(game, beam->controller_owner, false, NULL);
  if (!owner || !owner->monster || !owner->monster->definition)
    return qa_session_release(game->services.session, actor->id, error);
  q2m_context context = {.game = game, .actor = owner, .monster = owner->monster};
  if (!q2m_refresh(&context, error))
    return false;
  if (!q2m_alive(&context) || !controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  qa_vec3 origin, direction;
  bool available;
  if (!source_beam_pose(&context, beam->count, beam->style, &origin, &direction,
                         &available, error))
    return false;
  if (!available || !q2m_alive(&context) ||
      !controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, actor->id, &body, error))
    return false;
  if (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM))
    return true;
  body.origin = origin;
  body.angles = beam_angles(direction);
  beam->controller_direction = direction;
  beam->controller_target = context.monster->enemy;
  return qa_world_body_write(game->services.world, actor->id, &body, error) &&
      (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
       (qa_world_link(game->services.world, actor->id, NULL, error) &&
        (!controller_live(game, actor, beam, Q2M_CONTROLLER_BEAM) ||
         beam_fire(game, actor, context.monster->definition->species == Q2M_FIXBOT,
                    error))));
}
