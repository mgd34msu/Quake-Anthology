/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/director.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/core/cvars/index.h"
#include "qa/core/info_string.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef qa_source_bot_director director_t;
typedef qa_source_bot_round round_t;

enum { GENERATED_CAPACITY = 4096 };

static bool fail(director_t *d, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(d->error, sizeof d->error, format, args);
  va_end(args);
  return false;
}

static bool same_owned(qa_owned_actor left, qa_owned_actor right) {
  return qa_same_actor(left.id, right.id) && left.token == right.token &&
         (left.owner == right.owner || (left.owner != NULL && right.owner != NULL && strcmp(left.owner, right.owner) == 0));
}

/* ---- sequences: TS Map<OwnedActor, number> ---- */
static qa_source_bot_sequence *sequence_find(director_t *d, qa_owned_actor actor) {
  for (size_t i = 0; i < d->sequence_count; i++)
    if (same_owned(d->sequences[i].actor, actor))
      return &d->sequences[i];
  return NULL;
}
static void sequence_set(director_t *d, qa_owned_actor actor, int64_t next) {
  qa_source_bot_sequence *found = sequence_find(d, actor);
  if (found != NULL) {
    found->next = next;
    return;
  }
  if (d->sequence_count == d->sequence_capacity) {
    size_t capacity = d->sequence_capacity == 0 ? 8 : d->sequence_capacity * 2;
    qa_source_bot_sequence *grown = realloc(d->sequences, capacity * sizeof *grown);
    if (grown == NULL)
      qa_range_error("Out of memory recording a bot command sequence");
    d->sequences = grown;
    d->sequence_capacity = capacity;
  }
  d->sequences[d->sequence_count++] = (qa_source_bot_sequence){actor, next};
}
static void sequence_delete(director_t *d, qa_owned_actor actor) {
  qa_source_bot_sequence *found = sequence_find(d, actor);
  if (found == NULL)
    return;
  size_t index = (size_t)(found - d->sequences);
  memmove(found, found + 1, (d->sequence_count - index - 1) * sizeof *found);
  d->sequence_count--;
}

static bool host_actor(const qa_source_bot_director_host *host, int64_t client, qa_owned_actor *out) {
  return host->actor(host->ctx, client, out);
}

/* ---- goal world ---- */
static double world_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, int64_t pass_entity,
                          int64_t mask) {
  director_t *d = ctx;
  qa_source_bot_game_bots_a *game = d->options.host->game;
  qa_q3_base_server_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.pass_entity_num = (double)pass_entity;
  query.mask = (double)mask;
  if (bounds == NULL) {
    query.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  } else {
    query.shape.kind = QA_Q3_BASE_SHAPE_BOX;
    query.shape.mins = bounds->min;
    query.shape.maxs = bounds->max;
  }
  return game->trace(game->ctx, query).fraction;
}
static int64_t world_point_contents(void *ctx, qa_vec3 point) {
  director_t *d = ctx;
  const qa_source_bot_director_host *host = d->options.host;
  return host->point_contents(host->ctx, point);
}
static int64_t world_next_entity(void *ctx, int64_t after) {
  director_t *d = ctx;
  return qa_bot_entity_observations_next_entity(&d->round->ai.context.observations, after);
}
static qa_goal_entity_info world_entity_info(void *ctx, int64_t entity) {
  director_t *d = ctx;
  qa_aas_entity_info_bots_a info = qa_bot_entity_observations_info(&d->round->ai.context.observations, entity);
  qa_goal_entity_info out;
  out.type = (int64_t)info.type;
  out.model_index = (int64_t)info.model_index;
  out.origin = info.origin;
  out.last_visible_origin = info.last_visible_origin;
  out.last_update_time = info.last_update_time;
  return out;
}
static int64_t world_point_area(void *ctx, qa_vec3 origin) {
  director_t *d = ctx;
  qa_bot_navigation_bots_a *navigation = &d->round->navigation.bot;
  return navigation->point_area(navigation->ctx, origin);
}

