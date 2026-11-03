/* Port of src/app/bootstrap/simulation/bot-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/bots/behavior/q3/ai_definitions.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/save_image.h"

typedef struct id_entry { qa_actor_id actor; int64_t id; } id_entry;
typedef struct text_entry { int64_t id; char *value; } text_entry;
typedef struct model_entry { char *name; int64_t id; } model_entry;
typedef struct number_entry { int64_t id, value; } number_entry;
typedef struct pickup_cell { qa_bot_observed_pickup_bots_a pickup; qa_pickup_ammo_receipt *ammo, *weapons; qa_pickup_ammo_grant *grants; char *name; } pickup_cell;

struct qa_app_sim_shared_bot_world {
  qa_app_sim_shared_bot_world_options options;
  const qa_app_sim_bot_simulation *simulation;
  bool q2; /* source.kind */
  const qa_app_sim_bot_q1_source *q1_source;
  qa_q2_product_runtime *q2_source;
  bool has_arsenal; qa_q2_game_services *arsenal_game; qa_q2w_weapons *arsenal_weapons; /* q2WeaponSource() */
  qa_numeric_profile numeric;
  char *map_name;
  qa_app_sim_bot_arsenal *knowledge;
  qa_bot_arsenal_interface_bots_a knowledge_view;
  qa_source_bot_game_bots_a game;
  qa_bot_pickup_observations_bots_a pickups;
  qa_q3_config_string_store configstrings;
  qa_q3_game_memory *memory;
  id_entry *ids; size_t id_count, id_capacity; /* TS ids / actors maps (one row per pair) */
  text_entry *userinfos; size_t userinfo_count, userinfo_capacity;
  text_entry *strings; size_t string_count, string_capacity;
  model_entry *models; size_t model_count, model_capacity;
  int64_t *free_ids; size_t free_count, free_capacity;
  number_entry *generations; size_t generation_count, generation_capacity;
  int64_t *begun; size_t begun_count, begun_capacity;
  int64_t next_entity, next_generation;
  /* scratch */
  bool has_player_state; qa_q3_base_player_state player_state;
  char *player_name, *classname, *info;
  pickup_cell inspected;
  pickup_cell *candidates; qa_bot_observed_pickup_bots_a *candidate_view; size_t candidate_count, candidate_capacity;
};
typedef struct qa_app_sim_shared_bot_world world_t;

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static void *grow(void *p, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return p;
  *capacity = *capacity != 0 ? *capacity * 2 : 8;
  void *q = realloc(p, *capacity * size);
  if (q == NULL) qa_range_error("Out of memory");
  return q;
}
static bool is_live(world_t *w, qa_actor_id actor) { return qa_session_actor_registry_is_live(w->simulation->actors, actor); }

/* ---- TS metadata(actor) / clientInfo(actor) ---- */
typedef struct metadata { const char *model, *classname; double frame, max_health; bool hidden; } metadata;
static bool metadata_of(world_t *w, const qa_actor_id *actor, metadata *out) {
  if (actor == NULL) return false;
  qa_q2_game_services *q2_game = w->q2 ? w->q2_source->services : w->has_arsenal ? w->arsenal_game : NULL;
  qa_q2_entity *q2 = q2_game != NULL ? q2_game->entity(q2_game->ctx, actor) : NULL;
  if (q2 != NULL) {
    out->model = q2->model != NULL ? q2->model : ""; out->frame = q2->frame; out->classname = q2->classname;
    out->hidden = (qa_js_to_int32(q2->server_flags) & 1) != 0; out->max_health = q2->max_health; return true;
  }
  qa_q1_actor *q1 = !w->q2 ? qa_q1_get_entity(w->q1_source->game, actor) : NULL;
  if (q1 == NULL) return false;
  qa_q1_player_state *player = qa_q1_player(w->q1_source->game, *actor);
  out->model = q1->model != NULL ? q1->model : ""; out->frame = q1->frame; out->classname = q1->classname; out->hidden = false;
  out->max_health = player != NULL ? player->max_health : q1->max_health;
  return true;
}
typedef struct client_info { const char *name, *skin; bool spectator; double score; } client_info;
static bool client_info_of(world_t *w, const qa_actor_id *actor, client_info *out) {
  if (actor == NULL) return false;
  if (w->q2) {
    qa_q2_base_player_state *player = qa_q2_players_get_state(w->q2_source->players, *actor);
    if (player == NULL) return false;
    out->name = player->name; out->spectator = player->spectator; out->score = player->score; out->skin = player->skin;
    return true;
  }
  qa_comp_q1_source_client *player = qa_comp_q1_source_clients_get(&w->q1_source->composition->clients, *actor);
  if (player == NULL) return false;
  out->name = qa_comp_q1_source_client_name(player); out->spectator = player->observer; out->score = player->frags;
  out->skin = w->simulation->recipe->character.appearance.provider;
  return true;
}

