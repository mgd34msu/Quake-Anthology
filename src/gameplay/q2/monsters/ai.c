#include "internal.h"

enum { Q2M_TRAIL_POINTS = 8 };

static float vector_yaw(qa_vec3 direction);

typedef struct q2m_trail_point {
  qa_vec3 origin;
  uint64_t time_ns;
  float yaw;
} q2m_trail_point;

typedef struct q2m_player_trail {
  qa_actor_id actor;
  qa_vec3 previous_origin;
  q2m_trail_point points[Q2M_TRAIL_POINTS];
  size_t count;
  bool has_previous;
} q2m_player_trail;

typedef struct q2m_alert {
  qa_actor_id player, observer;
  uint64_t time_ns, hostile_ns;
} q2m_alert;

struct q2_monsters_runtime {
  qa_q2_monster_services services;
  q2m_player_trail *trails;
  size_t trail_count, trail_capacity;
  q2m_alert *alerts;
  size_t alert_count, alert_capacity;
  qa_actor_id sight_client, sight_observer;
  uint64_t sight_time_ns, last_frame_ns;
  bool began_frame;
};

bool qa_q2_monsters_bind_services(qa_q2_game *game,
                                 const qa_q2_monster_services *services,
                                 qa_error *error) {
  if (game == NULL || services == NULL || services->count == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 monsters require campaign counter services");
    return false;
  }
  if (!q2_monsters_init(game, error))
    return false;
  game->monster_runtime->services = *services;
  return true;
}

bool q2m_mission(q2m_context *context, qa_monster_mission *mission,
                  bool *present, qa_error *error) {
  const qa_monster_missions *missions =
      &context->game->monster_runtime->services.missions;
  *present = missions->lookup != NULL &&
             missions->lookup(missions->context, context->actor->id, mission);
  if (!*present || !q2m_alive(context))
    return true;
  if (mission->owner == 0 || mission->spawned == NULL ||
      mission->started == NULL || mission->killed == NULL ||
      mission->route == NULL || mission->use == NULL ||
      mission->combat_route == NULL || mission->found_target == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Incomplete authored monster mission binding");
    return false;
  }
  return true;
}

bool q2m_count(q2m_context *context, qa_q2_monster_count kind,
                qa_error *error) {
  const qa_q2_monster_services *services =
      &context->game->monster_runtime->services;
  if (services->count == NULL) {
    qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                 "Q2 monster campaign accounting is not bound");
    return false;
  }
  return services->count(services->context, context->actor->id, kind, error);
}

static bool runtime_reserve(void **storage, size_t *capacity, size_t count,
                            size_t stride, qa_error *error) {
  if (count <= *capacity)
    return true;
  size_t next = *capacity == 0 ? 4 : *capacity;
  while (next < count) {
    if (next > SIZE_MAX / 2) {
      qa_error_set(error, QA_ERROR_MEMORY, count,
                   "Q2 monster perception storage is too large");
      return false;
    }
    next *= 2;
  }
  if (next > SIZE_MAX / stride) {
    qa_error_set(error, QA_ERROR_MEMORY, count,
                 "Q2 monster perception storage is too large");
    return false;
  }
  void *grown = realloc(*storage, next * stride);
  if (grown == NULL) {
    qa_error_set(error, QA_ERROR_MEMORY, count,
                 "Unable to grow Q2 monster perception storage");
    return false;
  }
  *storage = grown;
  *capacity = next;
  return true;
}

bool q2_monsters_init(qa_q2_game *game, qa_error *error) {
  if (game == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 monster runtime requires a game");
    return false;
  }
  if (game->monster_runtime != NULL)
    return true;
  game->monster_runtime = calloc(1, sizeof(*game->monster_runtime));
  if (game->monster_runtime == NULL) {
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Unable to allocate Q2 monster perception runtime");
    return false;
  }
  return true;
}

void q2_monsters_close(qa_q2_game *game) {
  if (game == NULL || game->monster_runtime == NULL)
    return;
  free(game->monster_runtime->trails);
  free(game->monster_runtime->alerts);
  free(game->monster_runtime);
  game->monster_runtime = NULL;
}

void qa_q2_monsters_checkpoint_free(qa_q2_monsters_checkpoint *checkpoint) {
  if (checkpoint == NULL)
    return;
  free(checkpoint->trails);
  free(checkpoint->alerts);
  *checkpoint = (qa_q2_monsters_checkpoint){0};
}

bool qa_q2_monsters_capture(qa_q2_game *game,
                            qa_q2_monsters_checkpoint *out,
                            qa_error *error) {
  if (game == NULL || out == NULL || game->monster_runtime == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 monster perception capture requires a live runtime");
    return false;
  }
  if (!q2_checkpoint_idle(game, error))
    return false;

  q2_monsters_runtime *runtime = game->monster_runtime;
  qa_q2_monsters_checkpoint saved = {
      .version = 1,
      .sight_time_ns = runtime->sight_time_ns,
      .last_frame_ns = runtime->last_frame_ns,
      .began_frame = runtime->began_frame,
      .trail_count = runtime->trail_count,
      .alert_count = runtime->alert_count,
  };
  if (saved.trail_count != 0) {
    if (saved.trail_count > SIZE_MAX / sizeof(saved.trails[0])) {
      qa_error_set(error, QA_ERROR_MEMORY, saved.trail_count,
                   "Q2 monster trail checkpoint is too large");
      return false;
    }
    saved.trails = calloc(saved.trail_count, sizeof(saved.trails[0]));
    if (saved.trails == NULL) {
      qa_error_set(error, QA_ERROR_MEMORY, saved.trail_count,
                   "Unable to capture Q2 monster trails");
      return false;
    }
  }
  if (saved.alert_count != 0) {
    if (saved.alert_count > SIZE_MAX / sizeof(saved.alerts[0])) {
      qa_q2_monsters_checkpoint_free(&saved);
      qa_error_set(error, QA_ERROR_MEMORY, saved.alert_count,
                   "Q2 monster alert checkpoint is too large");
      return false;
    }
    saved.alerts = calloc(saved.alert_count, sizeof(saved.alerts[0]));
    if (saved.alerts == NULL) {
      qa_q2_monsters_checkpoint_free(&saved);
      qa_error_set(error, QA_ERROR_MEMORY, saved.alert_count,
                   "Unable to capture Q2 monster alerts");
      return false;
    }
  }

  if (!q2_save_reference(game, runtime->sight_client,
                         &saved.sight_client, error) ||
      !q2_save_reference(game, runtime->sight_observer,
                         &saved.sight_observer, error)) {
    qa_q2_monsters_checkpoint_free(&saved);
    return false;
  }
  for (size_t index = 0; index < runtime->trail_count; ++index) {
    const q2m_player_trail *source = &runtime->trails[index];
    qa_q2_monster_trail_checkpoint *target = &saved.trails[index];
    target->previous_origin = source->previous_origin;
    target->count = source->count;
    target->has_previous = source->has_previous;
    for (size_t point = 0; point < source->count; ++point) {
      target->points[point] = (qa_q2_monster_trail_point_checkpoint){
          .origin = source->points[point].origin,
          .time_ns = source->points[point].time_ns,
          .yaw = source->points[point].yaw,
      };
    }
    if (!q2_save_reference(game, source->actor, &target->actor, error)) {
      qa_q2_monsters_checkpoint_free(&saved);
      return false;
    }
  }
  for (size_t index = 0; index < runtime->alert_count; ++index) {
    const q2m_alert *source = &runtime->alerts[index];
    qa_q2_monster_alert_checkpoint *target = &saved.alerts[index];
    target->time_ns = source->time_ns;
    target->hostile_ns = source->hostile_ns;
    if (!q2_save_reference(game, source->player, &target->player, error) ||
        !q2_save_reference(game, source->observer, &target->observer, error)) {
      qa_q2_monsters_checkpoint_free(&saved);
      return false;
    }
  }
  *out = saved;
  return true;
}

bool qa_q2_monsters_restore(qa_q2_game *game,
                            const qa_q2_monsters_checkpoint *saved,
                            qa_error *error) {
  if (game == NULL || saved == NULL || game->monster_runtime == NULL ||
      saved->version != 1 ||
      (saved->trail_count != 0 && saved->trails == NULL) ||
      (saved->alert_count != 0 && saved->alerts == NULL) ||
      saved->trail_count > SIZE_MAX / sizeof(q2m_player_trail) ||
      saved->alert_count > SIZE_MAX / sizeof(q2m_alert)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Invalid Q2 monster perception checkpoint");
    return false;
  }
  if (!q2_checkpoint_idle(game, error))
    return false;

  q2m_player_trail *trails = NULL;
  q2m_alert *alerts = NULL;
  if (saved->trail_count != 0) {
    trails = calloc(saved->trail_count, sizeof(trails[0]));
    if (trails == NULL) {
      qa_error_set(error, QA_ERROR_MEMORY, saved->trail_count,
                   "Unable to restore Q2 monster trails");
      return false;
    }
  }
  if (saved->alert_count != 0) {
    alerts = calloc(saved->alert_count, sizeof(alerts[0]));
    if (alerts == NULL) {
      free(trails);
      qa_error_set(error, QA_ERROR_MEMORY, saved->alert_count,
                   "Unable to restore Q2 monster alerts");
      return false;
    }
  }

  qa_actor_id sight_client = {0}, sight_observer = {0};
  if (!q2_resolve_reference(game, saved->sight_client, &sight_client, error) ||
      !q2_resolve_reference(game, saved->sight_observer, &sight_observer,
                            error))
    goto failure;
  for (size_t index = 0; index < saved->trail_count; ++index) {
    const qa_q2_monster_trail_checkpoint *source = &saved->trails[index];
    if (source->count > Q2M_TRAIL_POINTS ||
        !qa_vec_finite(source->previous_origin)) {
      qa_error_set(error, QA_ERROR_FORMAT, index,
                   "Invalid Q2 monster trail checkpoint");
      goto failure;
    }
    q2m_player_trail *target = &trails[index];
    if (!q2_resolve_reference(game, source->actor, &target->actor, error))
      goto failure;
    target->previous_origin = source->previous_origin;
    target->count = source->count;
    target->has_previous = source->has_previous;
    for (size_t point = 0; point < source->count; ++point) {
      if (!qa_vec_finite(source->points[point].origin) ||
          !isfinite(source->points[point].yaw)) {
        qa_error_set(error, QA_ERROR_FORMAT, point,
                     "Invalid Q2 monster trail point checkpoint");
        goto failure;
      }
      target->points[point] = (q2m_trail_point){
          .origin = source->points[point].origin,
          .time_ns = source->points[point].time_ns,
          .yaw = source->points[point].yaw,
      };
    }
  }
  for (size_t index = 0; index < saved->alert_count; ++index) {
    const qa_q2_monster_alert_checkpoint *source = &saved->alerts[index];
    q2m_alert *target = &alerts[index];
    if (!q2_resolve_reference(game, source->player, &target->player, error) ||
        !q2_resolve_reference(game, source->observer, &target->observer,
                              error))
      goto failure;
    target->time_ns = source->time_ns;
    target->hostile_ns = source->hostile_ns;
  }

  q2_monsters_runtime *runtime = game->monster_runtime;
  free(runtime->trails);
  free(runtime->alerts);
  runtime->trails = trails;
  runtime->trail_count = saved->trail_count;
  runtime->trail_capacity = saved->trail_count;
  runtime->alerts = alerts;
  runtime->alert_count = saved->alert_count;
  runtime->alert_capacity = saved->alert_count;
  runtime->sight_client = sight_client;
  runtime->sight_observer = sight_observer;
  runtime->sight_time_ns = saved->sight_time_ns;
  runtime->last_frame_ns = saved->last_frame_ns;
  runtime->began_frame = saved->began_frame;
  return true;

failure:
  free(trails);
  free(alerts);
  return false;
}

static void remove_trail(q2_monsters_runtime *runtime, size_t index) {
  if (index + 1 < runtime->trail_count)
    memmove(&runtime->trails[index], &runtime->trails[index + 1],
            (runtime->trail_count - index - 1) * sizeof(runtime->trails[0]));
  --runtime->trail_count;
}

static void remove_alert(q2_monsters_runtime *runtime, size_t index) {
  if (index + 1 < runtime->alert_count)
    memmove(&runtime->alerts[index], &runtime->alerts[index + 1],
            (runtime->alert_count - index - 1) * sizeof(runtime->alerts[0]));
  --runtime->alert_count;
}

