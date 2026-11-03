/* Port of src/app/bootstrap/simulation/network-unified.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_unified.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/app/bootstrap/network/unified_control.h"
#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_event_values.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/content/held_weapon.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *out = xmalloc(length + 1);
  memcpy(out, text, length + 1);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---- unifiedPresentationFor ------------------------------------------------------- */
static const qa_save_value *get(const qa_save_value *node, const char *key) {
  return node == NULL ? NULL : qa_save_value_get(node, key);
}
static const char *kind_of(const qa_save_value *node) {
  const qa_save_value *kind = get(node, "kind");
  return kind != NULL && kind->kind == QA_SAVE_STRING ? kind->text : NULL;
}
/* `'key' in object`. */
static bool has(const qa_save_value *node, const char *key) { return get(node, key) != NULL; }
/* `target.equals(actor)`: an absent or null target throws as JS member access does. */
static bool equals(const qa_save_value *target, const char *expression, qa_actor_id actor) {
  if (target == NULL || target->kind == QA_SAVE_UNDEFINED)
    qa_raise("TypeError", 0, 0, "undefined is not an object (evaluating '%s.equals')", expression);
  if (target->kind == QA_SAVE_NULL)
    qa_raise("TypeError", 0, 0, "null is not an object (evaluating '%s.equals')", expression);
  qa_actor_id value;
  return qa_app_unified_event_actor_id(target, &value) && qa_same_actor(value, actor);
}
/* own(target): target === null || target.equals(actor). */
static bool own(const qa_save_value *target, const char *expression, qa_actor_id actor) {
  if (target != NULL && target->kind == QA_SAVE_NULL) return true;
  return equals(target, expression, actor);
}
/* `node.kind` on an object TS requires to exist. */
static const char *required_kind(const qa_save_value *node, const char *expression) {
  if (node == NULL || node->kind == QA_SAVE_UNDEFINED)
    qa_raise("TypeError", 0, 0, "undefined is not an object (evaluating '%s.kind')", expression);
  if (node->kind == QA_SAVE_NULL)
    qa_raise("TypeError", 0, 0, "null is not an object (evaluating '%s.kind')", expression);
  return kind_of(node);
}

bool qa_app_sim_unified_presentation_for(qa_actor_id actor, qa_client_id client, const qa_save_value *value) {
  const qa_save_value *recipient = get(value, "recipient");
  if (recipient != NULL && recipient->kind != QA_SAVE_UNDEFINED && !equals(recipient, "value.recipient", actor))
    return false;
  const char *kind = kind_of(value);
  const qa_save_value *event = get(value, "event");
  if (is(kind, "presentation-owner") || is(kind, "debug-graph")) return true;
  if (is(kind, "view-reset")) return own(get(value, "actor"), "value.actor", actor);
  if (is(kind, "q1-fog")) return own(get(event, "player"), "value.event.player", actor);
  if (is(kind, "q1")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "server-command")) return false;
    return has(event, "player") ? own(get(event, "player"), "event.player", actor) : true;
  }
  if (is(kind, "q1-composition")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "source-log") || is(event_kind, "developer-message")) return false;
    if (is(event_kind, "addon")) {
      const qa_save_value *inner = get(event, "event");
      if (has(inner, "player")) return own(get(inner, "player"), "event.event.player", actor);
      return !is(required_kind(inner, "event.event"), "developer-message");
    }
    if (is(event_kind, "ctf-status") || is(event_kind, "prompt") || is(event_kind, "clear-prompt"))
      return own(get(event, "actor"), "event.actor", actor);
    return true;
  }
  if (is(kind, "q2")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "pickup")) return own(get(event, "player"), "event.player", actor);
    if (is(event_kind, "centerprint") || is(event_kind, "print") || is(event_kind, "damage-indicator"))
      return own(get(event, "actor"), "event.actor", actor);
    return true;
  }
  if (is(kind, "q2-player")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "stufftext") || is(event_kind, "load-menu") || is(event_kind, "trail")) return false;
    if (is(event_kind, "userinfo")) return true;
    return is(event_kind, "print") ? own(get(event, "target"), "event.target", actor)
                                   : own(get(event, "actor"), "event.actor", actor);
  }
  if (is(kind, "q2-composition")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "kick")) return false;
    if (is(event_kind, "missionpack-entity")) return true;
    const qa_save_value *inner = get(event, "event");
    if (is(event_kind, "missionpack-player")) {
      if (inner == NULL || inner->kind == QA_SAVE_UNDEFINED || inner->kind == QA_SAVE_NULL)
        qa_raise("TypeError", 0, 0, "%s is not an object (evaluating 'event.event.actor')",
            inner != NULL && inner->kind == QA_SAVE_NULL ? "null" : "undefined");
      return own(get(inner, "actor"), "event.event.actor", actor);
    }
    if (is(event_kind, "grapple-prediction")) return own(get(event, "actor"), "event.actor", actor);
    const char *inner_kind = required_kind(inner, "event.event");
    if (is(inner_kind, "score-log")) return false;
    if (is(inner_kind, "grapple-cable") || is(inner_kind, "match-status")) return true;
    return own(get(inner, "actor"), "event.event.actor", actor);
  }
  if (is(kind, "q2-rerelease")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "autosave") || is(event_kind, "restart-level")) return false;
    if (is(event_kind, "alpha") || is(event_kind, "dynamic-light") || is(event_kind, "player-dogtag")
        || is(event_kind, "flashlight")) return true;
    return has(event, "actor") ? own(get(event, "actor"), "event.actor", actor) : true;
  }
  if (is(kind, "q3-source")) {
    const char *event_kind = required_kind(event, "event");
    if (is(event_kind, "console-command") || is(event_kind, "drop-client") || is(event_kind, "log")) return false;
    if (is(event_kind, "server-command")) {
      const qa_save_value *target = get(event, "client");
      double slot = target != NULL && target->kind == QA_SAVE_NUMBER ? target->number : NAN;
      return slot < 0 || slot == (double)client.slot;
    }
    return true;
  }
  if (is(kind, "q2-weapon"))
    return !is(required_kind(event, "value.event"), "view-weapon") || own(get(event, "actor"), "value.event.actor", actor);
  if (is(kind, "q3-character") || is(kind, "music") || is(kind, "q1-level") || is(kind, "q1-session")
      || is(kind, "q1-sky") || is(kind, "q1-client")) return true;
  return false; /* TS falls out of the switch: undefined */
}

