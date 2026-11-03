/* Port of src/app/bootstrap/simulation/network-q2-guest.ts. API 3 guest projection
 * through the shared native Q2 server channel. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q2_guest.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/core/commands/text.h"
#include "qa/core/md4.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/common/session.h"
#include "qa/network/q2/adapters.h"
#include "qa/network/q2/connectionless.h"
#include "qa/network/q2/handshake.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (!out) qa_range_error("Out of memory");
  memcpy(out, text, length + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* The discovery value the Q2 server network reads from host.discovery. */
typedef struct guest_discovery {
  void *ctx;
  qa_q2_status (*status)(void *ctx);
  qa_q2_host_info (*info)(void *ctx);
} guest_discovery;
typedef struct per_player {
  bool used; qa_app_q2_player player;
  qa_q2_server_data_result data; qa_app_q2_config_snapshot configs; qa_q2_baseline_list baselines; bool has_state;
  qa_q2_wire_frame frame; bool has_frame;
  qa_app_q2_raw_message *messages; size_t message_count;
} per_player;
typedef struct owned_message { qa_app_sim_q2_guest_message message; uint8_t *bytes; } owned_message;
typedef struct guest_host {
  qa_app_sim_q2_server_binding_options options;
  const qa_app_sim_q2_guest_world *world;
  char *map;
  qa_app_q2_downloads *downloads;
  qa_q2_server_message_options message_options;
  guest_discovery discovery;
  qa_wire_admission support;
  per_player *players; size_t player_count;
  owned_message *messages; size_t message_count;          /* messages */
  owned_message *local; size_t local_count;               /* localMessages */
  owned_message *progressed; size_t progressed_count;     /* progressedMessages */
  qa_client_id *local_recipients; size_t local_recipient_count;
  qa_q2_status_player *status_players; size_t status_player_count; char **status_names;
  char *server_info;
} guest_host;

