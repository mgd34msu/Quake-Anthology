/* Port of src/content/composition/q1/clients.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q1/clients.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include "qa/foundation/js.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy != NULL) memcpy(copy, text, length + 1);
  return copy;
}

/* JS Number(text) for userinfo values: blank -> 0, junk -> NaN. */
static double js_number(const char *text) {
  return text == NULL ? 0 : qa_js_string_to_number(text); /* Number(value ?? 0) */
}

static int64_t color_of(double parsed) {
  if (!isfinite(parsed)) return 0;
  double clamped = qa_js_max(0.0, qa_js_min(13.0, trunc(parsed)));
  return (int64_t)clamped;
}
static int64_t color(const char *value) { return color_of(js_number(value)); }

static const char *userinfo_get(const qa_comp_q1_source_client *client, const char *key) {
  for (size_t i = 0; i < client->userinfo_count; i++)
    if (strcmp(client->userinfo[i].key, key) == 0) return client->userinfo[i].value;
  return NULL;
}

static bool userinfo_set(qa_comp_q1_source_client *client, const char *key, const char *value) {
  for (size_t i = 0; i < client->userinfo_count; i++)
    if (strcmp(client->userinfo[i].key, key) == 0) {
      char *copy = dup_text(value);
      if (copy == NULL) return false;
      free((void *)client->userinfo[i].value);
      client->userinfo[i].value = copy;
      return true;
    }
  if (client->userinfo_count == client->userinfo_capacity) {
    size_t capacity = client->userinfo_capacity == 0 ? 4 : client->userinfo_capacity * 2;
    qa_comp_userinfo_entry *grown =
        (qa_comp_userinfo_entry *)realloc(client->userinfo, capacity * sizeof *grown);
    if (grown == NULL) return false;
    client->userinfo = grown;
    client->userinfo_capacity = capacity;
  }
  char *key_copy = dup_text(key), *value_copy = dup_text(value);
  if (key_copy == NULL || value_copy == NULL) { free(key_copy); free(value_copy); return false; }
  client->userinfo[client->userinfo_count++] = (qa_comp_userinfo_entry){key_copy, value_copy};
  return true;
}

static void client_clear_userinfo(qa_comp_q1_source_client *client) {
  for (size_t i = 0; i < client->userinfo_count; i++) {
    free((void *)client->userinfo[i].key);
    free((void *)client->userinfo[i].value);
  }
  client->userinfo_count = 0;
}

static bool client_init(qa_comp_q1_source_client *client, qa_owned_actor actor, int64_t slot,
                        const qa_comp_userinfo_entry *userinfo, size_t userinfo_count) {
  memset(client, 0, sizeof *client);
  client->actor = actor;
  client->slot = slot;
  client->respawn_requested_at = -1;
  for (size_t i = 0; i < userinfo_count; i++)
    if (!userinfo_set(client, userinfo[i].key, userinfo[i].value)) { client_clear_userinfo(client); free(client->userinfo); return false; }
  return true;
}

const char *qa_comp_q1_source_client_name(const qa_comp_q1_source_client *client) {
  const char *name = userinfo_get(client, "name");
  return name != NULL && name[0] != '\0' ? name : "unconnected";
}
int64_t qa_comp_q1_source_client_shirt(const qa_comp_q1_source_client *client) {
  return color(userinfo_get(client, "topcolor"));
}
int64_t qa_comp_q1_source_client_pants(const qa_comp_q1_source_client *client) {
  return color(userinfo_get(client, "bottomcolor"));
}
qa_comp_q1_client_snapshot qa_comp_q1_source_client_snapshot(const qa_comp_q1_source_client *client) {
  const qa_actor_id actor2 = client->actor.id;
  int64_t slot2 = client->slot;
  const char * name = qa_comp_q1_source_client_name(client);
  double frags2 = client->frags;
  int64_t shirt = qa_comp_q1_source_client_shirt(client);
  int64_t pants = qa_comp_q1_source_client_pants(client);
  return (qa_comp_q1_client_snapshot){actor2, slot2, name,
                                      frags2, shirt,
                                      pants, client->team, client->observer,
                                      client->no_target, client->userinfo, client->userinfo_count};
}