/* TS sourcePickupGoals(game).goal */
static bool pickup_goal(director_t *d, int64_t client, const qa_bot_observed_pickup_bots_a *pickup,
                        qa_source_pickup_goal *out) {
  qa_source_bot_game_bots_a *game = d->options.host->game;
  qa_bot_state_bots_a *state = qa_bot_state_store_get(&d->round->ai.context.states, client);
  if (pickup == NULL || state == NULL || strcmp(pickup->observation.availability_kind, "ready") != 0 ||
      !pickup->observation.eligible)
    return false;
  double utility = game->knowledge->pickup_utility(game->knowledge->ctx, &d->library, state, &pickup->preview);
  if (!(utility > 0))
    return false;
  out->actor = pickup->observation.actor;
  out->entity = pickup->entity;
  out->origin = pickup->origin;
  out->bounds = pickup->bounds;
  out->name = pickup->name;
  out->utility = utility;
  return true;
}
static bool pickups_owns_item_goal(void *ctx, int64_t client, int64_t entity) {
  director_t *d = ctx;
  const qa_bot_pickup_observations_bots_a *pickups = d->options.host->game->pickups;
  return pickups->owns_item_goal(pickups->ctx, client, entity);
}
static const qa_source_pickup_goal *pickups_candidates(void *ctx, int64_t client, size_t *out_count) {
  director_t *d = ctx;
  const qa_bot_pickup_observations_bots_a *pickups = d->options.host->game->pickups;
  size_t count = 0, n = 0;
  const qa_bot_observed_pickup_bots_a *observed =
      pickups == NULL ? NULL : pickups->candidates(pickups->ctx, client, &count);
  if (count > d->pickup_goal_capacity) {
    qa_source_pickup_goal *grown = realloc(d->pickup_goals, count * sizeof *grown);
    if (grown == NULL)
      qa_range_error("Out of memory listing bot pickup goals");
    d->pickup_goals = grown;
    d->pickup_goal_capacity = count;
  }
  for (size_t i = 0; i < count; i++)
    if (pickup_goal(d, client, &observed[i], &d->pickup_goals[n]))
      n++;
  *out_count = n;
  return d->pickup_goals;
}
static bool pickups_inspect(void *ctx, int64_t client, qa_actor_id actor, qa_source_pickup_goal *out) {
  director_t *d = ctx;
  const qa_bot_pickup_observations_bots_a *pickups = d->options.host->game->pickups;
  qa_bot_observed_pickup_bots_a observed;
  bool found = pickups != NULL && pickups->inspect(pickups->ctx, client, actor, &observed);
  return pickup_goal(d, client, found ? &observed : NULL, out);
}

static qa_goal_world goal_world(director_t *d) {
  qa_source_bot_game_bots_a *game = d->options.host->game;
  qa_goal_world world;
  memset(&world, 0, sizeof world);
  world.bsp_entities = &d->bsp_entities;
  world.has_navigation = true;
  world.navigation = d->round->navigation.goal;
  if (game->pickups != NULL) {
    world.has_source_pickups = true;
    world.source_pickups.ctx = d;
    world.source_pickups.owns_item_goal = game->pickups->owns_item_goal == NULL ? NULL : pickups_owns_item_goal;
    world.source_pickups.candidates = pickups_candidates;
    world.source_pickups.inspect = pickups_inspect;
  }
  world.point_area = world_point_area;
  world.point_area_ctx = d;
  world.host = (qa_goal_world_host){d, world_trace, world_point_contents, world_next_entity, world_entity_info};
  return world;
}

/* ---- round hosts ---- */
static int64_t ai_point_contents(void *ctx, qa_vec3 point) {
  round_t *r = ctx;
  return r->host->point_contents(r->host->ctx, point);
}
static int64_t ai_snapshot_entity(void *ctx, int64_t client, int64_t sequence) {
  round_t *r = ctx;
  return r->host->snapshot_entity(r->host->ctx, client, sequence);
}
static const char *ai_console_message(void *ctx, int64_t client) {
  round_t *r = ctx;
  return r->host->console_message(r->host->ctx, client);
}
static void ai_insert_console_command(void *ctx, const char *text) {
  round_t *r = ctx;
  const qa_source_bot_engine_bots_a *engine = &r->host->game->options.engine;
  engine->insert_console_command(engine->insert_ctx, text);
}
static void ai_check_bot_spawn(void *ctx) {
  round_t *r = ctx;
  qa_game_bot_catalog_check_spawn(&r->catalog);
}
static int64_t ai_load_map(void *ctx, const char *name) {
  (void)name; /* TS () => this.library.loadMap(this.goalWorld()) ignores the map name */
  round_t *r = ctx;
  qa_goal_world world = goal_world(r->director);
  return qa_bot_library_load_map_bots_a(&r->director->library, &world);
}
static void ai_user_command(void *ctx, int64_t client, const qa_q3_base_user_command *command) {
  round_t *r = ctx;
  director_t *d = r->director;
  qa_owned_actor actor;
  if (!host_actor(r->host, client, &actor))
    qa_range_error("Bot command has no admitted shared actor for client %lld", (long long)client);
  qa_source_bot_sequence *found = sequence_find(d, actor);
  int64_t sequence = found == NULL ? 0 : found->next;
  sequence_set(d, actor, sequence + 1);
  qa_source_bot_generated_command record;
  memset(&record, 0, sizeof record);
  record.actor = actor.id;
  record.provider = r->host->provider;
  record.sequence = (double)sequence;
  qa_q3_base_user_command copy = *command; /* TS { ...command, angles: { ...command.angles } } */
  r->host->encode_command(r->host->ctx, client, &copy, &record.encoded);
  qa_event_ring_admit(&d->generated, &record);
}
static int64_t catalog_allocate_client(void *ctx) {
  round_t *r = ctx;
  return r->host->allocate_client(r->host->ctx);
}
static bool catalog_setup_client(void *ctx, int64_t client, const qa_bot_settings_bots_a *settings, bool restart) {
  round_t *r = ctx;
  return qa_game_ai_setup_client_bots_a(&r->ai, client, settings, restart);
}
static void catalog_shutdown_client(void *ctx, int64_t client, bool restart) {
  round_t *r = ctx;
  qa_game_ai_shutdown_client_bots_a(&r->ai, client, restart);
  qa_owned_actor actor;
  if (host_actor(r->host, client, &actor))
    sequence_delete(r->director, actor);
}