/* ---- ids ---- */
static void retire(world_t *w) {
  for (size_t i = 0; i < w->id_count;) {
    if (is_live(w, w->ids[i].actor)) { i++; continue; }
    w->free_ids = grow(w->free_ids, &w->free_capacity, w->free_count, sizeof *w->free_ids);
    w->free_ids[w->free_count++] = w->ids[i].id;
    memmove(&w->ids[i], &w->ids[i + 1], (w->id_count - i - 1) * sizeof *w->ids);
    w->id_count--;
  }
}
static void set_generation(world_t *w, int64_t id, int64_t generation) {
  for (size_t i = 0; i < w->generation_count; i++) if (w->generations[i].id == id) { w->generations[i].value = generation; return; }
  w->generations = grow(w->generations, &w->generation_capacity, w->generation_count, sizeof *w->generations);
  w->generations[w->generation_count].id = id; w->generations[w->generation_count].value = generation; w->generation_count++;
}
static bool generation_of(world_t *w, int64_t id, int64_t *out) {
  for (size_t i = 0; i < w->generation_count; i++) if (w->generations[i].id == id) { *out = w->generations[i].value; return true; }
  return false;
}
int64_t qa_app_sim_shared_bot_world_entity_id(world_t *w, qa_actor_id actor) {
  const qa_app_sim_bot_simulation *sim = w->simulation;
  qa_app_sim_movement_player *player = sim->movement_player(sim->ctx, actor);
  if (player != NULL) return player->client.slot;
  metadata m;
  if (metadata_of(w, &actor, &m) && is(m.classname, "worldspawn")) return 1022;
  for (size_t i = 0; i < w->id_count; i++) if (qa_same_actor(w->ids[i].actor, actor)) return w->ids[i].id;
  retire(w);
  int64_t id = w->free_count != 0 ? w->free_ids[--w->free_count] : w->next_entity++;
  if (id >= 1022) fail("Live bot observations exceed the current simultaneous entity capacity");
  w->ids = grow(w->ids, &w->id_capacity, w->id_count, sizeof *w->ids);
  w->ids[w->id_count].actor = actor; w->ids[w->id_count].id = id; w->id_count++;
  set_generation(w, id, w->next_generation++);
  return id;
}
void qa_app_sim_shared_bot_world_refresh(world_t *w) {
  retire(w);
  size_t count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(w->simulation->actors, &count);
  qa_actor_id *actors = calloc(count != 0 ? count : 1, sizeof *actors);
  if (actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) actors[i] = observations[i].id;
  for (size_t i = 0; i < count; i++)
    if (qa_shared_body_table_read(w->simulation->bodies, actors[i]).has_value) qa_app_sim_shared_bot_world_entity_id(w, actors[i]);
  free(actors);
}
bool qa_app_sim_shared_bot_world_actor_for_id(world_t *w, int64_t number, qa_actor_id *out) {
  const qa_app_sim_bot_simulation *sim = w->simulation;
  bool found = false;
  qa_actor_id actor = {0};
  if (number < 64) {
    size_t count = 0;
    const qa_actor_id *players = sim->players(sim->ctx, &count);
    for (size_t i = 0; i < count && !found; i++) {
      qa_app_sim_movement_player *player = sim->movement_player(sim->ctx, players[i]);
      if (player != NULL && player->client.slot == number) { actor = players[i]; found = true; }
    }
  } else if (number == 1022) {
    if (!w->q2) { if (w->q1_source->game->world != NULL) { actor = w->q1_source->game->world->actor.id; found = true; } }
    else { actor = w->q2_source->services->host->world_actor(w->q2_source->services->host->ctx); found = true; }
  } else {
    for (size_t i = 0; i < w->id_count && !found; i++) if (w->ids[i].id == number) { actor = w->ids[i].actor; found = true; }
  }
  if (!found || !is_live(w, actor)) return false;
  *out = actor; return true;
}
static int64_t model_index(void *ctx, const char *name) {
  world_t *w = ctx;
  if (name == NULL || name[0] == '\0') return 0;
  if (name[0] == '*') return (int64_t)qa_js_string_to_number(name + 1);
  for (size_t i = 0; i < w->model_count; i++) if (strcmp(w->models[i].name, name) == 0) return w->models[i].id;
  int64_t index = (int64_t)w->simulation->world_model_count + (int64_t)w->model_count;
  w->models = grow(w->models, &w->model_capacity, w->model_count, sizeof *w->models);
  w->models[w->model_count].name = xstrdup(name); w->models[w->model_count].id = index; w->model_count++;
  return index;
}
static const char *text_get(const text_entry *entries, size_t count, int64_t id) {
  for (size_t i = 0; i < count; i++) if (entries[i].id == id) return entries[i].value;
  return NULL;
}
static void text_set(text_entry **entries, size_t *count, size_t *capacity, int64_t id, const char *value) {
  for (size_t i = 0; i < *count; i++) if ((*entries)[i].id == id) { free((*entries)[i].value); (*entries)[i].value = xstrdup(value); return; }
  *entries = grow(*entries, capacity, *count, sizeof **entries);
  (*entries)[*count].id = id; (*entries)[*count].value = xstrdup(value); (*count)++;
}
static const char *player_info(world_t *w, int64_t client) {
  qa_actor_id actor;
  bool has_actor = qa_app_sim_shared_bot_world_actor_for_id(w, client, &actor);
  client_info player;
  if (!client_info_of(w, has_actor ? &actor : NULL, &player)) return "";
  const char *spectator = player.spectator ? "3" : "0";
  size_t length = strlen(player.name) + strlen(player.skin) + 32;
  free(w->info);
  w->info = malloc(length);
  if (w->info == NULL) qa_range_error("Out of memory");
  snprintf(w->info, length, "\\n\\%s\\t\\%s\\model\\%s", player.name, spectator, player.skin);
  return w->info;
}

/* ---- pickups ---- */
static void cell_clear(pickup_cell *cell) {
  free(cell->ammo); free(cell->weapons); free(cell->grants); free(cell->name);
  memset(cell, 0, sizeof *cell);
}
static void *copy_array(const void *source, size_t count, size_t size) {
  if (count == 0) return NULL;
  void *p = malloc(count * size);
  if (p == NULL) qa_range_error("Out of memory");
  memcpy(p, source, count * size);
  return p;
}
static bool inspect_into(world_t *w, int64_t client, qa_actor_id actor, pickup_cell *cell) {
  qa_actor_id recipient;
  if (!qa_app_sim_shared_bot_world_actor_for_id(w, client, &recipient) || !is_live(w, actor)) return false;
  qa_body_state_result body = qa_shared_body_table_read(w->simulation->bodies, actor);
  if (!body.has_value) return false;
  qa_pickup_supply_observation observation;
  bool observed = !w->q2 ? qa_q1_observe_supply(w->q1_source->game, actor, recipient, &observation)
      : qa_q2_item_module_observe_supply(w->q2_source->items, w->q2_source->services, actor, recipient, &observation);
  if (!observed) return false;
  qa_pickup_ammo_grant *grants = copy_array(observation.offer.ammo, observation.offer.ammo_count, sizeof *grants);
  qa_pickup_supply_preview preview;
  bool previewed = !w->q2 ? qa_q1_preview_supply(w->q1_source->game, actor, recipient, &preview)
      : qa_q2_item_module_preview_supply(w->q2_source->items, w->q2_source->services, actor, recipient, &preview);
  if (!previewed) { free(grants); return false; }
  metadata entity;
  bool has_entity = metadata_of(w, &actor, &entity);
  cell_clear(cell);
  cell->grants = grants; observation.offer.ammo = grants;
  cell->ammo = copy_array(preview.ammo, preview.ammo_count, sizeof *cell->ammo);
  cell->weapons = copy_array(preview.weapons, preview.weapon_count, sizeof *cell->weapons);
  preview.ammo = cell->ammo; preview.weapons = cell->weapons;
  static const char *const kinds[] = {"ammo", "ammoWeapon", "weapon"};
  cell->name = xstrdup(has_entity && entity.classname != NULL ? entity.classname : kinds[observation.offer.kind]);
  cell->pickup.observation = observation; cell->pickup.preview = preview;
  cell->pickup.entity = qa_app_sim_shared_bot_world_entity_id(w, actor);
  cell->pickup.origin = body.value.origin; cell->pickup.bounds = body.value.bounds; cell->pickup.name = cell->name;
  return true;
}
static bool pickups_inspect(void *ctx, int64_t client, qa_actor_id actor, qa_bot_observed_pickup_bots_a *out) {
  world_t *w = ctx;
  if (!inspect_into(w, client, actor, &w->inspected)) return false;
  *out = w->inspected.pickup; return true;
}
static const qa_bot_observed_pickup_bots_a *pickups_candidates(void *ctx, int64_t client, size_t *out_count) {
  world_t *w = ctx;
  for (size_t i = 0; i < w->candidate_count; i++) cell_clear(&w->candidates[i]);
  w->candidate_count = 0;
  size_t actor_count = w->id_count;
  qa_actor_id *actors = calloc(actor_count != 0 ? actor_count : 1, sizeof *actors);
  if (actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < actor_count; i++) actors[i] = w->ids[i].actor;
  for (size_t i = 0; i < actor_count; i++) {
    if (w->candidate_count == w->candidate_capacity) {
      size_t capacity = w->candidate_capacity != 0 ? w->candidate_capacity * 2 : 16;
      pickup_cell *cells = realloc(w->candidates, capacity * sizeof *cells);
      qa_bot_observed_pickup_bots_a *view = realloc(w->candidate_view, capacity * sizeof *view);
      if (cells == NULL || view == NULL) qa_range_error("Out of memory");
      memset(cells + w->candidate_capacity, 0, (capacity - w->candidate_capacity) * sizeof *cells);
      w->candidates = cells; w->candidate_view = view; w->candidate_capacity = capacity;
    }
    if (inspect_into(w, client, actors[i], &w->candidates[w->candidate_count])) {
      w->candidate_view[w->candidate_count] = w->candidates[w->candidate_count].pickup; w->candidate_count++;
    }
  }
  free(actors);
  *out_count = w->candidate_count;
  return w->candidate_view;
}

