/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/source_game.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/bots/behavior/q3/ai_combat.h"
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>

typedef qa_q3_bot_game_bots_a game_t;

static qa_q3_entity_pool *pool_of(game_t *g) { return qa_app_q3_source_subsystems_owner_pool(g->source); }

/* ---- engine: { ...source.host.engine, insertConsoleCommand } ---- */
static void engine_print(void *ctx, const char *text) {
  game_t *g = ctx;
  g->host->engine.print(g->host->engine.ctx, text);
}
static const char *engine_get_userinfo(void *ctx, int64_t client) {
  game_t *g = ctx;
  char *text = g->host->engine.get_userinfo(g->host->engine.ctx, (double)client);
  free(g->userinfo);
  g->userinfo = text;
  return text == NULL ? "" : text;
}
static void engine_set_userinfo(void *ctx, int64_t client, const char *userinfo) {
  game_t *g = ctx;
  g->host->engine.set_userinfo(g->host->engine.ctx, (double)client, userinfo);
}
static void engine_send_server_command(void *ctx, int64_t client, const char *command) {
  game_t *g = ctx;
  g->host->engine.send_server_command(g->host->engine.ctx, (double)client, command);
}
static void engine_drop_client(void *ctx, int64_t client, const char *reason) {
  game_t *g = ctx;
  g->host->engine.drop_client(g->host->engine.ctx, (double)client, reason);
}
static void engine_append_console_command(void *ctx, const char *command) {
  game_t *g = ctx;
  g->host->engine.append_console_command(g->host->engine.ctx, command);
}

/* ---- pickups ---- */
static void pickup_cell_clear(qa_q3_bot_pickup_cell_bots_a *cell) {
  free(cell->ammo);
  free(cell->weapons);
  cell->ammo = cell->weapons = NULL;
}

static bool copy_receipts(const qa_pickup_ammo_receipt *source, size_t count, qa_pickup_ammo_receipt **out) {
  *out = NULL;
  if (count == 0)
    return true;
  *out = malloc(count * sizeof **out);
  if (*out == NULL)
    return false;
  memcpy(*out, source, count * sizeof **out);
  return true;
}

/* TS inspect(client, actor). The cell takes copies of the preview receipts so
 * the result outlives the host's preview scratch. */
static bool inspect_into(game_t *g, int64_t client, qa_actor_id actor, qa_q3_bot_pickup_cell_bots_a *cell) {
  qa_q3_base_entity_records *records = qa_app_q3_source_subsystems_owner_records(g->source);
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(records, true, actor);
  qa_q3_game_entity *recipient = qa_q3_entity_pool_at(pool_of(g), (double)client);
  if (entity == NULL || qa_q3_game_entity_classname(entity) == NULL || !qa_q3_game_entity_inuse(entity) ||
      !qa_q3_game_entity_inuse(recipient) || recipient->client == NULL)
    return false;
  /* TS source.observeSupply(actor, recipient.actor.id). */
  qa_actor_id recipient_actor = qa_q3_game_entity_actor(recipient).id;
  qa_q3_game_entity *pickup = qa_q3_base_entity_records_native_by_actor(records, true, actor);
  qa_q3_game_entity *player = qa_q3_base_entity_records_native_by_actor(records, true, recipient_actor);
  if (pickup == NULL || player == NULL)
    return false;
  qa_q3_supply_observation supply;
  qa_q3_item_lifecycle_context context =
      qa_app_q3_item_lifecycle_host_context(qa_app_q3_source_subsystems_owner_items(g->source));
  if (!qa_q3_observe_q3_supply(pickup, player, context, &supply))
    return false;
  qa_bot_observed_pickup_bots_a *out = &cell->pickup;
  out->observation = supply.observation;
  out->preview = supply.preview;
  if (!copy_receipts(supply.preview.ammo, supply.preview.ammo_count, &cell->ammo) ||
      !copy_receipts(supply.preview.weapons, supply.preview.weapon_count, &cell->weapons))
    qa_range_error("Out of memory observing a bot pickup");
  out->preview.ammo = cell->ammo;
  out->preview.weapons = cell->weapons;
  out->entity = qa_int32(entity->s.number);
  out->name = qa_q3_game_entity_classname(entity);
  out->origin = qa_q3_base_entity_shared_current_origin(&entity->r);
  out->bounds.min = qa_q3_base_entity_shared_mins(&entity->r);
  out->bounds.max = qa_q3_base_entity_shared_maxs(&entity->r);
  return true;
}