static round_t *create_round(director_t *d, const qa_source_bot_director_host *host,
                             qa_source_bot_navigation_fn navigation_for, void *navigation_ctx) {
  round_t *r = calloc(1, sizeof *r);
  if (r == NULL)
    return NULL;
  r->director = d;
  r->host = host;
  navigation_for(navigation_ctx, &d->library, &d->bsp_entities, &r->navigation);
  r->ai_host.ctx = r;
  r->ai_host.navigation = &r->navigation.bot;
  r->ai_host.bsp_entities = &d->bsp_entities;
  r->ai_host.point_contents = ai_point_contents;
  r->ai_host.get_snapshot_entity = ai_snapshot_entity;
  r->ai_host.get_console_message = ai_console_message;
  r->ai_host.insert_console_command = ai_insert_console_command;
  r->ai_host.check_bot_spawn = ai_check_bot_spawn;
  r->ai_host.load_map = ai_load_map;
  r->ai_host.user_command = ai_user_command;
  if (!qa_game_ai_bots_a_init(&r->ai, host->game, &d->library, &r->ai_host)) {
    free(r);
    return NULL;
  }
  qa_common_parse_state_init(&r->parser);
  qa_game_bot_catalog_bots_a_init(&r->catalog, host->game, d->options.files.files, &r->parser,
                                  (qa_game_bot_catalog_host_bots_a){r, catalog_allocate_client,
                                                                    catalog_setup_client,
                                                                    catalog_shutdown_client});
  return r;
}
static void destroy_round(round_t *r) {
  if (r == NULL)
    return;
  qa_game_ai_bots_a_destroy(&r->ai);
  free(r);
}

static void bsp_print(void *ctx, int severity, const char *text) {
  director_t *d = ctx;
  d->options.library.print(d->options.library.ctx, severity, text);
}

qa_source_bot_director *qa_source_bot_director_create(const qa_source_bot_director_options *options) {
  director_t *d = calloc(1, sizeof *d);
  if (d == NULL)
    return NULL;
  d->options = *options;
  d->round_phase = QA_SOURCE_BOT_ROUND_ACTIVE;
  if (!qa_event_ring_init(&d->generated, GENERATED_CAPACITY, sizeof(qa_source_bot_generated_command))) {
    free(d);
    return NULL;
  }
  if (!qa_event_ring_open_cursor(&d->generated, &d->generated_cursor, false)) {
    qa_event_ring_destroy(&d->generated);
    free(d);
    return NULL;
  }
  qa_bot_library_bots_a_init(&d->library, &d->options.library);
  qa_aas_bsp_entities_init(&d->bsp_entities);
  qa_aas_bsp_entities_configure(&d->bsp_entities, bsp_print, d, &d->library.memory);
  if (!d->options.restoring)
    qa_aas_bsp_entities_load(&d->bsp_entities, d->options.entities);
  d->round = create_round(d, d->options.host, d->options.navigation, d->options.navigation_ctx);
  if (d->round == NULL) {
    qa_source_bot_director_destroy(d);
    return NULL;
  }
  return d;
}

void qa_source_bot_director_destroy(qa_source_bot_director *d) {
  if (d == NULL)
    return;
  destroy_round(d->round);
  qa_aas_bsp_entities_destroy(&d->bsp_entities);
  qa_bot_library_bots_a_destroy(&d->library);
  qa_event_ring_close_cursor(&d->generated, &d->generated_cursor);
  qa_event_ring_destroy(&d->generated);
  free(d->sequences);
  free(d->taken);
  free(d->commands);
  free(d->roster);
  free(d->pickup_goals);
  free(d);
}

const char *qa_source_bot_director_error(const qa_source_bot_director *d) { return d->error; }
qa_source_bot_navigation *qa_source_bot_director_navigation(qa_source_bot_director *d) {
  return &d->round->navigation;
}
qa_game_ai_bots_a *qa_source_bot_director_ai(qa_source_bot_director *d) { return &d->round->ai; }
qa_game_bot_catalog_bots_a *qa_source_bot_director_catalog(qa_source_bot_director *d) { return &d->round->catalog; }
const qa_source_bot_director_options *qa_source_bot_director_options_of(const qa_source_bot_director *d) {
  return &d->options;
}

static bool generated_pending(director_t *d) {
  return qa_event_ring_pending(&d->generated, &d->generated_cursor) != 0;
}
static bool navigation_ready(director_t *d) {
  qa_bot_navigation_bots_a *navigation = &d->round->navigation.bot;
  return navigation->ready(navigation->ctx);
}