static per_player *player_slot(guest_host *h, double slot) {
  if (slot < 0) qa_range_error("Invalid Q2 client slot");
  if ((size_t)slot >= h->player_count) {
    size_t count = (size_t)slot + 1;
    per_player *grown = realloc(h->players, count * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    memset(grown + h->player_count, 0, (count - h->player_count) * sizeof *grown);
    h->players = grown; h->player_count = count;
  }
  return &h->players[(size_t)slot];
}
static void clear_state(per_player *entry) {
  if (!entry->has_state) return;
  for (size_t i = 0; i < entry->configs.count; i++) free(entry->configs.entries[i].value);
  free(entry->configs.entries);
  qa_q2_baseline_list_free(&entry->baselines);
  memset(&entry->configs, 0, sizeof entry->configs);
  entry->has_state = false;
}
static void clear_frame(per_player *entry) {
  if (entry->has_frame) qa_q2_wire_frame_free(&entry->frame);
  entry->has_frame = false;
}
static void clear_player(per_player *entry) {
  clear_state(entry); clear_frame(entry);
  free(entry->messages); entry->messages = NULL; entry->message_count = 0;
}
static void free_messages(owned_message *messages, size_t count) {
  for (size_t i = 0; i < count; i++) free(messages[i].bytes);
  free(messages);
}
/* requirePlayer(player) */
static void require_player(guest_host *h, qa_app_q2_player player) {
  per_player *current = player.client.slot >= 0 && (size_t)player.client.slot < h->player_count ? &h->players[player.client.slot] : NULL;
  if (current == NULL || !current->used || !qa_same_client(current->player.client, player.client)
      || !qa_same_actor(current->player.actor, player.actor))
    qa_raise("Error", 0, 0, "Q2 guest network player is retired");
}
static qa_q2_player_state native_state(guest_host *h, qa_app_q2_player player) {
  require_player(h, player);
  qa_q2_player_state state;
  memset(&state, 0, sizeof state);
  h->world->player_state(h->world->ctx, player.source_entity, &state, NULL);
  return state;
}
static qa_vec3 view_origin(guest_host *h, qa_app_q2_player player) {
  qa_q2_player_state state = native_state(h, player);
  const qa_numeric_operations *n = &h->world->numeric;
  qa_vec3 origin;
  origin.x = n->add(n->ctx, n->divide(n->ctx, state.movement.origin_eighths.values[0], 8), state.view.view_offset.x);
  origin.y = n->add(n->ctx, n->divide(n->ctx, state.movement.origin_eighths.values[1], 8), state.view.view_offset.y);
  origin.z = n->add(n->ctx, n->divide(n->ctx, state.movement.origin_eighths.values[2], 8), state.view.view_offset.z);
  return origin;
}
static const qa_bsp_node *world_nodes(const qa_decoded_world *world, size_t *out_count) {
  switch (world->kind) {
    case QA_DECODED_WORLD_Q1: *out_count = world->as.q1->node_count; return world->as.q1->nodes;
    case QA_DECODED_WORLD_Q2: *out_count = world->as.q2->node_count; return world->as.q2->nodes;
    case QA_DECODED_WORLD_Q3: *out_count = world->as.q3->node_count; return world->as.q3->nodes;
  }
  *out_count = 0;
  return NULL;
}
/* headnodeVisible(headnode, clusters) */
static bool headnode_visible(guest_host *h, double headnode, const double *clusters, size_t cluster_count) {
  qa_shared_scene_queries *scene = h->options.simulation->scene;
  size_t node_count = 0;
  const qa_bsp_node *nodes = world_nodes(&h->options.content->world, &node_count);
  qa_bsp_child *pending = xcalloc(1, sizeof *pending);
  size_t count = 1, capacity = 1;
  pending[0].is_node = !(headnode < 0);
  pending[0].index = headnode < 0 ? -1 - headnode : headnode;
  bool visible = false;
  while (count != 0 && !visible) {
    qa_bsp_child child = pending[--count];
    if (!child.is_node) {
      double target = qa_shared_leaf_cluster(scene, child.index);
      for (size_t i = 0; i < cluster_count && !visible; i++)
        if (qa_shared_cluster_visible(scene, clusters[i], target, "pvs")) visible = true;
      continue;
    }
    if (child.index < 0 || child.index != trunc(child.index) || child.index >= (double)node_count) {
      free(pending);
      qa_raise("Error", 0, 0, "Guest visibility headnode is outside the shared BSP");
    }
    const qa_bsp_node *node = &nodes[(size_t)child.index];
    if (count + 2 > capacity) {
      capacity = (count + 2) * 2;
      qa_bsp_child *grown = realloc(pending, capacity * sizeof *grown);
      if (!grown) { free(pending); qa_range_error("Out of memory"); }
      pending = grown;
    }
    pending[count++] = node->children[0];
    pending[count++] = node->children[1];
  }
  free(pending);
  return visible;
}
/* `[...new Set(leaves.map(leafCluster))]` */
static double *unique_clusters(qa_shared_scene_queries *scene, qa_leaf_query_result result, size_t *out_count) {
  size_t leaf_count = result.leaves.count;
  double *leaves = xcalloc(leaf_count, sizeof *leaves);
  memcpy(leaves, result.leaves.values, leaf_count * sizeof *leaves);
  double *clusters = xcalloc(leaf_count, sizeof *clusters);
  size_t count = 0;
  for (size_t i = 0; i < leaf_count; i++) {
    double cluster = qa_shared_leaf_cluster(scene, leaves[i]);
    bool seen = false;
    for (size_t j = 0; j < count && !seen; j++) seen = clusters[j] == cluster || (isnan(clusters[j]) && isnan(cluster));
    if (!seen) clusters[count++] = cluster;
  }
  free(leaves);
  *out_count = count;
  return clusters;
}

typedef struct command_scope { guest_host *h; qa_app_q2_player player; const char *text; } command_scope;
static void command_body(void *ctx) {
  command_scope *c = ctx;
  require_player(c->h, c->player);
  qa_command_tokens tokens;
  if (!qa_tokenize_command(c->text, QA_COMMAND_DIALECT_Q2_CLASSIC, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_command_tokens_destroy, &tokens);
  if (tokens.argc != 0)
    c->h->world->command(c->h->world->ctx, c->player.source_entity, (const char *const *)tokens.argv, tokens.argc, tokens.args_text);
  qa_cleanup_pop(&guard);
  qa_command_tokens_destroy(&tokens);
}
static void command_text(guest_host *h, qa_app_q2_player player, const char *text) {
  command_scope scope = {h, player, text};
  qa_app_q2_game_callback(command_body, &scope);
}
/* playerMessages(player, incoming) */
static void player_messages(guest_host *h, qa_app_q2_player player, const owned_message *incoming, size_t count) {
  require_player(h, player);
  per_player *entry = player_slot(h, (double)player.client.slot);
  free(entry->messages);
  entry->messages = count ? xcalloc(count, sizeof *entry->messages) : NULL;
  entry->message_count = 0;
  qa_shared_scene_queries *scene = h->options.simulation->scene;
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_q2_guest_message *message = &incoming[i].message;
    bool keep;
    if (message->unicast) keep = message->slot == player.source_entity;
    else if (is(message->scope, "all")) keep = true;
    else {
      qa_q2_entity_state entity = h->world->entity_state(h->world->ctx, player.source_entity);
      double from = qa_shared_point_leaf(scene, message->origin), to = qa_shared_point_leaf(scene, entity.origin);
      keep = qa_shared_areas_connected(scene, qa_shared_leaf_area(scene, from), qa_shared_leaf_area(scene, to))
          && qa_shared_cluster_visible(scene, qa_shared_leaf_cluster(scene, from), qa_shared_leaf_cluster(scene, to), message->scope);
    }
    if (keep) {
      qa_app_q2_raw_message out = {message->bytes, message->length, message->reliable};
      entry->messages[entry->message_count++] = out;
    }
  }
}

static bool host_rejects(void *ctx, const void *address) {
  guest_host *h = ctx;
  return h->options.rejects(h->options.rejects_ctx, address);
}
static const void *const *host_masters(void *ctx, size_t *out_count) {
  guest_host *h = ctx;
  *out_count = h->options.master_count;
  return h->options.masters;
}
static const void *host_supports_source_wire(void *ctx) {
  guest_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->options.simulation;
  qa_wire_admission_destroy(&h->support);
  const char *reasons[3]; size_t count = 0;
  if (h->options.content->world.kind != QA_DECODED_WORLD_Q2)
    reasons[count++] = "Original Quake II peers require Quake II BSP geometry; use local or unified presentation for foreign maps";
  if (strncmp(s->recipe->movement.provider, "q2:", 3) != 0) reasons[count++] = "Guest native wire requires Q2 movement";
  if (strncmp(s->recipe->character.definition.provider, "q2:", 3) != 0) reasons[count++] = "Guest native wire requires Q2 character state";
  h->support.supported = count == 0;
  h->support.reasons = count ? xcalloc(count, sizeof *h->support.reasons) : NULL;
  for (size_t i = 0; i < count; i++) h->support.reasons[i] = xstrdup(reasons[i]);
  h->support.reason_count = count;
  return &h->support;
}
static qa_q2_status discovery_status(void *ctx) {
  guest_host *h = ctx;
  const qa_app_sim_q2_guest_world *w = h->world;
  for (size_t i = 0; i < h->status_player_count; i++) free(h->status_names[i]);
  free(h->status_names); free(h->status_players); free(h->server_info);
  h->server_info = qa_cvar_registry_info_string(w->cvars, QA_Q2_CVAR_FLAG_SERVER_INFO, -1);
  size_t count = 0;
  const qa_app_sim_q2_guest_client *clients = w->clients(w->ctx, &count);
  h->status_players = count ? xcalloc(count, sizeof *h->status_players) : NULL;
  h->status_names = count ? xcalloc(count, sizeof *h->status_names) : NULL;
  h->status_player_count = count;
  for (size_t i = 0; i < count; i++) {
    qa_q2_player_state state;
    memset(&state, 0, sizeof state);
    w->player_state(w->ctx, clients[i].slot, &state, NULL);
    double score = state.view.stats.count > 14 ? state.view.stats.values[14] : 0;
    double ping = w->player_ping(w->ctx, clients[i].slot);
    qa_app_sim_info info; qa_app_sim_info_parse_q2(&info, clients[i].userinfo);
    const char *name = qa_app_sim_info_get(&info, "name");
    h->status_names[i] = xstrdup(name ? name : "");
    qa_app_sim_info_clear(&info);
    h->status_players[i].score = score; h->status_players[i].ping = ping; h->status_players[i].name = h->status_names[i];
  }
  qa_q2_status status = {h->server_info, h->status_players, count};
  return status;
}
static qa_q2_host_info discovery_info(void *ctx) {
  guest_host *h = ctx;
  const qa_app_sim_q2_guest_world *w = h->world;
  static _Thread_local char *name;
  free(name);
  name = qa_cvar_registry_variable_string(w->cvars, "hostname");
  size_t count = 0;
  w->clients(w->ctx, &count);
  qa_q2_host_info info = {name, h->map, (double)count, w->max_clients};
  return info;
}

typedef struct admit_scope {
  guest_host *h; const qa_net_address *from; const qa_q2_connect_request *request;
  qa_app_q2_admission result;
} admit_scope;
typedef struct connect_scope {
  guest_host *h; double slot; qa_client_id client; const char *userinfo;
  bool connected; qa_app_q2_admission result;
} connect_scope;
static void connect_body(void *ctx) {
  connect_scope *c = ctx;
  guest_host *h = c->h;
  const qa_app_sim_q2_guest_world *w = h->world;
  qa_app_sim_q2_guest_connection result = w->connect(w->ctx, c->slot + 1, c->userinfo, "", false);
  if (!result.allowed) {
    qa_engine_session_close_client(h->options.session, c->client);
    qa_app_sim_info info; qa_app_sim_info_parse_q2(&info, result.userinfo);
    const char *reason = qa_app_sim_info_get(&info, "rejmsg");
    static _Thread_local char text[512];
    snprintf(text, sizeof text, "%s", reason ? reason : "Connection refused");
    qa_app_sim_info_clear(&info);
    c->result.accepted = false; c->result.reason = text;
    return;
  }
  c->connected = true;
  const qa_app_sim_network_simulation *s = h->options.simulation;
  qa_owned_actor actor = s->register_q2_native_client(s->ctx, c->client);
  qa_app_q2_player player = {c->client, actor.id, c->slot + 1};
  per_player *entry = player_slot(h, c->slot);
  entry->used = true; entry->player = player;
  c->result.accepted = true; c->result.player = player;
}
static void disconnect_slot_body(void *ctx) { connect_scope *c = ctx; c->h->world->disconnect(c->h->world->ctx, c->slot + 1); }
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  guest_host *h = a->h;
  const qa_app_sim_q2_guest_world *w = h->world;
  size_t client_count = 0;
  const qa_app_sim_q2_guest_client *clients = w->clients(w->ctx, &client_count);
  double slot = 0;
  for (;;) {
    bool occupied = false;
    for (size_t i = 0; i < client_count && !occupied; i++) occupied = clients[i].slot - 1 == slot;
    if (!(slot < w->max_clients && occupied)) break;
    slot++;
  }
  if (slot == w->max_clients) { a->result.accepted = false; a->result.reason = "Server is full"; return; }
  qa_session_client *client = qa_engine_session_create_client(h->options.session, slot);
  qa_session_client_connect(client, "remote");
  qa_app_sim_info info; qa_app_sim_info_parse_q2(&info, a->request->userinfo);
  char *ip = qa_app_sim_address_key(a->from);
  qa_app_sim_info_set(&info, "ip", ip);
  free(ip);
  char *userinfo = qa_app_sim_info_text(&info);
  qa_app_sim_info_clear(&info);
  connect_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.slot = slot; scope.client = qa_session_client_id(client); scope.userinfo = userinfo;
  qa_caught_error error;
  bool ok = qa_try(connect_body, &scope, &error);
  free(userinfo);
  if (ok) { a->result = scope.result; return; }
  qa_caught_error cleanup;
  bool cleaned = !scope.connected || qa_try(disconnect_slot_body, &scope, &cleanup);
  qa_engine_session_close_client(h->options.session, scope.client);
  if (!cleaned) qa_raise("AggregateError", 0, 0, "Q2 guest connect cleanup failed");
  qa_rethrow(&error);
}
static qa_app_q2_admission host_admit(void *ctx, const void *from, const void *request) {
  admit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = ctx; scope.from = from; scope.request = request;
  qa_app_q2_game_callback(admit_body, &scope);
  return scope.result;
}