/* TS Map<OwnedActor, ...> key identity: the registry's owned handle. */
static bool same_owned(qa_owned_actor left, qa_owned_actor right) { return qa_same_owned_actor(left, right); }

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_comp_q1_source_clients *clients = (qa_comp_q1_source_clients *)ctx;
  for (size_t i = 0; i < clients->record_count; i++)
    if (same_owned(clients->records[i].actor, actor)) {
      client_clear_userinfo(&clients->records[i]);
      free(clients->records[i].userinfo);
      memmove(&clients->records[i], &clients->records[i + 1],
              (clients->record_count - i - 1) * sizeof *clients->records);
      clients->record_count--;
      return;
    }
}

void qa_comp_q1_source_clients_init(qa_comp_q1_source_clients *clients, qa_q1_services *game,
                                    qa_comp_q1_program program, const qa_comp_q1_services *services) {
  memset(clients, 0, sizeof *clients);
  clients->game = game;
  clients->program = program;
  clients->services = services;
  clients->release_listener = qa_session_actor_registry_on_release(game->host.actors, on_release, clients);
}

void qa_comp_q1_source_clients_destroy(qa_comp_q1_source_clients *clients) {
  if (clients->game != NULL)
    qa_session_actor_registry_off_release(clients->game->host.actors, clients->release_listener);
  for (size_t i = 0; i < clients->record_count; i++) {
    client_clear_userinfo(&clients->records[i]);
    free(clients->records[i].userinfo);
  }
  free(clients->records);
  memset(clients, 0, sizeof *clients);
}

qa_comp_q1_source_client *qa_comp_q1_source_clients_get(qa_comp_q1_source_clients *clients, qa_actor_id actor) {
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(clients->game->host.actors, actor, &owner)) return NULL;
  for (size_t i = 0; i < clients->record_count; i++)
    if (same_owned(clients->records[i].actor, owner)) return &clients->records[i];
  return NULL;
}

qa_comp_q1_source_client *qa_comp_q1_source_clients_require(qa_comp_q1_source_clients *clients, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(clients, actor);
  if (client == NULL) qa_range_error("Q1 source client is not admitted");
  return client;
}

/* TS String(team). The combat authority keeps the team string it is given
 * (TS strings are values), so team names are interned for the process. */
static const char *team_name(double team) {
  static struct interned_team { struct interned_team *next; char text[40]; } *interned = NULL;
  char text[40];
  (void)snprintf(text, sizeof text, "%s", QA_JS_NUMBER(team));
  for (const struct interned_team *entry = interned; entry != NULL; entry = entry->next)
    if (strcmp(entry->text, text) == 0) return entry->text;
  struct interned_team *entry = malloc(sizeof *entry);
  if (entry == NULL) qa_range_error("Out of memory");
  memcpy(entry->text, text, sizeof text);
  entry->next = interned;
  interned = entry;
  return entry->text;
}

static void apply_player_settings(qa_comp_q1_source_clients *clients, qa_comp_q1_source_client *client) {
  const char *team;
  if (clients->program == QA_COMP_Q1_CTF)
    team = client->team == 5 ? "red" : client->team == 14 ? "blue" : NULL;
  else if (client->team > 0)
    team = team_name(client->team);
  else
    team = NULL;
  qa_combat_trait_changes traits;
  memset(&traits, 0, sizeof traits);
  traits.has_team = true;
  traits.team = team;
  qa_gameplay_authority_set_traits(clients->game->host.combat, client->actor, traits);
  qa_q1_player_state *player = qa_q1_player(clients->game, client->actor.id);
  const char *auto_switch = userinfo_get(client, "qts_weapon_autoswitch");
  if (player != NULL && auto_switch != NULL)
    player->auto_switch = strcmp(auto_switch, "new") == 0 ? "new" : strcmp(auto_switch, "never") == 0 ? "never" : "always";
}

