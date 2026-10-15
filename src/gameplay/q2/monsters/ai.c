#include "qa/q2_sound.h"
#include "qa/allocation_gate.h"
#include "internal.h"
#include "reinforcements.h"
#include "medic.h"
#include "../entities/internal.h"
#include "../entities/hints.h"

enum { Q2M_TRAIL_POINTS = 8 };

static float vector_yaw(qa_vec3 direction);
static bool trail_length(qa_q2_game *, qa_actor_reference, size_t *, qa_error *);
static bool attack_body(q2m_context *, qa_actor_id, qa_body_state *, bool *,
                        qa_error *);
static void attack_enemy(q2m_context *, qa_actor_id);

typedef struct q2m_player_trail {
  qa_actor_id actor;
  qa_actor_reference head, tail;
  size_t count;
  size_t actor_next;
  uint64_t updated_ns;
  bool updated;
} q2m_player_trail;

typedef struct q2m_alert {
  qa_actor_id player, observer;
  uint64_t time_ns, hostile_ns;
  size_t observer_previous, observer_next;
} q2m_alert;

struct q2_monsters_runtime {
  struct qa_q2_monster *retired;
  qa_q2_monster_services services;
  qa_arena perception_storage;
  q2m_player_trail *trails;
  size_t trail_count, trail_capacity;
  size_t no_actor_trail;
  qa_actor_index trail_index;
  q2m_alert *alerts;
  size_t alert_count, alert_capacity;
  size_t no_player_alert;
  qa_actor_index alert_index, observer_index;
  qa_actor_id sight_client, sight_observer, disguise_violator;
  qa_actor_reference classic_trail[Q2M_TRAIL_POINTS];
  uint32_t classic_trail_head;
  bool classic_trail_active;
  uint64_t sight_time_ns, last_frame_ns, disguise_until_ns;
  bool began_frame;
};

static void index_trails(q2_monsters_runtime *runtime) {
  qa_actor_index_clear(&runtime->trail_index);
  runtime->no_actor_trail = SIZE_MAX;
  for (size_t index = runtime->trail_count; index-- > 0;) {
    q2m_player_trail *trail = &runtime->trails[index];
    trail->actor_next = SIZE_MAX;
    if (!trail->actor.registry) {
      trail->actor_next = runtime->no_actor_trail;
      runtime->no_actor_trail = index;
      continue;
    }
    size_t next;
    if (qa_actor_index_find(&runtime->trail_index, trail->actor, &next))
      trail->actor_next = next;
    qa_actor_index_put(&runtime->trail_index, trail->actor, index);
  }
}

static void index_alerts(q2_monsters_runtime *runtime) {
  qa_actor_index_clear(&runtime->alert_index);
  qa_actor_index_clear(&runtime->observer_index);
  runtime->no_player_alert = SIZE_MAX;
  for (size_t index = runtime->alert_count; index-- > 0;) {
    q2m_alert *alert = &runtime->alerts[index];
    if (alert->player.registry)
      qa_actor_index_put(&runtime->alert_index, alert->player, index);
    else runtime->no_player_alert = index;
    alert->observer_previous = alert->observer_next = SIZE_MAX;
    if (!alert->observer.registry) continue;
    size_t next;
    if (qa_actor_index_find(&runtime->observer_index, alert->observer, &next)) {
      alert->observer_next = next;
      runtime->alerts[next].observer_previous = index;
    }
    qa_actor_index_put(&runtime->observer_index, alert->observer, index);
  }
}

static void alert_observer(q2_monsters_runtime *runtime, size_t index, qa_actor_id observer) {
  q2m_alert *alert = &runtime->alerts[index];
  if (alert->observer.registry) {
    if (alert->observer_previous != SIZE_MAX)
      runtime->alerts[alert->observer_previous].observer_next = alert->observer_next;
    else if (alert->observer_next != SIZE_MAX)
      qa_actor_index_put(&runtime->observer_index, alert->observer, alert->observer_next);
    else qa_actor_index_remove(&runtime->observer_index, alert->observer);
    if (alert->observer_next != SIZE_MAX)
      runtime->alerts[alert->observer_next].observer_previous = alert->observer_previous;
  }
  alert->observer = observer;
  alert->observer_previous = alert->observer_next = SIZE_MAX;
  if (!observer.registry) return;
  size_t next;
  if (qa_actor_index_find(&runtime->observer_index, observer, &next)) {
    alert->observer_next = next;
    runtime->alerts[next].observer_previous = index;
  }
  qa_actor_index_put(&runtime->observer_index, observer, index);
}

static bool campaign_count(void *context, qa_actor_id id,
                           qa_q2_monster_count kind, qa_error *error) {
  qa_q2_game *game = context;
  q2_actor *actor = game && id.slot < game->capacity ? game->actors[id.slot] : NULL;
  if (!game || !game->entity_runtime || !actor || !actor->monster ||
      !qa_actor_id_equal(actor->id, id) || !q2_actor_live(game, id)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, id.slot,
                 "Q2 monster count lost its actual Source campaign owner and actor");
    return false;
  }
  return q2_campaign_monster_count(game, kind, error);
}

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

bool qa_q2_monsters_bind_missions(qa_q2_game *game, const qa_monster_missions *missions, qa_error *error) {
  if (!game || !missions || !q2_monsters_init(game, error)) return false;
  game->monster_runtime->services.missions = *missions;
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
  if (!services->count(services->context, context->actor->id, kind, error))
    return false;
  return services->count == campaign_count ||
      q2_campaign_monster_count(context->game, kind, error);
}

static void perception_storage_close(q2_monsters_runtime *runtime) {
  qa_actor_index_destroy(&runtime->trail_index);
  qa_actor_index_destroy(&runtime->alert_index);
  qa_actor_index_destroy(&runtime->observer_index);
  qa_arena_destroy(&runtime->perception_storage);
}

static bool perception_storage_prepare(qa_q2_game *game, q2_monsters_runtime *runtime,
                                        size_t retained_trails, size_t retained_alerts,
                                        qa_error *error) {
  const qa_actor_registry *registry = qa_session_actors(game->services.session);
  size_t live_rows = (size_t)qa_actors_capacity(registry) + 1;
  if (retained_trails > SIZE_MAX - live_rows ||
      retained_alerts > SIZE_MAX - live_rows) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Q2 monster perception storage is too large");
    return false;
  }
  runtime->trail_capacity = retained_trails + live_rows;
  runtime->alert_capacity = retained_alerts + live_rows;
  if (runtime->trail_capacity > SIZE_MAX / sizeof(*runtime->trails) ||
      runtime->alert_capacity > SIZE_MAX / sizeof(*runtime->alerts)) {
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Q2 monster perception storage is too large");
    return false;
  }
  qa_arena_init(&runtime->perception_storage, 0);
  runtime->trails = qa_arena_alloc(&runtime->perception_storage,
      runtime->trail_capacity * sizeof(*runtime->trails), _Alignof(q2m_player_trail), error);
  runtime->alerts = qa_arena_alloc(&runtime->perception_storage,
      runtime->alert_capacity * sizeof(*runtime->alerts), _Alignof(q2m_alert), error);
  if (!runtime->trails || !runtime->alerts ||
      !qa_actor_index_create(&runtime->trail_index, registry, &runtime->perception_storage, error) ||
      !qa_actor_index_create(&runtime->alert_index, registry, &runtime->perception_storage, error) ||
      !qa_actor_index_create(&runtime->observer_index, registry, &runtime->perception_storage, error))
    return false;
  memset(runtime->trails, 0, runtime->trail_capacity * sizeof(*runtime->trails));
  memset(runtime->alerts, 0, runtime->alert_capacity * sizeof(*runtime->alerts));
  qa_arena_seal(&runtime->perception_storage);
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
  game->monster_runtime->no_actor_trail = SIZE_MAX;
  game->monster_runtime->no_player_alert = SIZE_MAX;
  if (!perception_storage_prepare(game, game->monster_runtime, 0, 0, error)) {
    q2_monsters_close(game);
    return false;
  }
  if (!q2m_definitions_bind(game, error)) {
    q2_monsters_close(game);
    return false;
  }
  game->monster_runtime->services = (qa_q2_monster_services){
      .context = game, .count = campaign_count};
  return true;
}

void q2_monsters_close(qa_q2_game *game) {
  if (game == NULL || game->monster_runtime == NULL)
    return;
  q2_monsters_reclaim(game);
  perception_storage_close(game->monster_runtime);
  free(game->monster_definitions);
  game->monster_definitions = NULL; game->monster_definition_count = 0;
  free(game->monster_runtime);
  game->monster_runtime = NULL;
}

void q2_monsters_begin_map(qa_q2_game *game) {
  q2_monsters_reclaim(game);
  q2_monsters_runtime *runtime = game->monster_runtime;
  qa_actor_index_clear(&runtime->trail_index);
  qa_actor_index_clear(&runtime->alert_index);
  qa_actor_index_clear(&runtime->observer_index);
  *runtime = (q2_monsters_runtime){.services = runtime->services,
      .perception_storage = runtime->perception_storage,
      .trails = runtime->trails, .trail_capacity = runtime->trail_capacity,
      .trail_index = runtime->trail_index, .no_actor_trail = SIZE_MAX,
      .alerts = runtime->alerts, .alert_capacity = runtime->alert_capacity,
      .alert_index = runtime->alert_index, .observer_index = runtime->observer_index,
      .no_player_alert = SIZE_MAX};
}

void q2m_retire_monster(qa_q2_game *game, struct qa_q2_monster *monster) {
  monster->retired_next = game->monster_runtime->retired;
  game->monster_runtime->retired = monster;
}

void q2_monsters_reclaim(qa_q2_game *game) {
  if (!game || !game->monster_runtime)
    return;
  while (game->monster_runtime->retired) {
    struct qa_q2_monster *monster = game->monster_runtime->retired;
    game->monster_runtime->retired = monster->retired_next;
    q2m_free_monster(monster);
  }
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
      .sight_time_ns = runtime->sight_time_ns,
      .last_frame_ns = runtime->last_frame_ns,
      .disguise_until_ns = runtime->disguise_until_ns,
      .began_frame = runtime->began_frame,
      .trail_count = game->options.rules == QA_RULESET_Q2_RERELEASE ? runtime->trail_count : 0,
      .alert_count = runtime->alert_count,
      .classic_trail_head = runtime->classic_trail_head,
      .classic_trail_active = runtime->classic_trail_active,
  };
  memcpy(saved.classic_trail, runtime->classic_trail, sizeof(saved.classic_trail));
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
                         &saved.sight_observer, error) ||
      !q2_save_reference(game, runtime->disguise_violator,
                         &saved.disguise_violator, error)) {
    qa_q2_monsters_checkpoint_free(&saved);
    return false;
  }
  for (size_t index = 0; index < saved.trail_count; ++index) {
    const q2m_player_trail *source = &runtime->trails[index];
    qa_q2_monster_trail_checkpoint *target = &saved.trails[index];
    target->head = source->head;
    target->tail = source->tail;
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
      (saved->trail_count != 0 && saved->trails == NULL) ||
      (saved->alert_count != 0 && saved->alerts == NULL) ||
      saved->classic_trail_head >= Q2M_TRAIL_POINTS ||
      saved->trail_count > SIZE_MAX / sizeof(q2m_player_trail) ||
      saved->alert_count > SIZE_MAX / sizeof(q2m_alert)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Invalid Q2 monster perception checkpoint");
    return false;
  }
  if (!q2_checkpoint_idle(game, error))
    return false;

  q2_monsters_runtime prepared = {0};
  if (!perception_storage_prepare(game, &prepared, saved->trail_count,
                                  saved->alert_count, error)) goto failure;

  qa_actor_id sight_client = {0}, sight_observer = {0}, disguise_violator = {0};
  if (!q2_resolve_reference(game, saved->sight_client, &sight_client, error) ||
      !q2_resolve_reference(game, saved->sight_observer, &sight_observer,
                            error) ||
      !q2_resolve_reference(game, saved->disguise_violator, &disguise_violator,
                            error))
    goto failure;
  for (size_t index = 0; index < saved->trail_count; ++index) {
    const qa_q2_monster_trail_checkpoint *source = &saved->trails[index];
    q2m_player_trail *target = &prepared.trails[index];
    if (!q2_resolve_reference(game, source->actor, &target->actor, error))
      goto failure;
    target->head = source->head;
    target->tail = source->tail;
    if (!trail_length(game, target->tail, &target->count, error)) goto failure;
  }
  for (size_t index = 0; index < saved->alert_count; ++index) {
    const qa_q2_monster_alert_checkpoint *source = &saved->alerts[index];
    q2m_alert *target = &prepared.alerts[index];
    if (!q2_resolve_reference(game, source->player, &target->player, error) ||
        !q2_resolve_reference(game, source->observer, &target->observer,
                              error))
      goto failure;
    target->time_ns = source->time_ns;
    target->hostile_ns = source->hostile_ns;
  }

  q2_monsters_runtime *runtime = game->monster_runtime;
  perception_storage_close(runtime);
  runtime->perception_storage = prepared.perception_storage;
  runtime->trails = prepared.trails;
  runtime->trail_count = saved->trail_count;
  runtime->trail_capacity = prepared.trail_capacity;
  runtime->trail_index = prepared.trail_index;
  runtime->alerts = prepared.alerts;
  runtime->alert_count = saved->alert_count;
  runtime->alert_capacity = prepared.alert_capacity;
  runtime->alert_index = prepared.alert_index;
  runtime->observer_index = prepared.observer_index;
  runtime->trail_index.retained.storage = &runtime->perception_storage;
  runtime->alert_index.retained.storage = &runtime->perception_storage;
  runtime->observer_index.retained.storage = &runtime->perception_storage;
  index_trails(runtime);
  index_alerts(runtime);
  runtime->sight_client = sight_client;
  runtime->sight_observer = sight_observer;
  runtime->disguise_violator = disguise_violator;
  runtime->disguise_until_ns = saved->disguise_until_ns;
  runtime->sight_time_ns = saved->sight_time_ns;
  runtime->last_frame_ns = saved->last_frame_ns;
  runtime->began_frame = saved->began_frame;
  memcpy(runtime->classic_trail, saved->classic_trail, sizeof(runtime->classic_trail));
  runtime->classic_trail_head = saved->classic_trail_head;
  runtime->classic_trail_active = saved->classic_trail_active;
  return true;

failure:
  perception_storage_close(&prepared);
  return false;
}

static void remove_trail(q2_monsters_runtime *runtime, size_t index) {
  if (index + 1 < runtime->trail_count)
    memmove(&runtime->trails[index], &runtime->trails[index + 1],
            (runtime->trail_count - index - 1) * sizeof(runtime->trails[0]));
  --runtime->trail_count;
  index_trails(runtime);
}

static void remove_alert(q2_monsters_runtime *runtime, size_t index) {
  if (index + 1 < runtime->alert_count)
    memmove(&runtime->alerts[index], &runtime->alerts[index + 1],
            (runtime->alert_count - index - 1) * sizeof(runtime->alerts[0]));
  --runtime->alert_count;
  index_alerts(runtime);
}

void q2_monsters_release_actor(qa_q2_game *game, qa_actor_id actor) {
  if (game == NULL || game->monster_runtime == NULL)
    return;
  q2_monsters_runtime *runtime = game->monster_runtime;
  size_t index;
  if (qa_actor_index_find(&runtime->trail_index, actor, &index) &&
      !q2_player_trail_destroy(game, actor, &game->release_error))
    game->release_failed = true;
  if (qa_actor_id_equal(runtime->sight_client, actor))
    runtime->sight_client = (qa_actor_id){0};
  if (qa_actor_id_equal(runtime->sight_observer, actor)) {
    runtime->sight_observer = (qa_actor_id){0};
    runtime->sight_time_ns = 0;
  }
  if (qa_actor_id_equal(runtime->disguise_violator, actor)) {
    runtime->disguise_violator = (qa_actor_id){0};
    runtime->disguise_until_ns = 0;
  }
  while (qa_actor_index_find(&runtime->trail_index, actor, &index))
    remove_trail(runtime, index);
  while (qa_actor_index_find(&runtime->alert_index, actor, &index))
    remove_alert(runtime, index);
  while (qa_actor_index_find(&runtime->observer_index, actor, &index))
    remove_alert(runtime, index);
}

static q2m_player_trail *trail_for(q2_monsters_runtime *runtime,
                                   qa_actor_id actor, bool create,
                                   qa_error *error) {
  size_t index;
  if (actor.registry ? qa_actor_index_find(&runtime->trail_index, actor, &index) :
      ((index = runtime->no_actor_trail) != SIZE_MAX))
    return &runtime->trails[index];
  if (!create)
    return NULL;
  if (runtime->trail_count == runtime->trail_capacity) {
    qa_allocation_gate_capacity_exhausted();
    qa_error_set(error, QA_ERROR_MEMORY, runtime->trail_count,
                 "Q2 monster trail storage capacity exhausted");
    return NULL;
  }
  q2m_player_trail *trail = &runtime->trails[runtime->trail_count++];
  *trail = (q2m_player_trail){.actor = actor, .actor_next = SIZE_MAX};
  if (actor.registry) qa_actor_index_put(&runtime->trail_index, actor, runtime->trail_count - 1);
  else runtime->no_actor_trail = runtime->trail_count - 1;
  return trail;
}

static q2_actor *trail_node(qa_q2_game *game, qa_actor_id id) {
  q2_actor *node = q2_actor_get(game, id, false, NULL);
  return node && node->entity && node->entity->trail ? node : NULL;
}

static q2_actor *trail_reference(qa_q2_game *game, qa_actor_reference reference) {
  return trail_node(game, qa_actor_reference_resolve(
      qa_session_actors(game->services.session), reference));
}

static bool trail_length(qa_q2_game *game, qa_actor_reference marker,
                           size_t *count, qa_error *error) {
  *count = 0;
  while (qa_actor_reference_present(marker)) {
    q2_actor *node = trail_reference(game, marker);
    if (!node || *count == Q2M_TRAIL_POINTS) {
      qa_error_set(error, QA_ERROR_FORMAT, *count, "Invalid original Q2 client trail chain");
      return false;
    }
    ++*count;
    marker = node->entity->trail->newer;
  }
  return true;
}