static qa_app_q2_player host_carried_player(void *ctx, qa_client_id client) {
  guest_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->options.simulation;
  const qa_client_id *clients = NULL; const qa_actor_id *actors = NULL;
  size_t count = s->q2_native_players(s->ctx, &clients, &actors);
  size_t found = count;
  for (size_t i = 0; i < count && found == count; i++) if (qa_same_client(clients[i], client)) found = i;
  if (found == count) qa_raise("Error", 0, 0, "Q2 guest carried client is not owned by this world");
  qa_app_q2_player player = {client, actors[found], (double)client.slot + 1};
  per_player *entry = player_slot(h, (double)client.slot);
  entry->used = true; entry->player = player;
  return player;
}
typedef struct player_scope { guest_host *h; qa_app_q2_player player; const char *value; const void *command; double sequence; } player_scope;
static void begin_body(void *ctx) {
  player_scope *p = ctx;
  require_player(p->h, p->player);
  p->h->world->begin(p->h->world->ctx, p->player.source_entity);
  const qa_app_sim_network_simulation *s = p->h->options.simulation;
  s->notify_client_event(s->ctx, "admitted", p->player.actor);
}
static void host_begin(void *ctx, qa_app_q2_player player) {
  player_scope scope = {ctx, player, NULL, NULL, 0};
  qa_app_q2_game_callback(begin_body, &scope);
}
static void retire_body(void *ctx) {
  player_scope *p = ctx;
  if (!p->h->world->is_retired(p->h->world->ctx))
    p->h->options.simulation->disconnect_player(p->h->options.simulation->ctx, p->player.actor);
}
static void forget_body(void *ctx) {
  player_scope *p = ctx;
  per_player *entry = player_slot(p->h, (double)p->player.client.slot);
  clear_player(entry);
  entry->used = false;
}
static void close_body(void *ctx) { player_scope *p = ctx; qa_engine_session_close_client(p->h->options.session, p->player.client); }
static void disconnect_body(void *ctx) {
  player_scope *p = ctx;
  require_player(p->h, p->player);
  size_t failures = 0;
  qa_caught_error error;
  if (!qa_try(retire_body, p, &error)) failures++;
  if (!qa_try(forget_body, p, &error)) failures++;
  if (!qa_try(close_body, p, &error)) failures++;
  if (failures != 0) qa_raise("AggregateError", 0, 0, "Q2 guest client cleanup failed");
}
static void host_disconnect(void *ctx, qa_app_q2_player player, const char *reason) {
  (void)reason; /* TS disconnect(player) declares no reason parameter. */
  player_scope scope = {ctx, player, NULL, NULL, 0};
  qa_app_q2_game_callback(disconnect_body, &scope);
}