static bool pickups_owns_item_goal(void *ctx, int64_t client, int64_t number) {
  (void)client; /* TS (_client, number) */
  game_t *g = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool_of(g), (double)number);
  return qa_q3_game_entity_inuse(entity) && entity->item != NULL &&
         (entity->item->type == QA_Q3_BASE_IT_WEAPON || entity->item->type == QA_Q3_BASE_IT_AMMO);
}

static bool pickups_inspect(void *ctx, int64_t client, qa_actor_id actor, qa_bot_observed_pickup_bots_a *out) {
  game_t *g = ctx;
  pickup_cell_clear(&g->inspected);
  if (!inspect_into(g, client, actor, &g->inspected))
    return false;
  *out = g->inspected.pickup;
  return true;
}

static void candidates_clear(game_t *g) {
  for (size_t i = 0; i < g->candidate_count; i++)
    pickup_cell_clear(&g->candidates[i]);
  g->candidate_count = 0;
}

static const qa_bot_observed_pickup_bots_a *pickups_candidates(void *ctx, int64_t client, size_t *out_count) {
  game_t *g = ctx;
  candidates_clear(g);
  qa_q3_entity_pool *pool = pool_of(g);
  for (double number = 0; number < pool->num_entities; number++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, number);
    if (!qa_q3_game_entity_inuse(entity) || entity->item == NULL)
      continue;
    if (g->candidate_count == g->candidate_capacity) {
      size_t capacity = g->candidate_capacity == 0 ? 16 : g->candidate_capacity * 2;
      qa_q3_bot_pickup_cell_bots_a *cells = realloc(g->candidates, capacity * sizeof *cells);
      if (cells == NULL)
        qa_range_error("Out of memory listing bot pickups");
      g->candidates = cells;
      qa_bot_observed_pickup_bots_a *view = realloc(g->candidate_view, capacity * sizeof *view);
      if (view == NULL)
        qa_range_error("Out of memory listing bot pickups");
      g->candidate_view = view;
      g->candidate_capacity = capacity;
    }
    qa_q3_bot_pickup_cell_bots_a *cell = &g->candidates[g->candidate_count];
    memset(cell, 0, sizeof *cell);
    if (inspect_into(g, client, qa_q3_game_entity_actor(entity).id, cell)) {
      g->candidate_view[g->candidate_count] = cell->pickup;
      g->candidate_count++;
    } else
      pickup_cell_clear(cell);
  }
  *out_count = g->candidate_count;
  return g->candidate_view;
}

/* ---- knowledge without an arsenal: createBotArsenalKnowledge ---- */
static void source_update_inventory(void *ctx, struct qa_bot_state_bots_a *state) {
  (void)ctx; /* TS updateQ3BotInventory takes no closure */
  qa_update_q3_bot_inventory_bots_a(state);
}
static qa_bot_weapon_knowledge_bots_a *source_candidates(void *ctx, struct qa_bot_library_bots_a *library,
                                                         int64_t handle, size_t *out_count) {
  game_t *g = ctx;
  int64_t count = (int64_t)qa_q3_base_weapon_count(qa_app_q3_source_subsystems_owner_product(g->source)) - 1;
  qa_bot_weapon_knowledge_bots_a *out = calloc(count > 0 ? (size_t)count : 1, sizeof *out);
  if (out == NULL)
    qa_range_error("Out of memory listing bot weapons");
  size_t n = 0;
  for (int64_t weapon = 1; weapon <= count; weapon++) {
    qa_weapon_info_bots_a info;
    if (!qa_weapon_ai_get_weapon_info(&library->weapons, handle, weapon, &info) || !info.valid)
      continue;
    qa_bot_weapon_knowledge_bots_a *k = &out[n++];
    k->info = info;
    k->has_maximum_range = weapon == QA_Q3_WP_GAUNTLET;
    k->maximum_range = weapon == QA_Q3_WP_GAUNTLET ? 60 : 0;
    k->melee = weapon == QA_Q3_WP_GAUNTLET;
    k->has_personality_role = true;
    k->personality_role = weapon;
    k->has_supply = false;
  }
  *out_count = n;
  return out;
}

