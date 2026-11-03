/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/q3/ai_context.h"
#include "qa/core/game_numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qa_save_value *sv(qa_save_value *value) {
  if (value == NULL)
    qa_range_error("Out of memory building bot context checkpoint");
  return value;
}
static void set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!qa_save_value_set(object, key, child))
    qa_range_error("Out of memory building bot context checkpoint");
}
static void push(qa_save_value *array, qa_save_value *child) {
  if (!qa_save_value_push(array, child))
    qa_range_error("Out of memory building bot context checkpoint");
}
static void set_num(qa_save_value *object, const char *key, double value) {
  set(object, key, sv(qa_save_value_number(value)));
}
static qa_save_value *vec_value(qa_vec3 v) {
  qa_save_value *o = sv(qa_save_value_object());
  set_num(o, "x", v.x);
  set_num(o, "y", v.y);
  set_num(o, "z", v.z);
  return o;
}
static void field(const qa_save_reader *r, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(r, name, out))
    qa_range_error("Out of memory reading bot context checkpoint");
}
static void item(const qa_save_reader *r, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(r, index, out))
    qa_range_error("Out of memory reading bot context checkpoint");
}
static double num(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  double v = qa_save_reader_number(&cell);
  qa_save_reader_destroy(&cell);
  return v;
}
static double integer(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  double v = qa_save_reader_integer(&cell);
  qa_save_reader_destroy(&cell);
  return v;
}
static bool boolean(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  bool v = qa_save_reader_boolean(&cell);
  qa_save_reader_destroy(&cell);
  return v;
}
static qa_vec3 vec_field(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  qa_vec3 v = {num(&cell, "x"), num(&cell, "y"), num(&cell, "z")};
  qa_save_reader_destroy(&cell);
  return v;
}
static char *dup_text(const char *text) {
  size_t n = strlen(text) + 1;
  char *copy = malloc(n);
  if (copy == NULL)
    qa_range_error("Out of memory copying bot context text");
  memcpy(copy, text, n);
  return copy;
}

/* ------------------------------ BotCvar ------------------------------ */
void qa_bot_cvar_bots_a_init(qa_bot_cvar_bots_a *c, qa_cvar_registry *registry) {
  memset(c, 0, sizeof *c);
  c->registry = registry;
}
void qa_bot_cvar_register_bots_a(qa_bot_cvar_bots_a *c, const char *name, const char *def,
                                 int32_t flags) {
  if (strlen(name) >= sizeof c->name)
    qa_range_error("Bot cvar name exceeds source storage");
  snprintf(c->name, sizeof c->name, "%s", name);
  c->has_name = true;
  qa_command_dialect dialect = qa_cvar_registry_dialect(c->registry);
  int32_t selected = flags;
  if (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE)
    selected = (flags & (QA_CVAR_FLAG_ARCHIVE | QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_SERVER_INFO)) |
               ((flags & QA_CVAR_FLAG_LATCH) != 0 ? QA_Q2_CVAR_FLAG_LATCH : 0) |
               ((flags & (QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_INIT)) != 0 ? QA_Q2_CVAR_FLAG_NO_SET
                                                                            : 0);
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_register(c->registry, name, def, selected, &snapshot))
    qa_cvar_read_free(&snapshot);
  c->modification_count = -1;
  qa_bot_cvar_update_bots_a(c);
}
void qa_bot_cvar_update_bots_a(qa_bot_cvar_bots_a *c) {
  if (!c->has_name)
    return;
  qa_cvar_read read;
  memset(&read, 0, sizeof read);
  if (!qa_cvar_registry_find(c->registry, c->name, &read))
    return;
  if (read.modification_count != c->modification_count) {
    /* The source cell holds 255 characters; a longer value is cut rather than ending the frame. */
    snprintf(c->value, sizeof c->value, "%.255s", read.value);
    c->numeric_value = read.numeric_value;
    c->integer_value = (int64_t)read.integer_value;
    c->modification_count = read.modification_count;
  }
  qa_cvar_read_free(&read);
}
void qa_bot_cvar_write_integer(qa_bot_cvar_bots_a *c, double value) {
  if (trunc(value) != value || fabs(value) > 9007199254740991.0)
    qa_range_error("Bot cvar integer requires a safe integer");
  double wrapped = fmod(value, 4294967296.0);
  if (wrapped < 0)
    wrapped += 4294967296.0;
  c->integer_value = (int32_t)(uint32_t)wrapped;
}
qa_save_value *qa_bot_cvar_capture_save_state(const qa_bot_cvar_bots_a *c) {
  qa_save_value *o = sv(qa_save_value_object());
  set(o, "name", sv(c->has_name ? qa_save_value_string(c->name) : qa_save_value_null()));
  set(o, "value", sv(qa_save_value_string(c->value)));
  set_num(o, "numericValue", c->numeric_value);
  set_num(o, "integerValue", (double)c->integer_value);
  set_num(o, "modificationCount", c->modification_count);
  return o;
}
void qa_bot_cvar_restore_save_state(qa_bot_cvar_bots_a *c, const qa_save_value *value) {
  qa_save_reader reader, cell;
  if (!qa_save_reader_init(&reader, value, "bot.cvar"))
    qa_range_error("Out of memory reading bot context checkpoint");
  field(&reader, "name", &cell);
  c->has_name = !qa_save_reader_is_null(&cell);
  if (c->has_name) {
    const char *name = qa_save_reader_string(&cell);
    if (strlen(name) >= sizeof c->name)
      qa_save_reader_fail(&reader, "cached cvar name exceeds storage");
    snprintf(c->name, sizeof c->name, "%s", name);
  } else
    c->name[0] = '\0';
  qa_save_reader_destroy(&cell);
  field(&reader, "value", &cell);
  const char *cached = qa_save_reader_string(&cell);
  if (strlen(cached) > 255)
    qa_save_reader_fail(&reader, "cached cvar exceeds source storage");
  snprintf(c->value, sizeof c->value, "%s", cached);
  qa_save_reader_destroy(&cell);
  c->numeric_value = num(&reader, "numericValue");
  c->integer_value = (int64_t)integer(&reader, "integerValue");
  c->modification_count = integer(&reader, "modificationCount");
  qa_save_reader_destroy(&reader);
}