void qa_source_bot_director_begin_round_restart(qa_source_bot_director *d) {
  if (!d->loaded || d->closed || d->running || generated_pending(d) || d->round_phase != QA_SOURCE_BOT_ROUND_ACTIVE)
    qa_range_error("Bot round restart requires a live idle director");
  qa_game_ai_shutdown_bots_a(&d->round->ai, true);
  d->sequence_count = 0;
  d->round_phase = QA_SOURCE_BOT_ROUND_DETACHED;
}

bool qa_source_bot_director_bind_restarted_round(qa_source_bot_director *d,
                                                 const qa_source_bot_director_host *host,
                                                 qa_source_bot_navigation_fn navigation,
                                                 void *navigation_ctx) {
  if (d->closed || d->round_phase != QA_SOURCE_BOT_ROUND_DETACHED)
    qa_range_error("Detach bot round before rebinding");
  const qa_source_bot_game_bots_a *previous = d->options.host->game;
  if (host->game->options.product != previous->options.product ||
      host->game->max_clients(host->game->ctx) != previous->max_clients(previous->ctx))
    qa_range_error("Bot fast restart must retain its product and client capacity");
  d->options.host = host;
  d->options.navigation = navigation;
  d->options.navigation_ctx = navigation_ctx;
  d->options.restoring = false;
  round_t *next = create_round(d, host, navigation, navigation_ctx);
  if (next == NULL)
    qa_range_error("Out of memory binding a restarted bot round");
  round_t *old = d->round;
  d->round = next;
  destroy_round(old);
  qa_goal_world world = goal_world(d);
  qa_bot_goal_rebind_world(&d->library.goals, &world);
  if (!qa_game_ai_setup_bots_a(&d->round->ai, true) || !qa_game_ai_load_map_bots_a(&d->round->ai, true))
    return fail(d, "Source bot round initialization failed");
  qa_game_bot_catalog_initialize_bots(&d->round->catalog, true);
  d->round_phase = QA_SOURCE_BOT_ROUND_BOUND;
  return true;
}

void qa_source_bot_director_resume_round_bots(qa_source_bot_director *d) {
  if (d->closed || d->round_phase != QA_SOURCE_BOT_ROUND_BOUND)
    qa_range_error("Bind bot round before resuming");
  d->round_phase = QA_SOURCE_BOT_ROUND_ACTIVE;
}

static void set_or_abort(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child))
    qa_range_error("Out of memory capturing the bot director");
}

qa_save_value *qa_source_bot_director_checkpoint_orchestration(qa_source_bot_director *d) {
  if (!d->loaded || d->closed || d->running || generated_pending(d) || d->round_phase != QA_SOURCE_BOT_ROUND_ACTIVE)
    qa_range_error("Bot director checkpoint requires a completed frame in a live map");
  qa_save_value *object = qa_save_value_object(), *sequences = qa_save_value_array();
  if (object == NULL || sequences == NULL)
    qa_range_error("Out of memory capturing the bot director");
  set_or_abort(object, "version", qa_save_value_number(1));
  for (size_t i = 0; i < d->sequence_count; i++) {
    const qa_source_bot_sequence *entry = &d->sequences[i];
    qa_save_value *item = qa_save_value_object(), *actor = qa_save_value_object();
    if (item == NULL || actor == NULL)
      qa_range_error("Out of memory capturing the bot director");
    set_or_abort(actor, "slot", qa_save_value_number((double)entry->actor.id.slot));
    set_or_abort(actor, "generation", qa_save_value_number((double)entry->actor.id.generation));
    set_or_abort(item, "actor", actor);
    set_or_abort(item, "owner", qa_save_value_string(entry->actor.owner));
    set_or_abort(item, "next", qa_save_value_number((double)entry->next));
    if (!qa_save_value_push(sequences, item))
      qa_range_error("Out of memory capturing the bot director");
  }
  set_or_abort(object, "sequences", sequences);
  return object;
}

qa_save_value *qa_source_bot_director_capture_save_state(qa_source_bot_director *d) {
  qa_save_value *orchestration = qa_source_bot_director_checkpoint_orchestration(d);
  const char *assets = d->options.files.provenance == NULL
                           ? NULL : d->options.files.provenance(d->options.files.files.ctx);
  if (assets == NULL) {
    qa_save_value_free(orchestration);
    fail(d, "Exact bot persistence requires complete mounted asset provenance");
    return NULL;
  }
  qa_bot_memory_capture memory;
  if (!qa_bot_memory_checkpoint(&d->library.memory, &memory))
    qa_range_error("Out of memory capturing the bot director");
  qa_save_value *object = qa_save_value_object();
  if (object == NULL)
    qa_range_error("Out of memory capturing the bot director");
  set_or_abort(object, "version", qa_save_value_number(2));
  set_or_abort(object, "assets", qa_save_value_string(assets));
  set_or_abort(object, "memory", memory.image);
  memory.image = NULL;
  set_or_abort(object, "bsp", qa_aas_bsp_entities_checkpoint(&d->bsp_entities, &memory));
  set_or_abort(object, "library", qa_bot_library_capture_save_state(&d->library, &memory));
  set_or_abort(object, "ai", qa_game_ai_capture_save_state_bots_a(&d->round->ai));
  set_or_abort(object, "catalog", qa_game_bot_catalog_capture_save_state(&d->round->catalog));
  set_or_abort(object, "orchestration", orchestration);
  qa_bot_memory_capture_destroy(&memory);
  return object;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_range_error("Out of memory reading the bot director checkpoint");
  return out;
}