void q2_monsters_release_actor(qa_q2_game *game, qa_actor_id actor) {
  if (game == NULL || game->monster_runtime == NULL)
    return;
  q2_monsters_runtime *runtime = game->monster_runtime;
  if (qa_actor_id_equal(runtime->sight_client, actor))
    runtime->sight_client = (qa_actor_id){0};
  if (qa_actor_id_equal(runtime->sight_observer, actor)) {
    runtime->sight_observer = (qa_actor_id){0};
    runtime->sight_time_ns = 0;
  }
  for (size_t index = runtime->trail_count; index-- > 0;)
    if (qa_actor_id_equal(runtime->trails[index].actor, actor))
      remove_trail(runtime, index);
  for (size_t index = runtime->alert_count; index-- > 0;)
    if (qa_actor_id_equal(runtime->alerts[index].player, actor) ||
        qa_actor_id_equal(runtime->alerts[index].observer, actor))
      remove_alert(runtime, index);
}

static q2m_player_trail *trail_for(q2_monsters_runtime *runtime,
                                   qa_actor_id actor, bool create,
                                   qa_error *error) {
  for (size_t index = 0; index < runtime->trail_count; ++index)
    if (qa_actor_id_equal(runtime->trails[index].actor, actor))
      return &runtime->trails[index];
  if (!create)
    return NULL;
  if (!runtime_reserve((void **)&runtime->trails, &runtime->trail_capacity,
                       runtime->trail_count + 1, sizeof(runtime->trails[0]),
                       error))
    return NULL;
  q2m_player_trail *trail = &runtime->trails[runtime->trail_count++];
  *trail = (q2m_player_trail){.actor = actor};
  return trail;
}

static q2m_alert *alert_for(q2_monsters_runtime *runtime, qa_actor_id player,
                            bool create, qa_error *error) {
  for (size_t index = 0; index < runtime->alert_count; ++index)
    if (qa_actor_id_equal(runtime->alerts[index].player, player))
      return &runtime->alerts[index];
  if (!create)
    return NULL;
  if (!runtime_reserve((void **)&runtime->alerts, &runtime->alert_capacity,
                       runtime->alert_count + 1, sizeof(runtime->alerts[0]),
                       error))
    return NULL;
  q2m_alert *alert = &runtime->alerts[runtime->alert_count++];
  *alert = (q2m_alert){.player = player};
  return alert;
}

static bool runtime_targetable(qa_q2_game *game, qa_actor_id actor,
                               qa_builtin_actor_traits *traits) {
  *traits = (qa_builtin_actor_traits){0};
  if (!q2_actor_live(game, actor))
    return false;
  if (game->services.actor_traits == NULL ||
      !game->services.actor_traits(game->services.context, actor, traits) ||
      !traits->player || traits->spectator || traits->no_target)
    return false;
  qa_combat_state combat;
  qa_error ignored = {0};
  return qa_combat_read(game->services.combat, actor, &combat, &ignored) &&
         combat.health > 0.0f;
}

bool q2m_perception_begin(qa_q2_game *game, qa_error *error) {
  if (game == NULL || game->monster_runtime == NULL) {
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0,
                 "Q2 monster perception runtime is unavailable");
    return false;
  }
  q2_monsters_runtime *runtime = game->monster_runtime;
  if (runtime->began_frame && runtime->last_frame_ns == game->now_ns)
    return true;
  runtime->began_frame = true;
  runtime->last_frame_ns = game->now_ns;

  q2_trace_frame *players = q2_player_roster(game, error);
  if (players == NULL)
    return false;
  size_t count = players->snapshot.count;
  ptrdiff_t current = runtime->sight_client.registry == 0 ? 0 : -1;
  if (runtime->sight_client.registry != 0)
    for (size_t index = 0; index < count; ++index)
      if (qa_actor_id_equal(players->snapshot.ids[index],
                            runtime->sight_client)) {
        current = (ptrdiff_t)index;
        break;
      }
  runtime->sight_client = (qa_actor_id){0};
  for (size_t offset = 1; offset <= count; ++offset) {
    size_t index = (size_t)(current + (ptrdiff_t)offset) % count;
    qa_builtin_actor_traits traits;
    if (runtime_targetable(game, players->snapshot.ids[index], &traits)) {
      runtime->sight_client = players->snapshot.ids[index];
      break;
    }
  }

  bool result = true;
  for (size_t index = 0; index < count; ++index) {
    qa_actor_id player = players->snapshot.ids[index];
    qa_builtin_actor_traits traits;
    qa_body_state body;
    qa_error ignored = {0};
    if (!runtime_targetable(game, player, &traits) ||
        !qa_world_body_read(game->services.world, player, &body, &ignored))
      continue;
    q2m_player_trail *trail = trail_for(runtime, player, true, error);
    if (trail == NULL) {
      result = false;
      break;
    }
    bool record = trail->count == 0;
    if (!record) {
      qa_trace_query query = {
          .start = qa_vec_add(body.origin, qa_v3(0, 0, traits.view_height)),
          .end = trail->points[trail->count - 1].origin,
          .pass_actor = player,
          .policy = qa_collision_default_policy(QA_COLLISION_Q2),
      };
      query.policy.contents_mask = Q2M_OPAQUE_MASK;
      qa_trace_result trace;
      if (!qa_world_trace(game->services.world, &query, &trace, error)) {
        result = false;
        break;
      }
      record = trace.fraction != 1.0f;
    }
    if (record) {
      qa_vec3 origin = trail->has_previous ? trail->previous_origin
                                           : body.origin;
      float yaw = body.angles.y;
      if (trail->count != 0)
        yaw = vector_yaw(
            qa_vec_sub(origin, trail->points[trail->count - 1].origin));
      if (trail->count == Q2M_TRAIL_POINTS) {
        memmove(&trail->points[0], &trail->points[1],
                (Q2M_TRAIL_POINTS - 1) * sizeof(trail->points[0]));
        --trail->count;
      }
      trail->points[trail->count++] =
          (q2m_trail_point){.origin = origin,
                            .time_ns = game->now_ns,
                            .yaw = yaw};
    }
    trail->previous_origin = body.origin;
    trail->has_previous = true;
  }
  players->active = false;
  return result;
}

qa_actor_id q2m_current_sight_client(const qa_q2_game *game) {
  return game != NULL && game->monster_runtime != NULL
             ? game->monster_runtime->sight_client
             : (qa_actor_id){0};
}

bool q2m_perception_alert(q2m_context *context, qa_actor_id target,
                          qa_error *error) {
  if (!q2m_alive(context) || context->game->monster_runtime == NULL)
    return true;
  qa_builtin_actor_traits traits = {0};
  if (!runtime_targetable(context->game, target, &traits))
    return true;
  q2_monsters_runtime *runtime = context->game->monster_runtime;
  runtime->sight_observer = context->actor->id;
  runtime->sight_time_ns = context->game->now_ns;
  q2m_alert *alert = alert_for(runtime, target, true, error);
  if (alert == NULL)
    return false;
  alert->observer = context->actor->id;
  alert->time_ns = context->game->now_ns;
  alert->hostile_ns = q2m_after(context->game->now_ns, 1.0);
  return true;
}

static float vector_yaw(qa_vec3 direction) {
  return q2m_vector_angles(direction).y;
}

static bool actor_traits(q2m_context *context, qa_actor_id id,
                         qa_builtin_actor_traits *traits) {
  *traits = (qa_builtin_actor_traits){0};
  if (qa_actors_get(qa_session_actors(context->game->services.session), id) ==
      NULL)
    return false;
  if (context->game->services.actor_traits != NULL &&
      context->game->services.actor_traits(context->game->services.context, id,
                                           traits))
    return true;
  qa_actor_collision collision;
  if (qa_world_get_collision(context->game->services.world, id, &collision)) {
    traits->monster = collision.monster;
    traits->player = ((uint32_t)collision.contents & Q2_PLAYER_CONTENTS) != 0;
    traits->view_height = 22.0f;
    return traits->monster || traits->player;
  }
  return false;
}

static bool target_alive(q2m_context *context, qa_actor_id id,
                         qa_builtin_actor_traits *traits, qa_body_state *body) {
  qa_combat_state combat;
  qa_error ignored = {0};
  return id.registry != 0 && !qa_actor_id_equal(id, context->actor->id) &&
         actor_traits(context, id, traits) &&
         (traits->player || traits->monster || traits->damageable_target) &&
         !traits->spectator &&
         !traits->no_target &&
         qa_combat_read(context->game->services.combat, id, &combat,
                        &ignored) &&
         combat.health > 0.0f &&
         qa_world_body_read(context->game->services.world, id, body, &ignored);
}

static bool in_front(q2m_context *context, qa_actor_id id) {
  qa_body_state target;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &ignored))
    return false;
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  return qa_vec_dot(forward, qa_vec_normalize(qa_vec_sub(
                                 target.origin, context->body.origin))) > 0.3f;
}

float q2m_distance(q2m_context *context, qa_actor_id id) {
  qa_body_state target;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &ignored))
    return FLT_MAX;
  if (context->game->options.edition == QA_Q2_CLASSIC)
    return qa_vec_length(qa_vec_sub(target.origin, context->body.origin));
  float x = fmaxf(
      0.0f, fmaxf(target.origin.x + target.bounds.mins.x -
                      context->body.origin.x - context->body.bounds.maxs.x,
                  context->body.origin.x + context->body.bounds.mins.x -
                      target.origin.x - target.bounds.maxs.x));
  float y = fmaxf(
      0.0f, fmaxf(target.origin.y + target.bounds.mins.y -
                      context->body.origin.y - context->body.bounds.maxs.y,
                  context->body.origin.y + context->body.bounds.mins.y -
                      target.origin.y - target.bounds.maxs.y));
  float z = fmaxf(
      0.0f, fmaxf(target.origin.z + target.bounds.mins.z -
                      context->body.origin.z - context->body.bounds.maxs.z,
                  context->body.origin.z + context->body.bounds.mins.z -
                      target.origin.z - target.bounds.maxs.z));
  return sqrtf(x * x + y * y + z * z);
}

bool q2m_visible(q2m_context *context, qa_actor_id id, bool *visible,
                 qa_error *error) {
  *visible = false;
  qa_builtin_actor_traits traits;
  qa_body_state target;
  bool alive = target_alive(context, id, &traits, &target);
  if (!q2m_alive(context) || !alive)
    return true;
  qa_trace_query query = {
      .start = qa_vec_add(context->body.origin,
                          qa_v3(0, 0, context->monster->view_height)),
      .end = qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height)),
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  query.policy.contents_mask = Q2M_OPAQUE_MASK;
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context))
    return true;
  *visible = trace.fraction == 1.0f || (trace.hit == QA_TRACE_HIT_ACTOR &&
                                        qa_actor_id_equal(trace.actor, id));
  return true;
}

bool q2m_clear_shot(q2m_context *context, qa_vec3 start, bool *clear,
                    qa_error *error) {
  *clear = false;
  struct qa_q2_monster *monster = context->monster;
  qa_actor_id target_id = monster->enemy;
  qa_builtin_actor_traits traits;
  qa_body_state target;
  bool alive = target_alive(context, target_id, &traits, &target);
  if (!q2m_alive(context) || !alive)
    return true;
  qa_vec3 end =
      monster->manual_steering || monster->attack_state == Q2M_BLIND
          ? monster->blind_fire_target
          : qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height));
  qa_trace_query query = {
      .start = start,
      .end = end,
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  query.policy.contents_mask = context->game->options.edition == QA_Q2_RERELEASE
                                   ? Q2M_ATTACK_MASK | UINT32_C(0x40004000)
                                   : Q2M_ATTACK_MASK;
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context))
    return true;
  *clear = trace.fraction == 1.0f ||
           (trace.hit == QA_TRACE_HIT_ACTOR &&
            (qa_actor_id_equal(trace.actor, target_id) ||
             (context->game->options.edition == QA_Q2_RERELEASE &&
              actor_traits(context, trace.actor, &traits) && traits.player)));
  return true;
}

bool q2m_change_yaw(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  context->actor->physics.ideal_yaw = context->monster->ideal_yaw;
  context->actor->physics.yaw_speed = context->monster->yaw_speed;
  if (!qa_physics_change_yaw(context->game->services.physics,
                             context->actor->id, context->elapsed, error))
    return false;
  return !q2m_alive(context) || q2m_refresh(context, error);
}