/* ---- unifiedModelPresentations ---------------------------------------------------- */
void qa_app_sim_unified_model_presentations(qa_actor_id viewer, const qa_app_simulation_presentation *models,
    size_t count, qa_app_simulation_presentation *out) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_simulation_presentation *source = &models[i];
    if (!source->view_weapon || qa_same_actor(source->actor, viewer)) { out[i] = *source; continue; }
    qa_app_simulation_presentation value;
    memset(&value, 0, sizeof value);
    value.actor = source->actor; value.content = source->content; value.family = source->family;
    value.path = source->path;
    value.frame = 0; value.old_frame = 0; value.skin = 0; value.effects = 0; value.render_flags = 0;
    value.origin = (qa_vec3){0, 0, 0}; value.angles = (qa_vec3){0, 0, 0};
    value.scale = 1; value.visible = false; value.view_weapon = true;
    value.render_owner = source->render_owner;
    value.weapon_item = source->weapon_item;
    value.held_weapon = source->held_weapon;
    if (source->has_q3_weapon) {
      value.has_q3_weapon = true;
      value.q3_weapon_weapon = source->q3_weapon_weapon;
      value.q3_weapon_time_ms = source->q3_weapon_time_ms;
      value.has_q3_weapon_last_fire = source->has_q3_weapon_last_fire;
      value.q3_weapon_last_fire_ms = source->q3_weapon_last_fire_ms;
      value.q3_weapon_firing = source->q3_weapon_firing;
      value.q3_weapon_torso_animation = 0; value.q3_weapon_horizontal_speed = 0; value.q3_weapon_bob_cycle = 0;
    }
    out[i] = value;
  }
}

/* ---- createUnifiedApplicationServerHost ------------------------------------------- */
typedef struct player_entry { bool used; qa_app_unified_player player; bool has_info; qa_app_sim_info info; } player_entry;
typedef struct unified_host {
  qa_app_sim_unified_host_options options;
  const qa_app_sim_network_simulation *simulation;
  qa_comp_q1_source_composition *q1;
  qa_q2_product_runtime *q2;
  qa_app_q3_source_runtime *q3;
  player_entry *slots; size_t slot_count;
  /* Results returned to the server, owned until the next call. */
  qa_save_value **events; size_t event_count;
  qa_app_unified_resource_key *resources; size_t resource_count;
  char **resource_strings; size_t resource_string_count;
  qa_app_unified_component_publication *publications; size_t publication_count;
  qa_app_unified_native_publication *native; size_t native_count;
} unified_host;

static player_entry *entry_at(unified_host *h, double slot) {
  if (slot < 0 || slot != trunc(slot)) return NULL;
  if ((size_t)slot >= h->slot_count) {
    size_t count = (size_t)slot + 1;
    player_entry *grown = realloc(h->slots, count * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    memset(grown + h->slot_count, 0, (count - h->slot_count) * sizeof *grown);
    h->slots = grown; h->slot_count = count;
  }
  return &h->slots[(size_t)slot];
}
static void forget_info(player_entry *entry) { if (entry->has_info) qa_app_sim_info_clear(&entry->info); entry->has_info = false; }
static void store_info(player_entry *entry, const qa_app_sim_info *values) {
  qa_app_sim_info copy; qa_app_sim_info_copy(&copy, values);
  forget_info(entry); entry->info = copy; entry->has_info = true;
}
static qa_app_unified_player source_player(unified_host *h, qa_client_id client, qa_actor_id actor) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(h->simulation->actors, actor);
  if (!source.has_value || !is(source.provider, h->simulation->recipe->map.entities.provider))
    qa_raise("Error", 0, 0, "Unified player lost its authoritative source address");
  qa_app_unified_player player = {client, actor, (double)source.slot};
  return player;
}
static void require_player(unified_host *h, qa_app_unified_player player) {
  player_entry *current = player.client.slot >= 0 && (size_t)player.client.slot < h->slot_count ? &h->slots[player.client.slot] : NULL;
  if (current == NULL || !current->used || !qa_same_client(current->player.client, player.client)
      || !qa_same_actor(current->player.actor, player.actor) || !qa_session_actor_registry_is_live(h->simulation->actors, player.actor))
    qa_raise("Error", 0, 0, "Unified client belongs to a retired source player");
}
/* info(value, address) */
static void info_of(qa_app_sim_info *out, const char *value, const char *address) {
  qa_app_sim_info_parse_q2(out, value);
  qa_app_sim_info_set(out, "ip", address);
}
static void update(unified_host *h, qa_app_unified_player player, const qa_app_sim_info *values) {
  const qa_app_sim_network_simulation *s = h->simulation;
  if (h->q1 != NULL) {
    qa_comp_userinfo_entry *entries = xcalloc(values->count, sizeof *entries);
    for (size_t i = 0; i < values->count; i++) { entries[i].key = values->pairs[i].key; entries[i].value = values->pairs[i].value; }
    bool ok = qa_comp_q1_source_composition_userinfo(h->q1, player.actor, entries, values->count);
    free(entries);
    if (!ok) qa_range_error("Out of memory");
  } else if (h->q2 != NULL) {
    qa_q2_entity *entity = qa_q2_game_entity_of(h->q2->services, &player.actor);
    if (entity == NULL) qa_raise("Error", 0, 0, "Unified Q2 player lost its source entity");
    char *text = qa_app_sim_info_text(values);
    qa_q2_players_userinfo_changed(h->q2->players, entity, h->q2->services, text);
    free(text);
  } else if (h->q3 != NULL) {
    char *text = qa_app_sim_info_text(values);
    qa_app_q3_server_state_set_userinfo(qa_app_q3_source_runtime_host(h->q3)->server_state, (double)player.client.slot, text);
    free(text);
    qa_app_q3_source_runtime_userinfo_changed(h->q3, (double)player.client.slot);
  }
  store_info(entry_at(h, (double)player.client.slot), values);
  s->notify_client_event(s->ctx, "userinfo", player.actor);
}
static bool find_player_actor(const qa_app_sim_network_simulation *s, qa_client_id client, qa_actor_id *out) {
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && qa_same_client(movement->client, client)) { *out = players[i]; return true; }
  }
  return false;
}
static bool slot_taken(unified_host *h, double slot) {
  if (slot < (double)h->slot_count && h->slots[(size_t)slot].used) return true;
  const qa_app_sim_network_simulation *s = h->simulation;
  size_t count = 0;
  const qa_actor_id *players = s->players(s->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *movement = s->movement_player(s->ctx, players[i]);
    if (movement != NULL && (double)movement->client.slot == slot) return true;
  }
  return false;
}