bool qa_source_bot_director_restore_save_state(qa_source_bot_director *d, const qa_save_value *value,
                                               const qa_source_bot_restore_hooks *hooks) {
  if (d->loaded || d->closed || !d->options.restoring)
    qa_range_error("Bot director restoration requires an inert constructor");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "bot.director"))
    qa_range_error("Out of memory reading the bot director checkpoint");
  qa_save_reader cell = field(&reader, "version");
  /* Version 1 digested every file under bots/, navigation graphs included; version 2 leaves them out. */
  static const double versions[] = {1, 2};
  double version = qa_save_reader_choice_number(&cell, versions, 2);
  qa_save_reader_destroy(&cell);
  const char *(*digest)(void *) = version == 1 ? d->options.files.legacy_provenance : d->options.files.provenance;
  const char *assets = digest == NULL ? NULL : digest(d->options.files.files.ctx);
  bool matches = false;
  if (assets != NULL) {
    cell = field(&reader, "assets");
    matches = strcmp(qa_save_reader_string(&cell), assets) == 0;
    qa_save_reader_destroy(&cell);
  }
  if (!matches) {
    qa_save_reader_destroy(&reader);
    return fail(d, "Saved bot assets differ from mounted bot files");
  }
  qa_save_reader library = field(&reader, "library"), bsp = field(&reader, "bsp");
  cell = field(&library, "initialized");
  qa_save_reader_literal_boolean(&cell, true);
  qa_save_reader_destroy(&cell);
  cell = field(&bsp, "loaded");
  qa_save_reader_literal_boolean(&cell, true);
  qa_save_reader_destroy(&cell);
  if (!navigation_ready(d)) {
    qa_save_reader_destroy(&library);
    qa_save_reader_destroy(&bsp);
    qa_save_reader_destroy(&reader);
    return fail(d, "Saved bot director requires completed selected navigation");
  }
  qa_save_reader memory_cell = field(&reader, "memory");
  qa_bot_memory_restored memory;
  qa_bot_memory_restore(&d->library.memory, memory_cell.value, &memory);
  qa_save_reader_destroy(&memory_cell);
  qa_aas_bsp_entities_restore(&d->bsp_entities, bsp.value, &memory);
  qa_goal_world world = goal_world(d);
  qa_bot_library_restore_save_state(&d->library, library.value, &memory, &world, hooks->resolve_actor,
                                    hooks->resolve_actor_ctx, hooks->edge, hooks->edge_ctx);
  cell = field(&reader, "ai");
  qa_game_ai_restore_save_state_bots_a(&d->round->ai, cell.value, hooks->remap_observation,
                                       hooks->remap_observation_ctx);
  qa_save_reader_destroy(&cell);
  cell = field(&reader, "catalog");
  qa_game_bot_catalog_restore_save_state(&d->round->catalog, cell.value);
  qa_save_reader_destroy(&cell);
  qa_bot_memory_restored_destroy(&memory);
  d->loaded = true;
  cell = field(&reader, "orchestration");
  bool restored = qa_source_bot_director_restore_orchestration(d, cell.value, hooks->resolve_owned,
                                                               hooks->resolve_owned_ctx);
  qa_save_reader_destroy(&cell);
  qa_save_reader_destroy(&library);
  qa_save_reader_destroy(&bsp);
  qa_save_reader_destroy(&reader);
  return restored;
}

bool qa_source_bot_director_load(qa_source_bot_director *d, bool restart) {
  if (d->closed || d->loaded)
    qa_range_error("Bot director map lifetime is already loaded or closed");
  if (!navigation_ready(d))
    return fail(d, "Bot arena admission requires completed selected-movement navigation");
  qa_cvar_registry *cvars = d->options.host->game->options.cvars;
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_register(cvars, "bot_enable", "1", 0, &snapshot))
    qa_range_error("Out of memory registering bot_enable");
  qa_cvar_read_free(&snapshot);
  if (!qa_cvar_registry_register(cvars, "g_spSkill", "2", QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_LATCH, &snapshot))
    qa_range_error("Out of memory registering g_spSkill");
  qa_cvar_read_free(&snapshot);
  if (!qa_game_ai_setup_bots_a(&d->round->ai, false))
    return fail(d, "Source BotAISetup failed while loading behavior data");
  if (!qa_game_ai_load_map_bots_a(&d->round->ai, false))
    return fail(d, "Source BotAILoadMap failed while loading map goals");
  qa_game_bot_catalog_initialize_bots(&d->round->catalog, restart);
  d->loaded = true;
  return true;
}