bool q2m_face_enemy(q2m_context *context, qa_error *error) {
  qa_vec3 target = context->monster->blind_fire_target;
  if (!context->monster->manual_steering) {
    qa_body_state body;
    qa_error ignored = {0};
    if (!qa_world_body_read(context->game->services.world,
                            context->monster->enemy, &body, &ignored))
      return true;
    target = body.origin;
  }
  context->monster->ideal_yaw =
      vector_yaw(qa_vec_sub(target, context->body.origin));
  return q2m_change_yaw(context, error);
}

bool q2m_found_target(q2m_context *context, qa_actor_id id, qa_error *error) {
  qa_builtin_actor_traits traits;
  qa_body_state target;
  bool alive = target_alive(context, id, &traits, &target);
  if (!q2m_alive(context) || !alive)
    return true;
  if (context->game->hooks.can_target != NULL) {
    bool allowed = context->game->hooks.can_target(context->game->hooks.context,
                                                   context->actor->id, id);
    if (!q2m_alive(context) || !allowed)
      return true;
  }
  if (!q2m_perception_alert(context, id, error))
    return false;
  if (!q2m_alive(context))
    return true;
  struct qa_q2_monster *monster = context->monster;
  monster->enemy = id;
  monster->goal = id;
  monster->sound_target.present = false;
  monster->hostile_ns = q2m_after(context->game->now_ns, 1.0);
  monster->last_sighting = target.origin;
  monster->saved_goal = target.origin;
  monster->has_saved_goal = true;
  monster->blind_fire_target =
      qa_vec_add(target.origin, qa_vec_scale(target.velocity, -0.1f));
  monster->blind_fire_delay = 0.0f;
  bool first_trail = monster->trail_ns == 0;
  monster->trail_ns = context->game->now_ns;
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    if (first_trail)
      monster->attack_ns = q2m_after(context->game->now_ns, 0.6);
    monster->attack_ns = q2m_after(monster->attack_ns,
                                   context->game->options.skill == 0   ? 0.4
                                   : context->game->options.skill == 1 ? 0.2
                                                                       : 0.0);
  } else if (!monster->stand_ground) {
    monster->attack_ns = q2m_after(context->game->now_ns, 1.0);
  }
  context->actor->physics.enemy = id;
  context->actor->physics.goal = id;
  bool routed;
  if (!q2m_lifecycle_route(context, true, &routed, error))
    return false;
  if (!q2m_alive(context) || routed)
    return true;
  return q2m_set_move(context,
                      monster->stand_ground ? monster->definition->stand_move
                                            : monster->definition->run_move,
                      true, error);
}

static q2_actor *native_monster_actor(qa_q2_game *game, qa_actor_id id) {
  if (id.registry == 0 || id.slot >= game->capacity)
    return NULL;
  q2_actor *actor = game->actors[id.slot];
  return actor != NULL && qa_actor_id_equal(actor->id, id) &&
                 actor->monster != NULL &&
                 actor->projectile.kind == Q2_PROJECTILE_NONE &&
                 actor->monster->definition != NULL && q2_actor_live(game, id)
             ? actor
             : NULL;
}

static const char *trait_classname(q2m_context *context,
                                   const qa_builtin_actor_traits *traits) {
  return traits->classname == 0
             ? NULL
             : qa_strings_cstr(
                   qa_session_strings(context->game->services.session),
                   traits->classname);
}

static bool clear_medic_target(q2m_context *context) {
  struct qa_q2_monster *monster = context->monster;
  q2_actor *patient = native_monster_actor(context->game, monster->enemy);
  if (patient != NULL)
    patient->monster->resurrecting = false;
  monster->medic = false;
  monster->resurrect_target = (qa_actor_id){0};
  return true;
}

static bool target_tesla(q2m_context *context, qa_actor_id tesla,
                         qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (monster->medic)
    clear_medic_target(context);
  qa_builtin_actor_traits current = {0};
  if (monster->enemy.registry != 0 &&
      actor_traits(context, monster->enemy, &current) && current.player)
    monster->last_player_enemy = monster->enemy;
  if (qa_actor_id_equal(monster->enemy, tesla))
    return true;
  monster->old_enemy = monster->enemy;
  monster->enemy = tesla;
  monster->goal = tesla;
  context->actor->physics.enemy = tesla;
  context->actor->physics.goal = tesla;
  if ((monster->definition->flags & Q2M_HAS_RANGED) == 0)
    return q2m_found_target(context, tesla, error);
  return context->combat.health <= 0.0f ||
         q2m_set_move(context, monster->definition->attack_move, true, error);
}

bool q2m_react_to_damage(q2m_context *context, qa_actor_id attacker,
                         qa_error *error) {
  if (!q2m_alive(context) || attacker.registry == 0)
    return true;
  struct qa_q2_monster *monster = context->monster;
  const bool rogue = context->game->options.product == QA_Q2_ROGUE;

  if (rogue && monster->last_attack.inflictor.registry != 0) {
    qa_builtin_actor_traits inflictor_traits = {0};
    if (actor_traits(context, monster->last_attack.inflictor,
                     &inflictor_traits)) {
      const char *name = trait_classname(context, &inflictor_traits);
      const char *tesla_name = context->game->options.edition == QA_Q2_RERELEASE
                                   ? "tesla_mine"
                                   : "tesla";
      if (name != NULL && strcmp(name, tesla_name) == 0) {
        bool created;
        if (!qa_q2_mark_tesla_area(context->game, context->actor->id,
                                   monster->last_attack.inflictor, &created,
                                   error))
          return false;
        if (!q2m_alive(context))
          return true;
        bool engage = context->game->options.edition == QA_Q2_CLASSIC
                          ? created
                          : created || (q2_rerelease_word(context->game) & 1u);
        if (engage && !qa_actor_id_equal(monster->enemy,
                                         monster->last_attack.inflictor))
          return target_tesla(context, monster->last_attack.inflictor, error);
        return true;
      }
    }
  }

  if (qa_actor_id_equal(attacker, context->actor->id) ||
      qa_actor_id_equal(attacker, monster->enemy))
    return true;
  qa_builtin_actor_traits attacker_traits = {0};
  if (!actor_traits(context, attacker, &attacker_traits) ||
      (!attacker_traits.player && !attacker_traits.monster))
    return true;
  q2_actor *other = native_monster_actor(context->game, attacker);
  bool attacker_good = attacker_traits.no_source_friendly_fire ||
                       (other != NULL && other->monster->good_guy);
  if (monster->good_guy && (attacker_traits.player || attacker_good))
    return true;

  if (rogue) {
    float fraction = monster->base_health > 0.0f
                         ? context->combat.health / monster->base_health
                         : 0.0f;
    if (monster->enemy.registry != 0 && monster->target_anger) {
      if (q2_actor_live(context->game, monster->enemy) && fraction > 0.33f)
        return true;
      monster->target_anger = false;
    }
    if (context->game->options.edition == QA_Q2_RERELEASE &&
        monster->react_ns > context->game->now_ns)
      return true;
    if (monster->enemy.registry != 0 && monster->medic) {
      if (q2_actor_live(context->game, monster->enemy) && fraction > 0.25f)
        return true;
      clear_medic_target(context);
    }
    if (context->game->options.edition == QA_Q2_RERELEASE)
      monster->react_ns = q2m_after(
          context->game->now_ns,
          (double)q2_rerelease_time_ms(context->game, 3000, 5000) / 1000.0);
  }

  qa_actor_id chosen = {0};
  if (attacker_traits.player) {
    monster->sound_target.present = false;
    qa_builtin_actor_traits current_traits = {0};
    if (monster->enemy.registry != 0 &&
        actor_traits(context, monster->enemy, &current_traits) &&
        current_traits.player) {
      bool visible;
      if (!q2m_visible(context, monster->enemy, &visible, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (visible) {
        monster->old_enemy = attacker;
        return true;
      }
      monster->old_enemy = monster->enemy;
    }
    chosen = attacker;
  } else {
    const char *self_name = monster->definition->classname;
    const char *other_name = trait_classname(context, &attacker_traits);
    bool ignore = false;
    bool retaliate = false;
    if (other != NULL) {
      if (rogue || context->game->options.edition == QA_Q2_RERELEASE)
        ignore = monster->ignore_shots || other->monster->ignore_shots;
      else
        ignore = other->monster->definition->species == Q2M_TANK ||
                 other->monster->definition->species == Q2M_SUPERTANK ||
                 other->monster->definition->species == Q2M_MAKRON ||
                 other->monster->definition->species == Q2M_JORG;
      retaliate =
          (monster->definition->locomotion ==
               other->monster->definition->locomotion &&
           other_name != NULL && strcmp(self_name, other_name) != 0 &&
           !ignore) ||
          qa_actor_id_equal(other->monster->enemy, context->actor->id);
    } else {
      retaliate = other_name == NULL || strcmp(self_name, other_name) != 0;
    }
    qa_builtin_actor_traits current_traits = {0};
    if (monster->enemy.registry != 0 &&
        actor_traits(context, monster->enemy, &current_traits) &&
        current_traits.player)
      monster->old_enemy = monster->enemy;
    if (retaliate)
      chosen = attacker;
    else if (other != NULL && other->monster->enemy.registry != 0 &&
             !qa_actor_id_equal(other->monster->enemy, context->actor->id))
      chosen = other->monster->enemy;
    else
      return true;
  }

  monster->enemy = chosen;
  monster->goal = chosen;
  context->actor->physics.enemy = chosen;
  context->actor->physics.goal = chosen;
  return monster->ducked || q2m_found_target(context, chosen, error);
}

static bool hear_target(q2m_context *context, bool *found, qa_error *error) {
  *found = false;
  qa_q2_player_noise_record noise = {0};
  uint64_t age = context->game->options.edition == QA_Q2_CLASSIC
                     ? Q2M_TENTH
                     : context->game->frame_ns;
  bool present = qa_q2_player_noise_read(context->game, false, &noise);
  bool recent = present && noise.time_ns <= context->game->now_ns &&
                context->game->now_ns - noise.time_ns <= age;
  if (!recent && context->monster->enemy.registry == 0 &&
      (context->monster->spawnflags & 1u) == 0) {
    present = qa_q2_player_noise_read(context->game, true, &noise);
    recent = present && noise.time_ns <= context->game->now_ns &&
             context->game->now_ns - noise.time_ns <= age;
  }
  if (!recent ||
      qa_vec_length(qa_vec_sub(noise.origin, context->body.origin)) > 1000.0f)
    return true;
  qa_builtin_actor_traits traits;
  qa_body_state body;
  bool alive = target_alive(context, noise.owner, &traits, &body);
  if (!q2m_alive(context) || !alive || !traits.player)
    return true;
  if (context->game->hooks.can_target != NULL) {
    bool allowed = context->game->hooks.can_target(
        context->game->hooks.context, context->actor->id, noise.owner);
    if (!q2m_alive(context) || !allowed)
      return true;
  }
  struct qa_q2_monster *monster = context->monster;
  monster->ideal_yaw =
      vector_yaw(qa_vec_sub(noise.origin, context->body.origin));
  if (!monster->manual_steering && !q2m_change_yaw(context, error))
    return false;
  if (!q2m_alive(context)) {
    *found = true;
    return true;
  }
  monster = context->monster;
  monster->sound_target = (q2m_sound_target){
      .actor = noise.owner,
      .owner = noise.owner,
      .origin = noise.origin,
      .time_ns = noise.time_ns,
      .present = true,
  };
  monster->enemy = noise.owner;
  monster->goal = noise.owner;
  monster->last_sighting = noise.origin;
  monster->saved_goal = noise.origin;
  monster->has_saved_goal = true;
  monster->hostile_ns = q2m_after(context->game->now_ns, 1.0);
  context->actor->physics.enemy = noise.owner;
  context->actor->physics.goal = noise.owner;
  *found = true;
  return q2m_set_move(context,
                      monster->stand_ground ? monster->definition->stand_move
                                            : monster->definition->run_move,
                      true, error);
}

static bool boxes_close(const qa_body_state *self, const qa_body_state *target,
                        float distance) {
  qa_vec3 self_min = qa_vec_add(self->origin, self->bounds.mins);
  qa_vec3 self_max = qa_vec_add(self->origin, self->bounds.maxs);
  qa_vec3 target_min = qa_vec_add(target->origin, target->bounds.mins);
  qa_vec3 target_max = qa_vec_add(target->origin, target->bounds.maxs);
  return target_min.x <= self_max.x + distance &&
         target_max.x >= self_min.x - distance &&
         target_min.y <= self_max.y + distance &&
         target_max.y >= self_min.y - distance &&
         target_min.z <= self_max.z + distance &&
         target_max.z >= self_min.z - distance;
}

static uint64_t alert_hostile_until(const q2_monsters_runtime *runtime,
                                    qa_actor_id player) {
  if (runtime == NULL)
    return 0;
  for (size_t index = 0; index < runtime->alert_count; ++index)
    if (qa_actor_id_equal(runtime->alerts[index].player, player))
      return runtime->alerts[index].hostile_ns;
  return 0;
}

static bool visual_candidate(q2m_context *context, qa_actor_id candidate,
                             bool *accepted, qa_error *error) {
  *accepted = false;
  qa_builtin_actor_traits traits;
  qa_body_state target;
  if (!target_alive(context, candidate, &traits, &target) ||
      !q2m_alive(context))
    return true;
  float distance = q2m_distance(context, candidate);
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    if (distance > 940.0f)
      return true;
    if ((context->monster->spawnflags & 1u) == 0 && distance <= 440.0f &&
        alert_hostile_until(context->game->monster_runtime, candidate) >=
            context->game->now_ns) {
      *accepted = true;
      return true;
    }
    bool visible;
    if (!q2m_visible(context, candidate, &visible, error))
      return false;
    if (!q2m_alive(context))
      return true;
    *accepted = visible && (distance <= 20.0f || in_front(context, candidate));
    return true;
  }

  if (distance >= 1000.0f)
    return true;
  bool visible;
  if (!q2m_visible(context, candidate, &visible, error))
    return false;
  if (!q2m_alive(context) || !visible)
    return true;
  uint64_t hostile = traits.player
                         ? alert_hostile_until(context->game->monster_runtime,
                                               candidate)
                         : traits.hostile_until_ns;
  if (distance >= 80.0f && distance < 500.0f &&
      hostile < context->game->now_ns && !in_front(context, candidate))
    return true;
  if (distance >= 500.0f && !in_front(context, candidate))
    return true;
  *accepted = !context->monster->good_guy;
  return true;
}

static bool corpse_reserved(const qa_q2_game *game, qa_actor_id corpse,
                            qa_actor_id except) {
  for (q2_actor *actor = game->first_actor; actor != NULL;
       actor = actor->live_next) {
    if (qa_actor_id_equal(actor->id, except) || actor->monster == NULL ||
        actor->projectile.kind != Q2_PROJECTILE_NONE)
      continue;
    const struct qa_q2_monster *monster = actor->monster;
    if (monster->medic && qa_actor_id_equal(monster->resurrect_target, corpse))
      return true;
  }
  return false;
}

bool q2m_medic_acquire(q2m_context *context, bool preserve_enemy,
                       bool *acquired, qa_error *error) {
  *acquired = false;
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  if ((species != Q2M_MEDIC && species != Q2M_MEDIC_COMMANDER) ||
      monster->medic || monster->dead ||
      context->game->now_ns < monster->corpse_check_ns)
    return true;
  float radius =
      context->game->options.product == QA_Q2_ROGUE && monster->stand_ground
          ? 400.0f
          : 1024.0f;
  q2_trace_frame *nearby =
      q2_nearby(context->game, context->body.origin, radius, error);
  if (nearby == NULL)
    return false;
  bool result = true;
  qa_actor_id best = {0};
  float best_health = -1.0f;
  for (size_t index = 0; index < nearby->snapshot.count; ++index) {
    qa_actor_id id = nearby->snapshot.ids[index];
    if (qa_actor_id_equal(id, context->actor->id) ||
        id.slot >= context->game->capacity)
      continue;
    q2_actor *candidate_actor = context->game->actors[id.slot];
    if (candidate_actor == NULL ||
        !qa_actor_id_equal(candidate_actor->id, id) ||
        candidate_actor->monster == NULL ||
        candidate_actor->projectile.kind != Q2_PROJECTILE_NONE)
      continue;
    struct qa_q2_monster *candidate = candidate_actor->monster;
    if (!candidate->dead || !candidate->corpse || candidate->gibbed ||
        candidate->good_guy || candidate->resurrecting ||
        corpse_reserved(context->game, id, context->actor->id))
      continue;
    qa_combat_state combat;
    qa_body_state body;
    qa_error ignored = {0};
    if (!qa_combat_read(context->game->services.combat, id, &combat,
                        &ignored) ||
        combat.health > 0.0f ||
        !qa_world_body_read(context->game->services.world, id, &body, &ignored))
      continue;
    if (context->game->options.product == QA_Q2_ROGUE &&
        qa_vec_length(qa_vec_sub(body.origin, context->body.origin)) <= 32.0f)
      continue;
    qa_trace_query query = {
        .start =
            qa_vec_add(context->body.origin, qa_v3(0, 0, monster->view_height)),
        .end = body.origin,
        .pass_actor = context->actor->id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    query.policy.contents_mask = Q2M_OPAQUE_MASK;
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error)) {
      result = false;
      break;
    }
    bool visible =
        trace.fraction == 1.0f ||
        (trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, id));
    if (!visible || candidate->base_health <= best_health)
      continue;
    best = id;
    best_health = candidate->base_health;
  }
  nearby->active = false;
  if (!result || best.registry == 0) {
    if (result)
      monster->corpse_check_ns = q2m_after(context->game->now_ns, 0.5);
    return result;
  }
  if (preserve_enemy && monster->enemy.registry != 0)
    monster->old_enemy = monster->enemy;
  monster->enemy = best;
  monster->goal = best;
  monster->resurrect_target = best;
  monster->medic = true;
  context->actor->physics.enemy = best;
  context->actor->physics.goal = best;
  q2_actor *patient_actor = context->game->actors[best.slot];
  if (patient_actor != NULL && qa_actor_id_equal(patient_actor->id, best) &&
      patient_actor->monster != NULL)
    patient_actor->monster->resurrecting = true;
  *acquired = true;
  return q2m_set_move(context, monster->definition->run_move, true, error);
}