/* ---- configstrings / engine ---- */
static const char *configstring_get(void *ctx, double index) {
  world_t *w = ctx;
  if (index >= 544 && index < 608) return player_info(w, (int64_t)index - 544);
  const char *value = text_get(w->strings, w->string_count, (int64_t)index);
  return value != NULL ? value : "";
}
static void configstring_set(void *ctx, double index, const char *value) {
  world_t *w = ctx;
  text_set(&w->strings, &w->string_count, &w->string_capacity, (int64_t)index, value);
}
static void engine_print(void *ctx, const char *text) { world_t *w = ctx; w->options.print(w->options.ctx, text); }
static const char *engine_get_userinfo(void *ctx, int64_t client) {
  world_t *w = ctx;
  const char *value = text_get(w->userinfos, w->userinfo_count, client);
  return value != NULL ? value : "";
}
static void engine_set_userinfo(void *ctx, int64_t client, const char *info) {
  world_t *w = ctx;
  text_set(&w->userinfos, &w->userinfo_count, &w->userinfo_capacity, client, info);
}
static void engine_send_server_command(void *ctx, int64_t client, const char *text) { world_t *w = ctx; w->options.message(w->options.ctx, client, text); }
static void engine_drop_client(void *ctx, int64_t client, const char *reason) {
  world_t *w = ctx;
  w->options.print(w->options.ctx, reason); w->options.drop(w->options.ctx, client);
}
static void engine_console(void *ctx, const char *text) { world_t *w = ctx; w->options.console(w->options.ctx, text); }