const qa_actor_command *qa_source_bot_director_frame(qa_source_bot_director *d, double milliseconds,
                                                     size_t *out_count) {
  if (!d->loaded || d->closed || d->running || d->round_phase != QA_SOURCE_BOT_ROUND_ACTIVE)
    qa_range_error("Bot commands require a live idle director");
  if (generated_pending(d))
    qa_range_error("Bot command output from the previous source frame was not drained");
  d->running = true;
  qa_game_ai_start_frame_bots_a(&d->round->ai, milliseconds);
  size_t count = 0;
  qa_source_bot_generated_command *taken = qa_event_ring_take(&d->generated, &d->generated_cursor, &count);
  if (taken == NULL && count != 0)
    qa_range_error("Out of memory draining bot commands");
  qa_event_ring_retire(&d->generated);
  free(d->taken);
  d->taken = taken;
  free(d->commands);
  d->commands = count == 0 ? NULL : calloc(count, sizeof *d->commands);
  if (count != 0 && d->commands == NULL)
    qa_range_error("Out of memory draining bot commands");
  for (size_t i = 0; i < count; i++) {
    qa_actor_command *command = &d->commands[i];
    command->actor = taken[i].actor;
    command->source.kind = QA_COMMAND_SOURCE_BOT;
    command->source.provider = taken[i].provider;
    command->sequence = taken[i].sequence;
    command->command = taken[i].encoded.command;
    command->arsenal = taken[i].encoded.has_arsenal ? &taken[i].encoded.arsenal : NULL;
  }
  d->command_count = count;
  d->running = false;
  *out_count = count;
  return d->commands;
}

bool qa_source_bot_director_restore_orchestration(qa_source_bot_director *d, const qa_save_value *value,
                                                  qa_source_bot_resolve_owned_fn resolve, void *resolve_ctx) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "bot.director.orchestration"))
    qa_range_error("Out of memory reading bot director orchestration");
  qa_save_reader cell = field(&reader, "version");
  double version = qa_save_reader_literal_number(&cell, 1);
  qa_save_reader_destroy(&cell);
  qa_save_reader list = field(&reader, "sequences");
  size_t count = qa_save_reader_list_count(&list);
  typedef struct image_entry {
    qa_saved_actor_id actor;
    const char *owner;
    double next;
  } image_entry;
  image_entry *image = calloc(count == 0 ? 1 : count, sizeof *image);
  if (image == NULL)
    qa_range_error("Out of memory reading bot director orchestration");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, actor, slot, generation, owner, next;
    if (!qa_save_reader_item(&list, i, &entry))
      qa_range_error("Out of memory reading bot director orchestration");
    actor = field(&entry, "actor");
    slot = field(&actor, "slot");
    image[i].actor.slot = qa_save_reader_integer_min(&slot, 0);
    generation = field(&actor, "generation");
    image[i].actor.generation = qa_save_reader_integer_min(&generation, 0);
    owner = field(&entry, "owner");
    image[i].owner = qa_namespaced(&owner);
    next = field(&entry, "next");
    image[i].next = qa_save_reader_integer_min(&next, 0);
    qa_save_reader_destroy(&next);
    qa_save_reader_destroy(&owner);
    qa_save_reader_destroy(&generation);
    qa_save_reader_destroy(&slot);
    qa_save_reader_destroy(&actor);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&reader);
  if (!d->loaded || d->closed || generated_pending(d))
    qa_range_error("Restore bot director orchestration only after its child owners are live");
  if (version != 1) {
    free(image);
    return fail(d, "Unsupported bot director orchestration version");
  }
  qa_source_bot_sequence *sequences = calloc(count == 0 ? 1 : count, sizeof *sequences);
  if (sequences == NULL)
    qa_range_error("Out of memory reading bot director orchestration");
  size_t restored = 0;
  const qa_source_bot_director_host *host = d->options.host;
  int64_t max_clients = host->game->max_clients(host->game->ctx);
  for (size_t i = 0; i < count; i++) {
    const image_entry *entry = &image[i];
    if (!(entry->next <= 9007199254740991.0 && entry->next >= 0 && entry->next == trunc(entry->next))) {
      free(sequences);
      free(image);
      return fail(d, "Invalid saved bot command sequence");
    }
    qa_owned_actor actor;
    bool found = resolve(resolve_ctx, entry->actor, &actor);
    bool duplicate = false;
    for (size_t j = 0; found && j < restored; j++)
      if (same_owned(sequences[j].actor, actor))
        duplicate = true;
    if (!found || actor.owner == NULL || strcmp(actor.owner, entry->owner) != 0 || duplicate) {
      free(sequences);
      free(image);
      return fail(d, "Saved bot sequence has an invalid or duplicate actor");
    }
    bool connected = false;
    for (int64_t client = 0; client < max_clients; client++) {
      qa_owned_actor candidate;
      if (host_actor(host, client, &candidate) && same_owned(candidate, actor)) {
        connected = true;
        break;
      }
    }
    if (!connected) {
      free(sequences);
      free(image);
      return fail(d, "Saved bot command sequence has no restored transport connection");
    }
    sequences[restored++] = (qa_source_bot_sequence){actor, (int64_t)entry->next};
  }
  free(image);
  d->sequence_count = 0;
  for (size_t i = 0; i < restored; i++)
    sequence_set(d, sequences[i].actor, sequences[i].next);
  free(sequences);
  return true;
}