static q2_actor *classic_node(qa_q2_game *game, uint32_t index) {
  return q2_actor_get(game, qa_actor_reference_resolve(
      qa_session_actors(game->services.session),
      game->monster_runtime->classic_trail[index]), false, NULL);
}

bool q2_player_trail_begin(qa_q2_game *game, qa_error *error) {
  if (!q2_monsters_init(game, error)) return false;
  q2_monsters_runtime *runtime = game->monster_runtime;
  if (game->options.rules == QA_RULESET_Q2_RERELEASE || game->options.deathmatch ||
      runtime->classic_trail_active) return true;
  for (uint32_t i = 0; i < Q2M_TRAIL_POINTS; ++i) {
    q2_actor *node;
    if (!q2_entity_native_spawn(game, "player_trail", &(qa_body_state){0},
                                Q2E_POINT, &node, error)) return false;
    node->physics.motion = QA_PHYSICS_STATIONARY;
    node->physics.solid = QA_PHYSICS_NOT_SOLID;
    node->physics_bound = true;
    runtime->classic_trail[i] = qa_actor_reference_from_actor(
        qa_session_actors(game->services.session), game->options.owner, node->id);
  }
  runtime->classic_trail_head = 0;
  runtime->classic_trail_active = true;
  return true;
}

void q2_player_trail_read_level(qa_q2_game *game) {
  if (game->options.rules == QA_RULESET_Q2_CLASSIC)
    game->monster_runtime->classic_trail_head = 0;
}

bool q2_player_trail_client(qa_q2_game *game, qa_actor_id player, bool reading,
                            qa_actor_reference *head, qa_actor_reference *tail, qa_error *error) {
  q2m_player_trail *trail = trail_for(game->monster_runtime, player, reading, error);
  if (!trail) {
    if (reading) return false;
    *head = *tail = (qa_actor_reference){0};
    return true;
  }
  if (!reading) { *head = trail->head; *tail = trail->tail; return true; }
  trail->head = *head;
  trail->tail = *tail;
  return trail_length(game, trail->tail, &trail->count, error);
}

bool q2_player_trail_destroy(qa_q2_game *game, qa_actor_id player, qa_error *error) {
  if (!game || game->options.rules != QA_RULESET_Q2_RERELEASE || !game->monster_runtime)
    return true;
  for (q2_actor *actor = game->first_actor, *next; actor; actor = next) {
    next = actor->live_next;
    if (trail_node(game, actor->id) && (!player.registry ||
        qa_actor_id_equal(qa_actor_reference_resolve(qa_session_actors(game->services.session),
            actor->entity->trail->owner), player)))
      if (!qa_session_release(game->services.session, actor->id, error)) return false;
  }
  q2_monsters_runtime *runtime = game->monster_runtime;
  size_t index;
  if (player.registry) {
    if (qa_actor_index_find(&runtime->trail_index, player, &index)) {
      do {
        runtime->trails[index].head = runtime->trails[index].tail = (qa_actor_reference){0};
        runtime->trails[index].count = 0;
        index = runtime->trails[index].actor_next;
      } while (index != SIZE_MAX);
    }
  } else for (index = 0; index < runtime->trail_count; ++index) {
    runtime->trails[index].head = runtime->trails[index].tail = (qa_actor_reference){0};
    runtime->trails[index].count = 0;
  }
  return true;
}

static bool trail_visible(qa_q2_game *game, qa_actor_id observer,
                           const qa_body_state *body, float view_height,
                           q2_actor *node, bool *visible, qa_error *error) {
  qa_body_state point;
  if (!node) { *visible = false; return true; }
  if (!qa_world_body_read(game->services.world, node->id, &point, error)) return false;
  qa_trace_query query = {
      .start = qa_vec_add(body->origin, qa_v3(0, 0, view_height)),
      .end = point.origin, .pass_actor = observer,
      .policy = qa_collision_ruleset_policy(game->options.rules)};
  query.policy.contents_mask = qa_collision_contents_mask(Q2M_OPAQUE_MASK, QA_GAME_Q2);
  qa_trace_result trace;
  if (!qa_world_trace(game->services.world, &query, &trace, error)) return false;
  *visible = trace.fraction == 1.0f;
  return true;
}

static bool trail_add(qa_q2_game *game, q2m_player_trail *trail,
                       qa_vec3 origin, qa_error *error) {
  q2_monsters_runtime *runtime = game->monster_runtime;
  q2_actor *node;
  qa_body_state body = {.origin = origin};
  if (game->options.rules == QA_RULESET_Q2_CLASSIC) {
    if (!runtime->classic_trail_active) return true;
    uint32_t index = runtime->classic_trail_head;
    node = classic_node(game, index);
    q2_actor *previous = classic_node(game, (index + Q2M_TRAIL_POINTS - 1) & 7u);
    qa_body_state previous_body;
    if (!node || !previous ||
        !qa_world_body_read(game->services.world, previous->id, &previous_body, error)) return false;
    body.angles.y = vector_yaw(qa_vec_sub(origin, previous_body.origin));
    runtime->classic_trail_head = (index + 1) & 7u;
  } else {
    if (trail->count == Q2M_TRAIL_POINTS) {
      node = trail_reference(game, trail->tail);
      q2_actor *next = node ? trail_reference(game, node->entity->trail->newer) : NULL;
      if (!next) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 player trail lost its actual tail");
        return false;
      }
      trail->tail = qa_actor_reference_from_actor(qa_session_actors(game->services.session), game->options.owner, next->id);
      next->entity->trail->older = (qa_actor_reference){0};
      node->entity->trail->newer = node->entity->trail->older = (qa_actor_reference){0};
    } else {
      if (!q2_entity_native_spawn(game, "player_trail", &body, Q2E_POINT, &node, error))
        return false;
      q2_entity_trail_prepare(node->entity);
      node->physics.motion = QA_PHYSICS_STATIONARY;
      node->physics.solid = QA_PHYSICS_NOT_SOLID;
      node->physics_bound = true;
      ++trail->count;
    }
    q2_actor *head = trail_reference(game, trail->head);
    qa_actor_reference reference = qa_actor_reference_from_actor(
        qa_session_actors(game->services.session), game->options.owner, node->id);
    if (head) head->entity->trail->newer = reference;
    node->entity->trail->older = trail->head;
    node->entity->trail->owner = qa_actor_reference_from_actor(
        qa_session_actors(game->services.session), game->options.owner, trail->actor);
    trail->head = reference;
    if (!qa_actor_reference_present(trail->tail)) trail->tail = reference;
  }
  node->entity->timestamp_ns = game->now_ns;
  return qa_world_body_write(game->services.world, node->id, &body, error);
}

bool q2_player_trail_step(qa_q2_game *game, qa_actor_id player, qa_error *error) {
  if (game->options.deathmatch || game->player_runtime->intermission) return true;
  qa_builtin_actor_traits traits = {0};
  qa_combat_state combat;
  if (!q2_actor_live(game, player) || !game->services.actor_traits ||
      !game->services.actor_traits(game->services.context, player, &traits) ||
      !traits.player || traits.spectator) return true;
  if (!qa_combat_read(game->services.combat, player, &combat, error)) return false;
  if (combat.health <= 0) return true;
  q2m_player_trail *trail = trail_for(game->monster_runtime, player, true, error);
  if (!trail) return false;
  if (trail->updated && trail->updated_ns == game->now_ns) return true;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, player, &body, error)) return false;
  bool rerelease = game->options.rules == QA_RULESET_Q2_RERELEASE;
  q2_monsters_runtime *runtime = game->monster_runtime;
  q2_actor *last = rerelease ? trail_reference(game, trail->head) :
      runtime->classic_trail_active ? classic_node(game,
          (runtime->classic_trail_head + Q2M_TRAIL_POINTS - 1) & 7u) : NULL;
  bool visible;
  if (!trail_visible(game, player, &body, traits.view_height, last, &visible, error))
    return false;
  q2_actor *client = q2_actor_get(game, player, false, NULL);
  bool eligible = !rerelease || (qa_actor_reference_present(body.ground) &&
      (!client || !client->client || !client->client->info.noclip));
  if (!visible && eligible && !trail_add(game, trail, body.origin, error)) return false;
  trail->updated_ns = game->now_ns;
  trail->updated = true;
  return true;
}

static bool trail_pick(q2m_context *context, bool next, q2_actor **out,
                        qa_error *error) {
  qa_q2_game *game = context->game;
  q2_monsters_runtime *runtime = game->monster_runtime;
  *out = NULL;
  if (game->options.rules == QA_RULESET_Q2_CLASSIC) {
    if (!runtime->classic_trail_active) return true;
    uint32_t index = runtime->classic_trail_head;
    for (unsigned i = 0; i < Q2M_TRAIL_POINTS; ++i) {
      q2_actor *node = classic_node(game, index);
      if (!node || !node->entity) return true;
      if (node->entity->timestamp_ns > context->monster->trail_ns) break;
      index = (index + 1) & 7u;
    }
    q2_actor *marker = classic_node(game, index);
    if (!next) {
      bool visible;
      if (!trail_visible(game, context->actor->id, &context->body,
                           context->monster->view_height, marker, &visible, error)) return false;
      if (!visible) {
        q2_actor *prior = classic_node(game, (index + Q2M_TRAIL_POINTS - 1) & 7u);
        if (!trail_visible(game, context->actor->id, &context->body,
                             context->monster->view_height, prior, &visible, error)) return false;
        if (visible) marker = prior;
      }
    }
    *out = marker;
    return true;
  }
  q2m_player_trail *trail = trail_for(runtime, context->monster->enemy, false, NULL);
  if (!trail) return true;
  q2_actor *marker = trail_reference(game, trail->head);
  while (marker && marker->entity->timestamp_ns <= context->monster->trail_ns)
    marker = trail_reference(game, marker->entity->trail->older);
  if (next) {
    q2_actor *closest = NULL;
    float closest_distance = INFINITY;
    for (q2_actor *node = marker; node; node = trail_reference(game, node->entity->trail->older)) {
      qa_body_state point;
      if (!qa_world_body_read(game->services.world, node->id, &point, error)) return false;
      qa_vec3 delta = qa_vec_sub(point.origin, context->body.origin);
      float distance = qa_vec_dot(delta, delta);
      if (distance < closest_distance) { closest = node; closest_distance = distance; }
    }
    marker = closest ? trail_reference(game, closest->entity->trail->newer) : NULL;
  } else {
    while (marker) {
      bool visible;
      if (!trail_visible(game, context->actor->id, &context->body,
                           context->monster->view_height, marker, &visible, error)) return false;
      if (visible) break;
      marker = trail_reference(game, marker->entity->trail->older);
    }
  }
  *out = marker;
  return true;
}

static q2m_alert *alert_for(q2_monsters_runtime *runtime, qa_actor_id player,
                            bool create, qa_error *error) {
  size_t index;
  if (player.registry ? qa_actor_index_find(&runtime->alert_index, player, &index) :
      ((index = runtime->no_player_alert) != SIZE_MAX))
    return &runtime->alerts[index];
  if (!create)
    return NULL;
  if (runtime->alert_count == runtime->alert_capacity) {
    qa_allocation_gate_capacity_exhausted();
    qa_error_set(error, QA_ERROR_MEMORY, runtime->alert_count,
                 "Q2 monster alert storage capacity exhausted");
    return NULL;
  }
  q2m_alert *alert = &runtime->alerts[runtime->alert_count++];
  *alert = (q2m_alert){.player = player,
      .observer_previous = SIZE_MAX, .observer_next = SIZE_MAX};
  if (player.registry) qa_actor_index_put(&runtime->alert_index, player, runtime->alert_count - 1);
  else runtime->no_player_alert = runtime->alert_count - 1;
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

void qa_q2_monsters_disguise_violated(qa_q2_game *game, qa_actor_id actor) {
  game->monster_runtime->disguise_violator = actor;
  game->monster_runtime->disguise_until_ns = q2_deadline(game->now_ns, 500 * Q2_MS);
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

  qa_world_actor_snapshot players = {0};
  if (!q2_player_roster(game, &players, error))
    return false;
  size_t count = players.count;
  ptrdiff_t current = runtime->sight_client.registry == 0 ? 0 : -1;
  if (runtime->sight_client.registry != 0)
    for (size_t index = 0; index < count; ++index)
      if (qa_actor_id_equal(players.actors[index],
                            runtime->sight_client)) {
        current = (ptrdiff_t)index;
        break;
      }
  runtime->sight_client = (qa_actor_id){0};
  for (size_t offset = 1; offset <= count; ++offset) {
    size_t index = (size_t)(current + (ptrdiff_t)offset) % count;
    qa_builtin_actor_traits traits;
    if (runtime_targetable(game, players.actors[index], &traits) && !traits.disguised) {
      runtime->sight_client = players.actors[index];
      break;
    }
  }

  bool result = true;
  for (size_t index = 0; index < count; ++index)
    if (!q2_player_trail_step(game, players.actors[index], error)) {
      result = false;
      break;
    }
  q2_scratch_release(game, &players);
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
  if (!runtime_targetable(context->game, target, &traits) ||
      !q2m_alive(context) || !q2_actor_live(context->game, target))
    return true;
  q2_monsters_runtime *runtime = context->game->monster_runtime;
  runtime->sight_observer = context->actor->id;
  runtime->sight_time_ns = context->game->now_ns;
  q2m_alert *alert = alert_for(runtime, target, true, error);
  if (alert == NULL)
    return false;
  alert_observer(runtime, (size_t)(alert - runtime->alerts), context->actor->id);
  alert->time_ns = context->game->now_ns;
  alert->hostile_ns = q2m_after(context->game->now_ns, 1.0);
  return true;
}

static float vector_yaw(qa_vec3 direction) {
  return qa_vec_angles(direction,QA_DIRECTION_FLOAT,QA_VECTOR_VIEW).y;
}

static bool actor_traits(q2m_context *context, qa_actor_id id,
                         qa_builtin_actor_traits *traits, qa_error *error) {
  *traits = (qa_builtin_actor_traits){0};
  if (qa_actors_get(qa_session_actors(context->game->services.session), id) ==
      NULL)
    return true;
  if (context->game->services.actor_traits != NULL &&
      context->game->services.actor_traits(context->game->services.context, id,
                                           traits))
    return true;
  *traits = (qa_builtin_actor_traits){0};
  qa_actor_collision collision;
  qa_error observed = {0};
  if (qa_world_get_collision(context->game->services.world, id, &collision, &observed)) {
    traits->monster = collision.monster;
    traits->player = ((uint32_t)qa_collision_contents_export(collision.contents, QA_GAME_Q2, collision.q1_opaque_token) & Q2_PLAYER_CONTENTS) != 0;
    traits->view_height = 22.0f;
  }
  if (observed.code && error)
    *error = observed;
  return observed.code == QA_OK;
}

static bool target_alive(q2m_context *context, qa_actor_id id,
                         qa_builtin_actor_traits *traits, qa_body_state *body,
                         bool *living, qa_error *error) {
  *living = false;
  if (!id.registry || qa_actor_id_equal(id, context->actor->id))
    return true;
  if (!actor_traits(context, id, traits, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, id) ||
      (!traits->player && !traits->monster && !traits->damageable_target) ||
      traits->spectator || traits->no_target)
    return true;
  qa_combat_state combat;
  qa_error observed = {0};
  if (!qa_combat_read(context->game->services.combat, id, &combat, &observed)) {
    if (observed.code == QA_ERROR_NOT_FOUND || !q2_actor_live(context->game, id))
      return true;
    if (error)
      *error = observed;
    return false;
  }
  if (!q2m_alive(context) || !q2_actor_live(context->game, id) || combat.health <= 0)
    return true;
  if (!qa_world_body_read(context->game->services.world, id, body, &observed)) {
    if (observed.code == QA_ERROR_NOT_FOUND || !q2_actor_live(context->game, id))
      return true;
    if (error)
      *error = observed;
    return false;
  }
  *living = q2m_alive(context) && q2_actor_live(context->game, id);
  return true;
}

static bool in_front(q2m_context *context, qa_actor_id id) {
  qa_body_state target;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &ignored))
    return false;
  qa_vec3 forward;
  qa_angle_vectors(context->body.angles, &forward, NULL, NULL,QA_ANGLE_FLOAT_MATH);
  return qa_vec_dot(forward, qa_vec_normalize(qa_vec_sub(
                                 target.origin, context->body.origin))) > 0.3f;
}

float q2m_body_distance(qa_ruleset_id edition, const qa_body_state *self,
                        const qa_body_state *target) {
  if (edition == QA_RULESET_Q2_CLASSIC)
    return qa_vec_length(qa_vec_sub(target->origin, self->origin));
  float x = fmaxf(
      0.0f, fmaxf(target->origin.x + target->bounds.mins.x -
                      self->origin.x - self->bounds.maxs.x,
                  self->origin.x + self->bounds.mins.x -
                      target->origin.x - target->bounds.maxs.x));
  float y = fmaxf(
      0.0f, fmaxf(target->origin.y + target->bounds.mins.y -
                      self->origin.y - self->bounds.maxs.y,
                  self->origin.y + self->bounds.mins.y -
                      target->origin.y - target->bounds.maxs.y));
  float z = fmaxf(
      0.0f, fmaxf(target->origin.z + target->bounds.mins.z -
                      self->origin.z - self->bounds.maxs.z,
                  self->origin.z + self->bounds.mins.z -
                      target->origin.z - target->bounds.maxs.z));
  return sqrtf(x * x + y * y + z * z);
}

float q2m_distance(q2m_context *context, qa_actor_id id) {
  qa_body_state target;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &ignored))
    return FLT_MAX;
  return q2m_body_distance(context->monster->definition->source_rules,
                            &context->body, &target);
}