typedef struct admit_scope {
  unified_host *h; double slot; qa_client_id client; qa_app_sim_info *values;
  bool has_actor; qa_actor_id actor; qa_app_unified_player player;
} admit_scope;
static void admit_body(void *ctx) {
  admit_scope *a = ctx;
  unified_host *h = a->h;
  const qa_app_sim_network_simulation *s = h->simulation;
  if (h->q3 != NULL) {
    char *text = qa_app_sim_info_text(a->values);
    qa_app_q3_server_state_set_userinfo(qa_app_q3_source_runtime_host(h->q3)->server_state, a->slot, text);
    free(text);
  }
  a->actor = s->admit_player(s->ctx, a->client).actor;
  a->has_actor = true;
  qa_app_unified_player player = source_player(h, a->client, a->actor);
  player_entry *entry = entry_at(h, a->slot);
  entry->used = true; entry->player = player;
  if (h->q3 == NULL) update(h, player, a->values);
  else store_info(entry, a->values);
  a->player = player;
}
typedef struct call_scope { unified_host *h; qa_actor_id actor; qa_client_id client; } call_scope;
static void disconnect_body(void *ctx) { call_scope *c = ctx; c->h->simulation->disconnect_player(c->h->simulation->ctx, c->actor); }
static void close_body(void *ctx) { call_scope *c = ctx; qa_engine_session_close_client(c->h->options.session, c->client); }

static qa_app_unified_admission host_admit(void *ctx, const qa_net_address *address, const char *value) {
  unified_host *h = ctx;
  const qa_app_sim_network_simulation *s = h->simulation;
  char *key = address->kind == QA_NET_LOOPBACK ? xstrdup("localhost") : qa_app_sim_address_key(address);
  qa_app_sim_info values;
  info_of(&values, value, key);
  free(key);
  double first_slot = 0;
  if (h->q3 != NULL) {
    qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(h->q3)->cvars;
    const char *password = qa_app_sim_info_get(&values, "password");
    char *private_password = qa_cvar_registry_variable_string(cvars, "sv_privatePassword");
    if (!is(password ? password : "", private_password)) {
      double count = trunc(qa_cvar_registry_variable_value(cvars, "sv_privateClients"));
      first_slot = qa_js_max(0, count);
    }
    free(private_password);
  }
  double slot = first_slot;
  double max_clients = s->options->max_clients;
  while (slot < max_clients && slot_taken(h, slot)) slot++;
  qa_app_unified_admission result;
  memset(&result, 0, sizeof result);
  if (slot >= max_clients) {
    qa_app_sim_info_clear(&values);
    result.accepted = false; result.reason = "Server is full";
    return result;
  }
  if (h->q2 != NULL) {
    char *text = qa_app_sim_info_text(&values);
    qa_q2_connection_result allowed = qa_q2_players_connect(h->q2->players, h->q2->services, text);
    free(text);
    if (!allowed.allowed) {
      qa_app_sim_info_clear(&values);
      static _Thread_local char reason[256];
      snprintf(reason, sizeof reason, "%s", allowed.reason);
      result.accepted = false; result.reason = reason;
      return result;
    }
    const char *ip = qa_app_sim_info_get(&values, "ip");
    char *address_text = xstrdup(ip ? ip : "");
    qa_app_sim_info_clear(&values);
    info_of(&values, allowed.userinfo, address_text);
    free(address_text);
  }
  qa_session_client *client = qa_engine_session_create_client(h->options.session, slot);
  qa_session_client_connect(client, address->kind == QA_NET_LOOPBACK ? "loopback" : "remote");
  admit_scope scope;
  memset(&scope, 0, sizeof scope);
  scope.h = h; scope.slot = slot; scope.client = qa_session_client_id(client); scope.values = &values;
  qa_caught_error error;
  if (qa_try(admit_body, &scope, &error)) {
    qa_app_sim_info_clear(&values);
    result.accepted = true; result.player = scope.player;
    return result;
  }
  qa_app_sim_info_clear(&values);
  if (!scope.has_actor) {
    qa_actor_id found;
    if (find_player_actor(s, scope.client, &found)) { scope.actor = found; scope.has_actor = true; }
  }
  size_t failures = 1;
  qa_caught_error cleanup;
  call_scope call = {h, scope.actor, scope.client};
  if (scope.has_actor && !qa_try(disconnect_body, &call, &cleanup)) failures++;
  if (!qa_try(close_body, &call, &cleanup)) failures++;
  player_entry *entry = entry_at(h, slot);
  entry->used = false; forget_info(entry);
  if (failures > 1) qa_raise("AggregateError", 0, 0, "Unified admission cleanup failed");
  if (is(error.name, "Q3ClientAdmissionDenied")) {
    static _Thread_local char reason[QA_CAUGHT_MESSAGE_CAPACITY];
    snprintf(reason, sizeof reason, "%s", error.message);
    result.accepted = false; result.reason = reason;
    return result;
  }
  qa_rethrow(&error);
}

static qa_app_unified_player host_carried_player(void *ctx, qa_client_id client) {
  unified_host *h = ctx;
  qa_actor_id actor;
  if (!find_player_actor(h->simulation, client, &actor)) qa_raise("Error", 0, 0, "Unified carried client has no source player");
  qa_app_unified_player player = source_player(h, client, actor);
  player_entry *entry = entry_at(h, (double)client.slot);
  entry->used = true; entry->player = player;
  return player;
}

static void host_disconnect(void *ctx, qa_app_unified_player player) {
  unified_host *h = ctx;
  require_player(h, player);
  size_t failures = 0;
  qa_caught_error error;
  call_scope call = {h, player.actor, player.client};
  if (!qa_try(disconnect_body, &call, &error)) failures++;
  if (!qa_try(close_body, &call, &error)) failures++;
  player_entry *entry = entry_at(h, (double)player.client.slot);
  entry->used = false; forget_info(entry);
  if (failures != 0) qa_raise("AggregateError", 0, 0, "Unified player disconnect failed");
}

static void host_userinfo(void *ctx, qa_app_unified_player player, const char *value) {
  unified_host *h = ctx;
  require_player(h, player);
  player_entry *entry = entry_at(h, (double)player.client.slot);
  const char *ip = entry->has_info ? qa_app_sim_info_get(&entry->info, "ip") : NULL;
  char *address = xstrdup(ip ? ip : "");
  qa_app_sim_info values;
  info_of(&values, value, address);
  free(address);
  update(h, player, &values);
  qa_app_sim_info_clear(&values);
}