qa_comp_q1_source_client *qa_comp_q1_source_clients_attach(qa_comp_q1_source_clients *clients,
                                                           qa_owned_actor actor,
                                                           const qa_comp_q1_client_admission *admission) {
  qa_session_actor_registry_assert_owned(clients->game->host.actors, actor);
  for (size_t i = 0; i < clients->record_count; i++)
    if (same_owned(clients->records[i].actor, actor) || clients->records[i].slot == admission->slot)
      qa_range_error("Q1 source client slot is already admitted");
  double max_clients = clients->game->options.has_max_clients ? clients->game->options.max_clients : 1;
  if (admission->slot < 0 || (double)admission->slot >= max_clients)
    qa_range_error("Q1 source client slot is out of range");
  if (clients->record_count == clients->record_capacity) {
    size_t capacity = clients->record_capacity == 0 ? 4 : clients->record_capacity * 2;
    qa_comp_q1_source_client *grown =
        (qa_comp_q1_source_client *)realloc(clients->records, capacity * sizeof *grown);
    if (grown == NULL) return NULL;
    clients->records = grown;
    clients->record_capacity = capacity;
  }
  qa_comp_q1_source_client *client = &clients->records[clients->record_count];
  if (!client_init(client, actor, admission->slot, admission->userinfo, admission->userinfo_count)) return NULL;
  clients->record_count++;
  client->team = (double)qa_comp_q1_source_client_pants(client) + 1;
  apply_player_settings(clients, client);
  qa_comp_q1_source_clients_publish(clients, client);
  return client;
}

bool qa_comp_q1_source_clients_update(qa_comp_q1_source_clients *clients, qa_actor_id actor,
                                      const qa_comp_userinfo_entry *userinfo, size_t userinfo_count) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(clients, actor);
  int64_t previous = qa_comp_q1_source_client_pants(client);
  client_clear_userinfo(client);
  for (size_t i = 0; i < userinfo_count; i++)
    if (!userinfo_set(client, userinfo[i].key, userinfo[i].value)) return false;
  if (qa_comp_q1_source_client_pants(client) != previous)
    client->team = (double)qa_comp_q1_source_client_pants(client) + 1;
  apply_player_settings(clients, client);
  qa_comp_q1_source_clients_publish(clients, client);
  return true;
}

bool qa_comp_q1_source_clients_colors(qa_comp_q1_source_clients *clients, qa_actor_id actor,
                                      double shirt, double pants) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(clients, actor);
  char shirt_text[24], pants_text[24];
  /* TS: String(color(String(shirt))) — the string round-trip preserves the number. */
  (void)snprintf(shirt_text, sizeof shirt_text, "%lld", (long long)color_of(shirt));
  (void)snprintf(pants_text, sizeof pants_text, "%lld", (long long)color_of(pants));
  if (!userinfo_set(client, "topcolor", shirt_text) || !userinfo_set(client, "bottomcolor", pants_text))
    return false;
  client->team = (double)qa_comp_q1_source_client_pants(client) + 1;
  apply_player_settings(clients, client);
  qa_comp_q1_source_clients_publish(clients, client);
  return true;
}

double qa_comp_q1_source_clients_team_color(qa_comp_q1_source_clients *clients, qa_actor_id actor) {
  return qa_comp_q1_source_clients_require(clients, actor)->team;
}

void qa_comp_q1_source_clients_spawned(qa_comp_q1_source_clients *clients, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(clients, actor);
  client->god_mode = false;
  if (strcmp(clients->game->options.edition, "rerelease") == 0 && clients->program != QA_COMP_Q1_CTF) {
    if (clients->game->options.coop) client->team = 1;
    else if (clients->program == QA_COMP_Q1_ID1) client->team = -1;
  }
  apply_player_settings(clients, client);
  qa_comp_q1_source_clients_publish(clients, client);
}

void qa_comp_q1_source_clients_add_score(qa_comp_q1_source_clients *clients, qa_actor_id actor, double delta) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(clients, actor);
  client->frags = (double)(float)(client->frags + delta); /* Math.fround staging */
  qa_comp_q1_source_clients_publish(clients, client);
}

void qa_comp_q1_source_clients_set_observer(qa_comp_q1_source_clients *clients, qa_actor_id actor, bool observer) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(clients, actor);
  client->observer = observer;
  clients->services->set_observer(clients->services->ctx, actor, observer);
  qa_comp_q1_source_clients_publish(clients, client);
}