/* ---- SourceBotGame members ---- */
static int64_t game_type(void *ctx) { (void)ctx; return 0; }
static int64_t max_clients(void *ctx) { world_t *w = ctx; return (int64_t)w->simulation->max_clients; }
static int64_t entity_count(void *ctx) { world_t *w = ctx; return w->next_entity; }
static double clock_time(void *ctx) { world_t *w = ctx; return trunc(w->simulation->time_seconds(w->simulation->ctx) * 1000); }
static double clock_start_time(void *ctx) { (void)ctx; return 0; }
static double clock_intermission_time(void *ctx) {
  world_t *w = ctx;
  if (w->q2) {
    qa_q2_intermission intermission = qa_q2_players_intermission(w->q2_source->players);
    return is(intermission.kind, "playing") ? 0 : trunc(intermission.started * 1000);
  }
  qa_q1_services *game = w->q1_source->game;
  return !game->has_intermission ? 0 : trunc((game->intermission_exit_after - 5) * 1000);
}
static void knowledge_update_inventory(void *ctx, qa_bot_state_bots_a *state) {
  world_t *w = ctx;
  qa_bot_arsenal_interface_bots_a *inner = &w->knowledge->knowledge.knowledge;
  inner->update_inventory(inner->ctx, state);
  qa_actor_id actor;
  bool has_actor = qa_app_sim_shared_bot_world_actor_for_id(w, state->src->client, &actor);
  if (w->q2) {
    qa_q2_game_services *game = w->q2_source->services;
    qa_q2_player_powerups powers = {0};
    if (has_actor) powers = qa_q2_item_module_player_powerups(w->q2_source->items, actor);
    double now = game->host->now(game->host->ctx);
    state->src->inventory[QA_INV_QUAD] = has_actor && powers.quad_until > now;
    state->src->inventory[QA_INV_ENVIRONMENTSUIT] = has_actor && (powers.breather_until > now || powers.enviro_until > now);
  } else {
    qa_q1_services *game = w->q1_source->game;
    state->src->inventory[QA_INV_QUAD] = has_actor && qa_q1_powerup_expires(game, actor, "quad") > game->time;
    state->src->inventory[QA_INV_ENVIRONMENTSUIT] = has_actor && qa_q1_powerup_expires(game, actor, "suit") > game->time;
  }
}
/* The other BotArsenalKnowledge members forward to the binding with its own ctx. */
static qa_bot_arsenal_interface_bots_a *inner_of(void *ctx) { world_t *w = ctx; return &w->knowledge->knowledge.knowledge; }
static double k_pickup_utility(void *ctx, qa_bot_library_bots_a *library, qa_bot_state_bots_a *state, const qa_pickup_supply_preview *preview) {
  qa_bot_arsenal_interface_bots_a *k = inner_of(ctx); return k->pickup_utility(k->ctx, library, state, preview);
}
static int64_t k_choose_weapon(void *ctx, qa_bot_library_bots_a *library, qa_bot_state_bots_a *state) {
  qa_bot_arsenal_interface_bots_a *k = inner_of(ctx); return k->choose_weapon(k->ctx, library, state);
}
static int64_t k_activation_weapon(void *ctx, qa_bot_library_bots_a *library, qa_bot_state_bots_a *state) {
  qa_bot_arsenal_interface_bots_a *k = inner_of(ctx); return k->activation_weapon(k->ctx, library, state);
}
static qa_bot_weapon_tactics_bots_a k_tactics(void *ctx, int64_t weapon) { qa_bot_arsenal_interface_bots_a *k = inner_of(ctx); return k->tactics(k->ctx, weapon); }
static double k_aggression(void *ctx, qa_bot_state_bots_a *state) { qa_bot_arsenal_interface_bots_a *k = inner_of(ctx); return k->aggression(k->ctx, state); }
static bool k_weapon_info(void *ctx, qa_bot_library_bots_a *library, int64_t handle, int64_t weapon, qa_weapon_info_bots_a *out) {
  qa_bot_arsenal_interface_bots_a *k = inner_of(ctx); return k->weapon_info(k->ctx, library, handle, weapon, out);
}
static bool observable(void *ctx, int64_t number) {
  world_t *w = ctx;
  qa_actor_id actor;
  if (!qa_app_sim_shared_bot_world_actor_for_id(w, number, &actor) || !is_live(w, actor)) return false;
  if (!qa_shared_body_table_read(w->simulation->bodies, actor).has_value || !qa_shared_body_table_linked(w->simulation->bodies, actor).has_value) return false;
  metadata m;
  return !(metadata_of(w, &actor, &m) && m.hidden);
}
static bool brush(world_t *w, qa_actor_id actor) {
  qa_app_sim_shared_solid solid;
  return qa_app_sim_shared_physics_solid_of_actor(w->simulation->physics, actor, &solid) && is(solid.solid, "brush");
}
static qa_bot_observed_entity_bots_a game_entity(void *ctx, int64_t number) {
  world_t *w = ctx;
  const qa_app_sim_bot_simulation *sim = w->simulation;
  qa_actor_id actor;
  bool has_actor = qa_app_sim_shared_bot_world_actor_for_id(w, number, &actor);
  metadata entity;
  bool has_entity = metadata_of(w, has_actor ? &actor : NULL, &entity);
  qa_bot_observed_entity_bots_a out;
  memset(&out, 0, sizeof out);
  qa_q3_entity_state_init(&out.state);
  out.state.number = (int32_t)number;
  qa_body_state_result body = {0};
  qa_app_sim_movement_player *movement = NULL;
  if (has_actor) { body = qa_shared_body_table_read(sim->bodies, actor); movement = sim->movement_player(sim->ctx, actor); }
  client_info common;
  bool has_common = client_info_of(w, has_actor ? &actor : NULL, &common);
  if (has_actor && movement != NULL && has_common && body.has_value) {
    if (w->has_player_state) { qa_q3_base_player_state_record_destroy(&w->player_state); w->has_player_state = false; }
    if (!qa_q3_base_player_state_init(&w->player_state, QA_Q3_BASE_PRODUCT_BASEQ3, NULL)) qa_range_error("Out of memory");
    w->has_player_state = true;
    qa_q3_base_player_state *ps = &w->player_state;
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
    const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(QA_Q3_BASE_PRODUCT_BASEQ3);
    ps->client_num = (double)movement->client.slot;
    qa_q3_base_player_state_record_set_origin(ps, body.value.origin);
    qa_q3_base_player_state_record_set_velocity(ps, body.value.velocity);
    ps->viewangles = movement->view_angles;
    ps->viewheight = movement->view_height;
    ps->ground_entity_num = body.value.ground == NULL ? 1023 : (double)qa_app_sim_shared_bot_world_entity_id(w, *body.value.ground);
    ps->pm_type = common.spectator ? QA_Q3_PM_SPECTATOR : (combat.has_value ? combat.value.health : 0) <= 0 ? QA_Q3_PM_DEAD : QA_Q3_PM_NORMAL;
    ps->weapon = (double)w->knowledge->source_weapon(w->knowledge, number);
    ps->weapon_state = (double)w->knowledge->source_weapon_state(w->knowledge, number);
    qa_q3_base_player_state_slots_set(&ps->stats, schema->health, combat.has_value ? combat.value.health : 0);
    qa_q3_base_player_state_slots_set(&ps->stats, schema->armor,
        !combat.has_value || combat.value.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : combat.value.armor.regular.points);
    qa_q3_base_player_state_slots_set(&ps->stats, schema->max_health, has_entity ? entity.max_health : 100);
    qa_q3_base_player_state_slots_set(&ps->persistant, 0, common.score);
    qa_q3_base_player_state_slots_set(&ps->persistant, 3, common.spectator ? 3 : 0);
    qa_actor_id bot_actor;
    bool begun = false;
    for (size_t i = 0; i < w->begun_count; i++) if (w->begun[i] == number) begun = true;
    free(w->player_name); w->player_name = xstrdup(common.name);
    out.has_player = true; out.player_state = ps;
    out.player_connected = !w->options.actor(w->options.ctx, number, &bot_actor) || begun;
    out.player_team = common.spectator ? 3 : 0; out.player_name = w->player_name;
    out.last_hurt_client = 0; out.last_hurt_mod = 0;
    out.state.e_type = QA_Q3_BASE_ET_PLAYER; out.state.weapon = qa_js_to_int32(ps->weapon);
    out.state.pos.base = body.value.origin; out.state.apos.base = movement->view_angles;
    out.state.ground_entity_num = qa_js_to_int32(ps->ground_entity_num);
  } else out.state.e_type = has_actor && brush(w, actor) ? QA_Q3_BASE_ET_MOVER : QA_Q3_BASE_ET_GENERAL;
  qa_vec3 zero = {0, 0, 0};
  out.state.origin = body.has_value ? body.value.origin : zero;
  out.state.modelindex = has_entity ? (int32_t)model_index(w, entity.model) : 0;
  out.state.frame = has_entity ? qa_js_to_int32(entity.frame) : 0;
  int64_t generation = 0;
  out.generation = number < 64 ? (has_actor ? actor.generation : 0) : generation_of(w, number, &generation) ? generation : 0;
  out.present = has_actor && is_live(w, actor) && body.has_value;
  out.linked = has_actor && qa_shared_body_table_linked(sim->bodies, actor).has_value;
  out.hidden = has_entity && entity.hidden;
  qa_actor_id bot_actor;
  out.bot = has_actor && w->options.actor(w->options.ctx, number, &bot_actor) && qa_same_actor(bot_actor, actor);
  out.origin = body.has_value ? body.value.origin : zero;
  out.angles = body.has_value ? body.value.angles : zero;
  out.bounds = body.has_value ? body.value.bounds : (qa_bounds){zero, zero};
  out.contents = has_actor && brush(w, actor) ? 1 : !out.has_player ? 0 : 0x2000000;
  out.has_inline_model = has_entity && entity.model[0] == '*';
  out.inline_model = out.has_inline_model ? (int64_t)qa_js_string_to_number(entity.model + 1) : 0;
  free(w->classname); w->classname = has_entity && entity.classname != NULL ? xstrdup(entity.classname) : NULL;
  out.classname = w->classname;
  out.event_time = 0; out.has_activator_frame = false; out.proximity_trigger = false;
  return out;
}
static int64_t world_point_contents(void *ctx, qa_vec3 point, int64_t pass_entity) {
  (void)pass_entity; /* TS pointContents: point => ... */
  world_t *w = ctx;
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = point; /* target: world */
  query.policy.kind = "q3"; query.policy.contents_mask = -1; query.policy.curves = true; query.policy.player_curve_clip = true;
  query.numeric = w->numeric; query.pass_actor = NULL;
  qa_point_contents_result result = qa_shared_point_contents(w->simulation->scene, query);
  if (!is(result.kind, "q3")) fail("Bot contents did not use the Q3 decision representation");
  return (int64_t)result.contents;
}
static qa_q3_base_server_trace_result world_trace(void *ctx, qa_q3_base_server_trace_query query) {
  world_t *w = ctx;
  qa_trace_query trace;
  memset(&trace, 0, sizeof trace);
  trace.start = query.start; trace.end = query.end;
  if (query.shape.kind == QA_Q3_BASE_SHAPE_POINT) trace.shape.kind = "point";
  else { trace.shape.kind = "box"; trace.shape.bounds.min = query.shape.mins; trace.shape.bounds.max = query.shape.maxs; }
  trace.policy.kind = "q3"; trace.policy.contents_mask = query.mask; trace.policy.curves = true; trace.policy.player_curve_clip = true;
  trace.numeric = w->numeric;
  qa_actor_id pass;
  trace.pass_actor = qa_app_sim_shared_bot_world_actor_for_id(w, (int64_t)query.pass_entity_num, &pass) ? &pass : NULL;
  qa_trace_result result = qa_shared_trace(w->simulation->scene, trace);
  if (!is(result.kind, "q3")) fail("Bot trace did not use the Q3 decision representation");
  qa_q3_base_server_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = result.fraction; out.end = result.end;
  out.entity_num = result.hit.kind == QA_TRACE_HIT_ACTOR ? (double)qa_app_sim_shared_bot_world_entity_id(w, result.hit.actor)
      : result.hit.kind == QA_TRACE_HIT_WORLD ? 1022 : 1023;
  out.solidity = result.all_solid ? QA_Q3_BASE_SOLID_ALL_SOLID : result.start_solid ? QA_Q3_BASE_SOLID_START_SOLID : QA_Q3_BASE_SOLID_CLEAR;
  out.contact_is_plane = result.contact.has_plane; out.contact_plane = result.contact.plane;
  out.contents = result.contents; out.surface_flags = result.surface_flags;
  return out;
}
static int64_t choose_team(void *ctx, int64_t client) { (void)ctx; (void)client; return 0; } /* TS () => 0 */
static void activate_bot(void *ctx, int64_t client) {
  world_t *w = ctx;
  if (!w->q2) return;
  qa_actor_id actor;
  bool has_actor = w->options.actor(w->options.ctx, client, &actor);
  qa_q2_game_services *game = w->q2_source->services;
  qa_q2_entity *entity = game->entity(game->ctx, has_actor ? &actor : NULL);
  if (entity != NULL) entity->server_flags = (double)(qa_js_to_int32(entity->server_flags) | 16);
}
static void exit_level(void *ctx) {
  world_t *w = ctx;
  if (w->q2) qa_q2_players_end_deathmatch_level(w->q2_source->players, w->q2_source->services);
  else qa_q1_request_intermission_exit(w->q1_source->game, w->simulation->time_seconds(w->simulation->ctx), true);
}
static void reset_podium_players(void *ctx) { (void)ctx; } /* TS () => {} */
static void client_userinfo_changed(void *ctx, int64_t client) {
  world_t *w = ctx;
  qa_actor_id actor;
  if (!w->options.actor(w->options.ctx, client, &actor)) return;
  const char *userinfo = text_get(w->userinfos, w->userinfo_count, client);
  if (userinfo == NULL) userinfo = "";
  if (w->q2) {
    qa_q2_game_services *game = w->q2_source->services;
    qa_q2_entity *entity = game->entity(game->ctx, &actor);
    if (entity != NULL) qa_q2_players_userinfo_changed(w->q2_source->players, entity, game, userinfo);
    return;
  }
  /* fields = userinfo.split("\\"); pairs from index 1 */
  char *copy = xstrdup(userinfo);
  size_t field_count = 1;
  for (const char *p = copy; *p != '\0'; p++) if (*p == '\\') field_count++;
  char **fields = calloc(field_count, sizeof *fields);
  qa_comp_userinfo_entry *info = calloc(field_count, sizeof *info);
  if (fields == NULL || info == NULL) qa_range_error("Out of memory");
  size_t n = 0; fields[n++] = copy;
  for (char *p = copy; *p != '\0'; p++) if (*p == '\\') { *p = '\0'; fields[n++] = p + 1; }
  size_t info_count = 0;
  for (size_t index = 1; index + 1 < n; index += 2) {
    size_t at = info_count;
    for (size_t j = 0; j < info_count; j++) if (strcmp(info[j].key, fields[index]) == 0) at = j; /* Map.set keeps position */
    info[at].key = fields[index]; info[at].value = fields[index + 1];
    if (at == info_count) info_count++;
  }
  if (!qa_comp_q1_source_clients_update(&w->q1_source->composition->clients, actor, info, info_count)) qa_range_error("Out of memory");
  free(info); free(fields); free(copy);
}
static const char *client_connect(void *ctx, int64_t client, bool first_time, bool bot) {
  world_t *w = ctx;
  if (w->q2) {
    const char *userinfo = text_get(w->userinfos, w->userinfo_count, client);
    qa_q2_connection_result result = w->q2_source->rerelease == NULL
        ? qa_q2_players_connect(w->q2_source->players, w->q2_source->services, userinfo != NULL ? userinfo : "")
        : qa_q2_rerelease_players_connect(&w->q2_source->rerelease->players, w->q2_source->services, userinfo != NULL ? userinfo : "", bot);
    if (!result.allowed) { free(w->info); w->info = xstrdup(result.reason); return w->info; }
    text_set(&w->userinfos, &w->userinfo_count, &w->userinfo_capacity, client, result.userinfo);
  }
  client_userinfo_changed(w, client);
  return w->options.connect(w->options.ctx, client, !first_time) ? NULL : "Bot setup failed";
}
static void client_begin(void *ctx, int64_t client) {
  world_t *w = ctx;
  bool known = false;
  for (size_t i = 0; i < w->begun_count; i++) if (w->begun[i] == client) known = true;
  if (!known) { w->begun = grow(w->begun, &w->begun_capacity, w->begun_count, sizeof *w->begun); w->begun[w->begun_count++] = client; }
  w->options.begin(w->options.ctx, client);
}
static double memory_debug(void *ctx) { (void)ctx; return 0; } /* TS () => 0 */
static void memory_print(void *ctx, const char *text) { world_t *w = ctx; w->options.print(w->options.ctx, text); }
static bool arsenal_actor(void *ctx, int64_t client, qa_actor_id *out) { return qa_app_sim_shared_bot_world_actor_for_id(ctx, client, out); }
static void register_cvar(qa_cvar_registry *registry, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(registry, name, value, 0, &snapshot);
  qa_cvar_read_free(&snapshot);
}