static void host_command(void *ctx, qa_app_unified_player player, const char *name, const char *const *args, size_t count) {
  unified_host *h = ctx;
  require_player(h, player);
  if (h->q1 != NULL && is(name, "name")) {
    qa_comp_q1_source_client *record = qa_comp_q1_source_clients_require(&h->q1->clients, player.actor);
    qa_comp_q1_client_snapshot snapshot = qa_comp_q1_source_client_snapshot(record);
    qa_app_sim_info values; memset(&values, 0, sizeof values);
    for (size_t i = 0; i < snapshot.userinfo_count; i++)
      qa_app_sim_info_set(&values, snapshot.userinfo[i].key, snapshot.userinfo[i].value);
    /* (args[0] ?? 'unconnected').slice(0, 15): UTF-16 units equal bytes for ASCII names. */
    char name_value[16];
    snprintf(name_value, sizeof name_value, "%s", count > 0 && args[0] != NULL ? args[0] : "unconnected");
    qa_app_sim_info_set(&values, "name", name_value);
    update(h, player, &values);
    qa_app_sim_info_clear(&values);
    return;
  }
  if (h->q1 != NULL && is(name, "color")) {
    const char *first = count > 0 && args[0] != NULL ? args[0] : NULL;
    const char *second = count > 1 && args[1] != NULL ? args[1] : first;
    double shirt = first ? qa_js_string_to_number(first) : 0, pants = second ? qa_js_string_to_number(second) : 0;
    if (!qa_comp_q1_source_clients_colors(&h->q1->clients, player.actor, shirt, pants)) qa_range_error("Out of memory");
    return;
  }
  h->simulation->player_command(h->simulation->ctx, player.actor, name, args, count);
}

static bool kinds_match(qa_movement_state_kind state, qa_user_command_kind command) {
  switch (state) {
    case QA_MOVEMENT_STATE_Q1: return command == QA_USER_COMMAND_Q1;
    case QA_MOVEMENT_STATE_QW: return command == QA_USER_COMMAND_QW;
    case QA_MOVEMENT_STATE_Q2: return command == QA_USER_COMMAND_Q2;
    case QA_MOVEMENT_STATE_Q2_RERELEASE: return command == QA_USER_COMMAND_Q2_RERELEASE;
    case QA_MOVEMENT_STATE_Q3: return command == QA_USER_COMMAND_Q3;
  }
  return false;
}
static qa_actor_command host_input(void *ctx, qa_app_unified_player player, double sequence,
    const qa_user_command *command, const qa_arsenal_intent *arsenal) {
  unified_host *h = ctx;
  require_player(h, player);
  if (!(sequence == trunc(sequence) && fabs(sequence) <= (double)QA_MAX_SAFE_INTEGER) || sequence < 0)
    qa_raise("Error", 0, 0, "Invalid unified input sequence");
  qa_app_sim_movement_player *movement = h->simulation->movement_player(h->simulation->ctx, player.actor);
  if (movement == NULL || !kinds_match(movement->state.kind, command->kind))
    qa_raise("Error", 0, 0, "Unified command does not match selected movement");
  if (!qa_app_unified_command_in_bounds(command)) qa_raise("Error", 0, 0, "Unified command movement outside provider bounds");
  qa_actor_command out;
  memset(&out, 0, sizeof out);
  out.actor = player.actor;
  out.source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; out.source.client = player.client;
  out.sequence = sequence; out.command = *command; out.angle_space = "absolute";
  out.arsenal = arsenal;
  return out;
}

/* permitted(player, event) */
static bool permitted(qa_app_unified_player player, const qa_simulation_event *event) {
  if (is(event->audience.kind, "seat") || (is(event->audience.kind, "client") && !qa_same_client(event->audience.client, player.client)))
    return false;
  if (event->payload.kind == QA_SIM_EVENT_TRANSITION || event->payload.kind == QA_SIM_EVENT_DAMAGE) return false;
  if (event->payload.kind == QA_SIM_EVENT_MESSAGE && (event->payload.event.kind == QA_NETWORK_EVENT_COMMAND_TEXT
      || event->payload.event.kind == QA_NETWORK_EVENT_DISCONNECT)) return false;
  return true;
}
/* resource(id) */
static qa_resolved_resource_reference resource_of(unified_host *h, qa_resource_id id) {
  const qa_app_sim_network_simulation *s = h->simulation;
  qa_resolved_resource_reference found;
  if (s->event_resource(s->ctx, id, &found)) return found;
  for (size_t i = 0; i < s->recipe->resource_count; i++)
    if (is(s->recipe->resources[i].id, id)) return s->recipe->resources[i];
  qa_raise("Error", 0, 0, "Unified sound has no declared content resource");
}
static qa_content_id mount_content(const qa_resolved_resource_reference *value) {
  const qa_resource_provenance *provenance = &value->provenance;
  return provenance->kind == QA_CONTENT_MOUNT_ARCHIVE ? provenance->archive_mount->identity.content
                                                      : provenance->loose_mount->identity.content;
}
static void reset_resources(unified_host *h) {
  for (size_t i = 0; i < h->resource_string_count; i++) free(h->resource_strings[i]);
  free(h->resource_strings); h->resource_strings = NULL; h->resource_string_count = 0;
  free(h->resources); h->resources = NULL; h->resource_count = 0;
}
static const qa_app_unified_resource_key *host_resources(void *ctx, qa_app_unified_player player, const void *output_value,
    size_t *out_count) {
  unified_host *h = ctx;
  require_player(h, player);
  const qa_simulation_output *output = output_value;
  reset_resources(h);
  h->resources = output->event_count ? xcalloc(output->event_count, sizeof *h->resources) : NULL;
  h->resource_strings = output->event_count ? xcalloc(output->event_count, sizeof *h->resource_strings) : NULL;
  for (size_t i = 0; i < output->event_count; i++) {
    const qa_simulation_event *event = &output->events[i];
    if (!permitted(player, event) || event->payload.kind != QA_SIM_EVENT_SOUND) continue;
    qa_resolved_resource_reference value = resource_of(h, event->payload.resource);
    qa_app_unified_resource_key key = {mount_content(&value), value.requested_path, value.digest, (double)value.byte_length};
    /* result.set(value.id, key): an existing id keeps its position. */
    size_t at = 0;
    while (at < h->resource_count && !is(h->resource_strings[at], value.id)) at++;
    if (at == h->resource_count) {
      h->resource_strings[at] = xstrdup(value.id);
      h->resource_string_count = ++h->resource_count;
    }
    h->resources[at] = key;
  }
  *out_count = h->resource_count;
  return h->resources;
}