bool q2m_visible(q2m_context *context, qa_actor_id id, bool *visible,
                 qa_error *error) {
  *visible = false;
  qa_builtin_actor_traits traits;
  qa_body_state target;
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
    return true;
  qa_error observed = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &observed)) {
    if (observed.code == QA_ERROR_NOT_FOUND || !q2_actor_live(context->game, id))
      return true;
    if (error)
      *error = observed;
    return false;
  }
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
    return true;
  if (!qa_world_body_read(context->game->services.world, context->actor->id,
                          &context->body, error))
    return !q2m_alive(context);
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
    return true;
  if (!actor_traits(context, id, &traits, error))
    return false;
  if (!q2m_alive(context) ||
      !q2_actor_live(context->game, id))
    return true;
  qa_trace_query query = {
      .start = qa_vec_add(context->body.origin,
                          qa_v3(0, 0, context->monster->view_height)),
      .end = qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height)),
      .pass_actor = context->actor->id,
      .policy = qa_collision_ruleset_policy(context->game->options.rules),
  };
  query.policy.contents_mask = qa_collision_contents_mask(Q2M_OPAQUE_MASK, QA_GAME_Q2);
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context))
    return true;
  *visible = q2_actor_live(context->game, id) && trace.fraction == 1.0f;
  return true;
}