void qa_comp_q1_source_clients_publish(qa_comp_q1_source_clients *clients, const qa_comp_q1_source_client *client) {
  qa_comp_q1_event event = {0};
  event.kind = QA_COMP_Q1_EVENT_CLIENT;
  event.client = qa_comp_q1_source_client_snapshot(client);
  clients->services->emit(clients->services->ctx, &event);
}

static bool set_number(qa_save_value *object, const char *key, double value) {
  qa_save_value *child = qa_save_value_number(value);
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_boolean(qa_save_value *object, const char *key, bool value) {
  qa_save_value *child = qa_save_value_boolean(value);
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_string(qa_save_value *object, const char *key, const char *value) {
  qa_save_value *child = qa_save_value_string(value);
  return child != NULL && qa_save_value_set(object, key, child);
}

uint8_t *qa_comp_q1_source_clients_capture(qa_comp_q1_source_clients *clients, size_t *out_length) {
  qa_save_value *root = qa_save_value_object();
  qa_save_value *list = qa_save_value_array();
  bool ok = root != NULL && list != NULL;
  if (ok) ok = set_number(root, "version", 1) && set_string(root, "program", qa_comp_q1_program_name(clients->program)) &&
               set_number(root, "redCaptures", clients->red_captures) &&
               set_number(root, "blueCaptures", clients->blue_captures);
  for (size_t i = 0; ok && i < clients->record_count; i++) {
    const qa_comp_q1_source_client *client = &clients->records[i];
    qa_save_value *saved = qa_save_value_object(), *id = qa_save_value_object(), *info = qa_save_value_array();
    ok = saved != NULL && id != NULL && info != NULL;
    if (ok) ok = set_number(id, "slot", (double)client->actor.id.slot) &&
                 set_number(id, "generation", (double)client->actor.id.generation) &&
                 qa_save_value_set(saved, "actor", id) && set_number(saved, "slot", (double)client->slot);
    else { qa_save_value_free(saved); qa_save_value_free(id); qa_save_value_free(info); break; }
    for (size_t j = 0; ok && j < client->userinfo_count; j++) {
      qa_save_value *entry = qa_save_value_object();
      ok = entry != NULL && set_string(entry, "key", client->userinfo[j].key) &&
           set_string(entry, "value", client->userinfo[j].value) && qa_save_value_push(info, entry);
      if (!ok) qa_save_value_free(entry);
    }
    if (ok) ok = qa_save_value_set(saved, "userinfo", info) && set_number(saved, "frags", client->frags) &&
                 set_number(saved, "team", client->team) && set_boolean(saved, "observer", client->observer) &&
                 set_boolean(saved, "noTarget", client->no_target) && set_boolean(saved, "godMode", client->god_mode) &&
                 set_number(saved, "impulse", client->impulse) && set_boolean(saved, "use", client->use) &&
                 set_boolean(saved, "deathRecorded", client->death_recorded) &&
                 set_number(saved, "respawnRequestedAt", client->respawn_requested_at) &&
                 qa_save_value_push(list, saved);
    if (!ok) { bool attached = saved != NULL && qa_save_value_get(saved, "userinfo") != NULL; qa_save_value_free(saved); if (!attached) qa_save_value_free(info); }
  }
  if (ok) ok = qa_save_value_set(root, "clients", list);
  uint8_t *bytes = NULL;
  if (ok) bytes = qa_encode_checkpoint_value(root, out_length);
  else qa_save_value_free(list);
  qa_save_value_free(root);
  return bytes;
}

static double field_number(qa_save_reader *reader, const char *name, int mode) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_range_error("Out of memory");
  double value = mode == 0 ? qa_save_reader_integer_min(&field, 0) : qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static bool field_boolean(qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_range_error("Out of memory");
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}

bool qa_comp_q1_source_clients_restore(qa_comp_q1_source_clients *clients, const uint8_t *bytes, size_t length) {
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  if (decoded == NULL) return false;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q1:source-clients")) { qa_save_value_free(decoded); return false; }
  qa_save_reader version, program, list;
  if (!qa_save_reader_field(&reader, "version", &version) || !qa_save_reader_field(&reader, "program", &program))
    qa_range_error("Out of memory");
  (void)qa_save_reader_literal_number(&version, 1);
  (void)qa_save_reader_literal_string(&program, qa_comp_q1_program_name(clients->program));
  qa_save_reader_destroy(&version);
  qa_save_reader_destroy(&program);
  clients->red_captures = field_number(&reader, "redCaptures", 0);
  clients->blue_captures = field_number(&reader, "blueCaptures", 0);
  for (size_t i = 0; i < clients->record_count; i++) {
    client_clear_userinfo(&clients->records[i]);
    free(clients->records[i].userinfo);
  }
  clients->record_count = 0;
  if (!qa_save_reader_field(&reader, "clients", &list)) qa_range_error("Out of memory");
  bool ok = true;
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; ok && i < count; i++) {
    qa_save_reader saved, id;
    if (!qa_save_reader_item(&list, i, &saved) || !qa_save_reader_field(&saved, "actor", &id))
      qa_range_error("Out of memory");
    int64_t slot = (int64_t)field_number(&id, "slot", 0);
    int64_t generation = (int64_t)field_number(&id, "generation", 0);
    qa_owned_actor actor;
    const qa_saved_actor_id saved_id = {(double)slot, (double)generation};
    if (!qa_session_actor_registry_resolve_saved(clients->game->host.actors, saved_id, &actor))
      qa_save_reader_fail(&id, "missing source client actor");
    qa_save_reader_destroy(&id);
    int64_t client_slot = (int64_t)field_number(&saved, "slot", 0);
    for (size_t j = 0; j < clients->record_count; j++)
      if (clients->records[j].slot == client_slot) qa_save_reader_fail(&saved, "duplicate source client slot");
    if (clients->record_count == clients->record_capacity) {
      size_t capacity = clients->record_capacity == 0 ? 4 : clients->record_capacity * 2;
      qa_comp_q1_source_client *grown =
          (qa_comp_q1_source_client *)realloc(clients->records, capacity * sizeof *grown);
      if (grown == NULL) { ok = false; qa_save_reader_destroy(&saved); break; }
      clients->records = grown;
      clients->record_capacity = capacity;
    }
    qa_comp_q1_source_client *client = &clients->records[clients->record_count];
    memset(client, 0, sizeof *client);
    client->actor = actor;
    client->slot = client_slot;
    client->respawn_requested_at = -1;
    qa_save_reader info;
    if (!qa_save_reader_field(&saved, "userinfo", &info)) qa_range_error("Out of memory");
    size_t info_count = qa_save_reader_list_count(&info);
    for (size_t j = 0; ok && j < info_count; j++) {
      qa_save_reader entry, key, value;
      if (!qa_save_reader_item(&info, j, &entry) || !qa_save_reader_field(&entry, "key", &key) ||
          !qa_save_reader_field(&entry, "value", &value))
        qa_range_error("Out of memory");
      ok = userinfo_set(client, qa_save_reader_string(&key), qa_save_reader_string(&value));
      qa_save_reader_destroy(&key);
      qa_save_reader_destroy(&value);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&info);
    if (!ok) { client_clear_userinfo(client); free(client->userinfo); qa_save_reader_destroy(&saved); break; }
    clients->record_count++;
    client->frags = field_number(&saved, "frags", 1);
    client->observer = field_boolean(&saved, "observer");
    client->impulse = field_number(&saved, "impulse", 0);
    client->use = field_boolean(&saved, "use");
    client->death_recorded = field_boolean(&saved, "deathRecorded");
    client->respawn_requested_at = field_number(&saved, "respawnRequestedAt", 1);
    client->no_target = field_boolean(&saved, "noTarget");
    qa_save_reader god_mode;
    if (!qa_save_reader_field(&saved, "godMode", &god_mode)) qa_range_error("Out of memory");
    client->god_mode = qa_save_reader_is_undefined(&god_mode) ? false : qa_save_reader_boolean(&god_mode);
    qa_save_reader_destroy(&god_mode);
    client->team = field_number(&saved, "team", 1);
    qa_save_reader_destroy(&saved);
  }
  qa_save_reader_destroy(&list);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(decoded);
  return ok;
}