/* ---- frame deep copies (the frame is freed by qa_app_unified_presentation_frame_free) ---- */
static void *copy_array(const void *values, size_t count, size_t size) {
  if (count == 0 || values == NULL) return NULL;
  void *out = xmalloc(count * size);
  memcpy(out, values, count * size);
  return out;
}
static qa_scene_entity copy_entity(const qa_scene_entity *source, int depth);
static void copy_entity_into(qa_scene_entity *out, const qa_scene_entity *source, int depth) {
  *out = *source;
  if (source->actor) { qa_actor_id *actor = xmalloc(sizeof *actor); *actor = *source->actor; out->actor = actor; }
  out->pose.joints = copy_array(source->pose.joints, source->pose.joint_count, sizeof *source->pose.joints);
  if (source->attachment_count) {
    qa_scene_entity_attachment *attachments = xcalloc(source->attachment_count, sizeof *attachments);
    for (size_t i = 0; i < source->attachment_count; i++) {
      attachments[i].tag = source->attachments[i].tag;
      if (source->attachments[i].entity) {
        qa_scene_entity *entity = xmalloc(sizeof *entity);
        *entity = copy_entity(source->attachments[i].entity, depth + 1);
        attachments[i].entity = entity;
      }
    }
    out->attachments = attachments;
  } else out->attachments = NULL;
}
static qa_scene_entity copy_entity(const qa_scene_entity *source, int depth) {
  qa_scene_entity out;
  copy_entity_into(&out, source, depth);
  return out;
}
static qa_world_snapshot copy_snapshot(const qa_world_snapshot *source, qa_actor_id viewer) {
  qa_world_snapshot out = *source;
  out.actors = copy_array(source->actors, source->actor_count, sizeof *source->actors);
  qa_body_snapshot *bodies = copy_array(source->bodies, source->body_count, sizeof *source->bodies);
  for (size_t i = 0; i < source->body_count; i++)
    if (source->bodies[i].body.ground) {
      qa_actor_id *ground = xmalloc(sizeof *ground);
      *ground = *source->bodies[i].body.ground;
      bodies[i].body.ground = ground;
    }
  out.bodies = bodies;
  size_t kept = 0;
  qa_inventory_snapshot *inventories = xcalloc(source->inventory_count, sizeof *inventories);
  for (size_t i = 0; i < source->inventory_count; i++)
    if (qa_same_actor(source->inventories[i].actor, viewer)) {
      inventories[kept] = source->inventories[i];
      inventories[kept].entries = copy_array(source->inventories[i].entries, source->inventories[i].entry_count,
          sizeof *source->inventories[i].entries);
      kept++;
    }
  out.inventories = inventories; out.inventory_count = kept;
  qa_actor_configuration *configurations = copy_array(source->configurations, source->configuration_count,
      sizeof *source->configurations);
  for (size_t i = 0; i < source->configuration_count; i++)
    configurations[i].weapons.values = copy_array(source->configurations[i].weapons.values,
        source->configurations[i].weapons.count, sizeof *source->configurations[i].weapons.values);
  out.configurations = configurations;
  qa_scene_entity *entities = source->scene.entity_count ? xcalloc(source->scene.entity_count, sizeof *entities) : NULL;
  for (size_t i = 0; i < source->scene.entity_count; i++) copy_entity_into(&entities[i], &source->scene.entities[i], 0);
  out.scene.entities = entities;
  out.scene.lights = copy_array(source->scene.lights, source->scene.light_count, sizeof *source->scene.lights);
  out.scene.particles = copy_array(source->scene.particles, source->scene.particle_count, sizeof *source->scene.particles);
  out.scene.light_styles = copy_array(source->scene.light_styles, source->scene.light_style_count,
      sizeof *source->scene.light_styles);
  if (source->scene.area_bits) {
    qa_byte_view *bits = xmalloc(sizeof *bits);
    bits->length = source->scene.area_bits->length;
    bits->bytes = copy_array(source->scene.area_bits->bytes, bits->length, 1);
    out.scene.area_bits = bits;
  }
  return out;
}
static qa_save_value *clone_value(const void *value) {
  if (value == NULL) return NULL;
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  if (!bytes) qa_range_error("Out of memory");
  qa_save_value *out = qa_decode_checkpoint_value(bytes, length);
  free(bytes);
  if (!out) qa_range_error("Out of memory");
  return out;
}
static const qa_held_weapon_declaration *clone_held(const qa_held_weapon_declaration *source) {
  if (source == NULL) return NULL;
  qa_held_weapon_declaration *out = xmalloc(sizeof *out);
  *out = *source;
  qa_held_weapon_model *model = &out->model;
  if (source->model.digest) model->digest = xstrdup(source->model.digest);
  if (source->model.path) model->path = xstrdup(source->model.path);
  if (source->model.fallback) model->fallback = xstrdup(source->model.fallback);
  if (source->model.part) {
    qa_held_weapon_part *part = xmalloc(sizeof *part);
    *part = *source->model.part;
    const char **digests = part->digests.count ? xcalloc(part->digests.count, sizeof *digests) : NULL;
    for (size_t i = 0; i < part->digests.count; i++) digests[i] = xstrdup(source->model.part->digests.values[i]);
    part->digests.values = digests;
    part->vertices.values = copy_array(source->model.part->vertices.values, part->vertices.count, sizeof(double));
    model->part = part;
  }
  return out;
}
static void own_model(qa_app_simulation_presentation *model) {
  model->held_weapon = clone_held(model->held_weapon);
  if (model->flare.rim_color) model->flare.rim_color = copy_array(model->flare.rim_color, 1, sizeof *model->flare.rim_color);
  if (model->indexed_skin.name) model->indexed_skin.name = xstrdup(model->indexed_skin.name);
  model->indexed_skin.pixels.bytes = copy_array(model->indexed_skin.pixels.bytes, model->indexed_skin.pixels.length, 1);
  model->model_attachments = copy_array(model->model_attachments, model->model_attachment_count, sizeof *model->model_attachments);
  model->model_anchor = copy_array(model->model_anchor, model->model_anchor ? 1 : 0, sizeof *model->model_anchor);
  model->q3_grapple_cable = copy_array(model->q3_grapple_cable, model->q3_grapple_cable ? 1 : 0,
      sizeof *model->q3_grapple_cable);
}
static qa_app_sim_player_ui own_ui(qa_app_sim_player_ui ui) {
  ui.native_inventory = clone_value(ui.native_inventory);
  ui.inventory = copy_array(ui.inventory, ui.inventory_count, sizeof *ui.inventory);
  ui.powerups = copy_array(ui.powerups, ui.powerup_count, sizeof *ui.powerups);
  ui.items = copy_array(ui.items, ui.item_count, sizeof *ui.items);
  ui.weapon_status = copy_array(ui.weapon_status, ui.weapon_status ? 1 : 0, sizeof *ui.weapon_status);
  return ui;
}
static void set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (!child || !qa_save_value_set(object, key, child)) qa_range_error("Out of memory");
}
static qa_save_value *number(double value) { return qa_save_value_number(value); }
static qa_save_value *text(const char *value) { return qa_save_value_string(value); }
static qa_save_value *actor_node(qa_actor_id actor) {
  qa_save_value *out = qa_app_unified_wire_actor(actor);
  if (!out || !qa_save_value_attach(out, &actor, sizeof actor)) qa_range_error("Out of memory");
  return out;
}
static qa_save_value *client_node(qa_client_id client) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("Out of memory");
  set(out, "slot", number((double)client.slot));
  set(out, "generation", number((double)client.generation));
  if (!qa_save_value_attach(out, &client, sizeof client)) qa_range_error("Out of memory");
  return out;
}
static qa_save_value *seat_node(qa_seat_id seat) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("Out of memory");
  set(out, "index", number((double)seat.index));
  if (!qa_save_value_attach(out, &seat, sizeof seat)) qa_range_error("Out of memory");
  return out;
}
static qa_save_value *vector_node(qa_vec3 v) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("Out of memory");
  set(out, "x", number(v.x)); set(out, "y", number(v.y)); set(out, "z", number(v.z));
  return out;
}
static qa_save_value *time_node(qa_source_time time) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("Out of memory");
  set(out, "kind", text(time.kind == QA_SOURCE_TIME_MILLISECONDS ? "milliseconds" : "seconds"));
  set(out, "value", number(time.value));
  return out;
}
/* The TS SimulationEvent object (sound and message payloads: frame() drops the others). */
static qa_save_value *simulation_event_tree(const qa_simulation_event *event, const char *resource) {
  qa_save_value *out = qa_save_value_object();
  if (!out) qa_range_error("Out of memory");
  set(out, "sequence", number(event->sequence));
  set(out, "time", time_node(event->time));
  qa_save_value *audience = qa_save_value_object();
  if (!audience) qa_range_error("Out of memory");
  set(audience, "kind", text(event->audience.kind));
  if (is(event->audience.kind, "seat")) set(audience, "seat", seat_node(event->audience.seat));
  if (is(event->audience.kind, "client")) set(audience, "client", client_node(event->audience.client));
  set(out, "audience", audience);
  qa_save_value *payload = qa_save_value_object();
  if (!payload) qa_range_error("Out of memory");
  const qa_simulation_event_payload *p = &event->payload;
  if (p->kind == QA_SIM_EVENT_SOUND) {
    set(payload, "kind", text("sound"));
    set(payload, "resource", text(resource));
    set(payload, "actor", p->actor ? actor_node(*p->actor) : qa_save_value_null());
    set(payload, "origin", vector_node(p->origin));
    set(payload, "channel", number(p->channel));
    set(payload, "volume", number(p->volume));
    set(payload, "attenuation", number(p->attenuation));
  } else {
    set(payload, "kind", text("message"));
    const qa_network_event *e = &p->event;
    qa_save_value *message = qa_save_value_object();
    if (!message) qa_range_error("Out of memory");
    switch (e->kind) {
      case QA_NETWORK_EVENT_PRINT:
        set(message, "kind", text("print")); set(message, "level", number(e->level)); set(message, "text", text(e->text)); break;
      case QA_NETWORK_EVENT_CENTER_PRINT: set(message, "kind", text("center-print")); set(message, "text", text(e->text)); break;
      case QA_NETWORK_EVENT_CONFIG_STRING:
        set(message, "kind", text("config-string")); set(message, "index", number(e->index)); set(message, "value", text(e->value)); break;
      case QA_NETWORK_EVENT_SOUND:
        set(message, "kind", text("sound")); set(message, "entityNumber", number(e->entity_number));
        set(message, "channel", number(e->channel)); set(message, "soundIndex", number(e->sound_index));
        set(message, "origin", e->origin ? vector_node(*e->origin) : qa_save_value_null());
        set(message, "volume", number(e->volume)); set(message, "attenuation", number(e->attenuation));
        set(message, "delaySeconds", number(e->delay_seconds)); break;
      case QA_NETWORK_EVENT_Q1_DAMAGE:
        set(message, "kind", text("q1-damage")); set(message, "armor", number(e->armor)); set(message, "blood", number(e->blood));
        set(message, "source", vector_node(e->source)); break;
      case QA_NETWORK_EVENT_Q1_PARTICLE:
        set(message, "kind", text("q1-particle")); set(message, "origin", vector_node(e->origin_value));
        set(message, "direction", vector_node(e->direction)); set(message, "count", number(e->count));
        set(message, "color", number(e->color)); break;
      case QA_NETWORK_EVENT_Q2_LAYOUT: set(message, "kind", text("q2-layout")); set(message, "program", text(e->program)); break;
      case QA_NETWORK_EVENT_Q2_INVENTORY: {
        set(message, "kind", text("q2-inventory"));
        qa_save_value *counts = qa_save_value_array();
        if (!counts) qa_range_error("Out of memory");
        for (size_t i = 0; i < e->counts.count; i++) if (!qa_save_value_push(counts, number(e->counts.values[i]))) qa_range_error("Out of memory");
        set(message, "counts", counts); break;
      }
      case QA_NETWORK_EVENT_Q2_MUZZLE_FLASH:
        set(message, "kind", text("q2-muzzle-flash")); set(message, "entityNumber", number(e->entity_number));
        set(message, "flash", number(e->flash)); set(message, "monster", qa_save_value_boolean(e->monster)); break;
      case QA_NETWORK_EVENT_Q3_SERVER_COMMAND:
        set(message, "kind", text("q3-server-command")); set(message, "sequence", number(e->sequence)); set(message, "text", text(e->text)); break;
      case QA_NETWORK_EVENT_COMMAND_TEXT: case QA_NETWORK_EVENT_DISCONNECT:
        qa_range_error("Unified frame cannot carry a private network event");
    }
    set(payload, "event", message);
    if (p->has_source_presentation_sequence) set(payload, "sourcePresentationSequence", number(p->source_presentation_sequence));
  }
  set(out, "payload", payload);
  return out;
}