static qa_app_q2_game_state host_game_state(void *ctx, qa_app_q2_player player, const qa_q2_protocol_identity *protocol) {
  (void)protocol; /* TS gameState(player) declares no protocol parameter. */
  guest_host *h = ctx;
  require_player(h, player);
  const qa_app_sim_q2_guest_world *w = h->world;
  const qa_app_sim_network_simulation *s = h->options.simulation;
  per_player *entry = player_slot(h, (double)player.client.slot);
  clear_state(entry);
  size_t config_count = 0;
  const qa_q2_config_string_entry *configs = w->configstrings(w->ctx, &config_count);
  entry->configs.entries = config_count ? xcalloc(config_count, sizeof *entry->configs.entries) : NULL;
  const char *level = NULL;
  for (size_t i = 0; i < config_count; i++) {
    entry->configs.entries[i].index = configs[i].index;
    entry->configs.entries[i].value = xstrdup(configs[i].value);
    entry->configs.count = i + 1;
    if (configs[i].index == 0) level = entry->configs.entries[i].value;
  }
  memset(&entry->data, 0, sizeof entry->data);
  entry->data.servercount = 1;
  entry->data.attractloop = false;
  snprintf(entry->data.gamedir, sizeof entry->data.gamedir, "%s", qa_app_sim_network_gamedir(
      h->options.content->content_directory(h->options.content->ctx, s->recipe->map.entities.content)));
  entry->data.clientnum = player.source_entity - 1;
  snprintf(entry->data.levelname, sizeof entry->data.levelname, "%s", level ? level : h->map);
  entry->data.server_state = 2;
  entry->data.has_server_fps = true; entry->data.server_fps = 10;
  memset(&entry->baselines, 0, sizeof entry->baselines);
  size_t state_count = 0;
  const qa_q2_entity_state *states = w->entity_states(w->ctx, &state_count);
  for (size_t i = 0; i < state_count; i++) {
    const qa_q2_entity_state *state = &states[i];
    bool model = false;
    for (int m = 0; m < 4; m++) if (state->model_indexes[m] != 0) model = true;
    if (!(model || state->sound != 0 || state->effects != 0)) continue;
    qa_q2_wire_entity wire;
    qa_q2_from_entity(state, false, 0, 0, 0, 0, 0, 0, 0, 0, &wire);
    if (!qa_q2_baseline_list_set(&entry->baselines, state->number, &wire)) qa_range_error("Out of memory");
  }
  entry->has_state = true;
  qa_app_q2_game_state result = {&entry->data, &entry->configs, &entry->baselines};
  return result;
}