/* ---- knowledge over an arsenal binding: { ...arsenal.knowledge, updateInventory } ---- */
#define ARSENAL_KNOWLEDGE(g) ((g)->arsenal->knowledge)
static double arsenal_pickup_utility(void *ctx, struct qa_bot_library_bots_a *library,
                                     struct qa_bot_state_bots_a *state, const qa_pickup_supply_preview *preview) {
  game_t *g = ctx;
  return ARSENAL_KNOWLEDGE(g)->pickup_utility(ARSENAL_KNOWLEDGE(g)->ctx, library, state, preview);
}
static int64_t arsenal_choose_weapon(void *ctx, struct qa_bot_library_bots_a *library,
                                     struct qa_bot_state_bots_a *state) {
  game_t *g = ctx;
  return ARSENAL_KNOWLEDGE(g)->choose_weapon(ARSENAL_KNOWLEDGE(g)->ctx, library, state);
}
static int64_t arsenal_activation_weapon(void *ctx, struct qa_bot_library_bots_a *library,
                                         struct qa_bot_state_bots_a *state) {
  game_t *g = ctx;
  return ARSENAL_KNOWLEDGE(g)->activation_weapon(ARSENAL_KNOWLEDGE(g)->ctx, library, state);
}
static qa_bot_weapon_tactics_bots_a arsenal_tactics(void *ctx, int64_t weapon) {
  game_t *g = ctx;
  return ARSENAL_KNOWLEDGE(g)->tactics(ARSENAL_KNOWLEDGE(g)->ctx, weapon);
}
static double arsenal_aggression(void *ctx, struct qa_bot_state_bots_a *state) {
  game_t *g = ctx;
  return ARSENAL_KNOWLEDGE(g)->aggression(ARSENAL_KNOWLEDGE(g)->ctx, state);
}
static bool arsenal_weapon_info(void *ctx, struct qa_bot_library_bots_a *library, int64_t handle, int64_t weapon,
                                qa_weapon_info_bots_a *out) {
  game_t *g = ctx;
  return ARSENAL_KNOWLEDGE(g)->weapon_info(ARSENAL_KNOWLEDGE(g)->ctx, library, handle, weapon, out);
}
static void arsenal_update_inventory(void *ctx, struct qa_bot_state_bots_a *state) {
  game_t *g = ctx;
  ARSENAL_KNOWLEDGE(g)->update_inventory(ARSENAL_KNOWLEDGE(g)->ctx, state);
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(state->product);
  state->src->inventory[QA_INV_ARMOR] = state->src->cur_ps.stats[(size_t)schema->armor];
  qa_update_q3_bot_item_inventory_bots_a(state);
}

/* ---- game members ---- */
static int64_t game_game_type(void *ctx) {
  game_t *g = ctx;
  return (int64_t)qa_app_q3_source_subsystems_owner_game_type(g->source);
}
static int64_t game_max_clients(void *ctx) { return (int64_t)pool_of(ctx)->max_clients; }
static int64_t game_entity_count(void *ctx) { return (int64_t)pool_of(ctx)->num_entities; }
static qa_q3_base_server_trace_result game_trace(void *ctx, qa_q3_base_server_trace_query query) {
  game_t *g = ctx;
  return qa_q3_base_world_adapter_trace(qa_app_q3_source_subsystems_owner_world(g->source), query);
}
static int64_t game_point_contents(void *ctx, qa_vec3 point, int64_t pass_entity) {
  game_t *g = ctx;
  return qa_int32(qa_q3_base_world_adapter_point_contents(qa_app_q3_source_subsystems_owner_world(g->source), point,
                                                          (double)pass_entity));
}
static double clock_time(void *ctx) {
  game_t *g = ctx;
  return qa_app_q3_source_subsystems_owner_level(g->source)->base.time;
}
static double clock_start_time(void *ctx) {
  game_t *g = ctx;
  return qa_app_q3_source_subsystems_owner_level(g->source)->base.start_time;
}
static double clock_intermission_time(void *ctx) {
  game_t *g = ctx;
  return qa_app_q3_source_subsystems_owner_level(g->source)->base.intermission_time;
}

static bool linked(game_t *g, int64_t number) {
  qa_q3_base_link_state state;
  return qa_q3_base_world_adapter_link_state(qa_app_q3_source_subsystems_owner_world(g->source), (double)number,
                                             &state) &&
         state.linked;
}

static bool game_observable(void *ctx, int64_t number) {
  game_t *g = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool_of(g), (double)number);
  return qa_q3_game_entity_inuse(entity) && linked(g, number) &&
         (qa_int32(entity->r.sv_flags) & QA_Q3_BASE_SVF_NOCLIENT) == 0;
}

/* TS entity.s.copy(): one EntityState type, a plain record copy. */
static void copy_entity_state(qa_q3_base_entity_state *out, const qa_q3_entity_state_record *s) {
  qa_q3_entity_state_record_copy(out, s);
}

/* TS client.ps.copy() into the game's detached player-state cell. */
static const qa_q3_base_player_state *copy_player_state(game_t *g, const qa_q3_base_player_state *ps) {
  if (g->has_player_state) {
    qa_q3_base_player_state_record_destroy(&g->player_state);
    g->has_player_state = false;
  }
  if (!qa_q3_base_player_state_copy(&g->player_state, ps))
    qa_range_error("Out of memory observing a bot player state");
  g->has_player_state = true;
  return &g->player_state;
}