static qa_app_unified_presentation_frame_t host_frame(void *ctx, qa_app_unified_player player, const void *output_value,
    double epoch, double acknowledged_input) {
  unified_host *h = ctx;
  require_player(h, player);
  const qa_app_sim_network_simulation *s = h->simulation;
  const qa_simulation_output *output = output_value;
  qa_app_unified_presentation_frame_t frame;
  memset(&frame, 0, sizeof frame);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_app_unified_presentation_frame_free, &frame);
  frame.epoch = epoch;
  frame.acknowledged_input = acknowledged_input;
  frame.events = output->event_count ? xcalloc(output->event_count, sizeof *frame.events) : NULL;
  for (size_t i = 0; i < output->event_count; i++) {
    const qa_simulation_event *event = &output->events[i];
    if (!permitted(player, event)) continue;
    char *resource = NULL;
    if (event->payload.kind == QA_SIM_EVENT_SOUND) {
      qa_resolved_resource_reference value = resource_of(h, event->payload.resource);
      resource = qa_app_unified_resource_id_from_reference(&value);
      if (!resource) qa_range_error("Out of memory");
    }
    frame.events[frame.event_count++] = simulation_event_tree(event, resource);
    free(resource);
  }
  frame.prediction = qa_app_unified_project_prediction(&s->prediction, player.actor, acknowledged_input);
  frame.output.snapshot = copy_snapshot(&output->snapshot, player.actor);
  size_t model_count = 0;
  const qa_app_simulation_presentation *models = s->presentations(s->ctx, &model_count);
  frame.models = model_count ? xcalloc(model_count, sizeof *frame.models) : NULL;
  qa_app_sim_unified_model_presentations(player.actor, models, model_count, frame.models);
  for (size_t i = 0; i < model_count; i++) { own_model(&frame.models[i]); frame.model_count = i + 1; }
  size_t character_count = 0, text_count = 0;
  const qa_q3_character_view *characters = s->character_views(s->ctx, &character_count);
  frame.characters = copy_array(characters, character_count, sizeof *characters);
  frame.character_count = character_count;
  const qa_world_text *world = s->world_text(s->ctx, &text_count);
  frame.world_text = copy_array(world, text_count, sizeof *world);
  frame.world_text_count = text_count;
  frame.player_actor = player.actor;
  frame.player_view = s->player_view(s->ctx, player.actor);
  frame.player_ui = own_ui(s->player_ui(s->ctx, player.actor));
  qa_cleanup_pop(&guard);
  return frame;
}