/* TS random: { random: () => source.game.host.random(), crandom: () => source.game.host.random() * 2 - 1 } */
static double source_host_random(world_t *w) {
  if (w->q2) {
    qa_q2_foundation_host *host = w->q2_source->services->host;
    return host->random(host->ctx);
  }
  qa_q1_host *host = &w->q1_source->game->host;
  return host->random(host->ctx);
}
static double game_random(void *ctx) { return source_host_random(ctx); }
static double game_crandom(void *ctx) { return source_host_random(ctx) * 2 - 1; }

qa_app_sim_shared_bot_world *qa_app_sim_shared_bot_world_create(const qa_app_sim_shared_bot_world_options *options) {
  const qa_app_sim_bot_simulation *sim = options->simulation;
  qa_q2_product_runtime *q2 = sim->q2_source(sim->ctx);
  const qa_app_sim_bot_q1_source *q1 = q2 == NULL ? sim->q1_source(sim->ctx) : NULL;
  if (q2 == NULL && q1 == NULL) fail("Shared bot observations require an admitted Q1 or Q2 world");
  world_t *w = calloc(1, sizeof *w);
  if (w == NULL) return NULL;
  w->memory = calloc(1, sizeof *w->memory);
  if (w->memory == NULL) { free(w); return NULL; }
  w->options = *options; w->simulation = sim; w->q2 = q2 != NULL; w->q1_source = q1; w->q2_source = q2;
  w->has_arsenal = sim->q2_weapon_source(sim->ctx, &w->arsenal_game, &w->arsenal_weapons);
  if (!qa_app_sim_bot_numeric(sim, &w->numeric)) fail("Bot world has no source numeric policy");
  w->map_name = xstrdup(w->q2 ? q2->services->options.map_name : q1->game->map_name);
  w->next_entity = 64; w->next_generation = 1;
  w->knowledge = qa_app_sim_create_bot_arsenal(sim, arsenal_actor, w);
  if (w->knowledge == NULL) fail("Shared bot arsenal binding is unavailable");
  qa_cvar_registry *cvars = options->cvars;
  if (!options->restoring) {
    char max_clients_text[32], gravity_text[64];
    snprintf(max_clients_text, sizeof max_clients_text, "%s", QA_JS_NUMBER(sim->max_clients));
    snprintf(gravity_text, sizeof gravity_text, "%s", QA_JS_NUMBER(qa_app_sim_shared_physics_gravity(sim->physics)));
    register_cvar(cvars, "sv_maxclients", max_clients_text); register_cvar(cvars, "g_gametype", "0");
    register_cvar(cvars, "mapname", w->map_name); register_cvar(cvars, "sv_mapname", w->map_name);
    register_cvar(cvars, "g_spSkill", "2"); register_cvar(cvars, "bot_enable", "1"); register_cvar(cvars, "bot_minplayers", "0");
    register_cvar(cvars, "dedicated", "1"); register_cvar(cvars, "g_gravity", gravity_text);
    qa_cvar_snapshot snapshot;
    qa_cvar_registry_set(cvars, "mapname", w->map_name, true, &snapshot); qa_cvar_read_free(&snapshot);
    qa_cvar_registry_set(cvars, "sv_mapname", w->map_name, true, &snapshot); qa_cvar_read_free(&snapshot);
  }
  qa_q3_game_memory_init(w->memory, w, memory_debug, memory_print);
  w->configstrings.ctx = w; w->configstrings.get = configstring_get; w->configstrings.set = configstring_set;
  w->pickups.ctx = w; w->pickups.owns_item_goal = NULL; w->pickups.candidates = pickups_candidates; w->pickups.inspect = pickups_inspect;
  w->knowledge_view.ctx = w;
  w->knowledge_view.pickup_utility = k_pickup_utility; w->knowledge_view.choose_weapon = k_choose_weapon;
  w->knowledge_view.activation_weapon = k_activation_weapon; w->knowledge_view.tactics = k_tactics;
  w->knowledge_view.aggression = k_aggression; w->knowledge_view.update_inventory = knowledge_update_inventory;
  w->knowledge_view.weapon_info = k_weapon_info;
  qa_source_bot_game_bots_a *g = &w->game;
  g->ctx = w; g->pickups = &w->pickups;
  g->options.product = QA_Q3_BASE_PRODUCT_BASEQ3; g->options.cvars = cvars; g->options.configstrings = &w->configstrings;
  g->options.engine.ctx = w; g->options.engine.insert_ctx = w;
  g->options.engine.print = engine_print; g->options.engine.get_userinfo = engine_get_userinfo;
  g->options.engine.set_userinfo = engine_set_userinfo; g->options.engine.send_server_command = engine_send_server_command;
  g->options.engine.drop_client = engine_drop_client; g->options.engine.insert_console_command = engine_console;
  g->options.engine.append_console_command = engine_console;
  g->game_type = game_type; g->max_clients = max_clients; g->entity_count = entity_count;
  g->trace = world_trace; g->point_contents = world_point_contents;
  g->random.ctx = w; g->random.random = game_random; g->random.crandom = game_crandom;
  g->clock_time = clock_time; g->clock_start_time = clock_start_time; g->clock_intermission_time = clock_intermission_time;
  g->memory = w->memory; g->knowledge = &w->knowledge_view;
  g->entity = game_entity; g->observable = observable; g->model_index = model_index; g->choose_team = choose_team;
  g->activate_bot = activate_bot; g->exit_level = exit_level; g->reset_podium_players = reset_podium_players;
  g->client_userinfo_changed = client_userinfo_changed; g->client_connect = client_connect; g->client_begin = client_begin;
  if (!options->restoring) qa_app_sim_shared_bot_world_refresh(w);
  return w;
}
void qa_app_sim_shared_bot_world_destroy(world_t *w) {
  if (w == NULL) return;
  qa_app_sim_bot_arsenal_destroy(w->knowledge);
  if (w->has_player_state) qa_q3_base_player_state_record_destroy(&w->player_state);
  for (size_t i = 0; i < w->userinfo_count; i++) free(w->userinfos[i].value);
  for (size_t i = 0; i < w->string_count; i++) free(w->strings[i].value);
  for (size_t i = 0; i < w->model_count; i++) free(w->models[i].name);
  cell_clear(&w->inspected);
  for (size_t i = 0; i < w->candidate_capacity; i++) cell_clear(&w->candidates[i]);
  free(w->candidates); free(w->candidate_view);
  free(w->ids); free(w->userinfos); free(w->strings); free(w->models); free(w->free_ids); free(w->generations); free(w->begun);
  free(w->player_name); free(w->classname); free(w->info); free(w->map_name); free(w->memory); free(w);
}
qa_source_bot_game_bots_a *qa_app_sim_shared_bot_world_game(world_t *w) { return &w->game; }
qa_app_sim_bot_arsenal *qa_app_sim_shared_bot_world_knowledge(world_t *w) { return w->knowledge; }