static qa_bot_observed_entity_bots_a game_entity(void *ctx, int64_t number) {
  game_t *g = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool_of(g), (double)number);
  qa_q3_game_client *client = entity->client;
  qa_bot_observed_entity_bots_a out;
  memset(&out, 0, sizeof out);
  copy_entity_state(&out.state, &entity->s);
  const qa_q3_base_player_state *player_state = client == NULL ? NULL : copy_player_state(g, &client->ps);
  if (g->arsenal != NULL && player_state != NULL) {
    g->player_state.weapon = (double)g->arsenal->source_weapon(g->arsenal->ctx, number);
    g->player_state.weapon_state = (double)g->arsenal->source_weapon_state(g->arsenal->ctx, number);
    out.state.weapon = qa_int32(g->player_state.weapon);
  }
  bool inuse = qa_q3_game_entity_inuse(entity);
  out.generation = inuse ? qa_q3_game_entity_actor(entity).id.generation : 0;
  out.present = inuse;
  out.linked = linked(g, number);
  out.hidden = (qa_int32(entity->r.sv_flags) & QA_Q3_BASE_SVF_NOCLIENT) != 0;
  out.bot = (qa_int32(entity->r.sv_flags) & QA_Q3_BASE_SVF_BOT) != 0;
  out.origin = qa_q3_base_entity_shared_current_origin(&entity->r);
  out.angles = qa_q3_base_entity_shared_current_angles(&entity->r);
  out.bounds.min = qa_q3_base_entity_shared_mins(&entity->r);
  out.bounds.max = qa_q3_base_entity_shared_maxs(&entity->r);
  out.contents = qa_int32(entity->r.contents);
  out.has_inline_model = entity->r.model.kind == QA_Q3_BASE_MODEL_INLINE;
  out.inline_model = out.has_inline_model ? (int64_t)entity->r.model.index : 0;
  out.classname = qa_q3_game_entity_classname(entity);
  out.event_time = (int64_t)entity->event_time;
  out.has_activator_frame = entity->activator != NULL;
  out.activator_frame = entity->activator != NULL ? (int64_t)entity->activator->s.frame : 0;
  out.proximity_trigger =
      qa_q3_missile_runtime_is_proximity_trigger(qa_app_q3_source_subsystems_owner_missiles(g->source), entity);
  out.has_player = client != NULL && player_state != NULL;
  if (out.has_player) {
    out.player_state = player_state;
    out.player_connected = client->pers.connected == QA_Q3_CONN_CONNECTED;
    out.player_team = (int64_t)client->sess.session_team;
    out.player_name = client->pers.netname == NULL ? "" : client->pers.netname;
    out.last_hurt_client = (int64_t)client->last_hurt_client;
    out.last_hurt_mod = (int64_t)client->last_hurt_mod;
  }
  return out;
}

static int64_t game_model_index(void *ctx, const char *name) {
  game_t *g = ctx;
  return (int64_t)qa_q3_config_string_registry_model_index(qa_app_q3_source_subsystems_owner_config(g->source), name);
}
static int64_t game_choose_team(void *ctx, int64_t client) {
  game_t *g = ctx;
  qa_q3_entity_pool *pool = pool_of(g);
  qa_q3_ta_session_world_state world;
  memset(&world, 0, sizeof world);
  world.clients = pool->clients;
  world.client_count = QA_Q3_MAX_CLIENTS;
  world.max_clients = pool->max_clients;
  world.team_scores = &qa_app_q3_source_subsystems_owner_level(g->source)->team_scores;
  return (int64_t)qa_q3_ta_pick_team(&world, (double)client);
}
static void game_activate_bot(void *ctx, int64_t client) {
  game_t *g = ctx;
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool_of(g), (double)client);
  entity->r.sv_flags = (double)(qa_int32(entity->r.sv_flags) | QA_Q3_BASE_SVF_BOT);
  qa_q3_entity_pool_activate_client(pool_of(g), (double)client);
}
static void game_exit_level(void *ctx) {
  game_t *g = ctx;
  qa_q3_ta_match_runtime_exit_level(qa_app_q3_source_subsystems_owner_match(g->source));
}
static void game_reset_podium_players(void *ctx) {
  game_t *g = ctx;
  qa_q3_ta_arena_runtime_reset_podium_players(qa_app_q3_source_subsystems_owner_arenas(g->source));
}
static void game_client_userinfo_changed(void *ctx, int64_t client) {
  game_t *g = ctx;
  qa_q3_ta_client_admission_runtime_userinfo_changed(qa_app_q3_source_subsystems_owner_admission(g->source),
                                                     (double)client);
}
static const char *game_client_connect(void *ctx, int64_t client, bool first_time, bool is_bot) {
  game_t *g = ctx;
  return qa_q3_ta_client_admission_runtime_connect(qa_app_q3_source_subsystems_owner_admission(g->source),
                                                   (double)client, first_time, is_bot);
}
static void game_client_begin(void *ctx, int64_t client) {
  game_t *g = ctx;
  qa_q3_ta_client_admission_runtime_begin(qa_app_q3_source_subsystems_owner_admission(g->source), (double)client);
}