static bool entity_visible(guest_host *h, qa_app_q2_player player, const qa_q2_entity_state *state, qa_vec3 origin, double area,
    double cluster, const double *clusters, size_t cluster_count) {
  qa_shared_scene_queries *scene = h->options.simulation->scene;
  const qa_numeric_operations *n = &h->world->numeric;
  bool model = false;
  for (int m = 0; m < 4; m++) if (state->model_indexes[m] != 0) model = true;
  if (!model && state->effects == 0 && state->sound == 0 && state->event == 0) return false;
  if (state->number == player.source_entity) return true;
  qa_app_sim_q2_guest_entity_info info = h->world->entity_info(h->world->ctx, state->number);
  if (!qa_shared_areas_connected(scene, area, info.areas[0])
      && (info.areas[1] == 0 || !qa_shared_areas_connected(scene, area, info.areas[1]))) return false;
  if (qa_js_bit_and(state->render_effects, 128) != 0) return qa_shared_cluster_visible(scene, cluster, info.first_cluster, "phs");
  bool reached = false;
  if (!info.has_clusters) reached = headnode_visible(h, info.headnode, clusters, cluster_count);
  else for (size_t t = 0; t < info.cluster_count && !reached; t++)
    for (size_t f = 0; f < cluster_count && !reached; f++)
      if (qa_shared_cluster_visible(scene, clusters[f], info.clusters[t], "pvs")) reached = true;
  if (!reached) return false;
  double dx = n->subtract(n->ctx, origin.x, state->origin.x), dy = n->subtract(n->ctx, origin.y, state->origin.y);
  double dz = n->subtract(n->ctx, origin.z, state->origin.z);
  double xx = n->multiply(n->ctx, dx, dx), yy = n->multiply(n->ctx, dy, dy);
  double sum = n->add(n->ctx, xx, yy), zz = n->multiply(n->ctx, dz, dz);
  double distance = n->square_root(n->ctx, n->add(n->ctx, sum, zz));
  return state->model_indexes[0] != 0 || distance <= 400;
}
static const void *host_frame(void *ctx, qa_app_q2_player player, const void *output_value, const qa_q2_protocol_identity *protocol) {
  (void)protocol; /* TS frame(player, output) declares no protocol parameter. */
  guest_host *h = ctx;
  const qa_simulation_output *output = output_value;
  qa_shared_scene_queries *scene = h->options.simulation->scene;
  const qa_numeric_operations *n = &h->world->numeric;
  qa_vec3 origin = view_origin(h, player);
  double leaf = qa_shared_point_leaf(scene, origin), area = qa_shared_leaf_area(scene, leaf), cluster = qa_shared_leaf_cluster(scene, leaf);
  qa_bounds box;
  box.min.x = n->subtract(n->ctx, origin.x, 8); box.min.y = n->subtract(n->ctx, origin.y, 8); box.min.z = n->subtract(n->ctx, origin.z, 8);
  box.max.x = n->add(n->ctx, origin.x, 8); box.max.y = n->add(n->ctx, origin.y, 8); box.max.z = n->add(n->ctx, origin.z, 8);
  size_t cluster_count = 0;
  double *clusters = unique_clusters(scene, qa_shared_box_leaves(scene, box, 64), &cluster_count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, clusters);
  size_t state_count = 0;
  const qa_q2_entity_state *states = h->world->entity_states(h->world->ctx, &state_count);
  qa_q2_wire_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.entities = state_count ? xcalloc(state_count, sizeof *frame.entities) : NULL;
  qa_cleanup frame_guard;
  qa_cleanup_push(&frame_guard, (void (*)(void *))qa_q2_wire_frame_free, &frame);
  for (size_t i = 0; i < state_count; i++) {
    if (!entity_visible(h, player, &states[i], origin, area, cluster, clusters, cluster_count)) continue;
    qa_q2_wire_entity wire;
    qa_q2_from_entity(&states[i], false, 0, 0, 0, 0, 0, 0, 0, 0, &wire);
    qa_app_sim_q2_guest_entity_info info = h->world->entity_info(h->world->ctx, states[i].number);
    if (info.has_owner_slot && info.owner_slot == player.source_entity) wire.solid = 0;
    frame.entities[frame.entity_count++] = wire;
  }
  frame.server_frame = output->snapshot.frame.frame;
  frame.delta_frame = -1;
  frame.suppressed_count = 0;
  size_t bits_length = 0;
  const uint8_t *bits = qa_shared_area_bits(scene, area, &bits_length);
  frame.area_bits = xcalloc(bits_length, 1);
  memcpy(frame.area_bits, bits, bits_length);
  frame.area_bits_length = bits_length;
  qa_q2_player_state state = native_state(h, player);
  qa_q2_from_player_classic(&state, &frame.player);
  qa_cleanup_pop(&frame_guard);
  qa_cleanup_pop(&guard);
  free(clusters);
  per_player *entry = player_slot(h, (double)player.client.slot);
  clear_frame(entry);
  entry->frame = frame; entry->has_frame = true;
  return &entry->frame;
}