static void reset_events(unified_host *h) {
  for (size_t i = 0; i < h->event_count; i++) qa_save_value_free(h->events[i]);
  free(h->events); h->events = NULL; h->event_count = 0;
}
/* `{...event, event: {...event.event, client: -1}}` and the q1-composition userinfo strip. */
static qa_save_value *project_event(const qa_save_value *event) {
  qa_save_value *out = clone_value(event);
  /* The checkpoint clone drops identity attachments; restore them from the source. */
  const char *kind = kind_of(event);
  const qa_save_value *inner = get(event, "event");
  if (is(kind, "q3-source") && is(kind_of(inner), "server-command")) {
    qa_save_value *copy = (qa_save_value *)get(out, "event");
    for (size_t i = 0; i < copy->entry_count; i++)
      if (is(copy->entries[i].key, "client")) { qa_save_value_free(copy->entries[i].value); copy->entries[i].value = number(-1); }
  } else if (is(kind, "q1-composition") && is(kind_of(inner), "client")) {
    qa_save_value *copy = (qa_save_value *)get(out, "event");
    qa_save_value *client = (qa_save_value *)get(copy, "client");
    for (size_t i = 0; client != NULL && i < client->entry_count; i++)
      if (is(client->entries[i].key, "userinfo")) {
        qa_save_value_free(client->entries[i].value);
        client->entries[i].value = qa_save_value_array();
        if (!client->entries[i].value) qa_range_error("Out of memory");
      }
  }
  return out;
}
/* Copies attachments from `source` onto the structurally equal `copy`. */
static void restore_attachments(qa_save_value *copy, const qa_save_value *source) {
  if (copy == NULL || source == NULL) return;
  if (source->attachment && !copy->attachment && !qa_save_value_attach(copy, source->attachment, source->attachment_length))
    qa_range_error("Out of memory");
  if (source->kind == QA_SAVE_OBJECT && copy->kind == QA_SAVE_OBJECT)
    for (size_t i = 0; i < copy->entry_count; i++)
      restore_attachments(copy->entries[i].value, qa_save_value_get(source, copy->entries[i].key));
  if (source->kind == QA_SAVE_ARRAY && copy->kind == QA_SAVE_ARRAY)
    for (size_t i = 0; i < copy->item_count && i < source->item_count; i++) restore_attachments(copy->items[i], source->items[i]);
}
static qa_save_value *const *presentation_events(unified_host *h, qa_app_unified_player player,
    const qa_save_value *const *events, size_t count, size_t *out_count) {
  require_player(h, player);
  reset_events(h);
  h->events = count ? xcalloc(count, sizeof *h->events) : NULL;
  for (size_t i = 0; i < count; i++) {
    if (!qa_app_sim_unified_presentation_for(player.actor, player.client, events[i])) continue;
    qa_save_value *copy = project_event(events[i]);
    restore_attachments(copy, events[i]);
    h->events[h->event_count++] = copy;
  }
  *out_count = h->event_count;
  return h->events;
}
/* The application publishes its SimulationPresentationEvents (application_step.c network_publish): each
 * becomes the TS event object (unified_event_values.h) before the audience filter and projection. */
typedef struct typed_events { qa_save_value **trees; size_t count; } typed_events;
static void typed_events_free(void *arg) {
  typed_events *typed = arg;
  for (size_t i = 0; i < typed->count; i++) qa_save_value_free(typed->trees[i]);
  free(typed->trees);
  typed->trees = NULL; typed->count = 0;
}
static qa_save_value *const *host_presentation_events(void *ctx, qa_app_unified_player player,
    const void *const *events, size_t count, size_t *out_count) {
  typed_events typed = {count ? xcalloc(count, sizeof *typed.trees) : NULL, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, typed_events_free, &typed);
  for (size_t i = 0; i < count; i++)
    typed.trees[typed.count++] = qa_app_unified_presentation_event_tree(events[i]);
  qa_save_value *const *result =
      presentation_events(ctx, player, (const qa_save_value *const *)typed.trees, typed.count, out_count);
  qa_cleanup_pop(&guard);
  typed_events_free(&typed);
  return result;
}
static qa_save_value *const *host_initial_presentation(void *ctx, qa_app_unified_player player, size_t *out_count) {
  unified_host *h = ctx;
  size_t count = 0;
  qa_save_value *const *events = h->simulation->persistent_presentation(h->simulation->ctx, &count);
  return presentation_events(h, player, (const qa_save_value *const *)events, count, out_count);
}