/* --------------------------- GameAiContext --------------------------- */
bool qa_game_ai_context_bots_a_init(qa_game_ai_context_bots_a *c, qa_source_bot_game_bots_a *game,
                                    qa_bot_library_bots_a *library,
                                    const qa_game_ai_host_bots_a *host) {
  memset(c, 0, sizeof *c);
  if (!qa_bot_entity_observations_init(&c->observations, 1024))
    return false;
  c->game = game;
  c->library = library;
  c->host = host;
  qa_bot_state_store_bots_a_init(&c->states, game->options.product);
  return true;
}
static double game_random_random(void *ctx) { return (double)qa_game_random_random(ctx); }
static double game_random_crandom(void *ctx) { return (double)qa_game_random_crandom(ctx); }
qa_source_bot_random_bots_a qa_source_bot_game_random_bots_a(qa_q3_game_random *random) {
  qa_source_bot_random_bots_a out = {random, game_random_random, game_random_crandom};
  return out;
}
void qa_bot_push_node_switch_bots_a(qa_game_ai_context_bots_a *c, char *line) {
  if (line == NULL)
    qa_range_error("Out of memory recording AI node switch");
  if (c->node_switch_count == c->node_switch_capacity) {
    size_t capacity = c->node_switch_capacity != 0 ? c->node_switch_capacity * 2 : 64;
    char **grown = realloc(c->node_switches, capacity * sizeof *grown);
    if (grown == NULL)
      qa_range_error("Out of memory recording AI node switch");
    c->node_switches = grown;
    c->node_switch_capacity = capacity;
  }
  c->node_switches[c->node_switch_count++] = line;
}
void qa_game_ai_context_bots_a_destroy(qa_game_ai_context_bots_a *c) {
  qa_bot_entity_observations_destroy(&c->observations);
  qa_bot_state_store_bots_a_destroy(&c->states);
  for (size_t i = 0; i < c->node_switch_count; i++)
    free(c->node_switches[i]);
  free(c->node_switches);
  c->node_switches = NULL;
  c->node_switch_count = c->node_switch_capacity = 0;
  for (size_t i = 0; i < c->vm_cvar_count; i++) {
    free(c->vm_cvars[i].name);
    free(c->vm_cvars[i].cell);
  }
  free(c->vm_cvars);
  c->vm_cvars = NULL;
  c->vm_cvar_count = c->vm_cvar_capacity = 0;
  for (size_t i = 0; i < c->restored_name_count; i++)
    free(c->restored_names[i]);
  free(c->restored_names);
  c->restored_names = NULL;
  c->restored_name_count = 0;
}
int64_t qa_game_ai_context_game_type(const qa_game_ai_context_bots_a *c) {
  return c->deathmatch.gametype;
}
int64_t qa_game_ai_context_max_clients(const qa_game_ai_context_bots_a *c) {
  return c->deathmatch.maxclients;
}
qa_bot_navigation_bots_a *qa_game_ai_context_navigation(const qa_game_ai_context_bots_a *c) {
  return c->host->navigation;
}
double qa_game_ai_context_random(qa_game_ai_context_bots_a *c) {
  return c->game->random.random(c->game->random.ctx);
}
static qa_bot_cvar_bots_a *add_vm_cvar(qa_game_ai_context_bots_a *c, const char *name) {
  if (c->vm_cvar_count == c->vm_cvar_capacity) {
    size_t capacity = c->vm_cvar_capacity == 0 ? 16 : c->vm_cvar_capacity * 2;
    qa_game_ai_vm_cvar_bots_a *grown = realloc(c->vm_cvars, capacity * sizeof *grown);
    if (grown == NULL)
      qa_range_error("Out of memory adding a bot cvar");
    c->vm_cvars = grown;
    c->vm_cvar_capacity = capacity;
  }
  qa_bot_cvar_bots_a *cell = malloc(sizeof *cell);
  if (cell == NULL)
    qa_range_error("Out of memory adding a bot cvar");
  qa_bot_cvar_bots_a_init(cell, c->game->options.cvars);
  c->vm_cvars[c->vm_cvar_count].name = dup_text(name);
  c->vm_cvars[c->vm_cvar_count].cell = cell;
  c->vm_cvar_count++;
  return cell;
}
qa_bot_cvar_bots_a *qa_game_ai_context_cvar(qa_game_ai_context_bots_a *c, const char *name) {
  for (size_t i = 0; i < c->vm_cvar_count; i++)
    if (strcmp(c->vm_cvars[i].name, name) == 0)
      return c->vm_cvars[i].cell;
  return add_vm_cvar(c, name);
}
void qa_game_ai_context_register_cvar(qa_game_ai_context_bots_a *c, const char *name,
                                      const char *def, int32_t flags) {
  qa_bot_cvar_register_bots_a(qa_game_ai_context_cvar(c, name), name, def, flags);
}
bool qa_game_ai_context_get_client_state(qa_game_ai_context_bots_a *c, int64_t client,
                                         qa_q3_base_player_state *out) {
  qa_bot_observed_entity_bots_a entity = c->game->entity(c->game->ctx, client);
  if (!entity.present || !entity.has_player)
    return false;
  if (!qa_q3_base_player_state_copy(out, entity.player_state))
    qa_range_error("Out of memory copying a client state");
  return true;
}
bool qa_game_ai_context_get_entity_state(qa_game_ai_context_bots_a *c, int64_t entity_num,
                                         qa_q3_base_entity_state *out) {
  qa_bot_observed_entity_bots_a entity = c->game->entity(c->game->ctx, entity_num);
  if (!entity.present || !entity.linked || entity.hidden)
    return false;
  qa_q3_entity_state_record_copy(out, &entity.state);
  return true;
}
qa_bot_snapshot_entity_bots_a qa_game_ai_context_get_snapshot_entity(qa_game_ai_context_bots_a *c,
                                                                     int64_t client,
                                                                     int64_t sequence) {
  qa_bot_snapshot_entity_bots_a out;
  memset(&out, 0, sizeof out);
  int64_t entity_num = c->host->get_snapshot_entity(c->host->ctx, client, sequence);
  if (entity_num == -1) {
    out.next_sequence = -1;
    qa_q3_entity_state_record_init(&out.state, 0);
    return out;
  }
  out.next_sequence = (int32_t)(uint32_t)((uint64_t)sequence + 1u);
  if (!qa_game_ai_context_get_entity_state(c, entity_num, &out.state))
    qa_q3_entity_state_record_init(&out.state, 0);
  return out;
}
void qa_game_ai_context_reset_state(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *state) {
  qa_bot_free_waypoints_bots_a(c, state->checkpoints);
  qa_bot_free_waypoints_bots_a(c, state->patrol_points);
  qa_bot_state_reset_decision_state(state);
  qa_bot_library_bots_a *l = c->library;
  if (state->src->ms != 0)
    qa_bot_move_state_store_reset(&l->move_states, state->src->ms);
  if (state->src->gs != 0)
    qa_bot_goal_reset_goal_state(&l->goals, state->src->gs);
  if (state->src->ws != 0)
    qa_weapon_ai_reset_state(&l->weapons, state->src->ws);
  if (state->src->gs != 0)
    qa_bot_goal_reset_avoid_goals(&l->goals, state->src->gs);
  if (state->src->ms != 0)
    qa_bot_move_state_store_reset_avoid_reach(&l->move_states, state->src->ms);
}
void qa_bot_reset_state_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *state) {
  qa_game_ai_context_reset_state(c, state);
}