bool q2m_find_target(q2m_context *context, bool *found, qa_error *error) {
  *found = false;
  struct qa_q2_monster *monster = context->monster;
  if (monster->good_guy || monster->combat_point || monster->dead)
    return true;
  if (!q2m_perception_begin(context->game, error))
    return false;
  if (!q2m_alive(context))
    return true;
  q2_monsters_runtime *runtime = context->game->monster_runtime;
  qa_actor_id candidate = {0};
  uint64_t recent_age = context->game->options.edition == QA_Q2_CLASSIC
                            ? Q2M_TENTH
                            : context->game->frame_ns;

  if (context->game->options.edition == QA_Q2_RERELEASE) {
    q2_trace_frame *players = q2_player_roster(context->game, error);
    if (players == NULL)
      return false;
    size_t eligible = 0;
    for (size_t index = 0; index < players->snapshot.count; ++index) {
      qa_actor_id id = players->snapshot.ids[index];
      qa_builtin_actor_traits traits;
      qa_body_state body;
      qa_error ignored = {0};
      if (!runtime_targetable(context->game, id, &traits) ||
          !qa_world_body_read(context->game->services.world, id, &body,
                              &ignored))
        continue;
      bool visible = false;
      if (!boxes_close(&context->body, &body, 0.0f)) {
        if (!in_front(context, id))
          continue;
        if (!q2m_visible(context, id, &visible, error)) {
          players->active = false;
          return false;
        }
        if (!q2m_alive(context)) {
          players->active = false;
          return true;
        }
      } else {
        visible = true;
      }
      if (visible)
        players->snapshot.ids[eligible++] = id;
    }
    if (eligible != 0) {
      size_t selected = (size_t)floorf(q2m_random(context->game) * eligible);
      if (selected >= eligible)
        selected = eligible - 1;
      candidate = players->snapshot.ids[selected];
    }
    players->active = false;
    if (candidate.registry != 0 &&
        qa_actor_id_equal(candidate, monster->enemy) &&
        !monster->sound_target.present)
      return true;
    if (candidate.registry == 0 && (monster->spawnflags & 1u) == 0) {
      for (size_t index = 0; index < runtime->alert_count; ++index) {
        q2m_alert *alert = &runtime->alerts[index];
        if (alert->time_ns > context->game->now_ns ||
            context->game->now_ns - alert->time_ns > recent_age)
          continue;
        bool accepted;
        if (!visual_candidate(context, alert->observer, &accepted, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (accepted) {
          candidate = alert->observer;
          break;
        }
      }
    }
  } else if ((monster->spawnflags & 1u) == 0 &&
             runtime->sight_observer.registry != 0 &&
             runtime->sight_time_ns <= context->game->now_ns &&
             context->game->now_ns - runtime->sight_time_ns <= recent_age) {
    candidate = runtime->sight_observer;
    if (candidate.slot < context->game->capacity) {
      q2_actor *observer = context->game->actors[candidate.slot];
      if (observer != NULL && qa_actor_id_equal(observer->id, candidate) &&
          observer->monster != NULL &&
          qa_actor_id_equal(observer->monster->enemy, monster->enemy))
        return true;
    }
  }

  if (candidate.registry == 0) {
    bool heard;
    if (!hear_target(context, &heard, error))
      return false;
    if (heard) {
      *found = true;
      return true;
    }
    candidate = context->game->options.edition == QA_Q2_CLASSIC
                    ? runtime->sight_client
                    : (qa_actor_id){0};
  }
  if (candidate.registry == 0 || !q2_actor_live(context->game, candidate))
    return true;
  if (qa_actor_id_equal(candidate, monster->enemy)) {
    *found = true;
    return true;
  }

  qa_builtin_actor_traits selected;
  if (!actor_traits(context, candidate, &selected) || !q2m_alive(context))
    return true;
  if (selected.monster && candidate.slot < context->game->capacity &&
      context->game->actors[candidate.slot] != NULL &&
      qa_actor_id_equal(context->game->actors[candidate.slot]->id, candidate) &&
      context->game->actors[candidate.slot]->monster != NULL) {
    q2_actor *other = context->game->actors[candidate.slot];
    if (other->monster->enemy.registry == 0)
      return true;
    candidate = other->monster->enemy;
  }
  qa_builtin_actor_traits target_traits;
  if (!runtime_targetable(context->game, candidate, &target_traits))
    return true;
  bool accepted;
  if (!visual_candidate(context, candidate, &accepted, error))
    return false;
  if (!q2m_alive(context) || !accepted)
    return true;
  if (!q2m_found_target(context, candidate, error))
    return false;
  if (!q2m_alive(context))
    return true;
  monster = context->monster;
  if (monster->definition->sight_sound != NULL &&
      (context->game->options.edition == QA_Q2_CLASSIC ||
       !monster->close_sight_tripped)) {
    if (!q2m_sound(context, monster->definition->sight_sound, 2, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  context->monster->close_sight_tripped = true;
  *found = true;
  return true;
}

bool q2m_check_attack(q2m_context *context, bool *selected, qa_error *error) {
  *selected = false;
  struct qa_q2_monster *monster = context->monster;
  if (monster->medic && monster->resurrect_target.registry != 0 &&
      qa_actor_id_equal(monster->enemy, monster->resurrect_target) &&
      monster->resurrect_target.slot < context->game->capacity) {
    q2_actor *patient = context->game->actors[monster->resurrect_target.slot];
    if (patient != NULL &&
        qa_actor_id_equal(patient->id, monster->resurrect_target) &&
        patient->monster != NULL && patient->monster->corpse &&
        !patient->monster->gibbed) {
      monster->attack_state = Q2M_MISSILE;
      *selected = true;
      return true;
    }
    if (patient != NULL &&
        qa_actor_id_equal(patient->id, monster->resurrect_target) &&
        patient->monster != NULL)
      patient->monster->resurrecting = false;
    monster->medic = false;
    monster->resurrect_target = (qa_actor_id){0};
  }
  qa_builtin_actor_traits traits;
  qa_body_state target;
  bool enemy_alive = target_alive(context, monster->enemy, &traits, &target);
  if (!q2m_alive(context))
    return true;
  if (!enemy_alive) {
    monster->enemy = (qa_actor_id){0};
    monster->goal = monster->move_target;
    context->actor->physics.enemy = (qa_actor_id){0};
    context->actor->physics.goal = monster->move_target;
    return q2m_set_move(context,
                        monster->move_target.registry != 0
                            ? monster->definition->walk_move
                            : monster->definition->stand_move,
                        true, error);
  }
  bool visible;
  if (!q2m_visible(context, monster->enemy, &visible, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (visible) {
    monster->had_visibility = true;
    monster->lost_sight = false;
    monster->last_sighting = target.origin;
    monster->saved_goal = target.origin;
    monster->has_saved_goal = true;
    monster->blind_fire_target =
        qa_vec_add(target.origin, qa_vec_scale(target.velocity, -0.1f));
    monster->blind_fire_delay = 0.0f;
    monster->trail_ns = context->game->now_ns;
    monster->search_ns = q2m_after(context->game->now_ns, 5.0);
  }
  float distance = q2m_distance(context, monster->enemy);
  if (distance <= 80.0f && (monster->definition->flags & Q2M_HAS_MELEE) != 0) {
    monster->attack_state = Q2M_MELEE;
    *selected = true;
    return true;
  }
  if ((monster->definition->flags & Q2M_HAS_RANGED) == 0 ||
      context->game->now_ns < monster->attack_ns)
    return true;
  bool clear;
  qa_vec3 start =
      qa_vec_add(context->body.origin, qa_v3(0, 0, monster->view_height));
  if (!q2m_clear_shot(context, start, &clear, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!clear) {
    if ((monster->definition->flags & Q2M_BLIND_FIRE) == 0 ||
        !monster->had_visibility ||
        context->game->now_ns < monster->trail_ns + Q2M_TENTH)
      return true;
    float chance = monster->blind_fire_delay < 1.0f   ? 1.0f
                   : monster->blind_fire_delay < 7.5f ? 0.4f
                                                      : 0.1f;
    monster->blind_fire_delay += 4.0f + q2m_random(context->game) * 3.0f;
    if (q2m_random(context->game) > chance)
      return true;
    monster->attack_state = Q2M_BLIND;
    *selected = true;
    return true;
  }
  float chance;
  if (monster->stand_ground)
    chance = 0.4f;
  else if (distance < 500.0f)
    chance = 0.4f;
  else if (distance < 1000.0f)
    chance = 0.1f;
  else
    chance = 0.02f;
  if (context->game->options.skill == 0)
    chance *= 0.5f;
  else if (context->game->options.skill >= 2)
    chance *= 2.0f;
  if (monster->definition->flags & Q2M_BOSS)
    chance = fmaxf(chance, 0.5f);
  if (q2m_random(context->game) >= fminf(chance, 1.0f))
    return true;
  monster->attack_state = Q2M_MISSILE;
  monster->attack_ns = q2m_after(
      context->game->now_ns, context->game->options.edition == QA_Q2_RERELEASE
                                 ? 1.0 + q2m_random(context->game)
                                 : 2.0 * q2m_random(context->game));
  *selected = true;
  return true;
}

static bool select_species_attack(q2m_context *context, const char **move,
                                  qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  float distance = q2m_distance(context, monster->enemy);
  bool melee = monster->attack_state == Q2M_MELEE;
  bool blind = monster->attack_state == Q2M_BLIND;
  float random;

  *move = melee ? monster->definition->melee_move
          : monster->definition->attack_move != NULL
              ? monster->definition->attack_move
              : monster->definition->attack2_move;
  switch (species) {
  case Q2M_PARASITE:
    if (context->game->options.edition == QA_Q2_RERELEASE) {
      bool clear;
      if (!q2m_clear_shot(context, q2m_project_offset(context, qa_v3(-1.7f, 0, 1.2f)),
                          &clear, error))
        return false;
      if (!q2m_alive(context))
        return true;
      *move = clear ? "parasite_move_fire_proboscis" : NULL;
      if (clear && !q2m_parasite_interrupt(context, false, error))
        return false;
    }
    break;
  case Q2M_INFANTRY:
  case Q2M_TURRET_DRIVER:
    if (melee ||
        distance <
            (context->game->options.edition == QA_Q2_CLASSIC ? 80.0f : 20.0f)) {
      *move = "infantry_move_attack2";
    } else if (context->game->options.edition == QA_Q2_RERELEASE &&
               !monster->cocked) {
      *move = q2m_random(context->game) <= 0.1f ? "infantry_move_attack5"
                                                : "infantry_move_attack3";
    } else {
      *move = "infantry_move_attack1";
    }
    break;
  case Q2M_SOLDIER_LIGHT:
  case Q2M_SOLDIER:
  case Q2M_SOLDIER_SS:
    if (blind) {
      monster->manual_steering = true;
      *move = "soldier_move_attack1";
      monster->attack_ns =
          q2m_after(context->game->now_ns, 1.5 + q2m_random(context->game));
    } else if (species == Q2M_SOLDIER_SS) {
      *move = "soldier_move_attack4";
    } else if (context->game->options.edition == QA_Q2_RERELEASE &&
               !monster->stand_ground && distance >= 220.0f &&
               q2m_random(context->game) < 0.25f) {
      *move = "soldier_move_attack6";
    } else {
      *move = q2m_random(context->game) < 0.5f ? "soldier_move_attack1"
                                               : "soldier_move_attack2";
    }
    break;
  case Q2M_SOLDIER_RIPPER:
  case Q2M_SOLDIER_HYPER:
  case Q2M_SOLDIER_LASER:
    *move = blind                              ? "soldierh_move_attack1"
            : q2m_random(context->game) < 0.5f ? "soldierh_move_attack1"
                                               : "soldierh_move_attack2";
    monster->manual_steering = blind;
    break;
  case Q2M_BERSERK:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? "berserk_move_attack_spike"
                                               : "berserk_move_attack_club";
    break;
  case Q2M_BRAIN:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? "brain_move_attack1"
                                               : "brain_move_attack2";
    break;
  case Q2M_FLOATER:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? "floater_move_attack3"
                                               : "floater_move_attack2";
    else if (context->game->options.edition == QA_Q2_RERELEASE &&
             q2m_random(context->game) > 0.5f &&
             q2m_move_named(monster, "floater_move_attack1a") != NULL) {
      monster->attack_state = Q2M_SLIDING;
      *move = "floater_move_attack1a";
    }
    break;
  case Q2M_FLYER:
    if (melee)
      *move = "flyer_move_start_melee";
    else
      *move = "flyer_move_attack2";
    break;
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    if (!melee && distance <= 112.0f) {
      *move = NULL;
      break;
    }
    if (!melee) {
      qa_body_state enemy;
      qa_builtin_actor_traits traits = {.view_height = 22.0f};
      qa_error ignored = {0};
      if (qa_world_body_read(context->game->services.world, monster->enemy,
                             &enemy, &ignored)) {
        if (context->game->services.actor_traits != NULL) {
          qa_builtin_actor_traits shared = {0};
          if (context->game->services.actor_traits(
                  context->game->services.context, monster->enemy, &shared))
            traits = shared;
        }
        if (!q2m_alive(context))
          return true;
        monster->blind_fire_target =
            qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height));
      }
      if (!q2m_sound(context,
                     species == Q2M_GLADB && monster->style == 1
                         ? "weapons/plasshot.wav"
                         : "gladiator/railgun.wav",
                     1, 1.0f, error))
        return false;
    }
    break;
  case Q2M_GUNNER:
    *move = distance < 80.0f || q2m_random(context->game) > 0.5f
                ? "gunner_move_attack_chain"
                : "gunner_move_attack_grenade";
    break;
  case Q2M_GUN_COMMANDER:
    if (melee) {
      *move = "guncmdr_move_attack_kick";
    } else if (blind || distance > 400.0f) {
      *move = "guncmdr_move_attack_mortar";
    } else {
      *move = "guncmdr_move_attack_chain";
    }
    break;
  case Q2M_HOVER:
  case Q2M_DAEDALUS:
    *move = monster->attack_state == Q2M_SLIDING &&
                    q2m_move_named(monster, "hover_move_start_attack2") != NULL
                ? "hover_move_start_attack2"
                : "hover_move_start_attack";
    break;
  case Q2M_JORG:
    if (q2m_random(context->game) <= 0.75f) {
      if (!q2m_sound(context, "boss3/bs3atck1.wav", 2, 1.0f, error))
        return false;
      *move = "jorg_move_start_attack1";
    } else {
      if (!q2m_sound(context, "boss3/bs3atck2.wav", 2, 1.0f, error))
        return false;
      *move = "jorg_move_attack2";
    }
    break;
  case Q2M_MAKRON:
    random = q2m_random(context->game);
    *move = random <= 0.3f   ? "makron_move_attack3"
            : random <= 0.6f ? "makron_move_attack4"
                             : "makron_move_attack5";
    break;
  case Q2M_MEDIC:
  case Q2M_MEDIC_COMMANDER:
    if (monster->medic)
      *move = "medic_move_attackCable";
    else if (species == Q2M_MEDIC_COMMANDER &&
             (blind || (distance >= 80.0f && monster->monster_slots > 2 &&
                        q2m_random(context->game) > 0.2f)))
      *move = "medic_move_callReinforcements";
    else
      *move = "medic_move_attackBlaster";
    break;
  case Q2M_SUPERTANK:
    *move = distance <= 160.0f || q2m_random(context->game) < 0.3f
                ? "supertank_move_attack1"
                : "supertank_move_attack2";
    break;
  case Q2M_BOSS5:
    *move = distance <= 160.0f || q2m_random(context->game) < 0.3f
                ? "boss5_move_attack1"
                : "boss5_move_attack2";
    break;
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    if (blind) {
      monster->manual_steering = true;
      *move = "tank_move_attack_pre_rocket";
      break;
    }
    random = q2m_random(context->game);
    if (distance <= 125.0f)
      *move =
          random < 0.4f ? "tank_move_attack_chain" : "tank_move_attack_blast";
    else if (distance <= 250.0f)
      *move =
          random < 0.5f ? "tank_move_attack_chain" : "tank_move_attack_blast";
    else if (random < 0.33f)
      *move = "tank_move_attack_chain";
    else if (random < 0.66f) {
      monster->pain_ns = q2m_after(context->game->now_ns, 5.0);
      *move = "tank_move_attack_pre_rocket";
    } else
      *move = "tank_move_attack_blast";
    break;
  case Q2M_BOSS2: {
    bool guns = distance <= 125.0f || q2m_random(context->game) <= 0.6f;
    bool n64 = context->game->options.edition == QA_Q2_RERELEASE &&
               (monster->spawnflags & 8u) != 0;
    *move = guns  ? n64 ? "boss2_move_attack_hb" : "boss2_move_attack_pre_mg"
            : n64 ? "boss2_move_attack_rocket2"
                  : "boss2_move_attack_rocket";
    break;
  }
  case Q2M_ACTOR:
    monster->pause_ns = q2m_after(
        context->game->now_ns,
        context->game->options.edition == QA_Q2_RERELEASE
            ? 1.0 + q2m_random(context->game) * 1.6
            : (10.0 + floorf(q2m_random(context->game) * 16.0)) * 0.1);
    *move = "actor_move_attack";
    break;
  case Q2M_GEKK:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? "gekk_move_attack1"
                                               : "gekk_move_attack2";
    else if (distance < 80.0f)
      *move = "gekk_move_leapatk";
    else
      *move = "gekk_move_spit";
    break;
  case Q2M_STALKER:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? "stalker_move_swing_l"
                                               : "stalker_move_swing_r";
    else {
      if (context->game->options.skill > 0 &&
          q2m_random(context->game) >
              1.0f - 0.5f / (float)context->game->options.skill)
        monster->attack_state = Q2M_STRAIGHT;
      else {
        if (q2m_random(context->game) <= 0.5f)
          monster->lefty = !monster->lefty;
        monster->attack_state = Q2M_SLIDING;
      }
      *move = "stalker_move_shoot";
    }
    break;
  case Q2M_TURRET:
    *move = blind ? "turret_move_fire_blind" : "turret_move_fire";
    break;
  case Q2M_CARRIER: {
    monster->hold_frame = false;
    if (blind) {
      *move = "carrier_move_spawn";
      break;
    }
    qa_body_state target;
    qa_error ignored = {0};
    if (!qa_world_body_read(context->game->services.world, monster->enemy,
                            &target, &ignored)) {
      *move = NULL;
      break;
    }
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(target.origin, context->body.origin));
    qa_vec3 forward;
    qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
    float facing = qa_vec_dot(direction, forward);
    bool front = facing > 0.3f;
    bool back = facing < -0.3f;
    bool below = -direction.z > 0.95f;
    if (back || below) {
      *move = "carrier_move_attack_rocket";
      break;
    }
    if (!front) {
      *move = q2m_random(context->game) < 0.1f ? "carrier_move_attack_pre_mg"
                                               : "carrier_move_attack_rail";
      break;
    }
    random = q2m_random(context->game);
    if (distance <= 125.0f)
      *move = random < 0.8f ? "carrier_move_attack_pre_mg"
                            : "carrier_move_attack_rail";
    else if (distance < 600.0f && monster->monster_slots > 2)
      *move = random <= 0.2f   ? "carrier_move_attack_pre_mg"
              : random <= 0.4f ? "carrier_move_attack_pre_gren"
              : random <= 0.7f ? "carrier_move_attack_rail"
                               : "carrier_move_spawn";
    else if (distance < 600.0f)
      *move = random <= 0.3f    ? "carrier_move_attack_pre_mg"
              : random <= 0.65f ? "carrier_move_attack_pre_gren"
                                : "carrier_move_attack_rail";
    else if (monster->monster_slots > 2)
      *move = random < 0.3f    ? "carrier_move_attack_pre_mg"
              : random < 0.65f ? "carrier_move_attack_rail"
                               : "carrier_move_spawn";
    else
      *move = random < 0.45f ? "carrier_move_attack_pre_mg"
                             : "carrier_move_attack_rail";
    if (*move != NULL && strcmp(*move, "carrier_move_attack_rail") == 0 &&
        !q2m_sound(context, "gladiator/railgun.wav", 1, 1.0f, error))
      return false;
    break;
  }
  case Q2M_WIDOW:
    if (melee)
      *move = "widow_move_attack_kick";
    else if (monster->monster_used < monster->monster_slots &&
             q2m_random(context->game) < 0.35f)
      *move = "widow_move_spawn";
    else
      *move = q2m_random(context->game) < 0.65f
                  ? "widow_move_attack_pre_blaster"
                  : "widow_move_attack_pre_rail";
    break;
  case Q2M_WIDOW2:
    if (melee)
      *move = "widow2_move_tongs";
    else
      *move = q2m_random(context->game) < 0.75f ? "widow2_move_attack_pre_beam"
                                                : "widow2_move_attack_disrupt";
    break;
  case Q2M_ARACHNID: {
    qa_body_state target;
    qa_error ignored = {0};
    if (melee ||
        (monster->melee_ns < context->game->now_ns && distance < 80.0f)) {
      *move = "arachnid_melee";
    } else if (qa_world_body_read(context->game->services.world, monster->enemy,
                                  &target, &ignored) &&
               target.origin.z - context->body.origin.z > 150.0f) {
      *move = "arachnid_attack_up1";
    } else {
      *move = "arachnid_attack1";
    }
    break;
  }
  case Q2M_GUARDIAN:
    *move = distance > 500.0f ? "guardian_move_atk2_in"
            : monster->melee_ns < context->game->now_ns && distance < 120.0f
                ? "guardian_move_kick"
                : "guardian_move_atk1_in";
    break;
  case Q2M_SHAMBLER:
    if (melee) {
      random = q2m_random(context->game);
      *move = random > 0.6f || context->combat.health == 600.0f
                  ? "shambler_attack_smash"
              : random > 0.3f ? "shambler_attack_swingl"
                              : "shambler_attack_swingr";
    } else {
      *move = "shambler_attack_magic";
    }
    break;
  default:
    break;
  }
  return true;
}

static bool attack_selected(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (!q2m_face_enemy(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  float delta =
      fabsf(qa_builtin_angle_delta(monster->ideal_yaw, context->body.angles.y));
  if (delta > monster->yaw_speed * fmaxf(1.0f, context->elapsed * 10.0f))
    return true;
  const char *move;
  if (!select_species_attack(context, &move, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (move == NULL)
    return true;
  if (!q2m_set_move(context, move, true, error))
    return false;
  if (q2m_alive(context) && (monster->attack_state == Q2M_MELEE ||
                             monster->attack_state == Q2M_MISSILE ||
                             monster->attack_state == Q2M_BLIND))
    monster->attack_state = Q2M_STRAIGHT;
  return true;
}

static bool fly_trace(q2m_context *context, qa_vec3 start, qa_vec3 end,
                      const qa_bounds *bounds, qa_trace_result *out,
                      qa_error *error) {
  qa_trace_query query = {
      .start = start,
      .end = end,
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  query.policy.contents_mask = UINT32_C(0x00020003);
  if (bounds != NULL) {
    query.shape.kind = QA_SHAPE_BOX;
    query.shape.bounds = *bounds;
  }
  return qa_world_trace(context->game->services.world, &query, out, error);
}

static qa_vec3 fly_slerp(qa_vec3 from, qa_vec3 to, float fraction) {
  float product = fmaxf(-1.0f, fminf(1.0f, qa_vec_dot(from, to)));
  float first = 1.0f - fraction, second = fraction;
  if (fabsf(product) <= 0.9995f) {
    float angle = acosf(product), sine = sinf(angle);
    if (sine != 0.0f) {
      first = sinf((1.0f - fraction) * angle) / sine;
      second = sinf(fraction * angle) / sine;
    }
  }
  return qa_vec_add(qa_vec_scale(from, first), qa_vec_scale(to, second));
}

static qa_vec3 fly_random_direction(qa_q2_game *game) {
  qa_vec3 value = qa_v3(q2_rerelease_float(game, -1.0f, 1.0f),
                        q2_rerelease_float(game, -1.0f, 1.0f),
                        q2_rerelease_float(game, -1.0f, 1.0f));
  return qa_vec_normalize(value);
}

static qa_vec3 fly_hover_offset(q2m_context *context, bool has_enemy) {
  struct qa_q2_monster *monster = context->monster;
  if ((!has_enemy && !monster->medic) || monster->combat_point ||
      monster->sound_target.present || monster->hint_path)
    return qa_v3(0, 0, 0);
  float theta = q2_rerelease_float(context->game, 0.0f, 6.2831853071795864769f);
  float sample = monster->fly_above
                     ? 0.7f + q2_rerelease_float(context->game, 0.0f, 0.3f)
                 : monster->fly_buzzard || monster->medic
                     ? q2_rerelease_float(context->game, 0.0f, 1.0f)
                     : q2_rerelease_float(context->game, -1.0f, 1.0f) * 0.06f;
  sample = fmaxf(-1.0f, fminf(1.0f, sample));
  float phi = acosf(sample), sine = sinf(phi);
  qa_vec3 direction = qa_v3(sine * cosf(theta), sine * sinf(theta), cosf(phi));
  float distance = q2_rerelease_float(context->game, monster->fly_min_distance,
                                      monster->fly_max_distance);
  return qa_vec_scale(direction, distance);
}

static bool alternate_fly(q2m_context *context, bool *handled,
                          qa_error *error) {
  *handled = false;
  struct qa_q2_monster *monster = context->monster;
  if (context->game->options.edition != QA_Q2_RERELEASE ||
      !monster->alternate_fly)
    return true;
  *handled = true;
  if (monster->definition->locomotion == Q2M_SWIM && monster->water_level < 3)
    return true;

  qa_body_state enemy = {0};
  bool has_enemy = monster->enemy.registry != 0 &&
                   qa_world_body_read(context->game->services.world,
                                      monster->enemy, &enemy, NULL);
  bool enemy_visible = false;
  if (has_enemy && monster->fly_pinned &&
      !q2m_visible(context, monster->enemy, &enemy_visible, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (monster->fly_position_ns <= context->game->now_ns ||
      (has_enemy && monster->fly_pinned && !enemy_visible)) {
    monster->fly_pinned = false;
    int64_t milliseconds = q2_rerelease_time_ms(context->game, 3000, 10000);
    uint64_t interval =
        milliseconds > 0 ? (uint64_t)milliseconds * UINT64_C(1000000) : 0;
    monster->fly_position_ns = interval > UINT64_MAX - context->game->now_ns
                                   ? UINT64_MAX
                                   : context->game->now_ns + interval;
    monster->fly_ideal_position = fly_hover_offset(context, has_enemy);
  }

  float current_speed = qa_vec_length(context->body.velocity);
  qa_vec3 direction =
      current_speed == 0.0f
          ? qa_v3(0, 0, 0)
          : qa_vec_scale(context->body.velocity, 1.0f / current_speed);
  if (!qa_vec_finite(direction)) {
    *handled = false;
    return true;
  }

  qa_vec3 target = {0}, target_velocity = {0};
  bool has_target = false;
  if (has_enemy && !monster->combat_point && !monster->sound_target.present &&
      !monster->lost_sight) {
    target = enemy.origin;
    target_velocity = enemy.velocity;
    has_target = true;
  } else if (monster->sound_target.present) {
    target = monster->sound_target.origin;
    has_target = true;
  } else {
    qa_actor_id goal =
        monster->goal.registry != 0 ? monster->goal : monster->move_target;
    qa_body_state goal_body;
    if (goal.registry != 0 && qa_world_body_read(context->game->services.world,
                                                 goal, &goal_body, NULL)) {
      target = goal_body.origin;
      has_target = true;
    }
  }
  if (!has_target) {
    if (current_speed > 0.0f)
      current_speed = fmaxf(0.0f, current_speed - monster->fly_acceleration);
    else if (current_speed < 0.0f)
      current_speed = fminf(0.0f, current_speed + monster->fly_acceleration);
    context->body.velocity = qa_vec_scale(direction, current_speed);
    return q2m_write_body(context, false, error);
  }

  qa_vec3 wanted_position =
      monster->fly_pinned ? monster->fly_ideal_position
      : monster->combat_point || monster->sound_target.present ||
              monster->lost_sight
          ? target
          : qa_vec_add(qa_vec_add(target, qa_vec_scale(target_velocity, 0.25f)),
                       monster->fly_ideal_position);
  const qa_bounds fit_bounds = {.mins = {-8, -8, -8}, .maxs = {8, 8, 8}};
  qa_trace_result fit;
  if (!fly_trace(context, target, wanted_position, &fit_bounds, &fit, error))
    return false;
  if (!fit.all_solid)
    wanted_position = fit.end;

  qa_vec3 difference = qa_vec_sub(wanted_position, context->body.origin);
  if (difference.z > context->body.bounds.mins.z &&
      difference.z < context->body.bounds.maxs.z)
    difference.z = 0.0f;
  float wanted_distance = qa_vec_length(difference);
  qa_vec3 wanted_direction =
      wanted_distance == 0.0f
          ? qa_v3(0, 0, 0)
          : qa_vec_scale(difference, 1.0f / wanted_distance);
  if (!monster->manual_steering)
    monster->ideal_yaw =
        vector_yaw(qa_vec_normalize(qa_vec_sub(target, context->body.origin)));

  qa_trace_result obstruction;
  if (!fly_trace(
          context, context->body.origin,
          qa_vec_add(context->body.origin,
                     qa_vec_scale(wanted_direction, monster->fly_acceleration)),
          &context->body.bounds, &obstruction, error))
    return false;
  qa_vec3 forward, right;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, NULL);
  if (obstruction.fraction < 0.25f) {
    qa_vec3 bottom_start = qa_vec_add(context->body.origin,
                                      qa_v3(0, 0, context->body.bounds.mins.z));
    qa_vec3 top_start = qa_vec_add(context->body.origin,
                                   qa_v3(0, 0, context->body.bounds.maxs.z));
    qa_trace_result bottom_sight, bottom_step, top_sight, top_step;
    if (!fly_trace(context, bottom_start, wanted_position, NULL, &bottom_sight,
                   error) ||
        !fly_trace(context, context->body.origin,
                   qa_vec_add(context->body.origin,
                              qa_v3(0, 0,
                                    context->body.bounds.mins.z -
                                        monster->fly_acceleration)),
                   &context->body.bounds, &bottom_step, error) ||
        !fly_trace(context, top_start, wanted_position, NULL, &top_sight,
                   error) ||
        !fly_trace(context, context->body.origin,
                   qa_vec_add(context->body.origin,
                              qa_v3(0, 0,
                                    context->body.bounds.maxs.z +
                                        monster->fly_acceleration)),
                   &context->body.bounds, &top_step, error))
      return false;
    bool bottom_visible =
        bottom_sight.fraction == 1.0f && bottom_step.fraction == 1.0f;
    bool top_visible = top_sight.fraction == 1.0f && top_step.fraction == 1.0f;
    if (bottom_visible != top_visible) {
      wanted_direction =
          qa_vec_add(wanted_direction, qa_v3(0, 0, top_visible ? 1.0f : -1.0f));
    } else {
      qa_vec3 front = qa_vec_add(
          context->body.origin, qa_v3(forward.x * context->body.bounds.maxs.x,
                                      forward.y * context->body.bounds.maxs.y,
                                      forward.z * context->body.bounds.maxs.z));
      qa_vec3 side = qa_v3(right.x * context->body.bounds.maxs.x,
                           right.y * context->body.bounds.maxs.y,
                           right.z * context->body.bounds.maxs.z);
      qa_trace_result left, right_trace;
      if (!fly_trace(context, qa_vec_sub(front, side), wanted_position, NULL,
                     &left, error) ||
          !fly_trace(context, qa_vec_add(front, side), wanted_position, NULL,
                     &right_trace, error))
        return false;
      bool left_visible = left.fraction == 1.0f;
      bool right_visible = right_trace.fraction == 1.0f;
      if (left_visible != right_visible)
        wanted_direction =
            qa_vec_add(wanted_direction,
                       qa_vec_scale(right, right_visible ? 1.0f : -1.0f));
      else
        wanted_direction = obstruction.plane.normal;
    }
    wanted_direction = qa_vec_normalize(wanted_direction);
  }

  bool direct = (monster->fly_thrusters && !monster->fly_pinned) ||
                monster->combat_point || monster->lost_sight;
  float turn_factor =
      direct && qa_vec_dot(direction, wanted_direction) > 0.0f
          ? 0.45f
          : fminf(1.0f,
                  0.84f + 0.08f * (monster->fly_speed == 0.0f
                                       ? 0.0f
                                       : current_speed / monster->fly_speed));
  qa_vec3 final_direction =
      current_speed != 0.0f ? direction : wanted_direction;
  if (!qa_vec_finite(final_direction)) {
    *handled = false;
    return true;
  }

  qa_point_query point = {
      .point = qa_vec_add(context->body.origin,
                          qa_vec_scale(wanted_direction, current_speed)),
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  qa_point_contents contents;
  if (!qa_world_point_contents(context->game->services.world, &point, &contents,
                               error))
    return false;
  bool water_ahead = ((uint32_t)contents.contents & UINT32_C(32)) != 0;
  bool swimming = monster->definition->locomotion == Q2M_SWIM;
  bool bad_direction =
      swimming ? !water_ahead : monster->water_level < 3 && water_ahead;
  if (bad_direction) {
    if (monster->recovery_ns < context->game->now_ns) {
      monster->fly_recovery_direction = fly_random_direction(context->game);
      monster->recovery_ns = q2m_after(context->game->now_ns, 1.0);
    }
    wanted_direction = monster->fly_recovery_direction;
  }
  if (current_speed != 0.0f && turn_factor > 0.0f)
    final_direction = qa_vec_normalize(
        fly_slerp(direction, wanted_direction, 1.0f - turn_factor));

  float speed_factor =
      !has_enemy || direct ? 1.0f
      : qa_vec_dot(forward, wanted_direction) < -0.25f && current_speed != 0.0f
          ? 0.0f
          : fminf(1.0f, monster->fly_speed == 0.0f
                            ? 0.0f
                            : wanted_distance / monster->fly_speed);
  if (bad_direction)
    speed_factor = -speed_factor;
  float acceleration = monster->fly_acceleration;
  if (qa_vec_dot(final_direction, wanted_direction) < 0.25f)
    acceleration *= 2.0f;
  float wanted_speed =
      monster->manual_steering ? 0.0f : monster->fly_speed * speed_factor;
  if (current_speed > wanted_speed)
    current_speed = fmaxf(wanted_speed, current_speed - acceleration);
  else if (current_speed < wanted_speed)
    current_speed = fminf(wanted_speed, current_speed + acceleration);
  if (!qa_vec_finite(final_direction) || !isfinite(current_speed)) {
    *handled = false;
    return true;
  }
  context->body.velocity = qa_vec_scale(final_direction, current_speed);
  if (has_enemy && (monster->fly_buzzard || monster->medic)) {
    qa_vec3 pitch_direction =
        qa_vec_normalize(qa_vec_sub(context->body.origin, target));
    float desired = atan2f(pitch_direction.z,
                           hypotf(pitch_direction.x, pitch_direction.y)) *
                    57.29577951308232f;
    if (desired - context->body.angles.x > 180.0f)
      desired -= 360.0f;
    if (desired - context->body.angles.x < -180.0f)
      desired += 360.0f;
    context->body.angles.x +=
        context->elapsed * 4.0f * (desired - context->body.angles.x);
  }
  return q2m_write_body(context, false, error);
}

static uint32_t monster_move_mask(const qa_q2_game *game) {
  return Q2M_MONSTER_MASK |
         (game->options.edition == QA_Q2_RERELEASE ? UINT32_C(0x40000000)
                                                    : 0u);
}

static bool pursuit_trace(q2m_context *context, qa_vec3 end, bool shaped,
                          qa_trace_result *trace, qa_error *error) {
  qa_trace_query query = {
      .start = context->body.origin,
      .end = end,
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  if (shaped)
    query.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX,
                                   .bounds = context->body.bounds};
  query.policy.contents_mask =
      shaped ? monster_move_mask(context->game) : Q2M_OPAQUE_MASK;
  return qa_world_trace(context->game->services.world, &query, trace, error);
}

static bool set_body_yaw(q2m_context *context, float yaw, qa_error *error) {
  context->body.angles.y = yaw;
  return q2m_write_body(context, false, error);
}

static bool pursuit_goal(q2m_context *context, float distance, qa_vec3 *goal,
                         qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool new_goal = false;
  if (!monster->lost_sight) {
    monster->lost_sight = true;
    monster->pursuit_last_seen = true;
    monster->pursue_next = false;
    monster->pursue_temporary = false;
    new_goal = true;
  }
  if (monster->pursue_next) {
    monster->pursue_next = false;
    monster->search_ns = q2m_after(context->game->now_ns, 5.0);
    if (monster->pursue_temporary && monster->has_saved_goal) {
      monster->pursue_temporary = false;
      monster->last_sighting = monster->saved_goal;
      new_goal = true;
    } else {
      q2m_player_trail *trail =
          context->game->monster_runtime == NULL
              ? NULL
              : trail_for(context->game->monster_runtime, monster->enemy,
                          false, NULL);
      size_t marker = SIZE_MAX;
      if (trail != NULL)
        for (size_t index = 0; index < trail->count; ++index)
          if (trail->points[index].time_ns > monster->trail_ns) {
            marker = index;
            break;
          }
      if (monster->pursuit_last_seen && marker != SIZE_MAX) {
        qa_trace_result current;
        if (!pursuit_trace(context, trail->points[marker].origin, false,
                           &current, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (current.fraction != 1.0f && marker != 0) {
          qa_trace_result prior;
          if (!pursuit_trace(context, trail->points[marker - 1].origin, false,
                             &prior, error))
            return false;
          if (!q2m_alive(context))
            return true;
          if (prior.fraction == 1.0f)
            --marker;
        }
      }
      monster->pursuit_last_seen = false;
      if (marker != SIZE_MAX) {
        const q2m_trail_point *point = &trail->points[marker];
        monster->last_sighting = point->origin;
        monster->trail_ns = point->time_ns;
        monster->ideal_yaw = point->yaw;
        if (!set_body_yaw(context, point->yaw, error))
          return false;
        if (!q2m_alive(context))
          return true;
        new_goal = true;
      }
    }
  }

  float direct_distance =
      qa_vec_length(qa_vec_sub(monster->last_sighting, context->body.origin));
  if (direct_distance <= fabsf(distance))
    monster->pursue_next = true;
  if (new_goal && direct_distance > 0.0f) {
    qa_trace_result center;
    if (!pursuit_trace(context, monster->last_sighting, true, &center, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (center.fraction < 1.0f) {
      float probe_distance =
          direct_distance * (center.fraction + 1.0f) * 0.5f;
      monster->ideal_yaw = vector_yaw(
          qa_vec_sub(monster->last_sighting, context->body.origin));
      qa_vec3 forward, right;
      qa_vec3 angles = context->body.angles;
      angles.y = monster->ideal_yaw;
      qa_builtin_angle_vectors(angles, &forward, &right, NULL);
      qa_vec3 left_goal = qa_vec_add(
          context->body.origin,
          qa_vec_add(qa_vec_scale(forward, probe_distance),
                     qa_vec_scale(right, -16.0f)));
      qa_vec3 right_goal = qa_vec_add(
          context->body.origin,
          qa_vec_add(qa_vec_scale(forward, probe_distance),
                     qa_vec_scale(right, 16.0f)));
      qa_trace_result left, right_trace;
      if (!pursuit_trace(context, left_goal, true, &left, error) ||
          !q2m_alive(context) ||
          !pursuit_trace(context, right_goal, true, &right_trace, error))
        return false;
      if (!q2m_alive(context))
        return true;
      float center_fraction =
          direct_distance * center.fraction / probe_distance;
      float side = left.fraction >= center_fraction &&
                           left.fraction > right_trace.fraction
                       ? -16.0f
                   : right_trace.fraction >= center_fraction &&
                             right_trace.fraction > left.fraction
                       ? 16.0f
                       : 0.0f;
      if (side != 0.0f) {
        float fraction = side < 0.0f ? left.fraction : right_trace.fraction;
        monster->saved_goal = monster->last_sighting;
        monster->has_saved_goal = true;
        monster->pursue_temporary = true;
        monster->last_sighting = qa_vec_add(
            context->body.origin,
            qa_vec_add(qa_vec_scale(
                           forward,
                           fraction < 1.0f
                               ? probe_distance * fraction * 0.5f
                               : probe_distance),
                       qa_vec_scale(right, side)));
        monster->ideal_yaw = vector_yaw(
            qa_vec_sub(monster->last_sighting, context->body.origin));
      }
      if (!set_body_yaw(context, monster->ideal_yaw, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
  }
  *goal = monster->last_sighting;
  return true;
}

static bool try_step(q2m_context *context, float yaw, float distance,
                     bool *moved, qa_error *error) {
  if (!q2m_alive(context)) {
    *moved = true;
    return true;
  }
  return qa_physics_step_direction(context->game->services.physics,
                                   context->actor->id, yaw, distance,
                                   context->elapsed, moved, error);
}

static bool chase_goal(q2m_context *context, qa_vec3 goal, float distance,
                       bool *moved, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  float old = truncf(monster->ideal_yaw / 45.0f) * 45.0f;
  float turnaround = qa_builtin_angle_mod(old - 180.0f);
  qa_vec3 delta = qa_vec_sub(goal, context->body.origin);
  float x = delta.x > 10.0f ? 0.0f : delta.x < -10.0f ? 180.0f : -1.0f;
  float y = delta.y < -10.0f ? 270.0f : delta.y > 10.0f ? 90.0f : -1.0f;
  *moved = false;
  if (x >= 0.0f && y >= 0.0f) {
    float diagonal = x == 0.0f ? (y == 90.0f ? 45.0f : 315.0f)
                               : (y == 90.0f ? 135.0f : 215.0f);
    if (diagonal != turnaround &&
        !try_step(context, diagonal, distance, moved, error))
      return false;
    if (*moved || !q2m_alive(context))
      return true;
  }
  unsigned roll = (unsigned)floorf(q2m_random(context->game) * 4.0f);
  bool rogue = context->game->options.product == QA_Q2_ROGUE;
  if ((rogue ? (roll & 1u) != 0 : roll != 0) ||
      fabsf(delta.y) > fabsf(delta.x)) {
    float swap = x;
    x = y;
    y = swap;
  }
  const float directions[] = {x, y, old};
  for (size_t index = 0; index < sizeof(directions) / sizeof(directions[0]);
       ++index) {
    if (directions[index] < 0.0f || directions[index] == turnaround)
      continue;
    if (!try_step(context, directions[index], distance, moved, error))
      return false;
    if (*moved || !q2m_alive(context))
      return true;
  }
  bool descending = floorf(q2m_random(context->game) * 2.0f) == 0.0f;
  for (unsigned index = 0; index < 8; ++index) {
    float yaw = descending ? 315.0f - 45.0f * (float)index
                           : 45.0f * (float)index;
    if (yaw == turnaround)
      continue;
    if (!try_step(context, yaw, distance, moved, error))
      return false;
    if (*moved || !q2m_alive(context))
      return true;
  }
  if (!try_step(context, turnaround, distance, moved, error))
    return false;
  if (*moved || !q2m_alive(context))
    return true;
  monster->ideal_yaw = old;
  context->actor->physics.ideal_yaw = old;
  bool supported;
  if (!qa_physics_check_bottom(context->game->services.physics,
                               context->actor->id, context->body.origin,
                               &supported, error))
    return false;
  if (!supported)
    context->actor->physics.flags |= QA_PHYSICS_PARTIAL_GROUND;
  return true;
}

bool q2m_move_to_goal(q2m_context *context, float distance, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (monster->definition->locomotion == Q2M_STATIONARY)
    return true;
  if (monster->definition->locomotion == Q2M_WALK &&
      context->body.ground.registry == 0)
    return true;
  bool handled;
  if (!alternate_fly(context, &handled, error))
    return false;
  if (handled)
    return true;
  qa_actor_id goal_actor =
      monster->goal.registry != 0 ? monster->goal : monster->enemy;
  qa_vec3 goal = context->body.origin;
  qa_body_state target = {0};
  qa_error ignored = {0};
  bool has_goal = goal_actor.registry != 0 &&
                  qa_world_body_read(context->game->services.world, goal_actor,
                                     &target, &ignored);
  if (has_goal)
    goal = target.origin;
  bool ordinary_enemy = !monster->hint_path && !monster->combat_point &&
                        !monster->sound_target.present &&
                        monster->enemy.registry != 0;
  if (ordinary_enemy) {
    bool visible;
    if (!q2m_visible(context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!visible) {
      if (!pursuit_goal(context, distance, &goal, error))
        return false;
      if (!q2m_alive(context))
        return true;
      has_goal = true;
    } else if (qa_world_body_read(context->game->services.world,
                                  monster->enemy, &target, &ignored)) {
      monster->lost_sight = false;
      monster->last_sighting = target.origin;
      monster->trail_ns = context->game->now_ns;
      goal = target.origin;
      has_goal = true;
    }
  }
  if (!has_goal)
    return true;
  monster->ideal_yaw = vector_yaw(qa_vec_sub(goal, context->body.origin));
  qa_actor_id hazard = {0};
  if (!qa_q2_bad_area(context->game, context->actor->id, context->body.origin,
                      &hazard, error))
    return false;
  if (hazard.registry != 0) {
    qa_body_state bad;
    if (qa_world_body_read(context->game->services.world, hazard, &bad,
                           &ignored))
      monster->ideal_yaw =
          vector_yaw(qa_vec_sub(context->body.origin, bad.origin));
    monster->hazard = hazard;
  } else {
    monster->hazard = (qa_actor_id){0};
  }
  context->actor->physics.goal = goal_actor;
  context->actor->physics.enemy = monster->enemy;
  context->actor->physics.ideal_yaw = monster->ideal_yaw;
  if (ordinary_enemy && qa_world_body_read(context->game->services.world,
                                           monster->enemy, &target, &ignored) &&
      boxes_close(&context->body, &target, fabsf(distance)))
    return true;
  bool moved = false;
  unsigned direct_roll = (unsigned)floorf(q2m_random(context->game) * 4.0f);
  if ((direct_roll != 1 ||
       (context->game->options.product == QA_Q2_ROGUE &&
        monster->charging)) &&
      !try_step(context, monster->ideal_yaw, distance, &moved, error))
    return false;
  if (!moved && q2m_alive(context) &&
      !chase_goal(context, goal, distance, &moved, error))
    return false;
  return !q2m_alive(context) || q2m_refresh(context, error);
}

static bool frame_move(q2m_context *context, float distance, qa_error *error) {
  if (!qa_physics_walk_move(context->game->services.physics, context->actor->id,
                            context->body.angles.y, distance, context->elapsed,
                            true, true, &(bool){false}, error))
    return false;
  return !q2m_alive(context) || q2m_refresh(context, error);
}

static bool prone_shot(q2m_context *context, bool *eligible, qa_error *error) {
  *eligible = false;
  qa_actor_id enemy = context->monster->enemy;
  qa_body_state target;
  qa_combat_state combat;
  if (!q2_actor_live(context->game, enemy))
    return true;
  qa_error observed = {0};
  if (!qa_world_body_read(context->game->services.world, enemy, &target, &observed) ||
      !qa_combat_read(context->game->services.combat, enemy, &combat, &observed)) {
    if (observed.code == QA_ERROR_NOT_FOUND)
      return true;
    if (error)
      *error = observed;
    return false;
  }
  if (combat.health <= 0 || !q2m_alive(context))
    return true;
  qa_vec3 difference = qa_vec_sub(target.origin, context->body.origin);
  difference.z = 0;
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  *eligible = qa_vec_dot(forward, qa_vec_normalize(difference)) >= 0.8f;
  return true;
}

bool q2m_run_ai(q2m_context *context, q2m_ai_kind kind, const char *source_ai,
                float distance, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if ((kind == Q2M_AI_STAND || kind == Q2M_AI_RUN) &&
      (monster->definition->species == Q2M_MEDIC ||
       monster->definition->species == Q2M_MEDIC_COMMANDER)) {
    bool acquired;
    if (!q2m_medic_acquire(context, true, &acquired, error))
      return false;
    if (!q2m_alive(context) || acquired)
      return true;
  }
  switch (kind) {
  case Q2M_AI_NONE:
    return true;
  case Q2M_AI_STAND: {
    if (distance != 0 && !frame_move(context, distance, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (monster->stand_ground && monster->enemy.registry != 0) {
      qa_body_state enemy;
      if (qa_world_body_read(context->game->services.world, monster->enemy, &enemy, NULL))
        monster->ideal_yaw = vector_yaw(qa_vec_sub(enemy.origin, context->body.origin));
      if (!q2m_alive(context))
        return true;
      if (context->body.angles.y != monster->ideal_yaw && monster->temporary_stand_ground) {
        monster->stand_ground = false;
        monster->temporary_stand_ground = false;
        if (!q2m_set_move(context, monster->definition->run_move, false, error))
          return false;
      }
      if ((context->game->options.product != QA_Q2_ROGUE || !monster->manual_steering) &&
          !q2m_change_yaw(context, error))
        return false;
      if (!q2m_alive(context))
        return true;
      bool selected = false;
      if (!q2m_check_attack(context, &selected, error))
        return false;
      return !q2m_alive(context) || !selected || attack_selected(context, error);
    }
    bool found;
    if (!q2m_find_target(context, &found, error))
      return false;
    if (!q2m_alive(context) || found || monster->stand_ground)
      return true;
    if (context->game->now_ns > monster->pause_ns)
      return q2m_set_move(context, monster->definition->walk_move, false, error);
    if (monster->definition->idle_sound && !(monster->spawnflags & 1u) &&
        context->game->now_ns > monster->idle_ns) {
      bool play = monster->idle_ns != 0;
      if (play && !q2m_sound(context, monster->definition->idle_sound, 2, 2.0f, error))
        return false;
      if (q2m_alive(context))
        monster->idle_ns = q2m_after(context->game->now_ns,
                                     (play ? 15.0 : 0.0) + q2m_random(context->game) * 15.0);
    }
    return true;
  }
  case Q2M_AI_WALK: {
    if (!q2m_move_to_goal(context, distance, error))
      return false;
    bool found;
    return !q2m_alive(context) || q2m_find_target(context, &found, error);
  }
  case Q2M_AI_RUN: {
    if (monster->combat_point)
      return q2m_move_to_goal(context, distance, error);
    if (monster->enemy.registry == 0) {
      bool found;
      if (!q2m_find_target(context, &found, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    if (monster->enemy.registry != 0) {
      bool selected;
      if (!q2m_check_attack(context, &selected, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (selected)
        return attack_selected(context, error);
    }
    return q2m_move_to_goal(context, distance, error);
  }
  case Q2M_AI_CHARGE:
    if ((context->game->options.edition == QA_Q2_RERELEASE ||
         context->game->options.product == QA_Q2_ROGUE) &&
        monster->enemy.registry == 0)
      return true;
    if (!(monster->manual_steering ? q2m_change_yaw(context, error)
                                   : q2m_face_enemy(context, error)))
      return false;
    if (!q2m_alive(context))
      return true;
    if (distance == 0.0f)
      return true;
    if ((context->game->options.edition == QA_Q2_RERELEASE ||
         context->game->options.product == QA_Q2_ROGUE) &&
        monster->charging)
      return q2m_move_to_goal(context, distance, error);
    if ((context->game->options.edition == QA_Q2_RERELEASE ||
         context->game->options.product == QA_Q2_ROGUE) &&
        monster->attack_state == Q2M_SLIDING) {
      float side = monster->lefty ? 90.0f : -90.0f;
      float sideways = context->game->options.edition == QA_Q2_CLASSIC
                           ? distance
                           : distance * monster->move->sidestep_scale;
      bool moved = false;
      if (!qa_physics_walk_move(context->game->services.physics,
                                context->actor->id, monster->ideal_yaw + side,
                                sideways, context->elapsed, true, true, &moved,
                                error))
        return false;
      if (!q2m_alive(context) || moved)
        return true;
      monster->lefty = !monster->lefty;
      return qa_physics_walk_move(context->game->services.physics,
                                  context->actor->id, monster->ideal_yaw - side,
                                  sideways, context->elapsed, true, true,
                                  &moved, error);
    }
    return qa_physics_walk_move(context->game->services.physics,
                                context->actor->id, context->body.angles.y,
                                distance, context->elapsed, true, true,
                                &(bool){false}, error);
  case Q2M_AI_MOVE:
    return frame_move(context, distance, error);
  case Q2M_AI_SOLDIER_MOVE: {
    if (!frame_move(context, distance, error))
      return false;
    if (!q2m_alive(context))
      return true;
    bool prone;
    if (!prone_shot(context, &prone, error))
      return false;
    return !q2m_alive(context) || prone || q2m_dispatch(context, "soldier_stand_up", error);
  }
  case Q2M_AI_TURN: {
    if (distance != 0 && !frame_move(context, distance, error))
      return false;
    if (!q2m_alive(context))
      return true;
    bool found;
    if (!q2m_find_target(context, &found, error))
      return false;
    return !q2m_alive(context) || found ||
           (context->game->options.edition == QA_Q2_RERELEASE && monster->manual_steering) ||
           q2m_change_yaw(context, error);
  }
  case Q2M_AI_SOURCE:
    if (source_ai == NULL)
      return true;
    if (strcmp(source_ai, "ai_stand2") == 0)
      return q2m_run_ai(context, Q2M_AI_STAND, NULL, distance, error);
    if (strcmp(source_ai, "ai_movetogoal") == 0)
      return q2m_move_to_goal(context, distance, error);
    if (strcmp(source_ai, "ai_facing") == 0)
      return q2m_face_enemy(context, error);
    if (strcmp(source_ai, "ai_move2") == 0) {
      if (distance != 0.0f &&
          !qa_physics_walk_move(context->game->services.physics,
                                context->actor->id, context->body.angles.y,
                                distance, context->elapsed, true, true,
                                &(bool){false}, error))
        return false;
      return !q2m_alive(context) || q2m_change_yaw(context, error);
    }
    if (strcmp(source_ai, "ai_move_slide_left") == 0 ||
        strcmp(source_ai, "ai_move_slide_right") == 0) {
      float yaw = context->body.angles.y +
                  (strstr(source_ai, "left") != NULL ? 90.0f : -90.0f);
      return qa_physics_walk_move(
          context->game->services.physics, context->actor->id, yaw, distance,
          context->elapsed, true, true, &(bool){false}, error);
    }
    if (strcmp(source_ai, "parasite_charge_proboscis") == 0)
      return q2m_parasite_charge(context, distance, error);
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "%s references unsupported source AI %s",
                 monster->definition->classname, source_ai);
    return false;
  }
  return true;
}