/* ---- checkpoint ---- */
static qa_save_value *o_new(void) { qa_save_value *v = qa_save_value_object(); if (v == NULL) qa_range_error("Out of memory"); return v; }
static qa_save_value *a_new(void) { qa_save_value *v = qa_save_value_array(); if (v == NULL) qa_range_error("Out of memory"); return v; }
static void o_put(qa_save_value *o, const char *key, qa_save_value *v) { if (v == NULL || !qa_save_value_set(o, key, v)) qa_range_error("Out of memory"); }
static void a_push(qa_save_value *a, qa_save_value *v) { if (v == NULL || !qa_save_value_push(a, v)) qa_range_error("Out of memory"); }
static qa_save_value *n_new(double n) { return qa_save_value_number(n); }
static qa_save_value *id_value_pair(const char *key, int64_t id, const char *value_key, qa_save_value *value) {
  qa_save_value *o = o_new(); o_put(o, key, n_new((double)id)); o_put(o, value_key, value); return o;
}
qa_save_value *qa_app_sim_shared_bot_world_checkpoint(world_t *w) {
  qa_save_value *o = o_new(), *memory = o_new(), *actors = a_new(), *free_ids = a_new(), *generations = a_new(), *begun = a_new(),
      *userinfos = a_new(), *strings = a_new(), *models = a_new();
  o_put(o, "version", n_new(1)); o_put(o, "source", qa_save_value_string(w->q2 ? "q2" : "q1"));
  qa_q3_game_memory_save_state state = qa_q3_game_memory_capture_save_state(w->memory);
  o_put(memory, "pool", qa_save_value_bytes(state.pool, state.pool_length)); o_put(memory, "allocPoint", n_new(state.alloc_point));
  qa_q3_game_memory_save_state_destroy(&state);
  o_put(o, "memory", memory);
  o_put(o, "nextEntity", n_new((double)w->next_entity)); o_put(o, "nextGeneration", n_new((double)w->next_generation));
  for (size_t i = 0; i < w->id_count; i++) {
    qa_saved_actor_id saved = qa_saved_actor_id_of(w->ids[i].actor);
    qa_save_value *entry = o_new(), *actor = o_new();
    o_put(actor, "slot", n_new(saved.slot)); o_put(actor, "generation", n_new(saved.generation));
    o_put(entry, "actor", actor); o_put(entry, "id", n_new((double)w->ids[i].id));
    a_push(actors, entry);
  }
  o_put(o, "actors", actors);
  for (size_t i = 0; i < w->free_count; i++) a_push(free_ids, n_new((double)w->free_ids[i]));
  o_put(o, "freeIds", free_ids);
  for (size_t i = 0; i < w->generation_count; i++) a_push(generations, id_value_pair("id", w->generations[i].id, "generation", n_new((double)w->generations[i].value)));
  o_put(o, "generations", generations);
  for (size_t i = 0; i < w->begun_count; i++) a_push(begun, n_new((double)w->begun[i]));
  o_put(o, "begun", begun);
  for (size_t i = 0; i < w->userinfo_count; i++) a_push(userinfos, id_value_pair("id", w->userinfos[i].id, "value", qa_save_value_string(w->userinfos[i].value)));
  o_put(o, "userinfos", userinfos);
  for (size_t i = 0; i < w->string_count; i++) a_push(strings, id_value_pair("id", w->strings[i].id, "value", qa_save_value_string(w->strings[i].value)));
  o_put(o, "strings", strings);
  for (size_t i = 0; i < w->model_count; i++) {
    qa_save_value *entry = o_new();
    o_put(entry, "name", qa_save_value_string(w->models[i].name)); o_put(entry, "id", n_new((double)w->models[i].id));
    a_push(models, entry);
  }
  o_put(o, "models", models);
  return o;
}
static void rf(const qa_save_reader *r, const char *name, qa_save_reader *out) { if (!qa_save_reader_field(r, name, out)) qa_range_error("Out of memory"); }
static void ri(const qa_save_reader *r, size_t index, qa_save_reader *out) { if (!qa_save_reader_item(r, index, out)) qa_range_error("Out of memory"); }
static int64_t read_entity(const qa_save_reader *entry, int64_t limit) {
  int64_t id = (int64_t)qa_save_reader_integer_min(entry, 64);
  if (id >= limit) qa_save_reader_fail(entry, "observation entity outside allocated range");
  return id;
}
static void read_strings(const qa_save_reader *reader, const char *name, text_entry **out, size_t *out_count) {
  qa_save_reader list, entry, field;
  rf(reader, name, &list);
  size_t count = qa_save_reader_list_count(&list);
  text_entry *result = calloc(count != 0 ? count : 1, sizeof *result);
  if (result == NULL) qa_range_error("Out of memory");
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    ri(&list, i, &entry);
    rf(&entry, "id", &field); int64_t id = (int64_t)qa_save_reader_integer_min(&field, 0); qa_save_reader_destroy(&field);
    for (size_t j = 0; j < n; j++) if (result[j].id == id) qa_save_reader_fail(&entry, "duplicate string index");
    rf(&entry, "value", &field); result[n].id = id; result[n].value = xstrdup(qa_save_reader_string(&field)); n++;
    qa_save_reader_destroy(&field); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out = result; *out_count = n;
}
void qa_app_sim_shared_bot_world_restore_checkpoint(world_t *w, const qa_save_value *value,
    qa_app_sim_saved_actor_ref_fn actor_ref, void *actor_ctx) {
  qa_save_reader reader, field, list, entry, inner;
  if (!qa_save_reader_init(&reader, value, "sharedBotWorld")) qa_range_error("Out of memory");
  rf(&reader, "version", &field); qa_save_reader_literal_number(&field, 1); qa_save_reader_destroy(&field);
  rf(&reader, "source", &field); qa_save_reader_literal_string(&field, w->q2 ? "q2" : "q1"); qa_save_reader_destroy(&field);
  qa_q3_game_memory *restored_memory = calloc(1, sizeof *restored_memory);
  if (restored_memory == NULL) qa_range_error("Out of memory");
  qa_q3_game_memory_init(restored_memory, w, memory_debug, memory_print);
  rf(&reader, "memory", &field); qa_q3_game_memory_restore_save_state(restored_memory, field.value); qa_save_reader_destroy(&field);
  rf(&reader, "nextEntity", &field); int64_t entity_limit = (int64_t)qa_save_reader_integer_min(&field, 64);
  rf(&reader, "nextGeneration", &inner); int64_t generation_limit = (int64_t)qa_save_reader_integer_min(&inner, 1); qa_save_reader_destroy(&inner);
  if (entity_limit > 1022) qa_save_reader_fail(&field, "observation entity capacity exceeded");
  qa_save_reader_destroy(&field);
  /* actors */
  rf(&reader, "actors", &list);
  size_t actor_count = qa_save_reader_list_count(&list);
  id_entry *restored = calloc(actor_count != 0 ? actor_count : 1, sizeof *restored);
  qa_saved_actor_id *seen = calloc(actor_count != 0 ? actor_count : 1, sizeof *seen);
  if (restored == NULL || seen == NULL) qa_range_error("Out of memory");
  size_t restored_count = 0;
  for (size_t i = 0; i < actor_count; i++) {
    ri(&list, i, &entry);
    rf(&entry, "id", &field); int64_t id = read_entity(&field, entity_limit); qa_save_reader_destroy(&field);
    rf(&entry, "actor", &field); qa_saved_actor_id saved = qa_read_saved_actor(&field); qa_save_reader_destroy(&field);
    for (size_t j = 0; j < restored_count; j++)
      if (restored[j].id == id || (seen[j].slot == saved.slot && seen[j].generation == saved.generation)) qa_save_reader_fail(&entry, "duplicate observation entity or actor");
    qa_actor_id resolved = actor_ref(actor_ctx, saved);
    for (size_t j = 0; j < restored_count; j++) if (qa_same_actor(restored[j].actor, resolved)) qa_save_reader_fail(&entry, "duplicate remapped observation actor");
    seen[restored_count] = saved; restored[restored_count].actor = resolved; restored[restored_count].id = id; restored_count++;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list); free(seen);
  /* freeIds */
  rf(&reader, "freeIds", &list);
  size_t free_list_count = qa_save_reader_list_count(&list);
  int64_t *restored_free = calloc(free_list_count != 0 ? free_list_count : 1, sizeof *restored_free);
  if (restored_free == NULL) qa_range_error("Out of memory");
  size_t free_count = 0;
  for (size_t i = 0; i < free_list_count; i++) {
    ri(&list, i, &entry);
    int64_t id = read_entity(&entry, entity_limit);
    bool duplicate = false;
    for (size_t j = 0; j < free_count; j++) if (restored_free[j] == id) duplicate = true;
    for (size_t j = 0; j < restored_count; j++) if (restored[j].id == id) duplicate = true;
    if (duplicate) qa_save_reader_fail(&entry, "duplicate or active free observation entity");
    restored_free[free_count++] = id;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  if ((int64_t)(restored_count + free_count) != entity_limit - 64) qa_save_reader_fail(&reader, "allocated observation entities are missing");
  /* generations */
  rf(&reader, "generations", &list);
  size_t generation_list_count = qa_save_reader_list_count(&list);
  number_entry *restored_generations = calloc(generation_list_count != 0 ? generation_list_count : 1, sizeof *restored_generations);
  if (restored_generations == NULL) qa_range_error("Out of memory");
  size_t generation_count = 0;
  for (size_t i = 0; i < generation_list_count; i++) {
    ri(&list, i, &entry);
    rf(&entry, "id", &field); int64_t id = read_entity(&field, entity_limit); qa_save_reader_destroy(&field);
    rf(&entry, "generation", &field); int64_t generation = (int64_t)qa_save_reader_integer_min(&field, 1); qa_save_reader_destroy(&field);
    bool duplicate = generation >= generation_limit;
    for (size_t j = 0; j < generation_count; j++) if (restored_generations[j].id == id || restored_generations[j].value == generation) duplicate = true;
    if (duplicate) qa_save_reader_fail(&entry, "invalid or duplicate observation generation");
    restored_generations[generation_count].id = id; restored_generations[generation_count].value = generation; generation_count++;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  if ((int64_t)generation_count != entity_limit - 64) qa_save_reader_fail(&reader, "observation generations are missing");
  /* begun */
  rf(&reader, "begun", &list);
  size_t begun_list_count = qa_save_reader_list_count(&list);
  int64_t *restored_begun = calloc(begun_list_count != 0 ? begun_list_count : 1, sizeof *restored_begun);
  if (restored_begun == NULL) qa_range_error("Out of memory");
  size_t begun_count = 0;
  for (size_t i = 0; i < begun_list_count; i++) {
    ri(&list, i, &entry);
    int64_t id = (int64_t)qa_save_reader_integer_min(&entry, 0);
    bool duplicate = (double)id >= w->simulation->max_clients;
    for (size_t j = 0; j < begun_count; j++) if (restored_begun[j] == id) duplicate = true;
    if (duplicate) qa_save_reader_fail(&entry, "invalid or duplicate begun client");
    restored_begun[begun_count++] = id;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  text_entry *restored_userinfos, *restored_strings;
  size_t userinfo_count, string_count;
  read_strings(&reader, "userinfos", &restored_userinfos, &userinfo_count);
  read_strings(&reader, "strings", &restored_strings, &string_count);
  /* models */
  rf(&reader, "models", &list);
  size_t model_list_count = qa_save_reader_list_count(&list);
  model_entry *restored_models = calloc(model_list_count != 0 ? model_list_count : 1, sizeof *restored_models);
  if (restored_models == NULL) qa_range_error("Out of memory");
  size_t model_count = 0;
  for (size_t i = 0; i < model_list_count; i++) {
    ri(&list, i, &entry);
    rf(&entry, "name", &field); const char *name = qa_save_reader_string(&field);
    rf(&entry, "id", &inner); int64_t id = (int64_t)qa_save_reader_integer_min(&inner, (double)w->simulation->world_model_count); qa_save_reader_destroy(&inner);
    bool invalid = name[0] == '\0' || name[0] == '*';
    for (size_t j = 0; j < model_count; j++) if (strcmp(restored_models[j].name, name) == 0 || restored_models[j].id == id) invalid = true;
    if (invalid) qa_save_reader_fail(&entry, "invalid or duplicate model index");
    restored_models[model_count].name = xstrdup(name); restored_models[model_count].id = id; model_count++;
    qa_save_reader_destroy(&field); qa_save_reader_destroy(&entry);
  }
  for (size_t index = 0; index < model_count; index++) {
    bool found = false;
    for (size_t j = 0; j < model_count && !found; j++) found = restored_models[j].id == (int64_t)(w->simulation->world_model_count + index);
    if (!found) qa_save_reader_fail(&list, "model indices are not contiguous");
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&reader);
  /* apply: game.memory.restoreSaveState(restoredMemory.captureSaveState()) */
  memcpy(w->memory->pool, restored_memory->pool, sizeof w->memory->pool);
  w->memory->alloc_point = restored_memory->alloc_point;
  free(restored_memory);
  free(w->ids); w->ids = restored; w->id_count = restored_count; w->id_capacity = actor_count;
  free(w->free_ids); w->free_ids = restored_free; w->free_count = free_count; w->free_capacity = free_list_count;
  free(w->generations); w->generations = restored_generations; w->generation_count = generation_count; w->generation_capacity = generation_list_count;
  free(w->begun); w->begun = restored_begun; w->begun_count = begun_count; w->begun_capacity = begun_list_count;
  for (size_t i = 0; i < w->userinfo_count; i++) free(w->userinfos[i].value);
  free(w->userinfos); w->userinfos = restored_userinfos; w->userinfo_count = userinfo_count; w->userinfo_capacity = userinfo_count;
  for (size_t i = 0; i < w->string_count; i++) free(w->strings[i].value);
  free(w->strings); w->strings = restored_strings; w->string_count = string_count; w->string_capacity = string_count;
  for (size_t i = 0; i < w->model_count; i++) free(w->models[i].name);
  free(w->models); w->models = restored_models; w->model_count = model_count; w->model_capacity = model_list_count;
  w->next_entity = entity_limit; w->next_generation = generation_limit;
}