bool qa_source_bot_director_connect(qa_source_bot_director *d, int64_t client, bool restart) {
  return qa_game_bot_catalog_connect(&d->round->catalog, client, restart);
}
void qa_source_bot_director_shutdown_client(qa_source_bot_director *d, int64_t client, bool restart) {
  qa_game_bot_catalog_shutdown_client(&d->round->catalog, client, restart);
}
void qa_source_bot_director_remove_queued_begin(qa_source_bot_director *d, int64_t client) {
  qa_game_bot_catalog_remove_queued_begin(&d->round->catalog, client);
}
void qa_source_bot_director_console_command(qa_source_bot_director *d, const char *const *argv, size_t argc) {
  qa_game_bot_catalog_console_command(&d->round->catalog, argv, argc);
}
bool qa_source_bot_director_add_match_bot(qa_source_bot_director *d) {
  return qa_game_bot_catalog_add_match_bot(&d->round->catalog);
}
void qa_source_bot_director_test_aas(qa_source_bot_director *d, qa_vec3 origin) {
  qa_game_ai_test_aas_bots_a(&d->round->ai, origin);
}
void qa_source_bot_director_interbreed_end_match(qa_source_bot_director *d) {
  qa_game_ai_interbreed_end_match_bots_a(&d->round->ai);
}

const qa_source_bot_roster_entry *qa_source_bot_director_roster(qa_source_bot_director *d, size_t *out_count) {
  const qa_source_bot_director_host *host = d->options.host;
  int64_t max_clients = host->game->max_clients(host->game->ctx);
  d->roster_count = 0;
  for (int64_t client = 0; client < max_clients; client++) {
    qa_bot_state_bots_a *state = qa_bot_state_store_get(&d->round->ai.context.states, client);
    qa_owned_actor actor;
    bool has_actor = host_actor(host, client, &actor);
    if (state == NULL || state->src->inuse == 0 || !has_actor)
      continue;
    if (d->roster_count == d->roster_capacity) {
      size_t capacity = d->roster_capacity == 0 ? 8 : d->roster_capacity * 2;
      qa_source_bot_roster_entry *grown = realloc(d->roster, capacity * sizeof *grown);
      if (grown == NULL)
        qa_range_error("Out of memory listing the bot roster");
      d->roster = grown;
      d->roster_capacity = capacity;
    }
    qa_source_bot_roster_entry *entry = &d->roster[d->roster_count++];
    memset(entry, 0, sizeof *entry);
    entry->actor = actor;
    entry->source_client = client;
    memcpy(entry->settings.characterfile, state->src->characterfile, sizeof entry->settings.characterfile);
    entry->settings.characterfile[sizeof entry->settings.characterfile - 1] = 0;
    entry->settings.skill = state->src->skill;
    memcpy(entry->settings.team, state->src->team, sizeof entry->settings.team);
    entry->settings.team[sizeof entry->settings.team - 1] = 0;
    entry->state = state;
  }
  *out_count = d->roster_count;
  return d->roster;
}

static qa_bot_state_bots_a *order_state(director_t *d, int64_t client) {
  const qa_source_bot_director_host *host = d->options.host;
  if (!d->loaded || d->closed || client < 0 || client >= host->game->max_clients(host->game->ctx))
    return NULL;
  qa_bot_state_bots_a *state = qa_bot_state_store_get(&d->round->ai.context.states, client);
  qa_owned_actor actor;
  return state != NULL && state->src->inuse != 0 && host_actor(host, client, &actor) ? state : NULL;
}

qa_bot_goal_status qa_source_bot_director_goal_status(qa_source_bot_director *d, int64_t client) {
  qa_bot_state_bots_a *state = order_state(d, client);
  if (state == NULL || !state->has_scripted_order)
    return 0;
  qa_bot_order_state *order = &state->scripted_order;
  if (order->order.kind == QA_BOT_ORDER_FOLLOW) {
    qa_bot_order_entity reference = order->order.entity;
    qa_source_bot_game_bots_a *game = d->options.host->game;
    qa_bot_observed_entity_bots_a entity = game->entity(game->ctx, reference.number);
    if (!entity.present || entity.generation != reference.generation)
      order->progress = QA_BOT_ORDER_ERROR;
  }
  return qa_bot_order_status(order);
}