static owned_message *collect_messages(guest_host *h, size_t *out_count) {
  size_t count = 0;
  const qa_app_sim_q2_guest_message *raw = h->world->raw_messages(h->world->ctx, &count);
  owned_message *out = count ? xcalloc(count, sizeof *out) : NULL;
  for (size_t i = 0; i < count; i++) {
    out[i].message = raw[i];
    out[i].bytes = xcalloc(raw[i].length, 1);
    memcpy(out[i].bytes, raw[i].bytes, raw[i].length);
    out[i].message.bytes = out[i].bytes;
  }
  *out_count = count;
  return out;
}
static void host_observe(void *ctx, const void *output, const void *const *events, size_t count) {
  (void)output; (void)events; (void)count; /* TS observe() takes no arguments. */
  guest_host *h = ctx;
  const qa_app_sim_q2_guest_world *w = h->world;
  char *air = qa_cvar_registry_variable_string(w->cvars, "sv_airaccelerate");
  size_t config_count = 0;
  const qa_q2_config_string_entry *configs = w->configstrings(w->ctx, &config_count);
  const char *current = NULL;
  for (size_t i = 0; i < config_count; i++) if (configs[i].index == 29) current = configs[i].value;
  if (!is(current, air)) w->set_configstring(w->ctx, 29, air);
  free(air);
  h->local_recipient_count = 0;
  free_messages(h->local, h->local_count);
  h->local = collect_messages(h, &h->local_count);
  /* messages = [...progressedMessages, ...localMessages] */
  free_messages(h->messages, h->message_count);
  h->message_count = h->progressed_count + h->local_count;
  h->messages = h->message_count ? xcalloc(h->message_count, sizeof *h->messages) : NULL;
  for (size_t i = 0; i < h->progressed_count; i++) h->messages[i] = h->progressed[i];
  for (size_t i = 0; i < h->local_count; i++) {
    owned_message copy = h->local[i];
    copy.bytes = xcalloc(copy.message.length, 1);
    memcpy(copy.bytes, h->local[i].bytes, copy.message.length);
    copy.message.bytes = copy.bytes;
    h->messages[h->progressed_count + i] = copy;
  }
  free(h->progressed); h->progressed = NULL; h->progressed_count = 0;
}
static void host_observe_progress(void *ctx) {
  guest_host *h = ctx;
  h->local_recipient_count = 0;
  free_messages(h->local, h->local_count);
  h->local = collect_messages(h, &h->local_count);
  owned_message *grown = realloc(h->progressed, (h->progressed_count + h->local_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  h->progressed = grown;
  for (size_t i = 0; i < h->local_count; i++) {
    owned_message copy = h->local[i];
    copy.bytes = xcalloc(copy.message.length, 1);
    memcpy(copy.bytes, h->local[i].bytes, copy.message.length);
    copy.message.bytes = copy.bytes;
    h->progressed[h->progressed_count++] = copy;
  }
}
static const qa_app_q2_raw_message *host_local_messages(void *ctx, qa_app_q2_player player, size_t *out_count) {
  guest_host *h = ctx;
  require_player(h, player);
  for (size_t i = 0; i < h->local_recipient_count; i++)
    if (qa_same_client(h->local_recipients[i], player.client)) { *out_count = 0; return NULL; }
  qa_client_id *grown = realloc(h->local_recipients, (h->local_recipient_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  h->local_recipients = grown;
  h->local_recipients[h->local_recipient_count++] = player.client;
  player_messages(h, player, h->local, h->local_count);
  per_player *entry = player_slot(h, (double)player.client.slot);
  *out_count = entry->message_count;
  return entry->messages;
}
static const qa_app_q2_raw_message *host_raw_messages(void *ctx, qa_app_q2_player player, size_t *out_count) {
  guest_host *h = ctx;
  player_messages(h, player, h->messages, h->message_count);
  per_player *entry = player_slot(h, (double)player.client.slot);
  *out_count = entry->message_count;
  return entry->messages;
}
static const qa_app_q2_server_event *host_events(void *ctx, qa_app_q2_player player, const void *output,
    const void *const *events, size_t count, size_t *out_count) {
  (void)ctx; (void)player; (void)output; (void)events; (void)count; /* TS events: () => [] */
  *out_count = 0;
  return NULL;
}
typedef struct input_scope { guest_host *h; qa_app_q2_player player; const qa_q2_wire_usercmd *wire; double sequence; } input_scope;
static void input_body(void *ctx) {
  input_scope *in = ctx;
  guest_host *h = in->h;
  require_player(h, in->player);
  qa_user_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_USER_COMMAND_Q2;
  qa_q2_to_command(in->wire, &command.as.q2);
  qa_actor_command input;
  memset(&input, 0, sizeof input);
  input.actor = in->player.actor;
  input.source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; input.source.client = in->player.client;
  input.sequence = in->sequence; input.command = command;
  const qa_app_sim_network_simulation *s = h->options.simulation;
  s->observe_client_command(s->ctx, &input);
  h->world->think(h->world->ctx, in->player.source_entity, &command);
}
static const void *host_input(void *ctx, qa_app_q2_player player, const void *command, double sequence) {
  input_scope scope = {ctx, player, command, sequence};
  qa_app_q2_game_callback(input_body, &scope);
  return NULL; /* the guest thinks the command itself */
}
static const char *variable(void *ctx, const char *name) {
  guest_host *h = ctx;
  static _Thread_local char *value;
  free(value);
  value = qa_cvar_registry_variable_string(h->world->cvars, name);
  return value;
}
static void print_to(void *ctx, const char *text) { guest_host *h = ctx; h->options.print(h->options.print_ctx, text); }
static const char *host_expand_client_command(void *ctx, const char *text) {
  char *out = NULL;
  if (!qa_expand_command_macros(text, variable, ctx, print_to, ctx, QA_COMMAND_TEXT_SOURCE, &out)) return NULL;
  if (!out) qa_range_error("Out of memory");
  return out;
}
static void host_command(void *ctx, qa_app_q2_player player, const char *name, const char *const *args, size_t count) {
  size_t length = strlen(name) + 1;
  for (size_t i = 0; i < count; i++) length += strlen(args[i]) + 1;
  char *text = xcalloc(length, 1);
  strcpy(text, name);
  for (size_t i = 0; i < count; i++) { strcat(text, " "); strcat(text, args[i]); }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, text);
  command_text(ctx, player, text);
  qa_cleanup_pop(&guard);
  free(text);
}
static void host_command_text(void *ctx, qa_app_q2_player player, const char *text) { command_text(ctx, player, text); }
static void userinfo_body(void *ctx) {
  player_scope *p = ctx;
  require_player(p->h, p->player);
  p->h->world->userinfo(p->h->world->ctx, p->player.source_entity, p->value);
  const qa_app_sim_network_simulation *s = p->h->options.simulation;
  s->notify_client_event(s->ctx, "userinfo", p->player.actor);
}
static void host_userinfo(void *ctx, qa_app_q2_player player, const char *value) {
  player_scope scope = {ctx, player, value, NULL, 0};
  qa_app_q2_game_callback(userinfo_body, &scope);
}
static void host_print(void *ctx, const char *text) { print_to(ctx, text); }

qa_app_q2_server_host qa_app_sim_create_classic_q2_application_server_host(
    qa_app_sim_q2_server_binding_options options, const qa_app_sim_q2_guest_world *world) {
  if (!is(options.protocol.kind, "q2-classic")) qa_raise("Error", 0, 0, "Classic guest server requires protocol 34");
  qa_cvar_registry *cvars = world->cvars;
  guest_host *h = xcalloc(1, sizeof *h);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, h);
  h->options = options; h->world = world;
  /* mapPath.replace(/^maps\//, '').replace(/\.bsp$/, '') */
  const char *map = world->map_path;
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  h->map = xstrdup(map);
  size_t length = strlen(h->map);
  if (length >= 4 && strcmp(h->map + length - 4, ".bsp") == 0) h->map[length - 4] = '\0';
  h->downloads = qa_app_create_q2_application_downloads(
      options.content->for_content(options.content->ctx, options.content->recipe->map.entities.content), cvars, false);
  if (!h->downloads) qa_range_error("Out of memory");
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(cvars, "hostname", "noname", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_ARCHIVE, &snapshot); qa_cvar_read_free(&snapshot);
  const char *names[2] = {"protocol", "mapname"}, *values[2] = {"34", h->map};
  for (int i = 0; i < 2; i++) {
    qa_cvar_registry_register(cvars, names[i], values[i], QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_NO_SET, &snapshot); qa_cvar_read_free(&snapshot);
    qa_cvar_registry_set(cvars, names[i], values[i], true, &snapshot); qa_cvar_read_free(&snapshot);
  }
  size_t geometry_length = 0;
  uint8_t *geometry = options.content->read(options.content->ctx, &options.content->recipe->map.geometry, &geometry_length);
  char text[32];
  snprintf(text, sizeof text, "%s", QA_JS_NUMBER((double)qa_block_checksum(geometry, geometry_length)));
  free(geometry);
  world->set_configstring(world->ctx, 31, text);
  snprintf(text, sizeof text, "%s", QA_JS_NUMBER(world->max_clients));
  world->set_configstring(world->ctx, 30, text);
  char *air = qa_cvar_registry_variable_string(cvars, "sv_airaccelerate");
  world->set_configstring(world->ctx, 29, air);
  free(air);
  h->message_options.max_config_strings = 2080;
  h->message_options.inventory_slots = 256;
  h->discovery.ctx = h; h->discovery.status = discovery_status; h->discovery.info = discovery_info;
  qa_cleanup_pop(&guard);
  qa_app_q2_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = h;
  if (options.rejects) host.rejects = host_rejects;
  host.protocol = options.protocol;
  host.message_options = &h->message_options;
  host.max_clients = world->max_clients;
  host.downloads = h->downloads;
  host.administration = options.administration;
  if (options.masters) host.masters = host_masters;
  host.supports_source_wire = host_supports_source_wire;
  host.discovery = &h->discovery;
  host.admit = host_admit;
  host.carried_player = host_carried_player;
  host.begin = host_begin;
  host.disconnect = host_disconnect;
  host.game_state = host_game_state;
  host.frame = host_frame;
  host.observe = host_observe;
  host.observe_progress = host_observe_progress;
  host.local_messages = host_local_messages;
  host.raw_messages = host_raw_messages;
  host.events = host_events;
  host.input = host_input;
  host.expand_client_command = host_expand_client_command;
  host.command = host_command;
  host.command_text = host_command_text;
  host.userinfo = host_userinfo;
  host.print = host_print;
  return host;
}

void qa_app_sim_classic_q2_application_server_host_destroy(qa_app_q2_server_host host) {
  guest_host *h = host.ctx;
  if (h == NULL) return;
  for (size_t i = 0; i < h->player_count; i++) clear_player(&h->players[i]);
  free(h->players);
  free_messages(h->messages, h->message_count);
  free_messages(h->local, h->local_count);
  free_messages(h->progressed, h->progressed_count);
  free(h->local_recipients);
  for (size_t i = 0; i < h->status_player_count; i++) free(h->status_names[i]);
  free(h->status_names); free(h->status_players); free(h->server_info);
  qa_wire_admission_destroy(&h->support);
  qa_app_q2_downloads_destroy(h->downloads);
  free(h->map);
  free(h);
}