/* ------------------------------ capture ------------------------------ */
static int64_t waypoint_id(void *ctx, const qa_bot_waypoint_bots_a *value) {
  qa_game_ai_context_bots_a *c = ctx;
  if (value == NULL)
    return -1;
  ptrdiff_t id = value - c->deathmatch.waypoints;
  if (id < 0 || id >= QA_MAX_WAYPOINTS_BOTS_A)
    qa_range_error("Waypoint pointer escapes AI heap");
  return id;
}
static qa_save_value *id_value(int64_t id) {
  return sv(id < 0 ? qa_save_value_null() : qa_save_value_number((double)id));
}
static qa_save_value *goal_value(qa_bot_goal g) {
  qa_save_value *o = sv(qa_save_value_object());
  set(o, "origin", vec_value(g.origin));
  set_num(o, "area", (double)g.area);
  set(o, "mins", vec_value(g.mins));
  set(o, "maxs", vec_value(g.maxs));
  set_num(o, "entity", (double)g.entity);
  set_num(o, "number", (double)g.number);
  set_num(o, "flags", (double)g.flags);
  set_num(o, "itemInfo", (double)g.item_info);
  return o;
}
static qa_save_value *alternatives_value(const qa_alternative_goal *goals, size_t count) {
  qa_save_value *a = sv(qa_save_value_array());
  for (size_t i = 0; i < count; i++) {
    qa_save_value *o = sv(qa_save_value_object());
    set(o, "origin", vec_value(goals[i].origin));
    set_num(o, "area", (double)goals[i].area);
    set_num(o, "startTravelTime", goals[i].start_travel_time);
    set_num(o, "goalTravelTime", goals[i].goal_travel_time);
    set_num(o, "extraTravelTime", goals[i].extra_travel_time);
    push(a, o);
  }
  return a;
}
static qa_save_value *max_clients_value(const qa_game_ai_max_clients_cell_bots_a *cells,
                                        size_t count) {
  qa_save_value *a = sv(qa_save_value_array());
  for (size_t i = 0; i < count; i++) {
    qa_save_value *o = sv(qa_save_value_object());
    set(o, "name", sv(qa_save_value_string(cells[i].name)));
    set_num(o, "value", (double)cells[i].value);
    push(a, o);
  }
  return a;
}
/* TS BotEntityObservations.captureSaveState; the C observations module has the
 * record array but no checkpoint API, so the field order is reproduced here. */