static qa_bot_goal_status set_order(director_t *d, int64_t client, qa_bot_order order) {
  qa_bot_state_bots_a *state = order_state(d, client);
  if (state == NULL)
    return 0;
  if (state->has_scripted_order && qa_same_bot_order(state->scripted_order.order, order))
    return qa_source_bot_director_goal_status(d, client);
  qa_bot_clear_activate_goal_stack_bots_a(&d->round->ai.context, state);
  if (state->ai_node == QA_AI_NODE_SEEK_ACTIVATE)
    state->ai_node = QA_AI_NODE_SEEK_LTG;
  state->has_scripted_order = true;
  state->scripted_order = (qa_bot_order_state){order, QA_BOT_ORDER_IN_PROGRESS};
  qa_bot_move_state_store_reset_avoid_reach(&d->library.move_states, state->src->ms);
  return 2;
}

qa_bot_goal_status qa_source_bot_director_request_move_to_point(qa_source_bot_director *d, int64_t client,
                                                                qa_vec3 point) {
  if (!isfinite(point.x) || !isfinite(point.y) || !isfinite(point.z))
    return 0;
  qa_bot_order order;
  memset(&order, 0, sizeof order);
  order.kind = QA_BOT_ORDER_POINT;
  order.point = point;
  return set_order(d, client, order);
}

qa_bot_goal_status qa_source_bot_director_request_follow_entity(qa_source_bot_director *d, int64_t client,
                                                                int64_t number) {
  qa_source_bot_game_bots_a *game = d->options.host->game;
  if (number < 0 || number >= game->entity_count(game->ctx))
    return 0;
  qa_bot_observed_entity_bots_a entity = game->entity(game->ctx, number);
  if (!entity.present)
    return 0;
  qa_bot_order order;
  memset(&order, 0, sizeof order);
  order.kind = QA_BOT_ORDER_FOLLOW;
  order.entity = (qa_bot_order_entity){number, entity.generation};
  return set_order(d, client, order);
}

void qa_source_bot_director_clear_goal(qa_source_bot_director *d, int64_t client) {
  qa_bot_state_bots_a *state = order_state(d, client);
  if (state == NULL || !state->has_scripted_order)
    return;
  if (qa_bot_order_active(&state->scripted_order)) {
    qa_bot_clear_activate_goal_stack_bots_a(&d->round->ai.context, state);
    if (state->ai_node == QA_AI_NODE_SEEK_ACTIVATE)
      state->ai_node = QA_AI_NODE_SEEK_LTG;
  }
  state->has_scripted_order = false;
  qa_bot_move_state_store_reset_avoid_reach(&d->library.move_states, state->src->ms);
}

void qa_source_bot_arena_roster_free(qa_source_bot_arena_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(entries[i].name);
    free(entries[i].info);
  }
  free(entries);
}

bool qa_source_bot_director_arena_roster(qa_source_bot_director *d, const char *map,
                                         qa_source_bot_arena_entry **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  char *info = qa_game_bot_catalog_arena_info_by_map(&d->round->catalog, map);
  if (info == NULL)
    return true;
  char *bots = qa_info_value_for_key(info, "bots");
  free(info);
  if (bots == NULL)
    qa_range_error("Out of memory reading an arena roster");
  qa_source_bot_arena_entry *entries = NULL;
  size_t count = 0;
  /* TS split(" ").filter(Boolean) */
  for (char *cursor = bots; *cursor != 0;) {
    char *space = strchr(cursor, ' ');
    size_t length = space == NULL ? strlen(cursor) : (size_t)(space - cursor);
    if (length != 0) {
      char *name = malloc(length + 1);
      if (name == NULL)
        qa_range_error("Out of memory reading an arena roster");
      memcpy(name, cursor, length);
      name[length] = 0;
      char *bot = qa_game_bot_catalog_bot_info_by_name(&d->round->catalog, name);
      if (bot == NULL) {
        fail(d, "Arena %s names undefined source bot %s", map, name);
        free(name);
        qa_source_bot_arena_roster_free(entries, count);
        free(bots);
        return false;
      }
      qa_source_bot_arena_entry *grown = realloc(entries, (count + 1) * sizeof *grown);
      if (grown == NULL)
        qa_range_error("Out of memory reading an arena roster");
      entries = grown;
      entries[count++] = (qa_source_bot_arena_entry){name, bot};
    }
    if (space == NULL)
      break;
    cursor = space + 1;
  }
  free(bots);
  *out = entries;
  *out_count = count;
  return true;
}

void qa_source_bot_director_close(qa_source_bot_director *d, bool restart) {
  if (d->closed)
    return;
  size_t count = 0;
  const qa_source_bot_roster_entry *roster = qa_source_bot_director_roster(d, &count);
  int64_t *clients = calloc(count == 0 ? 1 : count, sizeof *clients);
  if (clients == NULL)
    qa_range_error("Out of memory closing the bot director");
  for (size_t i = 0; i < count; i++)
    clients[i] = roster[i].source_client;
  for (size_t i = 0; i < count; i++)
    qa_game_bot_catalog_shutdown_client(&d->round->catalog, clients[i], restart);
  free(clients);
  qa_aas_bsp_entities_dump(&d->bsp_entities);
  qa_bot_library_shutdown_bots_a(&d->library);
  d->sequence_count = 0;
  qa_event_ring_reset(&d->generated);
  d->closed = true;
}