bool q2m_clear_shot(q2m_context *context, qa_vec3 offset, bool *clear,
                    qa_error *error) {
  *clear = false;
  struct qa_q2_monster *monster = context->monster;
  qa_actor_id target_id = monster->enemy;
  bool rerelease = context->monster->definition->source_rules == QA_RULESET_Q2_RERELEASE;
  qa_builtin_actor_traits traits;
  qa_body_state target;
  bool alive;
  if (rerelease) {
    if (!attack_body(context, target_id, &target, &alive, error))
      return false;
    if (!q2m_alive(context) || !alive)
      return true;
    if (!actor_traits(context, target_id, &traits, error))
      return false;
  } else if (!target_alive(context, target_id, &traits, &target, &alive, error)) {
    return false;
  }
  if (!q2m_alive(context) || !alive || !q2_actor_live(context->game, target_id))
    return true;
  q2m_context projection = *context;
  projection.body.angles.y = monster->ideal_yaw;
  projection.body.angles.z = 0;
  qa_vec3 start = q2m_project_offset(&projection, offset);
  bool blind = monster->manual_steering || monster->attack_state == Q2M_BLIND ||
               (rerelease && monster->lost_sight);
  qa_vec3 end = blind ? monster->blind_fire_target
                      : qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height));
  qa_trace_query query = {
      .start = start,
      .end = end,
      .pass_actor = context->actor->id,
      .policy = qa_collision_ruleset_policy(context->game->options.rules),
  };
  query.policy.contents_mask = qa_collision_contents_mask(
      rerelease ? Q2_PROJECTILE_MASK & ~UINT32_C(0x04000000) : Q2M_ATTACK_MASK,
      QA_GAME_Q2);
  unsigned attempts = rerelease && !blind ? 2 : 1;
  for (unsigned attempt = 0; attempt < attempts; ++attempt) {
    if (attempt != 0)
      query.end = target.origin;
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if ((!rerelease && trace.fraction == 1.0f) ||
        (rerelease && trace.fraction > 0.8f && !trace.start_solid) ||
        (trace.hit == QA_TRACE_HIT_ACTOR &&
         qa_actor_id_equal(trace.actor, target_id))) {
      *clear = true;
      return true;
    }
    if (rerelease && trace.hit == QA_TRACE_HIT_ACTOR) {
      if (!actor_traits(context, trace.actor, &traits, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (traits.player) {
        *clear = true;
        return true;
      }
    }
  }
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

bool q2m_stand_state(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_move_id move = monster->definition->stand_move;
  if (monster->definition->species == Q2M_WIDOW) {
    if (!q2m_sound(context, QA_Q2_SFX_WIDOW_LAUGH, 1, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  bool floater = monster->definition->species == Q2M_FLOATER;
  if (floater) {
    if (context->game->options.rules == QA_RULESET_Q2_RERELEASE &&
        monster->move && monster->move->id == Q2M_MOVE_floater_move_disguise)
      move = Q2M_MOVE_floater_move_disguise;
    else
      move = q2m_random(context->game) <= .5f ? Q2M_MOVE_floater_move_stand1
                                           : Q2M_MOVE_floater_move_stand2;
  }
  return move == Q2M_MOVE_NONE || q2m_set_move(context, move, floater, error);
}

bool q2m_run_state(q2m_context *context, qa_error *error) {
    if (context->monster->definition->species == Q2M_FLOATER) {
      q2m_move_id move = context->game->options.rules == QA_RULESET_Q2_RERELEASE &&
          context->monster->move &&
          context->monster->move->id == Q2M_MOVE_floater_move_disguise
              ? Q2M_MOVE_floater_move_pop
              : context->monster->stand_ground ? Q2M_MOVE_floater_move_stand1
                                               : Q2M_MOVE_floater_move_run;
      return q2m_set_move(context, move, true, error);
    }
    bool tank_callback = context->monster->definition->species == Q2M_TANK ||
                         context->monster->definition->species == Q2M_TANK_COMMANDER;
    if (tank_callback && !q2m_callback_run(context, Q2M_CALLBACK_tank_run, error))
      return false;
    if (!q2m_alive(context))
      return true;
    bool actor_callback = context->monster->definition->species == Q2M_ACTOR;
    if (actor_callback && !q2m_callback_run(context,
        context->monster->stand_ground ? Q2M_CALLBACK_actor_stand : Q2M_CALLBACK_actor_run, error))
      return false;
    if (!q2m_alive(context)) return true;
    bool medic_callback = context->game->options.rules == QA_RULESET_Q2_RERELEASE &&
        (context->monster->definition->species == Q2M_MEDIC ||
         context->monster->definition->species == Q2M_MEDIC_COMMANDER);
    if (medic_callback && !q2m_callback_run(context,
        context->monster->stand_ground ? Q2M_CALLBACK_medic_stand : Q2M_CALLBACK_medic_run, error))
      return false;
    if (!q2m_alive(context)) return true;
    bool handled = false;
    if (!tank_callback && !actor_callback && !medic_callback && !context->monster->stand_ground &&
        !q2m_medic_callback(context, Q2M_CALLBACK_medic_run, &handled, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!tank_callback && !actor_callback && !medic_callback && !handled && !q2m_set_move(context,
        context->monster->stand_ground ? context->monster->definition->stand_move
                                      : context->monster->definition->run_move,
        context->monster->definition->species == Q2M_FIXBOT, error))
      return false;
  return true;
}

bool q2m_hunt_target(q2m_context *context, bool animate_state, qa_error *error) {
  qa_actor_id id = context->monster->enemy;
  if (!q2_actor_live(context->game, id))
    return true;
  context->monster->goal = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = qa_actor_reference_lifetime(id));
  if (animate_state &&
      !(context->monster->definition->species == Q2M_FLOATER &&
                context->monster->stand_ground
            ? q2m_stand_state(context, error)
            : q2m_run_state(context, error)))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_refresh(context, error))
    return !q2m_alive(context);
  if (!q2m_alive(context))
    return true;
  id = context->monster->enemy;
  if (!q2_actor_live(context->game, id))
    return true;
  qa_body_state target;
  qa_error observed = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &observed)) {
    if (!q2m_alive(context) || !q2_actor_live(context->game, id))
      return true;
    if (error)
      *error = observed;
    return false;
  }
  if (!q2m_alive(context) || !q2_actor_live(context->game, id) ||
      !qa_actor_id_equal(context->monster->enemy, id))
    return true;
  context->monster->ideal_yaw = vector_yaw(qa_vec_sub(target.origin, context->body.origin));
  if (context->game->options.rules == QA_RULESET_Q2_CLASSIC && !context->monster->stand_ground)
    context->monster->attack_ns = q2m_after(context->game->now_ns, 1);
  return true;
}

bool q2m_found_target(q2m_context *context, qa_actor_id id, qa_error *error) {
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
    return true;
  qa_body_state target;
  qa_error observed = {0};
  if (!qa_world_body_read(context->game->services.world, id, &target, &observed)) {
    if (observed.code == QA_ERROR_NOT_FOUND || !q2m_alive(context) ||
        !q2_actor_live(context->game, id))
      return true;
    if (error)
      *error = observed;
    return false;
  }
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
    return true;
  if (context->game->hooks.can_target != NULL) {
    bool allowed = context->game->hooks.can_target(context->game->hooks.context,
                                                   context->actor->id, id);
    if (!q2m_alive(context) || !q2_actor_live(context->game, id) || !allowed)
      return true;
  }
  qa_actor_player *player = qa_actors_player(qa_session_actors(context->game->services.session), id);
  if (player)
    player->disguised = false;
  if (!q2m_perception_alert(context, id, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
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
  if (context->game->options.rules == QA_RULESET_Q2_RERELEASE) {
    if (first_trail)
      monster->attack_ns = q2m_after(context->game->now_ns, 0.6);
    monster->attack_ns = q2m_after(monster->attack_ns,
                                   context->game->options.skill == 0   ? 0.4
                                   : context->game->options.skill == 1 ? 0.2
                                                                       : 0.0);
  }
  context->actor->physics.enemy = qa_actor_reference_lifetime(id);
  context->actor->physics.goal = qa_actor_reference_lifetime(id);
  bool routed;
  if (!q2m_lifecycle_route(context, true, &routed, error))
    return false;
  if (!q2m_alive(context) || routed)
    return true;
  return q2m_hunt_target(context, true, error);
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

static bool clear_medic_target(q2m_context *context, qa_error *error) {
  if (!q2m_medic_cleanup_patient(context, context->monster->enemy, error))
    return false;
  if (!q2m_alive(context))
    return true;
  context->monster->medic = false;
  context->monster->resurrect_target = (qa_actor_id){0};
  return true;
}

static bool target_tesla(q2m_context *context, qa_actor_id tesla,
                         qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (monster->medic && !clear_medic_target(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  qa_builtin_actor_traits current = {0};
  if (!actor_traits(context, monster->enemy, &current, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (current.player)
    monster->last_player_enemy = monster->enemy;
  if (qa_actor_id_equal(monster->enemy, tesla))
    return true;
  monster->old_enemy = monster->enemy;
  monster->enemy = tesla;
  monster->goal = tesla;
  context->actor->physics.enemy = qa_actor_reference_lifetime(tesla);
  context->actor->physics.goal = qa_actor_reference_lifetime(tesla);
  if ((monster->definition->flags & Q2M_HAS_RANGED) == 0)
    return q2m_found_target(context, tesla, error);
  return context->combat.health <= 0.0f || q2m_source_attack(context, false, error);
}

bool qa_q2_before_monster_step(qa_q2_game *game, qa_actor_id id, qa_vec3 *displacement,
                               bool *handled, qa_error *error) {
  if (!game || !displacement || !handled || !qa_vec_finite(*displacement)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 monster displacement");
    return false;
  }
  *handled = false;
  q2_actor *actor = native_monster_actor(game, id);
  if (!actor || (game->options.product != QA_Q2_ROGUE &&
                 game->options.rules != QA_RULESET_Q2_RERELEASE))
    return true;
  q2m_context context = {.game = game, .actor = actor, .monster = actor->monster};
  if (!q2m_refresh(&context, error))
    return !q2m_alive(&context);
  if (context.combat.health <= 0)
    return true;
  qa_actor_id area_id;
  if (!qa_q2_bad_area(game, id, context.body.origin, &area_id, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  struct qa_q2_monster *monster = context.monster;
  if (area_id.registry) {
    monster->hazard = area_id;
    qa_builtin_actor_traits traits;
    if (!actor_traits(&context, monster->enemy, &traits, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    qa_string_id tesla = game->runtime_names[game->options.rules == QA_RULESET_Q2_RERELEASE
                                               ? Q2_NAME_TESLA_MINE : Q2_NAME_TESLA];
    if (traits.classname && traits.classname == tesla) {
      qa_body_state area;
      if (!qa_world_body_read(game->services.world, area_id, &area, error))
        return false;
      if (!q2m_alive(&context) || !q2_actor_live(game, area_id))
        return true;
      qa_vec3 forward;
      qa_angle_vectors(context.body.angles, &forward, NULL, NULL,QA_ANGLE_FLOAT_MATH);
      float bad_dot = qa_vec_dot(forward, qa_vec_normalize(qa_vec_sub(area.origin, context.body.origin)));
      float move_dot = qa_vec_dot(forward, qa_vec_normalize(*displacement));
      if ((bad_dot < 0 && move_dot < 0) || (bad_dot > 0 && move_dot > 0))
        *displacement = qa_vec_scale(*displacement, -1);
    }
  } else if (monster->hazard.registry) {
    monster->hazard = (qa_actor_id){0};
    if (monster->old_enemy.registry) {
      qa_actor_id previous = monster->old_enemy;
      monster->enemy = monster->goal = previous;
      actor->physics.enemy = actor->physics.goal = qa_actor_reference_lifetime(previous);
      if (!q2m_found_target(&context, previous, error))
        return false;
      *handled = true;
    }
  }
  return true;
}

bool qa_q2_accept_ground(qa_q2_game *game, qa_actor_id id, qa_vec3 origin,
                         bool *accepted, qa_error *error) {
  if (!game || !accepted || !qa_vec_finite(origin)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 ground decision");
    return false;
  }
  *accepted = true;
  q2_actor *actor = native_monster_actor(game, id);
  if (!actor || (game->options.product != QA_Q2_ROGUE &&
                 game->options.rules != QA_RULESET_Q2_RERELEASE))
    return true;
  q2m_context context = {.game = game, .actor = actor, .monster = actor->monster};
  if (!q2m_refresh(&context, error))
    return !q2m_alive(&context);
  if (context.combat.health <= 0 || context.monster->hazard.registry)
    return true;
  qa_actor_id area_id;
  if (!qa_q2_bad_area(game, id, origin, &area_id, error))
    return false;
  if (!q2m_alive(&context) || !area_id.registry)
    return true;
  *accepted = false;
  q2_actor *area = q2_actor_get(game, area_id, false, NULL);
  if (!area || area->projectile.kind != Q2_BAD_AREA)
    return true;
  qa_actor_id owner = qa_actor_reference_resolve(qa_session_actors(game->services.session), area->projectile.owner);
  qa_builtin_actor_traits owner_traits, enemy_traits;
  if (!actor_traits(&context, owner, &owner_traits, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  qa_string_id tesla_name = game->runtime_names[game->options.rules == QA_RULESET_Q2_RERELEASE
                                                  ? Q2_NAME_TESLA_MINE : Q2_NAME_TESLA];
  if (!owner_traits.classname || owner_traits.classname != tesla_name)
    return true;
  qa_actor_id enemy = context.monster->enemy;
  if (!actor_traits(&context, enemy, &enemy_traits, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  qa_string_id protected_name = game->runtime_names[game->options.rules == QA_RULESET_Q2_RERELEASE
                                                      ? Q2_NAME_TESLA_MINE : Q2_NAME_TELSA];
  bool engage = !q2_actor_live(game, enemy);
  if (!engage && (!enemy_traits.classname || enemy_traits.classname != protected_name)) {
    bool visible = false;
    if (enemy_traits.player && !q2m_visible(&context, enemy, &visible, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    engage = !enemy_traits.player || !visible;
  }
  if (engage) {
    if (!target_tesla(&context, owner, error))
      return false;
    if (q2m_alive(&context))
      context.monster->source_blocked = true;
  }
  return true;
}

bool q2m_react_to_damage(q2m_context *context, qa_actor_id attacker,
                         qa_error *error) {
  if (!q2m_alive(context) || attacker.registry == 0)
    return true;
  struct qa_q2_monster *monster = context->monster;
  const bool rogue = context->game->options.product == QA_Q2_ROGUE ||
                     context->game->options.rules == QA_RULESET_Q2_RERELEASE;

  if (rogue && monster->last_attack.inflictor.registry != 0) {
    qa_builtin_actor_traits inflictor_traits = {0};
    if (!actor_traits(context, monster->last_attack.inflictor, &inflictor_traits, error))
      return false;
    if (!q2m_alive(context))
      return true;
    {
      qa_string_id tesla_name = context->game->runtime_names[
          context->game->options.rules == QA_RULESET_Q2_RERELEASE
              ? Q2_NAME_TESLA_MINE : Q2_NAME_TESLA];
      if (inflictor_traits.classname != 0 && inflictor_traits.classname == tesla_name) {
        bool created;
        if (!qa_q2_mark_tesla_area(context->game, context->actor->id,
                                   monster->last_attack.inflictor, &created,
                                   error))
          return false;
        if (!q2m_alive(context))
          return true;
        bool engage = context->game->options.rules == QA_RULESET_Q2_CLASSIC
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
  if (!actor_traits(context, attacker, &attacker_traits, error))
    return false;
  if (!q2m_alive(context) || (!attacker_traits.player && !attacker_traits.monster))
    return true;
  q2_actor *other = native_monster_actor(context->game, attacker);
  bool attacker_good = attacker_traits.no_source_friendly_fire ||
                       (other != NULL && other->monster->good_guy);
  if (monster->good_guy && (attacker_traits.player || attacker_good))
    return true;

  if (rogue) {
    float fraction = monster->max_health > 0.0f
                         ? (float)context->combat.health / monster->max_health
                         : 0.0f;
    if (monster->enemy.registry != 0 && monster->target_anger) {
      if (q2_actor_live(context->game, monster->enemy) && fraction > 0.33f)
        return true;
      monster->target_anger = false;
    }
    if (context->game->options.rules == QA_RULESET_Q2_RERELEASE &&
        monster->react_ns > context->game->now_ns)
      return true;
    if (monster->enemy.registry != 0 && monster->medic) {
      if (q2_actor_live(context->game, monster->enemy) && fraction > 0.25f)
        return true;
      if (!clear_medic_target(context, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    if (context->game->options.rules == QA_RULESET_Q2_RERELEASE)
      monster->react_ns = q2m_after(
          context->game->now_ns,
          (double)q2_rerelease_time_ms(context->game, 3000, 5000) / 1000.0);
  }

  qa_actor_id chosen = {0};
  if (attacker_traits.player) {
    monster->sound_target.present = false;
    qa_builtin_actor_traits current_traits = {0};
    if (!actor_traits(context, monster->enemy, &current_traits, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (current_traits.player) {
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
    qa_string_id self_name = monster->definition->classname_id;
    qa_string_id other_name = attacker_traits.classname;
    bool ignore = false;
    bool retaliate = false;
    if (other != NULL) {
      if (rogue || context->game->options.rules == QA_RULESET_Q2_RERELEASE)
        ignore = monster->ignore_shots || other->monster->ignore_shots;
      else
        ignore = other->monster->definition->species == Q2M_TANK ||
                 other->monster->definition->species == Q2M_SUPERTANK ||
                 other->monster->definition->species == Q2M_MAKRON ||
                 other->monster->definition->species == Q2M_JORG;
      retaliate =
          (monster->definition->locomotion ==
               other->monster->definition->locomotion &&
           other_name != 0 && self_name != other_name &&
           !ignore) ||
          qa_actor_id_equal(other->monster->enemy, context->actor->id);
    } else {
      retaliate = other_name == 0 || self_name != other_name;
    }
    qa_builtin_actor_traits current_traits = {0};
    if (!actor_traits(context, monster->enemy, &current_traits, error))
      return false;
    if (!q2m_alive(context))
      return true;
    other = native_monster_actor(context->game, attacker);
    if (current_traits.player)
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
  context->actor->physics.enemy = qa_actor_reference_lifetime(chosen);
  context->actor->physics.goal = qa_actor_reference_lifetime(chosen);
  return monster->ducked || q2m_found_target(context, chosen, error);
}

static bool hear_target(q2m_context *context, bool *found, qa_error *error) {
  *found = false;
  qa_q2_player_noise_record noise = {0};
  uint64_t age = context->game->options.rules == QA_RULESET_Q2_CLASSIC
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
  bool alive;
  if (!target_alive(context, noise.owner, &traits, &body, &alive, error))
    return false;
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
  context->actor->physics.enemy = qa_actor_reference_lifetime(noise.owner);
  context->actor->physics.goal = qa_actor_reference_lifetime(noise.owner);
  *found = true;
  if (monster->definition->species == Q2M_FLOATER)
    return monster->stand_ground ? q2m_stand_state(context, error)
                                 : q2m_run_state(context, error);
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
  size_t index;
  return (player.registry ? qa_actor_index_find(&runtime->alert_index, player, &index) :
      ((index = runtime->no_player_alert) != SIZE_MAX)) ?
      runtime->alerts[index].hostile_ns : 0;
}

static bool visual_candidate(q2m_context *context, qa_actor_id candidate,
                             bool *accepted, qa_error *error) {
  *accepted = false;
  qa_builtin_actor_traits traits;
  qa_body_state target;
  bool living;
  if (!target_alive(context, candidate, &traits, &target, &living, error))
    return false;
  if (!living || !q2m_alive(context))
    return true;
  float distance = q2m_distance(context, candidate);
  if (context->game->options.rules == QA_RULESET_Q2_RERELEASE) {
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

bool q2m_clear_flash_shot(q2m_context *context, int flash, bool *clear,
                          qa_error *error) {
  qa_vec3 offset;
  if (!q2m_muzzle_offset(context, flash, &offset, error))
    return false;
  return q2m_clear_shot(context, offset, clear, error);
}

static bool sight_reaction(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  if (species == Q2M_STALKER)
    return q2m_sound(context, QA_Q2_SFX_STALKER_SIGHT,
        monster->definition->source_rules == QA_RULESET_Q2_RERELEASE ? 2 : 1,
        1.0f, error);
  if (species == Q2M_INFANTRY) {
    q2_runtime_name sound = QA_Q2_SFX_INFANTRY_INFSGHT1;
    if (rerelease && q2_random_bounded(context->game, 2) != 0)
      sound = QA_Q2_SFX_INFANTRY_INFSRCH1;
    return q2m_sound(context, sound, rerelease ? 2 : 4, 1.0f, error);
  }
  bool heavy = species == Q2M_SOLDIER_RIPPER ||
               species == Q2M_SOLDIER_HYPER || species == Q2M_SOLDIER_LASER;
  bool soldier = heavy || species == Q2M_SOLDIER_LIGHT ||
                 species == Q2M_SOLDIER || species == Q2M_SOLDIER_SS;
  if (!soldier)
    return monster->definition->sight_sound == Q2_NAME_NONE ||
           q2m_sound(context, monster->definition->sight_sound, 2, 1.0f, error);
  q2_runtime_name sound = q2m_random(context->game) < 0.5f
                             ? QA_Q2_SFX_SOLDIER_SOLSGHT1
                             : QA_Q2_SFX_SOLDIER_SOLSRCH1;
  if (!q2m_sound(context, sound, 2, 1.0f, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, monster->enemy))
    return true;
  float distance = q2m_distance(context, monster->enemy);
  if (!q2m_alive(context) || !q2_actor_live(context->game, monster->enemy))
    return true;
  if (rerelease) {
    if (distance < 440.0f)
      return true;
    bool visible;
    if (!q2m_visible(context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context) || !visible)
      return true;
    if (monster->style != 1 && q2m_random(context->game) <= 0.75f)
      return true;
    if (monster->count < 4)
      return q2m_set_move(context, Q2M_MOVE_soldier_move_attack6, false, error);
    bool clear;
    if (!q2m_clear_flash_shot(context, 88, &clear, error))
      return false;
    return !q2m_alive(context) || !clear ||
           q2m_set_move(context, Q2M_MOVE_soldier_move_attack4, false, error);
  }
  bool rogue = !heavy && context->game->options.product == QA_Q2_ROGUE;
  if (context->game->options.skill == 0 || distance < (rogue ? 80.0f : 500.0f))
    return true;
  if (q2m_random(context->game) <= (rogue ? 0.75f : 0.5f) ||
      (rogue && monster->skin > 3))
    return true;
  q2m_move_id move = !heavy ? Q2M_MOVE_soldier_move_attack6
                    : monster->skin < 4 ? Q2M_MOVE_soldierh_move_attack6
                                        : Q2M_MOVE_soldierh_move_attack4;
  return q2m_set_move(context, move, false, error);
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
  uint64_t recent_age = context->game->options.rules == QA_RULESET_Q2_CLASSIC
                            ? Q2M_TENTH
                            : context->game->frame_ns;

  if (context->game->options.rules == QA_RULESET_Q2_RERELEASE) {
    qa_world_actor_snapshot players = {0};
    if (!q2_player_roster(context->game, &players, error))
      return false;
    size_t eligible = 0;
    for (size_t index = 0; index < players.count; ++index) {
      qa_actor_id id = players.actors[index];
      qa_builtin_actor_traits traits;
      qa_body_state body;
      qa_error ignored = {0};
      if (!runtime_targetable(context->game, id, &traits) || traits.disguised ||
          !qa_world_body_read(context->game->services.world, id, &body,
                              &ignored))
        continue;
      bool visible = false;
      if (!boxes_close(&context->body, &body, 0.0f)) {
        if (!in_front(context, id))
          continue;
        if (!q2m_visible(context, id, &visible, error)) {
          q2_scratch_release(context->game, &players);
          return false;
        }
        if (!q2m_alive(context)) {
          q2_scratch_release(context->game, &players);
          return true;
        }
      } else {
        visible = true;
      }
      if (visible)
        players.actors[eligible++] = id;
    }
    if (eligible != 0) {
      size_t selected = (size_t)floorf(q2m_random(context->game) * (float)eligible);
      if (selected >= eligible)
        selected = eligible - 1;
      candidate = players.actors[selected];
    }
    q2_scratch_release(context->game, &players);
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

  if (candidate.registry == 0 && runtime->disguise_until_ns > context->game->now_ns) {
    candidate = runtime->disguise_violator;
  } else if (candidate.registry == 0) {
    bool heard;
    if (!hear_target(context, &heard, error))
      return false;
    if (heard) {
      *found = true;
      return !monster->hint_path || q2_hint_stop(context, error);
    }
    candidate = context->game->options.rules == QA_RULESET_Q2_CLASSIC
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
  if (!actor_traits(context, candidate, &selected, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (context->game->options.rules == QA_RULESET_Q2_CLASSIC &&
      selected.player && selected.has_light_level && selected.light_level <= 5)
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
  if (monster->hint_path) {
    attack_enemy(context, candidate);
    if (!q2_hint_stop(context, error)) return false;
  } else if (!q2m_found_target(context, candidate, error)) {
    return false;
  }
  if (!q2m_alive(context))
    return true;
  monster = context->monster;
  if (context->game->options.rules == QA_RULESET_Q2_CLASSIC ||
      !monster->close_sight_tripped) {
    if (!sight_reaction(context, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  context->monster->close_sight_tripped = true;
  *found = true;
  return true;
}

static void attack_enemy(q2m_context *context, qa_actor_id enemy) {
  context->monster->enemy = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.enemy = qa_actor_reference_lifetime(enemy));
  if (context->actor->entity != NULL)
    context->actor->entity->enemy = enemy;
}

/* Perception admits dead patients and brutal targets; target_alive is for
 * acquiring a new ordinary opponent and intentionally has stricter rules. */
static bool attack_body(q2m_context *context, qa_actor_id id, qa_body_state *body,
                        bool *present, qa_error *error) {
  *present = false;
  if (!q2_actor_live(context->game, id))
    return true;
  qa_error observed = {0};
  if (!qa_world_body_read(context->game->services.world, id, body, &observed)) {
    if (!q2m_alive(context) || !q2_actor_live(context->game, id) ||
        observed.code == QA_ERROR_NOT_FOUND)
      return true;
    if (error)
      *error = observed;
    return false;
  }
  *present = q2m_alive(context) && q2_actor_live(context->game, id);
  return true;
}

static bool attack_health(q2m_context *context, qa_actor_id id, double *health,
                          qa_error *error) {
  *health = 0;
  if (!q2_actor_live(context->game, id))
    return true;
  qa_error observed = {0};
  qa_combat_state combat;
  if (!qa_combat_read(context->game->services.combat, id, &combat, &observed)) {
    if (!q2m_alive(context) || !q2_actor_live(context->game, id))
      return true;
    if (observed.code != QA_OK && observed.code != QA_ERROR_NOT_FOUND) {
      if (error)
        *error = observed;
      return false;
    }
  } else if (q2m_alive(context) && q2_actor_live(context->game, id)) {
    *health = combat.health;
  }
  return true;
}

static bool nonsolid_target(q2m_context *context, qa_actor_id id, bool *nonsolid,
                            qa_error *error) {
  *nonsolid = false;
  if (!q2_actor_live(context->game, id))
    return true;
  q2_actor *native = q2_actor_get(context->game, id, false, NULL);
  if (native && native->physics_bound) {
    *nonsolid = native->physics.solid == QA_PHYSICS_NOT_SOLID;
    return true;
  }
  qa_actor_collision collision;
  qa_error observed = {0};
  bool present = qa_world_get_collision(context->game->services.world, id, &collision, &observed);
  if (!q2m_alive(context) || !q2_actor_live(context->game, id))
    return true;
  if (observed.code != QA_OK && observed.code != QA_ERROR_NOT_FOUND) {
    if (error)
      *error = observed;
    return false;
  }
  *nonsolid = !present;
  return true;
}

typedef struct attack_chances {
  float stand, near, mid, far, strafe;
} attack_chances;

static bool default_attack(q2m_context *context, bool *selected, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  qa_q2_game *game = context->game;
  bool rerelease = game->options.rules == QA_RULESET_Q2_RERELEASE;
  bool rogue = rerelease || game->options.product == QA_Q2_ROGUE;
  q2m_species species = m->definition->species;
  bool boss_profile = species == Q2M_JORG || species == Q2M_MAKRON || species == Q2M_BOSS2;
  bool classic_boss = boss_profile && !rerelease;
  bool allow_nonsolid = classic_boss ? species == Q2M_BOSS2 && rogue : rogue;
  attack_chances profile = {.stand = .7f, .near = .25f, .mid = .06f, .strafe = 1};
  if (boss_profile)
    profile = (attack_chances){.stand = .4f,
        .near = species == Q2M_BOSS2 ? .8f : .4f,
        .mid = species == Q2M_BOSS2 ? .8f : .2f};

  qa_actor_id enemy = m->enemy;
  qa_body_state target;
  bool present;
  if (!attack_body(context, enemy, &target, &present, error))
    return false;
  if (!present || !q2m_alive(context))
    return true;
  qa_builtin_actor_traits traits;
  if (!actor_traits(context, enemy, &traits, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  qa_vec3 eye = qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height));
  if (!attack_body(context, context->actor->id, &context->body, &present, error))
    return false;
  if (!present || !q2_actor_live(game, enemy))
    return true;
  qa_vec3 start = qa_vec_add(context->body.origin, qa_v3(0, 0, m->view_height));
  double health;
  if (!attack_health(context, enemy, &health, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  if (health > 0) {
    qa_trace_query query = {.start = start, .end = eye, .pass_actor = context->actor->id,
                            .policy = qa_collision_ruleset_policy(context->game->options.rules)};
    query.policy.contents_mask = qa_collision_contents_mask(classic_boss ? UINT32_C(0x02000019)
        : rerelease ? UINT32_C(0x4200001b) : UINT32_C(0x0200001b), QA_GAME_Q2);
    qa_trace_result trace;
    if (!qa_world_trace(game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context) || !q2_actor_live(game, enemy))
      return true;
    bool hit = trace.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(trace.actor, enemy);
    if (!hit && rerelease && trace.hit == QA_TRACE_HIT_ACTOR) {
      qa_builtin_actor_traits obstruction;
      if (!actor_traits(context, trace.actor, &obstruction, error))
        return false;
      if (!q2m_alive(context) || !q2_actor_live(game, enemy))
        return true;
      hit = q2_actor_live(game, trace.actor) && obstruction.player;
    }
    if (!hit) {
      bool nonsolid = false;
      if (allow_nonsolid && !nonsolid_target(context, enemy, &nonsolid, error))
        return false;
      if (!q2m_alive(context) || !q2_actor_live(game, enemy))
        return true;
      if (!nonsolid || trace.fraction < 1) {
        if (classic_boss || !rogue || !(m->definition->flags & Q2M_BLIND_FIRE) ||
            (rerelease && !m->had_visibility) || m->blind_fire_delay > 20)
          return true;
        bool visible;
        if (!q2m_visible(context, enemy, &visible, error))
          return false;
        if (!q2m_alive(context) || !q2_actor_live(game, enemy) || visible)
          return true;
        if (trace.hit == QA_TRACE_HIT_ACTOR) {
          qa_builtin_actor_traits obstruction;
          if (!actor_traits(context, trace.actor, &obstruction, error))
            return false;
          if (!q2m_alive(context) || !q2_actor_live(game, enemy) || obstruction.monster)
            return true;
        }
        if (game->now_ns < m->attack_ns ||
            game->now_ns < q2m_after(m->trail_ns, m->blind_fire_delay))
          return true;
        query.end = m->blind_fire_target;
        query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x02000000), QA_GAME_Q2);
        if (!qa_world_trace(game->services.world, &query, &trace, error))
          return false;
        if (!q2m_alive(context) || !q2_actor_live(game, enemy))
          return true;
        if (!trace.all_solid && !trace.start_solid &&
            (trace.fraction == 1 || (trace.hit == QA_TRACE_HIT_ACTOR &&
                                    qa_actor_id_equal(trace.actor, enemy)))) {
          m->attack_state = Q2M_BLIND;
          *selected = true;
        }
        return true;
      }
    }
  }

  if (!attack_body(context, enemy, &target, &present, error))
    return false;
  if (!present || !q2m_alive(context))
    return true;
  if (!attack_body(context, context->actor->id, &context->body, &present, error))
    return false;
  if (!present || !q2_actor_live(game, enemy))
    return true;
  float distance = q2m_body_distance(game->options.rules, &context->body, &target);
  if (classic_boss)
    m->ideal_yaw = vector_yaw(qa_vec_sub(target.origin, context->body.origin));
  if (rerelease ? distance <= 20 : distance < 80) {
    if (!rerelease && !classic_boss && game->options.skill == 0 &&
        floorf(q2m_random(game) * 4) != 0) {
      if (rogue)
        m->attack_state = Q2M_STRAIGHT;
      return true;
    }
    m->attack_state = (m->definition->flags & Q2M_HAS_MELEE) &&
                             (!rerelease || m->melee_ns <= game->now_ns)
                         ? Q2M_MELEE : Q2M_MISSILE;
    *selected = true;
    return true;
  }
  if (rerelease && m->attack_state == Q2M_MELEE && m->melee_ns > game->now_ns)
    m->attack_state = Q2M_MISSILE;
  if (!(m->definition->flags & Q2M_HAS_RANGED)) {
    if (!classic_boss && rogue)
      m->attack_state = Q2M_STRAIGHT;
    return true;
  }
  if (game->now_ns < m->attack_ns || (!rerelease && distance >= 1000))
    return true;
  float chance;
  if (rerelease)
    chance = m->stand_ground ? profile.stand : distance <= 440 ? profile.near
        : distance <= 940 ? profile.mid : profile.far;
  else if (classic_boss)
    chance = m->stand_ground ? .4f : species == Q2M_BOSS2 ? .8f
        : distance < 500 ? .4f : .2f;
  else {
    chance = m->stand_ground ? .4f : distance < 500 ? .1f : .02f;
    chance *= game->options.skill == 0 ? .5f : game->options.skill >= 2 ? 2 : 1;
  }
  bool nonsolid = false;
  if (allow_nonsolid && !nonsolid_target(context, enemy, &nonsolid, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  bool fire = rerelease ? (!traits.player && nonsolid) || q2m_random(game) < chance
                       : q2m_random(game) < chance || nonsolid;
  if (fire) {
    m->attack_state = Q2M_MISSILE;
    m->attack_ns = rerelease ? game->now_ns
                             : q2m_after(game->now_ns, 2 * q2m_random(game));
    *selected = true;
    return true;
  }
  if (m->definition->locomotion == Q2M_FLY &&
      (!rerelease || m->strafe_ns <= game->now_ns)) {
    chance = classic_boss || !rogue ? .3f : species == Q2M_DAEDALUS ? .8f : .6f;
    if (!classic_boss && rogue) {
      if (traits.classname == game->runtime_names[Q2_NAME_TESLA] ||
          (rerelease && traits.classname == game->runtime_names[Q2_NAME_TESLA_MINE]))
        chance = 0;
      else if (rerelease)
        chance *= profile.strafe;
    }
    if (rerelease && chance == 0)
      return true;
    q2m_attack_state next = q2m_random(game) < chance ? Q2M_SLIDING : Q2M_STRAIGHT;
    if (rerelease && next != m->attack_state)
      m->strafe_ns = q2m_after(game->now_ns, (double)q2_rerelease_time_ms(game, 1000, 3000) / 1000);
    m->attack_state = next;
  } else if (rerelease && m->definition->locomotion != Q2M_FLY) {
    m->attack_state = Q2M_STRAIGHT;
  }
  return true;
}

static bool widow_check_attack(q2m_context *context, bool *selected, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  qa_q2_game *game = context->game;
  qa_actor_id enemy = monster->enemy;
  qa_body_state target;
  bool present;
  if (!attack_body(context, enemy, &target, &present, error))
    return false;
  if (!present)
    return true;
  qa_builtin_actor_traits traits;
  if (!actor_traits(context, enemy, &traits, error) ||
      !q2m_widow_powerups(context, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  bool second = monster->definition->species == Q2M_WIDOW2;
  if (!second && (monster->move->id == Q2M_MOVE_widow_move_run) &&
      ((monster->frame >= 14 && monster->frame <= 18) || monster->frame == 22))
    return true;
  float distance = q2m_body_distance(game->options.rules, &context->body, &target);
  bool slots = q2m_summon_has_slots(monster, 2);
  if (q2m_random(game) < .8f && slots && distance > 150.0f) {
    monster->source_blocked = true;
    monster->attack_state = Q2M_MISSILE;
    *selected = true;
    return true;
  }
  double health;
  if (!attack_health(context, enemy, &health, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  bool nonsolid = false;
  if (health > 0.0f) {
    qa_trace_query query = {
        .start = qa_vec_add(context->body.origin, qa_v3(0, 0, monster->view_height)),
        .end = qa_vec_add(target.origin, qa_v3(0, 0, traits.view_height)),
        .pass_actor = context->actor->id,
        .policy = qa_collision_ruleset_policy(context->game->options.rules)};
    query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x02000019), QA_GAME_Q2);
    qa_trace_result trace;
    if (!qa_world_trace(game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context) || !q2_actor_live(game, enemy))
      return true;
    if (trace.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(trace.actor, enemy)) {
      if (traits.player && slots) {
        monster->attack_state = Q2M_BLIND;
        *selected = true;
        return true;
      }
      if (!nonsolid_target(context, enemy, &nonsolid, error))
        return false;
      if (!q2m_alive(context) || !q2_actor_live(game, enemy) ||
          !nonsolid || trace.fraction < 1.0f)
        return true;
    }
  }
  monster->ideal_yaw = vector_yaw(qa_vec_sub(target.origin, context->body.origin));
  bool melee = distance <= 100.0f;
  if (second) {
    qa_vec3 forward, right, up;
    qa_angle_vectors(context->body.angles, &forward, &right, &up,QA_ANGLE_FLOAT_MATH);
    qa_vec3 tongue = qa_vec_add(context->body.origin,
        qa_vec_add(qa_vec_scale(forward, 17.48f),
            qa_vec_add(qa_vec_scale(right, .10f), qa_vec_scale(up, 68.92f))));
    qa_vec3 delta = qa_vec_sub(tongue, target.origin);
    float pitch = qa_vec_angles(delta,QA_DIRECTION_FLOAT,QA_VECTOR_VIEW).x;
    if (pitch < -180.0f)
      pitch += 360.0f;
    melee = monster->timestamp_ns < game->now_ns && distance < 300.0f &&
            qa_vec_length(delta) <= 256.0f && fabsf(pitch) <= 30.0f;
  }
  if (melee) {
    if (game->options.skill == 0 && floorf(q2m_random(game) * 4.0f) != 0.0f)
      return true;
    monster->attack_state = Q2M_MELEE;
    *selected = true;
    return true;
  }
  if (game->now_ns < monster->attack_ns)
    return true;
  float chance = monster->stand_ground ? .4f
      : second ? (distance < 1000.0f ? .8f : .5f)
      : distance < 80.0f ? .8f : distance < 500.0f ? .7f
      : distance < 1000.0f ? .6f : .5f;
  bool fire = q2m_random(game) < chance;
  if (!fire && !nonsolid_target(context, enemy, &nonsolid, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(game, enemy))
    return true;
  if (fire || nonsolid) {
    monster->attack_state = Q2M_MISSILE;
    *selected = true;
  }
  return true;
}

bool q2m_gekk_check_jump(q2m_context *context, const qa_body_state *target) {
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  qa_bounds self_bounds = qa_bounds_translate(context->body.bounds, context->body.origin);
  qa_bounds target_bounds = qa_bounds_translate(target->bounds, target->origin);
  qa_linked_body linked;
  if (qa_world_linked(context->game->services.world, context->actor->id, &linked))
    self_bounds = linked.absolute_bounds;
  if (qa_world_linked(context->game->services.world, context->monster->enemy, &linked))
    target_bounds = linked.absolute_bounds;
  float self_bottom = self_bounds.mins.z;
  float target_bottom = target_bounds.mins.z;
  if (rerelease) {
    if (self_bottom + 125.0f < target_bottom)
      return false;
  } else {
    float target_height = target->bounds.maxs.z - target->bounds.mins.z;
    if (self_bottom > target_bottom + .75 * target_height ||
        self_bounds.maxs.z < target_bottom + .25 * target_height)
      return false;
  }
  qa_vec3 delta = qa_vec_sub(context->body.origin, target->origin);
  delta.z = 0;
  float distance = qa_vec_length(delta);
  if (distance < 100.0f)
    return false;
  if (distance > 100.0f) {
    float draw = rerelease ? q2_rerelease_float(context->game, 0, 1)
                           : q2m_random(context->game);
    if (draw < (rerelease ? context->actor->physics.water_level >= 2 ? .2f : .9f
                          : .9))
      return false;
  }
  return true;
}

bool q2m_gekk_check_attack(q2m_context *context, bool *selected,
                           qa_error *error) {
  *selected = false;
  struct qa_q2_monster *monster = context->monster;
  qa_actor_id enemy = monster->enemy;
  double health;
  if (!attack_health(context, enemy, &health, error))
    return false;
  if (!q2m_alive(context) || health <= 0)
    return true;
  qa_body_state target;
  bool present;
  if (!attack_body(context, enemy, &target, &present, error))
    return false;
  if (!q2m_alive(context) || !present)
    return true;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  float distance = q2m_body_distance(context->game->options.rules,
                                     &context->body, &target);
  if (rerelease ? distance <= 20.0f && monster->melee_ns <= context->game->now_ns
               : distance < 80.0f) {
    monster->attack_state = Q2M_MELEE;
    *selected = true;
    return true;
  }
  if (rerelease) {
    if (monster->attack_state == Q2M_STRAIGHT &&
        monster->attack_ns > context->game->now_ns)
      return true;
    bool visible;
    if (!q2m_visible(context, enemy, &visible, error))
      return false;
    if (!q2m_alive(context) || !visible)
      return true;
  }
  if (q2m_gekk_check_jump(context, &target)) {
    monster->attack_state = Q2M_MISSILE;
    *selected = true;
    return true;
  }
  qa_vec3 delta = qa_vec_sub(context->body.origin, target.origin);
  delta.z = 0;
  float horizontal = qa_vec_length(delta);
  qa_bounds self_bounds = qa_bounds_translate(context->body.bounds, context->body.origin);
  qa_bounds target_bounds = qa_bounds_translate(target.bounds, target.origin);
  qa_linked_body linked;
  if (rerelease) {
    if (qa_world_linked(context->game->services.world, context->actor->id, &linked))
      self_bounds = linked.absolute_bounds;
    if (qa_world_linked(context->game->services.world, enemy, &linked))
      target_bounds = linked.absolute_bounds;
  }
  bool close = horizontal >= 100.0f ||
      (rerelease ? self_bounds.maxs.z > target_bounds.mins.z
                : context->body.origin.z < target.origin.z);
  bool swimming = rerelease
      ? (context->actor->physics.flags & QA_PHYSICS_SWIMMING) != 0
      : context->actor->physics.water_level != 0;
  if (close && !swimming) {
    monster->attack_state = Q2M_MISSILE;
    *selected = true;
  }
  return true;
}

static bool check_attack_slot(q2m_context *context, bool *selected, bool *started,
                              qa_error *error) {
  if (context->monster->definition->species == Q2M_GEKK)
    return q2m_gekk_check_attack(context, selected, error);
  bool handled;
  if (!q2m_medic_check_attack(context, &handled, selected, started, error))
    return false;
  if (!handled && q2m_alive(context) &&
      (context->monster->definition->species == Q2M_WIDOW ||
       context->monster->definition->species == Q2M_WIDOW2))
    return widow_check_attack(context, selected, error);
  return handled || !q2m_alive(context) || default_attack(context, selected, error);
}

static bool pending_attack(const struct qa_q2_monster *monster, bool blind) {
  return monster->attack_state == Q2M_MISSILE || monster->attack_state == Q2M_MELEE ||
         (blind && monster->attack_state == Q2M_BLIND);
}

static void sound_target_refresh(q2m_context *context) {
  struct qa_q2_monster *monster = context->monster;
  qa_q2_player_noise_record noise;
  for (unsigned channel = 0; channel != 2; ++channel) {
    if (qa_q2_player_noise_read(context->game, channel != 0, &noise) &&
        qa_actor_id_equal(noise.owner, monster->sound_target.actor)) {
      monster->sound_target.origin = noise.origin;
      monster->sound_target.time_ns = noise.time_ns;
      break;
    }
  }
}

bool q2m_check_attack(q2m_context *context, bool *selected, bool *started,
                      bool *enemy_visible, qa_error *error) {
  *selected = *started = *enemy_visible = false;
  struct qa_q2_monster *monster = context->monster;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  bool rogue = rerelease || context->game->options.product == QA_Q2_ROGUE;
  if (rerelease && monster->temporary_stand_ground)
    monster->stand_ground = monster->temporary_stand_ground = false;
  if (monster->combat_point)
    return true;
  if (monster->sound_target.present) {
    sound_target_refresh(context);
    if (monster->sound_target.time_ns >= context->game->now_ns ||
        context->game->now_ns - monster->sound_target.time_ns <= 5 * Q2M_SECOND) {
      monster->hostile_ns = q2m_after(context->game->now_ns, 1);
      return true;
    }
    if (qa_actor_id_equal(monster->goal, monster->enemy))
      monster->goal = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = qa_actor_reference_lifetime(monster->move_target));
    monster->sound_target.present = false;
    if (monster->temporary_stand_ground)
      monster->stand_ground = monster->temporary_stand_ground = false;
  }

  qa_body_state target;
  double health;
  bool present;
  qa_actor_id enemy = monster->enemy;
  if (!attack_body(context, enemy, &target, &present, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!attack_health(context, enemy, &health, error))
    return false;
  if (!q2m_alive(context))
    return true;
  present = present && q2_actor_live(context->game, enemy);
  bool rejected = !present || (monster->medic ? health > 0
                                  : monster->brutal ? !rerelease && health <= -80
                                                    : health <= 0);
  if (rejected) {
    if (rogue)
      monster->medic = false;
    attack_enemy(context, (qa_actor_id){0});
    monster->close_sight_tripped = false;
    if (rerelease)
      monster->goal = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = (qa_actor_reference){0});
    qa_actor_id restored = monster->old_enemy;
    if (!attack_health(context, restored, &health, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (health <= 0 && rogue) {
      restored = monster->last_player_enemy;
      if (!attack_health(context, restored, &health, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (health > 0)
        monster->last_player_enemy = (qa_actor_id){0};
    }
    if (health > 0) {
      attack_enemy(context, restored);
      monster->old_enemy = (qa_actor_id){0};
      if (!q2m_hunt_target(context, true, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (!attack_body(context, monster->enemy, &target, &present, error))
        return false;
      if (!q2m_alive(context) || !present)
        return true;
    } else {
      bool walk = monster->move_target.registry != 0 &&
                  (!rerelease || !monster->stand_ground);
      if (walk)
        monster->goal = qa_actor_reference_resolve(qa_session_actors(context->game->services.session), context->actor->physics.goal = qa_actor_reference_lifetime(monster->move_target));
      else
        monster->pause_ns = rerelease ? UINT64_MAX
            : q2m_after(context->game->now_ns, 100000000);
      *selected = *started = true;
      return walk ? q2m_set_move(context, monster->definition->walk_move,
                                 false, error)
                  : q2m_stand_state(context, error);
    }
  }
  bool visible;
  if (!q2m_visible(context, monster->enemy, &visible, error))
    return false;
  *enemy_visible = visible;
  if (!q2m_alive(context))
    return true;
  if (visible) {
    monster->search_ns = q2m_after(context->game->now_ns, 5);
    monster->last_sighting = target.origin;
    if (rogue) {
      monster->lost_sight = false;
      monster->trail_ns = context->game->now_ns;
      monster->blind_fire_target = target.origin;
      monster->blind_fire_delay = 0;
    }
    if (rerelease) {
      monster->had_visibility = true;
      monster->saved_goal = target.origin;
      monster->has_saved_goal = true;
      monster->blind_fire_target =
          qa_vec_add(target.origin, qa_vec_scale(target.velocity, -.1f));
      q2m_alert *alert = alert_for(context->game->monster_runtime, monster->enemy, true, error);
      if (alert == NULL)
        return false;
      alert->hostile_ns = q2m_after(context->game->now_ns, 1);
    }
  }
  if (rerelease) {
    if (monster->check_attack_ns <= context->game->now_ns) {
      monster->check_attack_ns = q2m_after(context->game->now_ns, .1);
      if (!check_attack_slot(context, selected, started, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    if (pending_attack(monster, true)) {
      *selected = true;
      *started = false;
    }
    return true;
  }
  if (rogue) {
    if (!check_attack_slot(context, selected, started, error))
      return false;
    if (!q2m_alive(context) || !*selected)
      return true;
    if (pending_attack(monster, true))
      *started = false;
    else
      *selected = visible;
    return true;
  }
  if (pending_attack(monster, false)) {
    *selected = true;
    return true;
  }
  if (!visible)
    return true;
  if (!check_attack_slot(context, selected, started, error))
    return false;
  if (*selected)
    *started = true;
  return true;
}

void q2m_flyer_flight(struct qa_q2_monster *monster, bool melee) {
  if (melee) {
    monster->fly_pinned = false;
    monster->fly_position_ns = 0;
  }
  monster->fly_thrusters = melee;
  monster->fly_acceleration = melee ? 20.0f : 15.0f;
  monster->fly_speed = melee ? 210.0f : 165.0f;
  monster->fly_min_distance = melee ? 0.0f : 45.0f;
  monster->fly_max_distance = melee ? 10.0f : 200.0f;
}

static bool select_species_attack(q2m_context *context, q2m_move_id *move,
                                  qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  float distance = q2m_distance(context, monster->enemy);
  if (!q2m_alive(context)) {
    *move = Q2M_MOVE_NONE;
    return true;
  }
  bool melee = monster->attack_state == Q2M_MELEE;
  bool blind = monster->attack_state == Q2M_BLIND;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  float random;

  *move = melee ? monster->definition->melee_move
          : monster->definition->attack_move != Q2M_MOVE_NONE
              ? monster->definition->attack_move
              : monster->definition->attack2_move;
  switch (species) {
  case Q2M_PARASITE:
    if (context->game->options.rules == QA_RULESET_Q2_RERELEASE) {
      bool clear;
      if (!q2m_clear_shot(context, qa_v3(-1.7f, 0, 1.2f),
                          &clear, error))
        return false;
      if (!q2m_alive(context))
        return true;
      *move = clear ? Q2M_MOVE_parasite_move_fire_proboscis : Q2M_MOVE_NONE;
      if (clear && !q2m_parasite_interrupt(context, false, error))
        return false;
    }
    break;
  case Q2M_INFANTRY:
  case Q2M_TURRET_DRIVER:
    if (rerelease && species == Q2M_INFANTRY) {
      monster->dodging = false;
      if (monster->attack_state == Q2M_SLIDING)
        monster->attack_state = Q2M_STRAIGHT;
      *move = Q2M_MOVE_NONE;
      if (distance <= 20.0f && monster->melee_ns <= context->game->now_ns) {
        *move = Q2M_MOVE_infantry_move_attack2;
      } else {
        bool clear;
        if (!q2m_clear_flash_shot(context, 26, &clear, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (clear) {
          if (monster->cocked) {
            *move = Q2M_MOVE_infantry_move_attack1;
          } else {
            *move = q2_rerelease_float(context->game, 0, 1) <= 0.1f
                        ? Q2M_MOVE_infantry_move_attack5
                        : Q2M_MOVE_infantry_move_attack3;
            monster->next_frame = 244;
          }
        }
      }
      break;
    }
    if (melee ||
        distance <
            (context->game->options.rules == QA_RULESET_Q2_CLASSIC ? 80.0f : 20.0f)) {
      *move = Q2M_MOVE_infantry_move_attack2;
    } else if (context->game->options.rules == QA_RULESET_Q2_RERELEASE &&
               !monster->cocked) {
      *move = q2m_random(context->game) <= 0.1f ? Q2M_MOVE_infantry_move_attack5
                                                : Q2M_MOVE_infantry_move_attack3;
    } else {
      *move = Q2M_MOVE_infantry_move_attack1;
    }
    break;
  case Q2M_CHICK:
  case Q2M_CHICK_HEAT:
    if (rerelease && !melee) {
      bool clear;
      if (!q2m_clear_flash_shot(context, 57, &clear, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (!clear) {
        *move = Q2M_MOVE_NONE;
        break;
      }
      monster->dodging = false;
      if (monster->attack_state == Q2M_SLIDING)
        monster->attack_state = Q2M_STRAIGHT;
      if (blind) {
        float chance = monster->blind_fire_delay < 1.0f ? 1.0f
                       : monster->blind_fire_delay < 7.5f ? 0.4f : 0.1f;
        random = q2_rerelease_float(context->game, 0, 1);
        monster->blind_fire_delay +=
            (float)q2_rerelease_time_ms(context->game, 5500, 6500) / 1000.0f;
        if (qa_vec_equal(monster->blind_fire_target, qa_v3(0, 0, 0)) ||
            random > chance) {
          *move = Q2M_MOVE_NONE;
          break;
        }
        monster->manual_steering = true;
        monster->attack_ns = q2m_after(context->game->now_ns,
            (double)q2_rerelease_time_ms(context->game, 0, 2000) / 1000.0);
      }
      *move = Q2M_MOVE_chick_move_start_attack1;
    }
    break;
  case Q2M_SOLDIER_LIGHT:
  case Q2M_SOLDIER:
  case Q2M_SOLDIER_SS:
  case Q2M_SOLDIER_RIPPER:
  case Q2M_SOLDIER_HYPER:
  case Q2M_SOLDIER_LASER: {
    bool heavy = species == Q2M_SOLDIER_RIPPER || species == Q2M_SOLDIER_HYPER ||
                 species == Q2M_SOLDIER_LASER;
    if (rerelease) {
      monster->dodging = false;
      if (monster->attack_state == Q2M_SLIDING)
        monster->attack_state = Q2M_STRAIGHT;
      *move = Q2M_MOVE_NONE;
      if (blind) {
        float chance = monster->blind_fire_delay < 1.0f ? 1.0f
                       : monster->blind_fire_delay < 7.5f ? 0.4f : 0.1f;
        random = q2m_random(context->game);
        monster->blind_fire_delay +=
            4.1f + (float)q2_rerelease_time_ms(context->game, 0, 3000) / 1000.0f;
        if (qa_vec_equal(monster->blind_fire_target, qa_v3(0, 0, 0)) || random > chance)
          break;
        monster->manual_steering = true;
        *move = monster->style == 1 ? Q2M_MOVE_soldierh_move_attack1
                                    : Q2M_MOVE_soldier_move_attack1;
        monster->attack_ns = q2m_after(context->game->now_ns,
            (double)q2_rerelease_time_ms(context->game, 1500, 2500) / 1000.0);
        break;
      }
      random = q2m_random(context->game);
      if (!monster->source_blocked && !monster->stand_ground && random < 0.25f &&
          monster->count <= 3 && distance >= 220.0f) {
        *move = Q2M_MOVE_soldier_move_attack6;
        break;
      }
      if (monster->count < 4) {
        bool attack1 = false, attack2;
        if (monster->style != 0 || monster->count < 2 || monster->count > 3 ||
            distance > 286.0f) {
          if (!q2m_clear_flash_shot(context, 39, &attack1, error))
            return false;
          if (!q2m_alive(context))
            return true;
        }
        if (!q2m_clear_flash_shot(context, 40, &attack2, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (attack1 && (!attack2 || q2m_random(context->game) < 0.5f))
          *move = monster->style == 1 ? Q2M_MOVE_soldierh_move_attack1
                                      : Q2M_MOVE_soldier_move_attack1;
        else if (attack2)
          *move = monster->style == 1 ? Q2M_MOVE_soldierh_move_attack2
                                      : Q2M_MOVE_soldier_move_attack2;
      } else {
        bool clear;
        if (!q2m_clear_flash_shot(context, 88, &clear, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (clear)
          *move = Q2M_MOVE_soldier_move_attack4;
      }
    } else if (heavy) {
      *move = blind ? Q2M_MOVE_soldierh_move_attack1
              : q2m_random(context->game) < 0.5f ? Q2M_MOVE_soldierh_move_attack1
                                                 : Q2M_MOVE_soldierh_move_attack2;
      monster->manual_steering = blind;
    } else if (blind) {
      monster->manual_steering = true;
      *move = Q2M_MOVE_soldier_move_attack1;
      monster->attack_ns =
          q2m_after(context->game->now_ns, 1.5 + q2m_random(context->game));
    } else if (species == Q2M_SOLDIER_SS) {
      *move = Q2M_MOVE_soldier_move_attack4;
    } else {
      *move = q2m_random(context->game) < 0.5f ? Q2M_MOVE_soldier_move_attack1
                                               : Q2M_MOVE_soldier_move_attack2;
    }
    break;
  }
  case Q2M_BERSERK:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? Q2M_MOVE_berserk_move_attack_spike
                                               : Q2M_MOVE_berserk_move_attack_club;
    break;
  case Q2M_BRAIN:
    if (melee) {
      random = q2m_random(context->game);
      *move = (rerelease ? random <= 0.5f : random < 0.5f)
                  ? Q2M_MOVE_brain_move_attack1 : Q2M_MOVE_brain_move_attack2;
    } else if (rerelease) {
      *move = Q2M_MOVE_NONE;
      if (distance <= 440.0f && q2m_random(context->game) < 0.5f)
        *move = Q2M_MOVE_brain_move_attack3;
      else if (!(monster->spawnflags & 8u))
        *move = Q2M_MOVE_brain_move_attack4;
    }
    break;
  case Q2M_FLOATER:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? Q2M_MOVE_floater_move_attack3
                                               : Q2M_MOVE_floater_move_attack2;
    else if (rerelease) {
      if (q2m_random(context->game) > 0.5f) {
        monster->attack_state = Q2M_STRAIGHT;
        *move = Q2M_MOVE_floater_move_attack1;
      } else {
        if (q2m_random(context->game) <= 0.5f)
          monster->lefty = !monster->lefty;
        monster->attack_state = Q2M_SLIDING;
        *move = Q2M_MOVE_floater_move_attack1a;
      }
    }
    break;
  case Q2M_FLYER: {
    if (!rerelease) {
      *move = melee ? Q2M_MOVE_flyer_move_start_melee
                    : Q2M_MOVE_flyer_move_attack2;
      break;
    }
    if (context->combat.mass > 50) {
      *move = Q2M_MOVE_flyer_move_kamikaze;
      break;
    }
    if (melee) {
      *move = Q2M_MOVE_flyer_move_start_melee;
      q2m_flyer_flight(monster, true);
      break;
    }
    monster->attack_state = Q2M_STRAIGHT;
    bool visible = false;
    if (monster->enemy.registry != 0 &&
        !q2m_visible(context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (visible && distance <= 225.0f &&
        q2_rerelease_float(context->game, 0, 1) >
            (distance / 225.0f) * 0.35f) {
      *move = Q2M_MOVE_flyer_move_start_melee;
      q2m_flyer_flight(monster, true);
    } else {
      *move = Q2M_MOVE_flyer_move_attack2;
    }
    if (!monster->fly_pinned && q2_random_bounded(context->game, 2) == 0 &&
        monster->enemy.registry != 0) {
      if (!q2m_visible(context, monster->enemy, &visible, error))
        return false;
      if (!q2m_alive(context))
        return true;
      qa_body_state target;
      if (visible &&
          qa_world_body_read(context->game->services.world, monster->enemy,
                             &target, NULL)) {
        if (!q2m_alive(context))
          return true;
        monster->fly_pinned = true;
        monster->fly_position_ns = q2m_after(monster->fly_position_ns, 1.7);
        if (q2_random_bounded(context->game, 2) == 0)
          monster->fly_ideal_position = qa_vec_add(
              context->body.origin,
              qa_vec_scale(context->body.velocity,
                           q2_rerelease_float(context->game, 0, 1)));
        else
          monster->fly_ideal_position =
              qa_vec_add(monster->fly_ideal_position, target.origin);
      }
    }
    break;
  }
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    if (!melee && !rerelease && distance <= 112.0f) {
      *move = Q2M_MOVE_NONE;
      break;
    }
    if (!melee) {
      qa_body_state enemy;
      qa_builtin_actor_traits traits = {.view_height = 22.0f};
      qa_error ignored = {0};
      if (qa_world_body_read(context->game->services.world, monster->enemy,
                             &enemy, &ignored)) {
        if (!q2m_alive(context))
          return true;
        if (rerelease) {
          if (qa_vec_length(qa_vec_sub(context->body.origin, enemy.origin)) <= 82.0f &&
              monster->melee_ns <= context->game->now_ns) {
            *move = Q2M_MOVE_NONE;
            break;
          }
          bool clear;
          if (!q2m_clear_flash_shot(context, 61, &clear, error))
            return false;
          if (!q2m_alive(context))
            return true;
          if (!clear) {
            *move = Q2M_MOVE_NONE;
            break;
          }
        }
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
      } else if (rerelease) {
        *move = Q2M_MOVE_NONE;
        break;
      }
      bool plasma = monster->style == 1 && (rerelease || species == Q2M_GLADB);
      if (rerelease)
        *move = plasma ? Q2M_MOVE_gladb_move_attack_gun
                        : Q2M_MOVE_gladiator_move_attack_gun;
      if (!q2m_sound(context,
                     plasma
                         ? QA_Q2_SFX_WEAPONS_PLASSHOT
                         : QA_Q2_SFX_GLADIATOR_RAILGUN,
                     1, 1.0f, error))
        return false;
    }
    break;
  case Q2M_GUNNER:
    *move = distance < 80.0f || q2m_random(context->game) > 0.5f
                ? Q2M_MOVE_gunner_move_attack_chain
                : Q2M_MOVE_gunner_move_attack_grenade;
    break;
  case Q2M_GUN_COMMANDER:
    return q2m_guncmdr_attack_move(context, move, error);
  case Q2M_HOVER:
  case Q2M_DAEDALUS:
    *move = monster->attack_state == Q2M_SLIDING &&
                    q2m_move_find(monster, Q2M_MOVE_hover_move_start_attack2) != NULL
                ? Q2M_MOVE_hover_move_start_attack2
                : Q2M_MOVE_hover_move_start_attack;
    break;
  case Q2M_JORG:
    if ((context->game->options.rules == QA_RULESET_Q2_RERELEASE
             ? q2_rerelease_float(context->game, 0, 1) : q2m_random(context->game)) <= 0.75f) {
      if (!q2m_sound(context, QA_Q2_SFX_BOSS3_BS3ATCK1,
                       context->game->options.rules == QA_RULESET_Q2_RERELEASE ? 1 : 2,
                       1.0f, error))
        return false;
      if (!q2m_alive(context)) return true;
      if (!q2m_weapon_sound(context, QA_Q2_SFX_BOSS3_W_LOOP, error)) return false;
      *move = Q2M_MOVE_jorg_move_start_attack1;
    } else {
      if (!q2m_sound(context, QA_Q2_SFX_BOSS3_BS3ATCK2, 2, 1.0f, error))
        return false;
      *move = Q2M_MOVE_jorg_move_attack2;
    }
    break;
  case Q2M_MAKRON:
    random = q2m_random(context->game);
    *move = random <= 0.3f   ? Q2M_MOVE_makron_move_attack3
            : random <= 0.6f ? Q2M_MOVE_makron_move_attack4
                             : Q2M_MOVE_makron_move_attack5;
    break;
  case Q2M_MEDIC:
  case Q2M_MEDIC_COMMANDER:
    return q2m_medic_attack_move(context, distance, move, error);
  case Q2M_SUPERTANK:
    *move = distance <= 160.0f || q2m_random(context->game) < 0.3f
                ? Q2M_MOVE_supertank_move_attack1
                : Q2M_MOVE_supertank_move_attack2;
    break;
  case Q2M_BOSS5:
    *move = distance <= 160.0f || q2m_random(context->game) < 0.3f
                ? Q2M_MOVE_boss5_move_attack1
                : Q2M_MOVE_boss5_move_attack2;
    break;
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER: {
    double health;
    if (!attack_health(context, monster->enemy, &health, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (context->game->options.rules == QA_RULESET_Q2_RERELEASE ? health <= 0 : health < 0) {
      monster->brutal = false;
      *move = Q2M_MOVE_tank_move_attack_strike;
      break;
    }
    if (rerelease) {
      *move = Q2M_MOVE_NONE;
      if (blind) {
        float chance = monster->blind_fire_delay < 1.0f ? 1.0f
                       : monster->blind_fire_delay < 7.5f ? 0.4f : 0.1f;
        random = q2_rerelease_float(context->game, 0, 1);
        monster->blind_fire_delay +=
            5.2f + (float)q2_rerelease_time_ms(context->game, 0, 3000) / 1000.0f;
        if (qa_vec_equal(monster->blind_fire_target, qa_v3(0, 0, 0)) ||
            random > chance)
          break;
        bool rocket, blaster;
        if (!q2m_clear_flash_shot(context, 23, &rocket, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (!q2m_clear_flash_shot(context, 1, &blaster, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (!rocket && !blaster)
          break;
        bool use_rocket = rocket && blaster
                              ? q2_random_bounded(context->game, 2) == 0
                              : rocket;
        monster->manual_steering = true;
        if (use_rocket) {
          *move = Q2M_MOVE_tank_move_attack_fire_rocket;
        } else {
          *move = Q2M_MOVE_tank_move_attack_blast;
          monster->next_frame = 62;
        }
        monster->attack_ns = q2m_after(context->game->now_ns,
            (double)q2_rerelease_time_ms(context->game, 3000, 5000) / 1000.0);
        monster->pain_ns = q2m_after(context->game->now_ns, 5);
        break;
      }
      qa_body_state enemy;
      bool present;
      if (!attack_body(context, monster->enemy, &enemy, &present, error))
        return false;
      if (!q2m_alive(context) || !present)
        return true;
      float range = qa_vec_length(qa_vec_sub(enemy.origin, context->body.origin));
      random = q2_rerelease_float(context->game, 0, 1);
      bool allow_machinegun = true;
      if (range <= 250.0f) {
        qa_builtin_actor_traits traits;
        if (!actor_traits(context, monster->enemy, &traits, error))
          return false;
        if (!q2m_alive(context))
          return true;
        allow_machinegun = traits.classname !=
            context->game->runtime_names[Q2_NAME_TESLA_MINE];
      }
      bool machinegun = false, rocket = false, blaster;
      if (allow_machinegun) {
        if (!q2m_clear_flash_shot(context, 8, &machinegun, error))
          return false;
        if (!q2m_alive(context))
          return true;
      }
      if (range > 250.0f) {
        if (!q2m_clear_flash_shot(context, 23, &rocket, error))
          return false;
        if (!q2m_alive(context))
          return true;
      }
      if (machinegun && random < (range <= 125.0f ? 0.5f
                                 : range <= 250.0f ? 0.25f : 0.33f)) {
        *move = Q2M_MOVE_tank_move_attack_chain;
      } else if (range > 250.0f && rocket && random < 0.66f) {
        *move = Q2M_MOVE_tank_move_attack_pre_rocket;
        monster->pain_ns = q2m_after(context->game->now_ns, 5);
      } else {
        if (!q2m_clear_flash_shot(context, 1, &blaster, error))
          return false;
        if (!q2m_alive(context))
          return true;
        if (blaster)
          *move = Q2M_MOVE_tank_move_attack_blast;
      }
      break;
    }
    if (blind) {
      monster->manual_steering = true;
      *move = Q2M_MOVE_tank_move_attack_pre_rocket;
      break;
    }
    random = q2m_random(context->game);
    if (distance <= 125.0f)
      *move =
          random < 0.4f ? Q2M_MOVE_tank_move_attack_chain : Q2M_MOVE_tank_move_attack_blast;
    else if (distance <= 250.0f)
      *move =
          random < 0.5f ? Q2M_MOVE_tank_move_attack_chain : Q2M_MOVE_tank_move_attack_blast;
    else if (random < 0.33f)
      *move = Q2M_MOVE_tank_move_attack_chain;
    else if (random < 0.66f) {
      monster->pain_ns = q2m_after(context->game->now_ns, 5.0);
      *move = Q2M_MOVE_tank_move_attack_pre_rocket;
    } else
      *move = Q2M_MOVE_tank_move_attack_blast;
    break;
  }
  case Q2M_BOSS2: {
    bool guns = distance <= 125.0f || q2m_random(context->game) <= 0.6f;
    bool n64 = context->game->options.rules == QA_RULESET_Q2_RERELEASE &&
               (monster->spawnflags & 8u) != 0;
    *move = guns  ? n64 ? Q2M_MOVE_boss2_move_attack_hb : Q2M_MOVE_boss2_move_attack_pre_mg
            : n64 ? Q2M_MOVE_boss2_move_attack_rocket2
                  : Q2M_MOVE_boss2_move_attack_rocket;
    break;
  }
  case Q2M_ACTOR:
    if (context->game->options.rules == QA_RULESET_Q2_RERELEASE)
      monster->fire_ns = q2m_after(context->game->now_ns, 1.0 + q2m_random(context->game) * 1.6);
    else
      monster->pause_ns = q2m_after(context->game->now_ns,
          (10.0 + floorf(q2m_random(context->game) * 16.0f)) * 0.1);
    *move = Q2M_MOVE_actor_move_attack;
    break;
  case Q2M_FIXBOT:
    *move = Q2M_MOVE_fixbot_move_attack2;
    q2m_fixbot_flight(context, false, false);
    break;
  case Q2M_GEKK:
    if (melee) {
      if (context->actor->physics.water_level >= (rerelease ? 2 : 1))
        *move = Q2M_MOVE_gekk_move_attack;
      else {
        float draw = rerelease ? q2_rerelease_float(context->game, 0, 1)
                               : q2m_random(context->game);
        *move = draw > (rerelease ? .66f : .66) ? Q2M_MOVE_gekk_move_attack1
                                               : Q2M_MOVE_gekk_move_attack2;
      }
    } else if (rerelease && (context->actor->physics.flags & QA_PHYSICS_SWIMMING)) {
      qa_physics_properties target;
      bool submerged = context->game->services.physics != NULL &&
          context->game->services.physics->services.read != NULL &&
          context->game->services.physics->services.read(
              context->game->services.physics->services.context,
              monster->enemy, &target) && target.water_level >= 2;
      if (!q2m_alive(context))
        return true;
      if (submerged && distance <= 440.0f) {
        *move = Q2M_MOVE_NONE;
      } else {
        context->actor->physics.flags &= ~(uint32_t)QA_PHYSICS_SWIMMING;
        monster->alternate_fly = false;
        *move = Q2M_MOVE_gekk_move_leapatk;
        monster->next_frame = 75;
      }
    } else if (!rerelease) {
      if ((context->actor->physics.flags & QA_PHYSICS_SWIMMING) ||
          context->actor->physics.water_level != 0) {
        *move = Q2M_MOVE_NONE;
      } else if ((q2m_random(context->game) > .5 && distance >= 80.0f) ||
                 q2m_random(context->game) > .8) {
        *move = Q2M_MOVE_gekk_move_spit;
      } else {
        *move = Q2M_MOVE_gekk_move_leapatk;
      }
    } else if (distance >= 940.0f) {
      if (q2_rerelease_float(context->game, 0, 1) > .5f)
        *move = Q2M_MOVE_gekk_move_spit;
      else {
        *move = Q2M_MOVE_gekk_move_run_start;
        monster->attack_ns = q2m_after(context->game->now_ns, 2);
      }
    } else if (q2_rerelease_float(context->game, 0, 1) > .7f) {
      *move = Q2M_MOVE_gekk_move_spit;
    } else if ((monster->spawnflags & 16u) ||
               q2_rerelease_float(context->game, 0, 1) > .7f) {
      *move = Q2M_MOVE_gekk_move_run_start;
      monster->attack_ns = q2m_after(context->game->now_ns, 1.4);
    } else {
      *move = Q2M_MOVE_gekk_move_leapatk;
    }
    break;
  case Q2M_STALKER:
    if (melee)
      *move = q2m_random(context->game) < 0.5f ? Q2M_MOVE_stalker_move_swing_l
                                               : Q2M_MOVE_stalker_move_swing_r;
    else {
      float luck = q2m_random(context->game);
      if (context->game->options.rules == QA_RULESET_Q2_RERELEASE
              ? luck > 0.5f
              : context->game->options.skill == 0 ||
                    luck > 1.0f - 0.5f / (float)context->game->options.skill)
        monster->attack_state = Q2M_STRAIGHT;
      else {
        if (q2m_random(context->game) <= 0.5f)
          monster->lefty = !monster->lefty;
        monster->attack_state = Q2M_SLIDING;
      }
      *move = Q2M_MOVE_stalker_move_shoot;
    }
    break;
  case Q2M_TURRET:
    *move = blind ? Q2M_MOVE_turret_move_fire_blind : Q2M_MOVE_turret_move_fire;
    break;
  case Q2M_CARRIER: {
    monster->hold_frame = false;
    if (blind) {
      *move = Q2M_MOVE_carrier_move_spawn;
      break;
    }
    qa_body_state target;
    qa_error ignored = {0};
    if (!qa_world_body_read(context->game->services.world, monster->enemy,
                            &target, &ignored)) {
      *move = Q2M_MOVE_NONE;
      break;
    }
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(target.origin, context->body.origin));
    qa_vec3 forward;
    qa_angle_vectors(context->body.angles, &forward, NULL, NULL,QA_ANGLE_FLOAT_MATH);
    float facing = qa_vec_dot(direction, forward);
    bool front = facing > 0.3f;
    bool back = facing < -0.3f;
    bool below = -direction.z > 0.95f;
    if (back || below) {
      *move = Q2M_MOVE_carrier_move_attack_rocket;
      break;
    }
    if (!front) {
      *move = q2m_random(context->game) < 0.1f ? Q2M_MOVE_carrier_move_attack_pre_mg
                                               : Q2M_MOVE_carrier_move_attack_rail;
      break;
    }
    random = q2m_random(context->game);
    if (distance <= 125.0f)
      *move = random < 0.8f ? Q2M_MOVE_carrier_move_attack_pre_mg
                            : Q2M_MOVE_carrier_move_attack_rail;
    else if (distance < 600.0f && monster->monster_slots > 2)
      *move = random <= 0.2f   ? Q2M_MOVE_carrier_move_attack_pre_mg
              : random <= 0.4f ? Q2M_MOVE_carrier_move_attack_pre_gren
              : random <= 0.7f ? Q2M_MOVE_carrier_move_attack_rail
                               : Q2M_MOVE_carrier_move_spawn;
    else if (distance < 600.0f)
      *move = random <= 0.3f    ? Q2M_MOVE_carrier_move_attack_pre_mg
              : random <= 0.65f ? Q2M_MOVE_carrier_move_attack_pre_gren
                                : Q2M_MOVE_carrier_move_attack_rail;
    else if (monster->monster_slots > 2)
      *move = random < 0.3f    ? Q2M_MOVE_carrier_move_attack_pre_mg
              : random < 0.65f ? Q2M_MOVE_carrier_move_attack_rail
                               : Q2M_MOVE_carrier_move_spawn;
    else
      *move = random < 0.45f ? Q2M_MOVE_carrier_move_attack_pre_mg
                             : Q2M_MOVE_carrier_move_attack_rail;
    if (*move != Q2M_MOVE_NONE && (*move == Q2M_MOVE_carrier_move_attack_rail) &&
        !q2m_sound(context, QA_Q2_SFX_GLADIATOR_RAILGUN, 1, 1.0f, error))
      return false;
    break;
  }
  case Q2M_WIDOW:
    if (melee)
      *move = Q2M_MOVE_widow_move_attack_kick;
    else
      return q2m_widow_attack_move(context, false, distance, move, error);
    break;
  case Q2M_WIDOW2:
    if (melee)
      *move = Q2M_MOVE_widow2_move_tongs;
    else
      return q2m_widow_attack_move(context, true, distance, move, error);
    break;
  case Q2M_ARACHNID: {
    qa_body_state target;
    qa_error ignored = {0};
    if (melee ||
        (monster->melee_ns < context->game->now_ns && distance < 80.0f)) {
      *move = Q2M_MOVE_arachnid_melee;
    } else if (qa_world_body_read(context->game->services.world, monster->enemy,
                                  &target, &ignored) &&
               target.origin.z - context->body.origin.z > 150.0f) {
      *move = Q2M_MOVE_arachnid_attack_up1;
    } else {
      *move = Q2M_MOVE_arachnid_attack1;
    }
    break;
  }
  case Q2M_GUARDIAN:
    *move = distance > 440.0f ? Q2M_MOVE_guardian_move_atk2_in
            : monster->melee_ns < context->game->now_ns && distance < 120.0f
                ? Q2M_MOVE_guardian_move_kick
                : Q2M_MOVE_guardian_move_atk1_in;
    break;
  case Q2M_SHAMBLER:
    if (melee) {
      random = q2m_random(context->game);
      *move = random > 0.6f || context->combat.health == 600.0f
                  ? Q2M_MOVE_shambler_attack_smash
              : random > 0.3f ? Q2M_MOVE_shambler_attack_swingl
                              : Q2M_MOVE_shambler_attack_swingr;
    } else {
      *move = Q2M_MOVE_shambler_attack_magic;
    }
    break;
  default:
    break;
  }
  return true;
}

static bool apply_species_attack(q2m_context *context, q2m_move_id move,
                                 qa_error *error) {
  if (!q2m_alive(context) || move == Q2M_MOVE_NONE)
    return true;
  bool rerelease = context->monster->definition->source_rules == QA_RULESET_Q2_RERELEASE;
  q2m_species species = context->monster->definition->species;
  bool immediate = rerelease &&
      (species == Q2M_JORG || species == Q2M_MEDIC ||
       species == Q2M_MEDIC_COMMANDER || species == Q2M_GUN_COMMANDER);
  if (!q2m_set_move(context, move, immediate, error))
    return false;
  return !q2m_alive(context) || !rerelease || species != Q2M_GUN_COMMANDER ||
         move != Q2M_MOVE_guncmdr_move_attack_mortar ||
         q2m_callback_run(context, Q2M_CALLBACK_monster_duck_down, error);
}

bool q2m_source_attack(q2m_context *context, bool melee, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  q2m_species species = context->monster->definition->species;
  if (!melee && context->monster->medic &&
      (species == Q2M_MEDIC || species == Q2M_MEDIC_COMMANDER)) {
    if (!q2_actor_live(context->game, context->monster->enemy))
      return true;
    q2m_move_id move = Q2M_MOVE_NONE;
    if (!q2m_medic_attack_move(context, INFINITY, &move, error))
      return false;
    return !q2m_alive(context) || !move || q2m_set_move(context, move,
        context->game->options.rules == QA_RULESET_Q2_RERELEASE, error);
  }
  qa_builtin_actor_traits traits;
  qa_body_state enemy;
  bool living;
  if (!target_alive(context, context->monster->enemy, &traits, &enemy, &living, error))
    return false;
  if (!q2m_alive(context) || !living)
    return true;
  if (species != Q2M_GUN_COMMANDER)
    context->monster->attack_state = melee ? Q2M_MELEE : Q2M_MISSILE;
  q2m_move_id move;
  if (!select_species_attack(context, &move, error))
    return false;
  return apply_species_attack(context, move, error);
}

static bool attack_selected(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  qa_body_state target;
  bool present;
  if (!attack_body(context, monster->enemy, &target, &present, error))
    return false;
  if (!q2m_alive(context) || !present)
    return true;
  monster->ideal_yaw = vector_yaw(qa_vec_sub(target.origin, context->body.origin));
  if (!monster->manual_steering && !q2m_change_yaw(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  float delta =
      fabsf(qa_angle_delta(monster->ideal_yaw, context->body.angles.y));
  if (delta > 45)
    return true;
  bool melee = monster->attack_state == Q2M_MELEE;
  q2m_move_id move;
  if (!select_species_attack(context, &move, error))
    return false;
  if (!q2m_alive(context))
    return true;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  q2m_species species = monster->definition->species;
  if (move == Q2M_MOVE_NONE && species != Q2M_GEKK &&
      !(rerelease && (species == Q2M_INFANTRY || species == Q2M_CHICK ||
                     species == Q2M_CHICK_HEAT || species == Q2M_TANK ||
                     species == Q2M_TANK_COMMANDER || species == Q2M_GUN_COMMANDER)))
    return true;
  if (!apply_species_attack(context, move, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (rerelease && !melee) {
    switch (monster->definition->species) {
    case Q2M_SOLDIER_LIGHT:
    case Q2M_SOLDIER:
    case Q2M_SOLDIER_SS:
    case Q2M_SOLDIER_RIPPER:
    case Q2M_SOLDIER_HYPER:
    case Q2M_SOLDIER_LASER:
      break;
    case Q2M_GEKK:
    case Q2M_INFANTRY:
    case Q2M_CHICK:
    case Q2M_CHICK_HEAT:
    case Q2M_TANK:
    case Q2M_TANK_COMMANDER:
      monster->attack_ns = q2m_after(context->game->now_ns,
          (double)q2_rerelease_time_ms(context->game, 1000, 2000) / 1000.0);
      break;
    default:
      monster->attack_ns = q2m_after(context->game->now_ns, 1 + q2m_random(context->game));
      break;
    }
  }
  if (!rerelease || pending_attack(monster, true))
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
      .policy = qa_collision_ruleset_policy(context->game->options.rules),
  };
  query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x00020003), QA_GAME_Q2);
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
  if (context->game->options.rules != QA_RULESET_Q2_RERELEASE ||
      !monster->alternate_fly)
    return true;
  *handled = true;
  if ((context->actor->physics.flags & QA_PHYSICS_SWIMMING) && monster->water_level < 3)
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
  qa_angle_vectors(context->body.angles, &forward, &right, NULL,QA_ANGLE_FLOAT_MATH);
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
      .policy = qa_collision_ruleset_policy(context->game->options.rules),
  };
  qa_point_contents contents;
  if (!qa_world_point_contents(context->game->services.world, &point, &contents,
                               error))
    return false;
  bool water_ahead = ((uint32_t)qa_collision_point_contents_export(contents.contents, QA_GAME_Q2, contents.q1_opaque_token) & UINT32_C(32)) != 0;
  bool swimming = (context->actor->physics.flags & QA_PHYSICS_SWIMMING) != 0;
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
    float desired = qa_vec_angles(pitch_direction, QA_DIRECTION_FLOAT, 0).x;
    if (desired - context->body.angles.x > 180.0f)
      desired -= 360.0f;
    if (desired - context->body.angles.x < -180.0f)
      desired += 360.0f;
    context->body.angles.x +=
        context->elapsed * 4.0f * (desired - context->body.angles.x);
  } else {
    context->body.angles.x = 0.0f;
  }
  return q2m_write_body(context, false, error);
}

static uint32_t monster_move_mask(const qa_q2_game *game) {
  return Q2M_MONSTER_MASK |
         (game->options.rules == QA_RULESET_Q2_RERELEASE ? UINT32_C(0x40000000)
                                                    : 0u);
}

static bool pursuit_trace(q2m_context *context, qa_vec3 end, bool shaped,
                          qa_trace_result *trace, qa_error *error) {
  qa_trace_query query = {
      .start = context->body.origin,
      .end = end,
      .pass_actor = context->actor->id,
      .policy = qa_collision_ruleset_policy(context->game->options.rules),
  };
  if (shaped)
    query.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX,
                                   .bounds = context->body.bounds};
  query.policy.contents_mask =
      qa_collision_contents_mask(shaped ? monster_move_mask(context->game) : Q2M_OPAQUE_MASK, QA_GAME_Q2);
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
      q2_actor *marker;
      if (!trail_pick(context, !monster->pursuit_last_seen, &marker, error)) return false;
      monster->pursuit_last_seen = false;
      if (marker) {
        qa_body_state point;
        if (!qa_world_body_read(context->game->services.world, marker->id, &point, error))
          return false;
        monster->last_sighting = point.origin;
        monster->trail_ns = marker->entity->timestamp_ns;
        monster->ideal_yaw = point.angles.y;
        if (!set_body_yaw(context, point.angles.y, error)) return false;
        if (!q2m_alive(context)) return true;
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
      qa_angle_vectors(angles, &forward, &right, NULL,QA_ANGLE_FLOAT_MATH);
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
  if (!qa_physics_step_direction(context->game->services.physics,
                                  context->actor->id, yaw, distance,
                                  context->elapsed, moved, error))
    return false;
  if (*moved && q2m_alive(context))
    context->monster->source_blocked = false;
  return true;
}

static bool chase_goal(q2m_context *context, qa_vec3 goal, float distance,
                       bool *moved, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  float old = truncf(monster->ideal_yaw / 45.0f) * 45.0f;
  float turnaround = qa_angle_wrap(old - 180.0f);
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
  bool medic_blocked = monster->definition->species == Q2M_MEDIC_COMMANDER ||
      (monster->definition->species == Q2M_MEDIC &&
       (context->game->options.rules == QA_RULESET_Q2_RERELEASE ||
        context->game->options.product == QA_Q2_ROGUE));
  for (size_t index = 0; index < sizeof(directions) / sizeof(directions[0]);
       ++index) {
    if (index == 2 &&
        (monster->definition->species == Q2M_STALKER || medic_blocked)) {
      if (!q2m_refresh(context, error))
        return !q2m_alive(context);
      bool accepted;
      if (!(medic_blocked
                ? q2m_medic_blocked(context, distance, &accepted, error)
                : q2m_stalker_blocked(context, distance, &accepted, error)))
        return false;
      if (!q2m_alive(context) || accepted) {
        *moved = true;
        return true;
      }
    }
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
      !qa_actor_reference_present(context->body.ground) &&
      !(context->actor->physics.flags & (QA_PHYSICS_FLYING | QA_PHYSICS_SWIMMING)))
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
  if (monster->sound_target.present) {
    goal = monster->sound_target.origin;
    has_goal = true;
  }
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
  context->actor->physics.goal = qa_actor_reference_lifetime(goal_actor);
  context->actor->physics.enemy = qa_actor_reference_lifetime(monster->enemy);
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
  if (!moved && q2m_alive(context) && monster->source_blocked) {
    monster->source_blocked = false;
    return true;
  }
  if (!moved && q2m_alive(context) &&
      !chase_goal(context, goal, distance, &moved, error))
    return false;
  return !q2m_alive(context) || q2m_refresh(context, error);
}

static bool walk_move(q2m_context *context, float yaw, float distance, bool *moved, qa_error *error) {
  if (!qa_physics_walk_move(context->game->services.physics, context->actor->id,
                            yaw, distance, context->elapsed, true, true, moved, error))
    return false;
  if (q2m_alive(context))
    context->monster->source_blocked = false;
  return true;
}

static bool frame_move(q2m_context *context, float distance, qa_error *error) {
  if (!walk_move(context, context->body.angles.y, distance, &(bool){false}, error))
    return false;
  return !q2m_alive(context) || q2m_refresh(context, error);
}

static bool run_slide(q2m_context *context, float distance, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  bool extended = rerelease || context->game->options.product == QA_Q2_ROGUE;
  qa_body_state enemy;
  bool present;
  if (!attack_body(context, monster->enemy, &enemy, &present, error))
    return false;
  if (!q2m_alive(context) || !present)
    return true;
  monster->ideal_yaw = vector_yaw(qa_vec_sub(enemy.origin, context->body.origin));
  if ((!extended || !monster->manual_steering) && !q2m_change_yaw(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (rerelease && !(context->actor->physics.flags & QA_PHYSICS_FLYING))
    distance = fminf(distance, 8.0f / ((float)context->game->frame_ns / 10000000.0f));
  float side = monster->lefty ? 90.0f : -90.0f;
  bool moved;
  if (!walk_move(context, monster->ideal_yaw + side, distance, &moved, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_refresh(context, error))
    return false;
  if (moved)
    return true;
  if (extended && monster->dodging) {
    if (!q2m_callback_run(context, Q2M_CALLBACK_monster_done_dodge, error))
      return false;
    if (q2m_alive(context))
      monster->attack_state = Q2M_STRAIGHT;
    return true;
  }
  monster->lefty = !monster->lefty;
  if (!walk_move(context, monster->ideal_yaw - side, distance, &moved, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_refresh(context, error))
    return false;
  if (extended && !moved) {
    if (monster->dodging &&
        !q2m_callback_run(context, Q2M_CALLBACK_monster_done_dodge, error))
      return false;
    if (q2m_alive(context))
      monster->attack_state = Q2M_STRAIGHT;
  }
  return true;
}

static bool run_sighting(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  qa_body_state enemy;
  bool present;
  if (!attack_body(context, monster->enemy, &enemy, &present, error))
    return false;
  if (!q2m_alive(context) || !present)
    return true;
  monster->lost_sight = false;
  monster->last_sighting = enemy.origin;
  monster->trail_ns = context->game->now_ns;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  if (rerelease || context->game->options.product == QA_Q2_ROGUE) {
    monster->blind_fire_target = rerelease
        ? qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, -.1f)) : enemy.origin;
    monster->blind_fire_delay = 0.0f;
  }
  if (rerelease) {
    monster->saved_goal = enemy.origin;
    monster->has_saved_goal = true;
  }
  return true;
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
  qa_angle_vectors(context->body.angles, &forward, NULL, NULL,QA_ANGLE_FLOAT_MATH);
  *eligible = qa_vec_dot(forward, qa_vec_normalize(difference)) >= 0.8f;
  return true;
}

static bool idle_search(q2m_context *context, bool search, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  q2_runtime_name sound = 0, alternate = 0, third = 0;
  q2m_move_id move = Q2M_MOVE_NONE;
  q2m_callback_id callback = Q2M_CALLBACK_NONE;
  int channel = 2;
  float attenuation = search ? 1.0f : 2.0f;
  bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
  bool empty_search = false;

  switch (species) {
  case Q2M_INFANTRY:
    if (!search) {
      sound = QA_Q2_SFX_INFANTRY_INFIDLE1;
      move = Q2M_MOVE_infantry_move_fidget;
    }
    break;
  case Q2M_FLYER:
  case Q2M_KAMIKAZE:
    if (!search) sound = QA_Q2_SFX_FLYER_FLYSRCH1;
    break;
  case Q2M_FLOATER:
    if (!search) sound = QA_Q2_SFX_FLOATER_FLTIDLE1;
    break;
  case Q2M_TANK:
  case Q2M_TANK_COMMANDER:
    if (!search) sound = QA_Q2_SFX_TANK_TNKIDLE1;
    break;
  case Q2M_GLADIATOR:
  case Q2M_GLADB:
    sound = search ? QA_Q2_SFX_GLADIATOR_GLDSRCH1 : QA_Q2_SFX_GLADIATOR_GLDIDLE1;
    break;
  case Q2M_MUTANT:
    sound = search ? QA_Q2_SFX_MUTANT_MUTSRCH1 : QA_Q2_SFX_MUTANT_MUTIDLE1;
    if (!search) move = Q2M_MOVE_mutant_move_idle;
    break;
  case Q2M_PARASITE:
    if (!search) move = Q2M_MOVE_parasite_move_start_fidget;
    break;
  case Q2M_BRAIN:
    sound = search ? QA_Q2_SFX_BRAIN_BRNSRCH1 : QA_Q2_SFX_BRAIN_BRNLENS1;
    if (!search) {
      channel = 0;
      move = Q2M_MOVE_brain_move_idle;
    }
    break;
  case Q2M_MEDIC:
  case Q2M_MEDIC_COMMANDER:
    callback = search ? Q2M_CALLBACK_medic_search : Q2M_CALLBACK_medic_idle;
    break;
  case Q2M_GEKK:
    if (search) {
      callback = Q2M_CALLBACK_gekk_search;
    } else {
      bool swimming = rerelease
          ? !(monster->spawnflags & 32u) && context->actor->physics.water_level >= 2
          : context->actor->physics.water_level != 0;
      move = swimming ? Q2M_MOVE_gekk_move_swim_start : Q2M_MOVE_gekk_move_idle;
    }
    break;
  case Q2M_STALKER:
    if (!search) move = Q2M_MOVE_stalker_move_idle;
    break;
  case Q2M_SHAMBLER:
    if (!search && rerelease) sound = QA_Q2_SFX_SHAMBLER_SIDLE;
    break;
  case Q2M_WIDOW:
  case Q2M_TURRET:
    empty_search = search;
    break;
  case Q2M_BERSERK:
    if (search) {
      sound = QA_Q2_SFX_BERSERK_BERSRCH1;
      if (rerelease) alternate = QA_Q2_SFX_BERSERK_IDLE;
    }
    break;
  case Q2M_GUNNER:
    if (search) sound = QA_Q2_SFX_GUNNER_GUNSRCH1;
    break;
  case Q2M_GUN_COMMANDER:
    if (search) sound = QA_Q2_SFX_GUNCMDR_GCDRSRCH1;
    break;
  case Q2M_HOVER:
  case Q2M_DAEDALUS:
    if (search) {
      bool daedalus = (rerelease || context->game->options.product == QA_Q2_ROGUE) &&
                      context->combat.mass >= 225;
      sound = daedalus ? QA_Q2_SFX_DAEDALUS_DAEDSRCH1 : QA_Q2_SFX_HOVER_HOVSRCH1;
      alternate = daedalus ? QA_Q2_SFX_DAEDALUS_DAEDSRCH2 : QA_Q2_SFX_HOVER_HOVSRCH2;
    }
    break;
  case Q2M_JORG:
    if (search) {
      sound = QA_Q2_SFX_BOSS3_BS3SRCH1;
      alternate = QA_Q2_SFX_BOSS3_BS3SRCH2;
      third = QA_Q2_SFX_BOSS3_BS3SRCH3;
    }
    break;
  case Q2M_BOSS2:
  case Q2M_WIDOW2:
    if (search) {
      sound = QA_Q2_SFX_BOSSHOVR_BHVUNQV1;
      attenuation = 0;
    }
    break;
  case Q2M_SUPERTANK:
  case Q2M_BOSS5:
    if (search) {
      sound = QA_Q2_SFX_BOSSTANK_BTKUNQV1;
      alternate = QA_Q2_SFX_BOSSTANK_BTKUNQV2;
    }
    break;
  default:
    break;
  }
  if ((!sound && move == Q2M_MOVE_NONE && callback == Q2M_CALLBACK_NONE && !empty_search) ||
      (!search && (monster->spawnflags & 1u)) ||
      context->game->now_ns <= monster->idle_ns)
    return true;

  bool play = monster->idle_ns != 0;
  if (play && !(species == Q2M_INFANTRY && rerelease && monster->enemy.registry)) {
    if (callback != Q2M_CALLBACK_NONE) {
      if (!q2m_callback_run(context, callback, error))
        return false;
    } else {
      if (third) {
        float choice = rerelease ? q2_rerelease_float(context->game, 0, 1)
                                 : q2m_random(context->game);
        if (choice > (rerelease ? 0.6f : 0.6)) sound = third;
        else if (choice > (rerelease ? 0.3f : 0.3)) sound = alternate;
      } else if (alternate) {
        if (species == Q2M_BERSERK ? q2_random_bounded(context->game, 2) == 0
                                  : (rerelease ? q2_rerelease_float(context->game, 0, 1)
                                               : q2m_random(context->game)) >= 0.5f)
          sound = alternate;
      } else if ((species == Q2M_BOSS2 || species == Q2M_WIDOW2) &&
                 (rerelease ? q2_rerelease_float(context->game, 0, 1)
                            : q2m_random(context->game)) >= 0.5f) {
        sound = 0;
      }
      if (species == Q2M_STALKER &&
          (rerelease ? q2_rerelease_float(context->game, 0, 1)
                     : q2m_random(context->game)) >= (rerelease ? 0.35f : 0.35))
        move = Q2M_MOVE_stalker_move_idle2;
      if (move != Q2M_MOVE_NONE && species != Q2M_BRAIN) {
        if (!q2m_set_move(context, move, false, error))
          return false;
        if (!q2m_alive(context))
          return true;
      }
      if (sound && !q2m_sound(context, sound, channel, attenuation, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (move != Q2M_MOVE_NONE && species == Q2M_BRAIN &&
          !q2m_set_move(context, move, false, error))
        return false;
    }
  }
  if (q2m_alive(context)) {
    double delay = rerelease
                       ? (double)q2_rerelease_time_ms(context->game,
                                                     play ? 15000 : 0,
                                                     play ? 30000 : 15000) / 1000.0
                       : (play ? 15.0 : 0.0) + q2m_random(context->game) * 15.0;
    monster->idle_ns = q2m_after(context->game->now_ns, delay);
  }
  return true;
}

bool q2m_run_ai(q2m_context *context, q2m_ai_kind kind,
                float distance, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
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
        if (!(monster->definition->species == Q2M_FLOATER
                  ? q2m_run_state(context, error)
                  : q2m_set_move(context, monster->definition->run_move, false, error)))
          return false;
      }
      if ((context->game->options.product != QA_Q2_ROGUE || !monster->manual_steering) &&
          !q2m_change_yaw(context, error))
        return false;
      if (!q2m_alive(context))
        return true;
      bool selected = false, started = false, visible;
      if (!q2m_check_attack(context, &selected, &started, &visible, error))
        return false;
      return !q2m_alive(context) || !selected || started || attack_selected(context, error);
    }
    bool found;
    if (!q2m_find_target(context, &found, error))
      return false;
    if (!q2m_alive(context) || found || monster->stand_ground)
      return true;
    if (context->game->now_ns > monster->pause_ns)
      return q2m_set_move(context, monster->definition->walk_move, false, error);
    return idle_search(context, false, error);
  }
  case Q2M_AI_WALK: {
    if (!q2m_move_to_goal(context, distance, error))
      return false;
    bool found;
    if (!q2m_alive(context))
      return true;
    if (!q2m_find_target(context, &found, error))
      return false;
    if (!q2m_alive(context) || found)
      return true;
    return idle_search(context, true, error);
  }
  case Q2M_AI_RUN: {
    bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
    bool rogue = !rerelease && context->game->options.product == QA_Q2_ROGUE;
    bool extended = rerelease || rogue;
    if (monster->combat_point)
      return q2m_move_to_goal(context, distance, error);
    if (rogue) {
      monster->ducked = false;
      if (context->body.bounds.maxs.z != monster->normal_height &&
          !q2m_callback_run(context, Q2M_CALLBACK_monster_duck_up, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    bool hint_handled;
    if (!q2_hint_run(context, distance, &hint_handled, error)) return false;
    if (hint_handled || !q2m_alive(context)) return true;
    bool already_moved = false;
    if (monster->sound_target.present) {
      sound_target_refresh(context);
      bool touching;
      if (rerelease) {
        qa_body_state noise = {.origin = monster->sound_target.origin,
            .bounds = {{-8, -8, -8}, {8, 8, 8}}};
        touching = boxes_close(&context->body, &noise,
            distance * ((float)Q2M_TENTH / (float)context->game->frame_ns));
      } else {
        touching = qa_vec_length(qa_vec_sub(context->body.origin,
                                           monster->sound_target.origin)) < 64.0f;
      }
      bool facing = fabsf(qa_angle_delta(monster->ideal_yaw,
                                       context->body.angles.y)) <= 45.0f;
      if (touching && (!rerelease || facing)) {
        monster->stand_ground = monster->temporary_stand_ground = true;
        if (rerelease) {
          context->body.angles.y = monster->ideal_yaw;
          if (!q2m_write_body(context, false, error))
            return false;
          if (!q2m_alive(context))
            return true;
          monster->close_sight_tripped = false;
        }
        return q2m_stand_state(context, error);
      }
      if (!(rerelease && touching ? q2m_change_yaw(context, error)
                                  : q2m_move_to_goal(context, distance, error)))
        return false;
      if (!q2m_alive(context))
        return true;
      already_moved = extended;
      bool found;
      if (!q2m_find_target(context, &found, error))
        return false;
      if (!q2m_alive(context) || !found)
        return true;
    }
    if (monster->enemy.registry == 0) {
      bool found;
      if (!q2m_find_target(context, &found, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    bool selected, started, visible;
    if (!q2m_check_attack(context, &selected, &started, &visible, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (selected && !started && !attack_selected(context, error))
      return false;
    if (!q2m_alive(context) || (!extended && selected))
      return true;
    if (extended) {
      if (!visible && monster->attack_state == Q2M_SLIDING)
        monster->attack_state = Q2M_STRAIGHT;
      if (monster->dodging)
        monster->attack_state = Q2M_SLIDING;
    }
    if (monster->attack_state == Q2M_SLIDING) {
      if (!already_moved && !run_slide(context, distance, error))
        return false;
      if (!q2m_alive(context) || !extended ||
          (!selected && monster->attack_state == Q2M_SLIDING))
        return true;
    } else if (extended && monster->charging && !monster->manual_steering) {
      if (!q2m_face_enemy(context, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    if (selected) {
      if ((distance != 0.0f || (rerelease && monster->alternate_fly)) &&
          !already_moved && monster->attack_state == Q2M_STRAIGHT &&
          !monster->stand_ground && !q2m_move_to_goal(context, distance, error))
        return false;
      return !q2m_alive(context) || !visible || run_sighting(context, error);
    }
    if (visible) {
      if (!already_moved && !q2m_move_to_goal(context, distance, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (!run_sighting(context, error))
        return false;
      return !rerelease || !q2m_alive(context) ||
             q2m_distance(context, monster->enemy) > 50.0f || q2m_change_yaw(context, error);
    }
    bool found_hint;
    if (!q2_hint_check_lost(context, &found_hint, error)) return false;
    if (found_hint || !q2m_alive(context)) return true;
    if (context->game->options.cooperative) {
      bool found;
      if (!q2m_find_target(context, &found, error))
        return false;
      if (!q2m_alive(context) || (found && !rerelease))
        return true;
    }
    bool timeout = monster->search_ns && context->game->now_ns > monster->search_ns &&
                   context->game->now_ns - monster->search_ns > 20 * Q2M_SECOND;
    if (!already_moved && !q2m_move_to_goal(context, distance, error))
      return false;
    if (timeout && q2m_alive(context))
      monster->search_ns = 0;
    return true;
  }
  case Q2M_AI_CHARGE: {
    bool rerelease = context->game->options.rules == QA_RULESET_Q2_RERELEASE;
    bool expansion = rerelease || context->game->options.product == QA_Q2_ROGUE;
    if (expansion) {
      qa_body_state target;
      bool present, visible;
      if (!attack_body(context, monster->enemy, &target, &present, error))
        return false;
      if (!q2m_alive(context) || !present)
        return true;
      if (!q2m_visible(context, monster->enemy, &visible, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (visible)
        monster->blind_fire_target = rerelease
            ? qa_vec_add(target.origin, qa_vec_scale(target.velocity, -0.1f))
            : target.origin;
      if (!monster->manual_steering)
        monster->ideal_yaw = vector_yaw(qa_vec_sub(target.origin, context->body.origin));
    }
    if (!(monster->manual_steering || expansion ? q2m_change_yaw(context, error)
                                               : q2m_face_enemy(context, error)))
      return false;
    if (!q2m_alive(context))
      return true;
    if (distance != 0.0f || (rerelease && monster->alternate_fly)) {
      if (expansion && monster->charging)
        return q2m_move_to_goal(context, distance, error);
      bool fly;
      if (!alternate_fly(context, &fly, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (fly) {
        monster->source_blocked = false;
        if (expansion && monster->attack_state == Q2M_SLIDING)
          return true;
      } else if (expansion && monster->attack_state == Q2M_SLIDING) {
        qa_builtin_actor_traits traits;
        if (!actor_traits(context, monster->enemy, &traits, error))
          return false;
        if (!q2m_alive(context))
          return true;
        qa_string_id tesla = context->game->runtime_names[
            rerelease ? Q2_NAME_TESLA_MINE : Q2_NAME_TESLA];
        float side = traits.classname == tesla ? 0.0f
                     : monster->lefty ? 90.0f : -90.0f;
        float sideways = rerelease ? distance * monster->move->sidestep_scale
                                    : distance;
        bool moved = false;
        if (!walk_move(context, monster->ideal_yaw + side, sideways, &moved, error))
          return false;
        if (!q2m_alive(context) || moved)
          return true;
        monster->lefty = !monster->lefty;
        if (!walk_move(context, monster->ideal_yaw - side, sideways, &moved, error))
          return false;
      } else if (!walk_move(context, context->body.angles.y, distance, &(bool){false}, error)) {
        return false;
      }
      if (!q2m_alive(context))
        return true;
      if (rerelease && !q2m_refresh(context, error))
        return false;
    }
    return !rerelease || !q2m_alive(context) ||
           q2m_distance(context, monster->enemy) > 50.0f || q2m_change_yaw(context, error);
  }
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
    return !q2m_alive(context) || prone || q2m_callback_run(context, Q2M_CALLBACK_soldier_stand_up, error);
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
           (context->game->options.rules == QA_RULESET_Q2_RERELEASE && monster->manual_steering) ||
           q2m_change_yaw(context, error);
  }
  case Q2M_AI_STAND2:
    if (monster->definition->species == Q2M_GEKK && (monster->spawnflags & 8u)) {
      if (!frame_move(context, distance, error))
        return false;
      return !q2m_alive(context) || idle_search(context, false, error);
    }
    return q2m_run_ai(context, Q2M_AI_STAND, distance, error);
  case Q2M_AI_MOVE_TO_GOAL:
    return q2m_move_to_goal(context, distance, error);
  case Q2M_AI_FACING:
    return q2m_face_enemy(context, error);
  case Q2M_AI_MOVE2: {
      if (!q2_actor_live(context->game, monster->goal))
        return q2m_set_move(context, Q2M_MOVE_fixbot_move_stand, false, error);
      if (distance != 0.0f &&
          !walk_move(context, context->body.angles.y, distance, &(bool){false}, error))
        return false;
      if (!q2m_alive(context)) return true;
      qa_body_state goal;
      if (!qa_world_body_read(context->game->services.world, monster->goal, &goal, error))
        return !q2_actor_live(context->game, monster->goal);
      monster->ideal_yaw = vector_yaw(qa_vec_sub(goal.origin, context->body.origin));
      return q2m_change_yaw(context, error);
  }
  case Q2M_AI_SLIDE_LEFT:
  case Q2M_AI_SLIDE_RIGHT: {
    float yaw = context->body.angles.y + (kind == Q2M_AI_SLIDE_RIGHT ? 90.0f : -90.0f);
    return walk_move(context, yaw, distance, &(bool){false}, error);
  }
  case Q2M_AI_PARASITE_CHARGE:
    return q2m_parasite_charge(context, distance, error);
  }
  return true;
}