static qa_save_value *observations_value(const qa_bot_entity_observations *o) {
  qa_save_value *a = sv(qa_save_value_array());
  for (int64_t index = 0; index < o->capacity; index++) {
    qa_aas_entity_info_bots_a v = qa_bot_entity_observations_info(o, index);
    qa_save_value *r = sv(qa_save_value_object());
    set(r, "valid", sv(qa_save_value_boolean(v.valid)));
    set_num(r, "number", (double)v.number);
    set_num(r, "type", v.type);
    set_num(r, "flags", v.flags);
    set(r, "origin", vec_value(v.origin));
    set(r, "angles", vec_value(v.angles));
    set(r, "oldOrigin", vec_value(v.old_origin));
    set(r, "mins", vec_value(v.mins));
    set(r, "maxs", vec_value(v.maxs));
    set(r, "lastVisibleOrigin", vec_value(v.last_visible_origin));
    set_num(r, "lastUpdateTime", v.last_update_time);
    set_num(r, "updateInterval", v.update_interval);
    set_num(r, "groundEntity", v.ground_entity);
    set_num(r, "solid", v.solid);
    set_num(r, "modelIndex", v.model_index);
    set_num(r, "modelIndex2", v.model_index2);
    set_num(r, "frame", v.frame);
    set_num(r, "event", v.event);
    set_num(r, "eventParameter", v.event_parameter);
    set_num(r, "powerups", v.powerups);
    set_num(r, "weapon", v.weapon);
    set_num(r, "legsAnimation", v.legs_animation);
    set_num(r, "torsoAnimation", v.torso_animation);
    set(r, "generation", id_value(v.has_generation ? v.generation : -1));
    push(a, r);
  }
  return a;
}
qa_save_value *qa_game_ai_context_capture_save_state(qa_game_ai_context_bots_a *c) {
  qa_save_value *out = sv(qa_save_value_object());
  set_num(out, "time", c->time);
  set_num(out, "regularUpdateTime", c->regular_update_time);
  set_num(out, "numBots", c->num_bots);
  set(out, "interbreed", sv(qa_save_value_boolean(c->interbreed)));
  set_num(out, "interbreedMatchCount", c->interbreed_match_count);
  set(out, "states",
      qa_bot_state_store_capture_save_state(&c->states, c->game->memory, waypoint_id, c));
  set(out, "observations", observations_value(&c->observations));
  qa_save_value *switches = sv(qa_save_value_array());
  for (size_t i = 0; i < c->node_switch_count; i++)
    push(switches, sv(qa_save_value_string(c->node_switches[i])));
  set(out, "nodeSwitches", switches);
  qa_save_value *cvars = sv(qa_save_value_array());
  for (size_t i = 0; i < c->vm_cvar_count; i++) {
    qa_save_value *o = sv(qa_save_value_object());
    set(o, "name", sv(qa_save_value_string(c->vm_cvars[i].name)));
    set(o, "state", qa_bot_cvar_capture_save_state(c->vm_cvars[i].cell));
    push(cvars, o);
  }
  set(out, "cvars", cvars);
  const qa_game_ai_deathmatch_state_bots_a *dm = &c->deathmatch;
  qa_save_value *d = sv(qa_save_value_object());
  set_num(d, "gametype", (double)dm->gametype);
  set_num(d, "maxclients", (double)dm->maxclients);
  set_num(d, "lastTeleportTime", dm->last_teleport_time);
  set_num(d, "maxBspModelIndex", (double)dm->max_bsp_model_index);
  set(d, "alternateRoutesSetup", sv(qa_save_value_boolean(dm->alternate_routes_setup)));
  set(d, "ctfRedFlag", goal_value(dm->ctf_red_flag));
  set(d, "ctfBlueFlag", goal_value(dm->ctf_blue_flag));
  set(d, "ctfNeutralFlag", goal_value(dm->ctf_neutral_flag));
  set(d, "redObelisk", goal_value(dm->red_obelisk));
  set(d, "blueObelisk", goal_value(dm->blue_obelisk));
  set(d, "neutralObelisk", goal_value(dm->neutral_obelisk));
  set(d, "lastTeleportOrigin", vec_value(dm->last_teleport_origin));
  set(d, "redAlternateGoals", alternatives_value(dm->red_alternate_goals, dm->red_alternate_goal_count));
  set(d, "blueAlternateGoals",
      alternatives_value(dm->blue_alternate_goals, dm->blue_alternate_goal_count));
  qa_save_value *waypoints = sv(qa_save_value_array());
  for (int i = 0; i < QA_MAX_WAYPOINTS_BOTS_A; i++) {
    const qa_bot_waypoint_bots_a *w = &dm->waypoints[i];
    qa_save_value *o = sv(qa_save_value_object());
    set(o, "inuse", sv(qa_save_value_boolean(w->inuse)));
    set(o, "name", sv(qa_save_value_string(w->name)));
    set(o, "goal", goal_value(qa_bot_goal_state_get(&w->goal)));
    set(o, "next", id_value(waypoint_id(c, w->next)));
    set(o, "prev", id_value(waypoint_id(c, w->prev)));
    push(waypoints, o);
  }
  set(d, "waypoints", waypoints);
  set(d, "freeWaypoints", id_value(waypoint_id(c, dm->free_waypoints)));
  set(out, "deathmatch", d);
  const qa_game_ai_team_state_bots_a *team = &c->team;
  qa_save_value *t = sv(qa_save_value_object());
  set_num(t, "numTeamMatesMaxClients", (double)team->num_team_mates_max_clients);
  set_num(t, "sortTeamMatesMaxClients", (double)team->sort_team_mates_max_clients);
  set_num(t, "teamOrdersMaxClients", (double)team->team_orders_max_clients);
  set_num(t, "clientFromNameMaxClients", (double)team->client_from_name_max_clients);
  set_num(t, "clientOnSameTeamFromNameMaxClients",
          (double)team->client_on_same_team_from_name_max_clients);
  qa_save_value *preferences = sv(qa_save_value_array());
  for (int i = 0; i < 64; i++) {
    qa_save_value *o = sv(qa_save_value_object());
    set(o, "name", sv(qa_save_value_string(team->task_preferences[i].name)));
    set_num(o, "preference", (double)team->task_preferences[i].preference);
    push(preferences, o);
  }
  set(t, "taskPreferences", preferences);
  set(out, "team", t);
  set(out, "chat", max_clients_value(c->chat.max_clients, c->chat.max_client_count));
  qa_save_value *command = sv(qa_save_value_object()), *leaders = sv(qa_save_value_array());
  for (int i = 0; i < 64; i++)
    push(leaders, sv(qa_save_value_boolean(c->command.not_leader[i])));
  set(command, "notLeader", leaders);
  set(command, "maxClients", max_clients_value(c->command.max_clients, c->command.max_client_count));
  set(out, "command", command);
  return out;
}