static const qa_app_unified_component_publication *host_components(void *ctx, qa_app_unified_player player, size_t *out_count) {
  unified_host *h = ctx;
  require_player(h, player);
  free(h->publications); h->publications = NULL; h->publication_count = 0;
  size_t count = 0;
  const qa_app_sim_unified_mod_source *sources = h->options.mod_presentation_sources
      ? h->options.mod_presentation_sources(h->options.mod_ctx, &count) : NULL;
  h->publications = count ? xcalloc(count, sizeof *h->publications) : NULL;
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_unified_mod_source *source = &sources[i];
    source->assert_current(source->ctx);
    qa_app_unified_component_publication publication;
    memset(&publication, 0, sizeof publication);
    if (!source->context(source->ctx, player.actor, &publication)) continue;
    const qa_qvm_scene_context *scene = NULL;
    bool scene_runtime = is(source->runtime, "qvm-scene");
    if (scene_runtime && source->scene != NULL && !source->scene(source->ctx, &scene)) scene = NULL;
    if (scene_runtime && scene == NULL) qa_raise("Error", 0, 0, "Component scene publication is unavailable");
    publication.identity.owner = source->owner;
    publication.identity.identity = source->identity;
    publication.identity.identity_wire = source->identity_wire;
    publication.identity.generation = source->generation(source->ctx);
    publication.identity.abi = source->abi_profile;
    publication.identity.runtime = source->runtime;
    publication.viewer = player.actor;
    publication.bindings = source->bindings(source->ctx, &publication.binding_count);
    if (scene != NULL)
      publication.scene = source->select_scene(source->ctx, scene, player.actor, h->options.content->world.kind == QA_DECODED_WORLD_Q1
          ? (double)h->options.content->world.as.q1->leaf_count : h->options.content->world.kind == QA_DECODED_WORLD_Q2
          ? (double)h->options.content->world.as.q2->leaf_count : (double)h->options.content->world.as.q3->leaf_count);
    h->publications[h->publication_count++] = publication;
  }
  *out_count = h->publication_count;
  return h->publications;
}
static const qa_app_unified_native_publication *host_native_components(void *ctx, qa_app_unified_player player, size_t *out_count) {
  unified_host *h = ctx;
  require_player(h, player);
  free(h->native); h->native = NULL; h->native_count = 0;
  size_t count = 0;
  const qa_app_sim_unified_mod_source *sources = h->options.mod_client_presentation_sources
      ? h->options.mod_client_presentation_sources(h->options.mod_ctx, &count) : NULL;
  h->native = count ? xcalloc(count, sizeof *h->native) : NULL;
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_unified_mod_source *source = &sources[i];
    source->assert_current(source->ctx);
    qa_mod_client_presentation_frame frame;
    memset(&frame, 0, sizeof frame);
    if (!source->frame(source->ctx, player.actor, &frame) || frame.kind != QA_MOD_CLIENT_PRESENTATION_NATIVE) continue;
    qa_app_unified_native_publication publication;
    memset(&publication, 0, sizeof publication);
    publication.identity.owner = source->owner;
    publication.identity.identity = source->identity;
    publication.identity.identity_wire = source->identity_wire;
    publication.identity.generation = source->generation(source->ctx);
    publication.viewer = player.actor;
    publication.frame = frame;
    h->native[h->native_count++] = publication;
  }
  *out_count = h->native_count;
  return h->native;
}
static bool host_component_command(void *ctx, qa_app_unified_player player, qa_presentation_owner owner, double generation,
    const char *const *args, size_t count) {
  unified_host *h = ctx;
  require_player(h, player);
  size_t source_count = 0;
  const qa_app_sim_unified_mod_source *sources = h->options.mod_presentation_sources
      ? h->options.mod_presentation_sources(h->options.mod_ctx, &source_count) : NULL;
  const qa_app_sim_unified_mod_source *source = NULL;
  for (size_t i = 0; i < source_count && source == NULL; i++)
    if (qa_same_presentation_owner(&sources[i].owner, owner) && sources[i].generation(sources[i].ctx) == generation) source = &sources[i];
  if (source == NULL || !source->live(source->ctx, player.actor)) return false;
  qa_app_unified_component_publication context;
  memset(&context, 0, sizeof context);
  if (!source->context(source->ctx, player.actor, &context)) return false;
  source->assert_current(source->ctx);
  if (source->client_command == NULL) return false;
  source->client_command(source->ctx, player.actor, args, count);
  return true;
}

static bool host_session_matches(void *ctx, qa_session_id session) {
  unified_host *h = ctx;
  return session.token == h->simulation->session.token;
}
static qa_app_unified_actor_reference host_actor_reference(void *ctx, qa_actor_id actor) {
  (void)ctx; /* TS wireActor reads only the actor. */
  qa_app_unified_actor_reference reference = {(double)actor.slot, (double)actor.generation};
  return reference;
}

qa_app_unified_server_host qa_app_sim_create_unified_application_server_host(qa_app_sim_unified_host_options options) {
  const qa_app_sim_network_simulation *s = options.simulation;
  qa_comp_q1_source_composition *q1 = s->q1_source(s->ctx);
  qa_q2_product_runtime *q2 = s->q2_source(s->ctx);
  qa_app_q3_source_runtime *q3 = s->q3_source(s->ctx);
  if (q1 == NULL && q2 == NULL && q3 == NULL)
    qa_raise("Error", 0, 0, "Unified hosting currently requires a TypeScript Q1, Q2 or Q3 source game");
  unified_host *h = xcalloc(1, sizeof *h);
  h->options = options; h->simulation = s; h->q1 = q1; h->q2 = q2; h->q3 = q3;
  qa_app_unified_server_host host;
  memset(&host, 0, sizeof host);
  host.ctx = h;
  host.mode = s->options->mode;
  host.max_clients = s->options->max_clients;
  host.session = s->session;
  host.session_matches = host_session_matches;
  host.actor_reference = host_actor_reference;
  host.admit = host_admit;
  host.disconnect = host_disconnect;
  host.carried_player = host_carried_player;
  host.userinfo = host_userinfo;
  host.command = host_command;
  host.component_command = host_component_command;
  host.input = host_input;
  host.initial_presentation = host_initial_presentation;
  host.resources = host_resources;
  host.presentation_events = host_presentation_events;
  host.frame = host_frame;
  host.components = host_components;
  host.native_components = host_native_components;
  return host;
}

void qa_app_sim_unified_application_server_host_destroy(qa_app_unified_server_host host) {
  unified_host *h = host.ctx;
  if (h == NULL) return;
  for (size_t i = 0; i < h->slot_count; i++) forget_info(&h->slots[i]);
  free(h->slots);
  reset_events(h);
  reset_resources(h);
  free(h->publications);
  free(h->native);
  free(h);
}