bool qa_q3_bot_game_bots_a_init(qa_q3_bot_game_bots_a *g, struct qa_app_q3_source_subsystems_owner *source,
                                const struct qa_app_q3_source_host *host,
                                void (*insert_console_command)(void *ctx, const char *text),
                                void *insert_ctx, const qa_q3_bot_arsenal_binding_bots_a *arsenal) {
  memset(g, 0, sizeof *g);
  g->source = source;
  g->host = host;
  g->insert_console_command = insert_console_command;
  g->insert_ctx = insert_ctx;
  g->arsenal = arsenal;
  g->configstrings = (qa_q3_config_string_store){host->configstrings_ctx, host->configstring_get,
                                                 host->configstring_set};
  qa_source_bot_game_bots_a *game = &g->game;
  game->ctx = g;
  if (arsenal != NULL) {
    g->pickups = (qa_bot_pickup_observations_bots_a){g, pickups_owns_item_goal, pickups_candidates, pickups_inspect};
    game->pickups = &g->pickups;
  }
  game->options.product = qa_app_q3_source_subsystems_owner_product(source);
  game->options.cvars = host->cvars;
  game->options.configstrings = &g->configstrings;
  qa_source_bot_engine_bots_a *engine = &game->options.engine;
  engine->ctx = g;
  engine->insert_ctx = insert_ctx;
  engine->print = engine_print;
  engine->get_userinfo = engine_get_userinfo;
  engine->set_userinfo = engine_set_userinfo;
  engine->send_server_command = engine_send_server_command;
  engine->drop_client = engine_drop_client;
  engine->insert_console_command = insert_console_command;
  engine->append_console_command = engine_append_console_command;
  game->game_type = game_game_type;
  game->max_clients = game_max_clients;
  game->entity_count = game_entity_count;
  game->trace = game_trace;
  game->point_contents = game_point_contents;
  game->random = qa_source_bot_game_random_bots_a(qa_app_q3_source_subsystems_owner_random(source));
  game->clock_time = clock_time;
  game->clock_start_time = clock_start_time;
  game->clock_intermission_time = clock_intermission_time;
  game->memory = qa_app_q3_source_subsystems_owner_memory(source);
  if (arsenal == NULL) {
    qa_bot_arsenal_data_bots_a data = {g, source_update_inventory, source_candidates};
    qa_create_bot_arsenal_knowledge_bots_a(&g->source_knowledge, &data);
    game->knowledge = &g->source_knowledge.knowledge;
  } else {
    g->arsenal_knowledge = (qa_bot_arsenal_interface_bots_a){
        g, arsenal_pickup_utility, arsenal_choose_weapon, arsenal_activation_weapon, arsenal_tactics,
        arsenal_aggression, arsenal_update_inventory, arsenal_weapon_info};
    game->knowledge = &g->arsenal_knowledge;
  }
  game->entity = game_entity;
  game->observable = game_observable;
  game->model_index = game_model_index;
  game->choose_team = game_choose_team;
  game->activate_bot = game_activate_bot;
  game->exit_level = game_exit_level;
  game->reset_podium_players = game_reset_podium_players;
  game->client_userinfo_changed = game_client_userinfo_changed;
  game->client_connect = game_client_connect;
  game->client_begin = game_client_begin;
  return true;
}

void qa_q3_bot_game_bots_a_destroy(qa_q3_bot_game_bots_a *g) {
  candidates_clear(g);
  free(g->candidates);
  free(g->candidate_view);
  pickup_cell_clear(&g->inspected);
  if (g->arsenal == NULL)
    qa_bot_arsenal_knowledge_bots_a_destroy(&g->source_knowledge);
  if (g->has_player_state)
    qa_q3_base_player_state_record_destroy(&g->player_state);
  free(g->userinfo);
  memset(g, 0, sizeof *g);
}