/* ------------------------------ restore ------------------------------ */
static qa_bot_goal goal_field(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  qa_bot_goal g;
  memset(&g, 0, sizeof g);
  g.origin = vec_field(&cell, "origin");
  g.area = (int64_t)integer(&cell, "area");
  g.mins = vec_field(&cell, "mins");
  g.maxs = vec_field(&cell, "maxs");
  g.entity = (int64_t)integer(&cell, "entity");
  g.number = (int64_t)integer(&cell, "number");
  g.flags = (int64_t)integer(&cell, "flags");
  g.item_info = (int64_t)integer(&cell, "itemInfo");
  qa_save_reader_destroy(&cell);
  return g;
}
static qa_bot_waypoint_bots_a *waypoint_at(void *ctx, int64_t id) {
  qa_game_ai_context_bots_a *c = ctx;
  if (id < 0)
    return NULL;
  if (id >= QA_MAX_WAYPOINTS_BOTS_A)
    qa_range_error("bot.context: waypoint outside heap");
  return &c->deathmatch.waypoints[id];
}
static int64_t nullable_id(const qa_save_reader *r, const char *name) {
  qa_save_reader cell;
  field(r, name, &cell);
  int64_t v = qa_save_reader_is_null(&cell) ? -1 : (int64_t)qa_save_reader_integer_min(&cell, 0);
  qa_save_reader_destroy(&cell);
  return v;
}
static size_t alternatives_field(const qa_save_reader *r, const char *name, qa_alternative_goal *out) {
  qa_save_reader list;
  field(r, name, &list);
  size_t count = qa_save_reader_list_count(&list);
  if (count > QA_MAX_ALT_ROUTE_GOALS_BOTS_A)
    qa_save_reader_fail(&list, "alternate goal list exceeds its source query");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell;
    item(&list, i, &cell);
    out[i].origin = vec_field(&cell, "origin");
    out[i].area = (int64_t)integer(&cell, "area");
    out[i].start_travel_time = num(&cell, "startTravelTime");
    out[i].goal_travel_time = num(&cell, "goalTravelTime");
    out[i].extra_travel_time = num(&cell, "extraTravelTime");
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  return count;
}
/* The C cells borrow their names; restored names live with the context. */
static const char *intern_name(qa_game_ai_context_bots_a *c, const char *name) {
  for (size_t i = 0; i < c->restored_name_count; i++)
    if (strcmp(c->restored_names[i], name) == 0)
      return c->restored_names[i];
  char **grown = realloc(c->restored_names, (c->restored_name_count + 1) * sizeof *grown);
  if (grown == NULL)
    qa_range_error("Out of memory reading bot context checkpoint");
  c->restored_names = grown;
  c->restored_names[c->restored_name_count] = dup_text(name);
  return c->restored_names[c->restored_name_count++];
}
static size_t max_clients_field(qa_game_ai_context_bots_a *c, const qa_save_reader *list,
                                qa_game_ai_max_clients_cell_bots_a *cells, size_t capacity) {
  size_t count = qa_save_reader_list_count(list);
  if (count > capacity)
    qa_save_reader_fail(list, "maxclients cache exceeds its source cells");
  qa_game_ai_max_clients_cell_bots_a next[8];
  if (capacity > 8)
    qa_range_error("maxclients cache capacity");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell, leaf;
    item(list, i, &cell);
    field(&cell, "name", &leaf);
    const char *name = qa_save_reader_string(&leaf);
    for (size_t j = 0; j < i; j++)
      if (strcmp(next[j].name, name) == 0)
        qa_save_reader_fail(list, "duplicate cached cell");
    next[i].name = intern_name(c, name);
    qa_save_reader_destroy(&leaf);
    next[i].value = (int64_t)integer(&cell, "value");
    qa_save_reader_destroy(&cell);
  }
  memcpy(cells, next, count * sizeof *next);
  return count;
}
static void observations_restore(qa_bot_entity_observations *o, const qa_save_value *value,
                                 qa_bot_remap_observation_fn remap, void *remap_ctx) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "bot.observations"))
    qa_range_error("Out of memory reading bot context checkpoint");
  size_t count = qa_save_reader_list_count(&reader);
  if ((int64_t)count != o->capacity)
    qa_save_reader_fail(&reader, "incorrect observation extent");
  qa_aas_entity_info_bots_a *next = calloc(count == 0 ? 1 : count, sizeof *next);
  bool *slots = calloc(count == 0 ? 1 : count, sizeof *slots);
  if (next == NULL || slots == NULL)
    qa_range_error("Out of memory reading bot context checkpoint");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell, leaf;
    item(&reader, i, &cell);
    qa_aas_entity_info_bots_a *r = &next[i];
    r->number = (int64_t)qa_save_reader_integer_min((field(&cell, "number", &leaf), &leaf), 0);
    qa_save_reader_destroy(&leaf);
    int64_t generation = nullable_id(&cell, "generation");
    if (generation >= 0) {
      remap(remap_ctx, r->number, generation, &r->number, &r->generation);
      r->has_generation = true;
    }
    r->valid = boolean(&cell, "valid");
    r->type = num(&cell, "type");
    r->flags = num(&cell, "flags");
    r->ground_entity = num(&cell, "groundEntity");
    r->solid = num(&cell, "solid");
    r->model_index = num(&cell, "modelIndex");
    r->model_index2 = num(&cell, "modelIndex2");
    r->frame = num(&cell, "frame");
    r->event = num(&cell, "event");
    r->event_parameter = num(&cell, "eventParameter");
    r->powerups = num(&cell, "powerups");
    r->weapon = num(&cell, "weapon");
    r->legs_animation = num(&cell, "legsAnimation");
    r->torso_animation = num(&cell, "torsoAnimation");
    r->last_update_time = (float)num(&cell, "lastUpdateTime");
    r->update_interval = (float)num(&cell, "updateInterval");
    r->origin = vec_field(&cell, "origin");
    r->angles = vec_field(&cell, "angles");
    r->old_origin = vec_field(&cell, "oldOrigin");
    r->mins = vec_field(&cell, "mins");
    r->maxs = vec_field(&cell, "maxs");
    r->last_visible_origin = vec_field(&cell, "lastVisibleOrigin");
    qa_save_reader_destroy(&cell);
    if (r->number < 0 || r->number >= o->capacity || slots[r->number])
      qa_save_reader_fail(&reader, "invalid remapped observation slot");
    slots[r->number] = true;
  }
  for (size_t i = 0; i < count; i++)
    o->records[next[i].number] = next[i];
  free(next);
  free(slots);
  qa_save_reader_destroy(&reader);
}
void qa_game_ai_context_restore_save_state(qa_game_ai_context_bots_a *c, const qa_save_value *value,
                                           qa_bot_remap_observation_fn remap, void *remap_ctx) {
  qa_save_reader reader, dm, team, list, cell, leaf;
  if (!qa_save_reader_init(&reader, value, "bot.context"))
    qa_range_error("Out of memory reading bot context checkpoint");
  field(&reader, "deathmatch", &dm);
  field(&reader, "team", &team);
  qa_game_ai_deathmatch_state_bots_a *d = &c->deathmatch;
  field(&dm, "waypoints", &list);
  size_t count = qa_save_reader_list_count(&list);
  if (count != QA_MAX_WAYPOINTS_BOTS_A)
    qa_save_reader_fail(&dm, "incorrect waypoint heap extent");
  for (size_t i = 0; i < count; i++) {
    item(&list, i, &cell);
    qa_bot_waypoint_bots_a *w = &d->waypoints[i];
    w->inuse = boolean(&cell, "inuse");
    field(&cell, "name", &leaf);
    const char *name = qa_save_reader_string(&leaf);
    if (strlen(name) >= sizeof w->name)
      qa_save_reader_fail(&cell, "waypoint name exceeds storage");
    snprintf(w->name, sizeof w->name, "%s", name);
    qa_save_reader_destroy(&leaf);
    qa_bot_goal_state_copy_from(&w->goal, goal_field(&cell, "goal"));
    w->next = waypoint_at(c, nullable_id(&cell, "next"));
    w->prev = waypoint_at(c, nullable_id(&cell, "prev"));
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  d->free_waypoints = waypoint_at(c, nullable_id(&dm, "freeWaypoints"));
  c->time = num(&reader, "time");
  c->regular_update_time = num(&reader, "regularUpdateTime");
  c->num_bots = num(&reader, "numBots");
  c->interbreed = boolean(&reader, "interbreed");
  c->interbreed_match_count = num(&reader, "interbreedMatchCount");
  d->gametype = (int64_t)num(&dm, "gametype");
  d->maxclients = (int64_t)num(&dm, "maxclients");
  d->last_teleport_time = num(&dm, "lastTeleportTime");
  d->max_bsp_model_index = (int64_t)num(&dm, "maxBspModelIndex");
  d->alternate_routes_setup = boolean(&dm, "alternateRoutesSetup");
  d->ctf_red_flag = goal_field(&dm, "ctfRedFlag");
  d->ctf_blue_flag = goal_field(&dm, "ctfBlueFlag");
  d->ctf_neutral_flag = goal_field(&dm, "ctfNeutralFlag");
  d->red_obelisk = goal_field(&dm, "redObelisk");
  d->blue_obelisk = goal_field(&dm, "blueObelisk");
  d->neutral_obelisk = goal_field(&dm, "neutralObelisk");
  d->last_teleport_origin = vec_field(&dm, "lastTeleportOrigin");
  d->red_alternate_goal_count = alternatives_field(&dm, "redAlternateGoals", d->red_alternate_goals);
  d->blue_alternate_goal_count =
      alternatives_field(&dm, "blueAlternateGoals", d->blue_alternate_goals);
  qa_game_ai_team_state_bots_a *t = &c->team;
  t->num_team_mates_max_clients = (int64_t)integer(&team, "numTeamMatesMaxClients");
  t->sort_team_mates_max_clients = (int64_t)integer(&team, "sortTeamMatesMaxClients");
  t->team_orders_max_clients = (int64_t)integer(&team, "teamOrdersMaxClients");
  t->client_from_name_max_clients = (int64_t)integer(&team, "clientFromNameMaxClients");
  t->client_on_same_team_from_name_max_clients =
      (int64_t)integer(&team, "clientOnSameTeamFromNameMaxClients");
  field(&team, "taskPreferences", &list);
  if (qa_save_reader_list_count(&list) != 64)
    qa_save_reader_fail(&team, "incorrect team preference extent");
  for (size_t i = 0; i < 64; i++) {
    item(&list, i, &cell);
    field(&cell, "name", &leaf);
    const char *name = qa_save_reader_string(&leaf);
    if (strlen(name) >= sizeof t->task_preferences[i].name)
      qa_save_reader_fail(&cell, "team preference name exceeds storage");
    snprintf(t->task_preferences[i].name, sizeof t->task_preferences[i].name, "%s", name);
    qa_save_reader_destroy(&leaf);
    t->task_preferences[i].preference = (int64_t)integer(&cell, "preference");
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  field(&reader, "chat", &list);
  c->chat.max_client_count = max_clients_field(
      c, &list, c->chat.max_clients, sizeof c->chat.max_clients / sizeof c->chat.max_clients[0]);
  qa_save_reader_destroy(&list);
  qa_save_reader command;
  field(&reader, "command", &command);
  field(&command, "maxClients", &list);
  c->command.max_client_count = max_clients_field(
      c, &list, c->command.max_clients,
      sizeof c->command.max_clients / sizeof c->command.max_clients[0]);
  qa_save_reader_destroy(&list);
  field(&command, "notLeader", &list);
  if (qa_save_reader_list_count(&list) != 64)
    qa_save_reader_fail(&reader, "incorrect leader cell extent");
  for (size_t i = 0; i < 64; i++) {
    item(&list, i, &cell);
    c->command.not_leader[i] = qa_save_reader_boolean(&cell);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&command);
  for (size_t i = 0; i < c->node_switch_count; i++)
    free(c->node_switches[i]);
  c->node_switch_count = 0;
  field(&reader, "nodeSwitches", &list);
  count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    item(&list, i, &cell);
    qa_bot_push_node_switch_bots_a(c, dup_text(qa_save_reader_string(&cell)));
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  for (size_t i = 0; i < c->vm_cvar_count; i++) {
    free(c->vm_cvars[i].name);
    free(c->vm_cvars[i].cell);
  }
  c->vm_cvar_count = 0;
  field(&reader, "cvars", &list);
  count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    item(&list, i, &cell);
    field(&cell, "name", &leaf);
    const char *name = qa_save_reader_string(&leaf);
    for (size_t j = 0; j < c->vm_cvar_count; j++)
      if (strcmp(c->vm_cvars[j].name, name) == 0)
        qa_save_reader_fail(&reader, "duplicate bot cvar");
    qa_bot_cvar_bots_a *entry = add_vm_cvar(c, name);
    qa_save_reader_destroy(&leaf);
    field(&cell, "state", &leaf);
    qa_bot_cvar_restore_save_state(entry, leaf.value);
    qa_save_reader_destroy(&leaf);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&list);
  field(&reader, "observations", &cell);
  observations_restore(&c->observations, cell.value, remap, remap_ctx);
  qa_save_reader_destroy(&cell);
  field(&reader, "states", &cell);
  qa_bot_state_store_restore_save_state(&c->states, cell.value, c->game->memory, waypoint_at, c,
                                        remap, remap_ctx);
  qa_save_reader_destroy(&cell);
  qa_save_reader_destroy(&team);
  qa_save_reader_destroy(&dm);
  qa_save_reader_destroy(&reader);
}
