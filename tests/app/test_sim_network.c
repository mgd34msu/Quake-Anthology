/* C checks for src/app/bootstrap/simulation/network*.ts: the TS tests that import these
 * modules (tests/app/unified-host.test.ts, tests/network/unified-events.test.ts,
 * tests/network/unified/frame.test.ts, tests/first-playable/held-weapon.test.ts,
 * tests/network/q2/rerelease-native.test.ts) plus host-level checks over sentinel
 * contexts. GPL-2.0-or-later. */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/simulation/network_q2_rerelease_native.h"
#include "qa/app/bootstrap/simulation/network_unified.h"
#include "qa/app/bootstrap/simulation/network_q1.h"
#include "qa/app/bootstrap/simulation/network_qw.h"
#include "qa/app/bootstrap/network/unified_frame_values.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/server_write.h"
#include "qa/network/q2/connectionless.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/common/session.h"

static int checks, failures;
#define CHECK(...) do { checks++; if (!(__VA_ARGS__)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

/* ---- tree builders (unified-event-codec shape) ------------------------------------- */
static qa_save_value *obj(void) { qa_save_value *v = qa_save_value_object(); if (!v) abort(); return v; }
static void put(qa_save_value *o, const char *key, qa_save_value *v) { if (!v || !qa_save_value_set(o, key, v)) abort(); }
static qa_save_value *str(const char *text) { return qa_save_value_string(text); }
static qa_save_value *num(double value) { return qa_save_value_number(value); }
static qa_save_value *actor_node(qa_actor_id actor) {
  qa_save_value *v = qa_app_unified_wire_actor(actor);
  if (!v || !qa_save_value_attach(v, &actor, sizeof actor)) abort();
  return v;
}
static qa_save_value *vec(double x, double y, double z) { qa_save_value *v = obj(); put(v, "x", num(x)); put(v, "y", num(y)); put(v, "z", num(z)); return v; }
/* { ...base, kind, event } */
static qa_save_value *presentation(const char *kind, qa_save_value *event) {
  qa_save_value *v = obj();
  put(v, "sequence", num(0)); put(v, "content", str("q2:classic:baseq2:fixture")); put(v, "seconds", num(0));
  put(v, "kind", str(kind)); put(v, "event", event);
  return v;
}
static qa_save_value *event_kind(const char *kind) { qa_save_value *v = obj(); put(v, "kind", str(kind)); return v; }

static void test_unified_audience(void) {
  /* tests/app/unified-host.test.ts */
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "unified-audience"));
  qa_actor_id actor = qa_identity_owner_actor(&owner, 1, 0), other = qa_identity_owner_actor(&owner, 2, 0);
  qa_client_id client = qa_identity_owner_client(&owner, 0, 0);
  qa_save_value *cases[15];
  bool expected[15];
  size_t n = 0;
  qa_save_value *e;
  e = event_kind("inventory"); put(e, "actor", actor_node(other)); put(e, "entries", qa_save_value_array());
  cases[n] = presentation("q2-player", e); expected[n++] = false;
  e = event_kind("inventory"); put(e, "actor", actor_node(actor)); put(e, "entries", qa_save_value_array());
  cases[n] = presentation("q2-player", e); expected[n++] = true;
  qa_save_value *inner = event_kind("ir"); put(inner, "actor", actor_node(other)); put(inner, "until", num(5));
  e = event_kind("missionpack-player"); put(e, "event", inner);
  cases[n] = presentation("q2-composition", e); expected[n++] = false;
  inner = event_kind("grapple-cable"); put(inner, "actor", actor_node(other)); put(inner, "start", vec(0, 0, 0));
  put(inner, "end", vec(1, 0, 0)); put(inner, "offset", vec(0, 0, 0));
  e = event_kind("ctf"); put(e, "event", inner);
  cases[n] = presentation("q2-composition", e); expected[n++] = true;
  e = event_kind("kick"); put(e, "actor", actor_node(actor));
  cases[n] = presentation("q2-composition", e); expected[n++] = false;
  cases[n] = presentation("q2-rerelease", event_kind("autosave")); expected[n++] = false;
  e = event_kind("server-command"); put(e, "text", str("map next"));
  cases[n] = presentation("q1", e); expected[n++] = false;
  inner = event_kind("punch-angle"); put(inner, "player", actor_node(other)); put(inner, "angles", vec(1, 0, 0));
  e = event_kind("addon"); put(e, "event", inner);
  cases[n] = presentation("q1-composition", e); expected[n++] = false;
  e = event_kind("console-command"); put(e, "execution", str("now")); put(e, "text", str("quit"));
  cases[n] = presentation("q3-source", e); expected[n++] = false;
  e = event_kind("server-command"); put(e, "client", num(1)); put(e, "text", str("cp private"));
  cases[n] = presentation("q3-source", e); expected[n++] = false;
  e = event_kind("server-command"); put(e, "client", num(-1)); put(e, "text", str("cp public"));
  cases[n] = presentation("q3-source", e); expected[n++] = true;
  /* Each denial above has an owner case: the viewer keeps events addressed to its own slot or actor. */
  e = event_kind("server-command"); put(e, "client", num(0)); put(e, "text", str("cp own"));
  cases[n] = presentation("q3-source", e); expected[n++] = true;
  inner = event_kind("ir"); put(inner, "actor", actor_node(actor)); put(inner, "until", num(5));
  e = event_kind("missionpack-player"); put(e, "event", inner);
  cases[n] = presentation("q2-composition", e); expected[n++] = true;
  inner = event_kind("punch-angle"); put(inner, "player", actor_node(actor)); put(inner, "angles", vec(1, 0, 0));
  e = event_kind("addon"); put(e, "event", inner);
  cases[n] = presentation("q1-composition", e); expected[n++] = true;
  for (size_t i = 0; i < n; i++) {
    bool got = qa_app_sim_unified_presentation_for(actor, client, cases[i]);
    if (got != expected[i]) fprintf(stderr, "audience case %zu: got %d\n", i, got);
    CHECK(got == expected[i]);
    qa_save_value_free(cases[i]);
  }
  /* tests/network/unified-events.test.ts: a decoded music pause keeps its local recipient. */
  qa_save_value *pause = event_kind("pause");
  put(pause, "paused", qa_save_value_boolean(true));
  qa_save_value *music = presentation("music", pause);
  put(music, "recipient", actor_node(actor));
  CHECK(qa_app_sim_unified_presentation_for(actor, client, music));
  CHECK(!qa_app_sim_unified_presentation_for(qa_identity_owner_actor(&owner, 99, 0), client, music));
  qa_save_value_free(music);
  qa_identity_owner_destroy(&owner);
}

typedef struct audience_scope { qa_actor_id actor; qa_client_id client; const qa_save_value *value; bool result; } audience_scope;
static void audience_body(void *ctx) {
  audience_scope *scope = ctx;
  scope->result = qa_app_sim_unified_presentation_for(scope->actor, scope->client, scope->value);
}
static void test_unified_audience_type_error(void) {
  /* own(undefined): TS reads `.equals` of undefined and throws TypeError. */
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "unified-type-error"));
  audience_scope scope = {qa_identity_owner_actor(&owner, 1, 0), qa_identity_owner_client(&owner, 0, 0), NULL, false};
  qa_save_value *value = presentation("q2", event_kind("pickup"));
  scope.value = value;
  qa_caught_error caught;
  CHECK(!qa_try(audience_body, &scope, &caught));
  CHECK(strcmp(caught.name, "TypeError") == 0);
  qa_save_value_free(value);
  /* An unknown kind falls out of the TS switch (undefined): not delivered. */
  value = presentation("not-a-kind", event_kind("x"));
  scope.value = value;
  CHECK(qa_try(audience_body, &scope, &caught) && !scope.result);
  qa_save_value_free(value);
  qa_identity_owner_destroy(&owner);
}

static void test_unified_models(void) {
  /* tests/network/unified/frame.test.ts and tests/first-playable/held-weapon.test.ts */
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "server"));
  qa_actor_id id = qa_identity_owner_actor(&owner, 4, 2), other = qa_identity_owner_actor(&owner, 5, 3);
  qa_app_simulation_presentation own, foreign;
  memset(&own, 0, sizeof own);
  own.actor = id; own.content = "q3:base:baseq3:1"; own.family = QA_GAME_FAMILY_Q3; own.path = "models/own.md3";
  own.view_weapon = true; own.visible = true; own.frame = 3; own.scale = 1;
  foreign = own;
  foreign.actor = other; foreign.path = "models/foreign.md3"; foreign.weapon_item = "mod:custom";
  static const qa_held_weapon_declaration held_none;
  foreign.held_weapon = &held_none;
  foreign.frame = 7; foreign.old_frame = 6; foreign.skin = 2; foreign.effects = 1; foreign.render_flags = 4;
  foreign.origin = (qa_vec3){1, 2, 3}; foreign.angles = (qa_vec3){4, 5, 6}; foreign.scale = 2;
  foreign.has_previous_origin = true; foreign.previous_origin = (qa_vec3){1, 2, 3};
  foreign.has_q3_weapon = true; foreign.q3_weapon_time_ms = 1000; foreign.q3_weapon_torso_animation = 7;
  foreign.has_q3_weapon_last_fire = true; foreign.q3_weapon_last_fire_ms = 995; foreign.q3_weapon_firing = true;
  foreign.q3_weapon_horizontal_speed = 200; foreign.q3_weapon_bob_cycle = 35; foreign.q3_weapon_weapon = 2;
  qa_app_simulation_presentation renderer = foreign;
  renderer.actor = qa_identity_owner_actor(&owner, 6, 0); renderer.render_owner = "source-client";
  qa_app_simulation_presentation models[3] = {own, foreign, renderer}, out[3];
  qa_app_sim_unified_model_presentations(id, models, 3, out);
  CHECK(memcmp(&out[0], &own, sizeof own) == 0);
  const qa_app_simulation_presentation *held = &out[1];
  CHECK(qa_same_actor(held->actor, other)); CHECK(strcmp(held->content, foreign.content) == 0);
  CHECK(strcmp(held->path, foreign.path) == 0); CHECK(strcmp(held->weapon_item, "mod:custom") == 0);
  CHECK(held->held_weapon == &held_none);
  CHECK(!held->visible); CHECK(held->view_weapon); CHECK(held->frame == 0 && held->old_frame == 0 && held->skin == 0);
  CHECK(held->effects == 0 && held->render_flags == 0 && held->scale == 1);
  CHECK(held->origin.x == 0 && held->origin.y == 0 && held->origin.z == 0);
  CHECK(held->angles.x == 0 && held->angles.y == 0 && held->angles.z == 0);
  CHECK(!held->has_previous_origin && held->model_anchor == NULL && held->model_attachments == NULL && !held->has_shader_beam);
  CHECK(!held->has_indexed_skin);
  CHECK(held->has_q3_weapon && held->q3_weapon_time_ms == 1000 && held->q3_weapon_torso_animation == 0);
  CHECK(held->has_q3_weapon_last_fire && held->q3_weapon_last_fire_ms == 995 && held->q3_weapon_firing);
  CHECK(held->q3_weapon_horizontal_speed == 0 && held->q3_weapon_bob_cycle == 0 && held->q3_weapon_weapon == 2);
  CHECK(strcmp(out[2].render_owner, "source-client") == 0);
  /* held-weapon.test.ts: the viewer keeps its own model; others see the public identity. */
  qa_app_simulation_presentation visible = foreign, mine[1], theirs[1];
  visible.visible = true;
  qa_app_sim_unified_model_presentations(other, &visible, 1, mine);
  CHECK(memcmp(&mine[0], &visible, sizeof visible) == 0);
  qa_app_sim_unified_model_presentations(qa_identity_owner_actor(&owner, 1, 0), &visible, 1, theirs);
  CHECK(!theirs[0].visible && theirs[0].origin.x == 0 && theirs[0].angles.y == 0);
  /* A non-view model is never projected. */
  qa_app_simulation_presentation world = foreign, kept[1];
  world.view_weapon = false;
  qa_app_sim_unified_model_presentations(id, &world, 1, kept);
  CHECK(memcmp(&kept[0], &world, sizeof world) == 0);
  qa_identity_owner_destroy(&owner);
}

static qa_q2_protocol_identity protocol(const char *kind, int64_t version) {
  qa_q2_protocol_identity p;
  memset(&p, 0, sizeof p);
  p.kind = kind; p.version = version;
  return p;
}
static void classic_transcoder(void *ctx) { (void)ctx; (void)qa_app_sim_create_rerelease_native_message_transcoder(protocol("q2-classic", 34)); }
static void test_rerelease_transcoder(void) {
  /* tests/network/q2/rerelease-native.test.ts */
  qa_q2_wire_codec source;
  CHECK(qa_q2_wire_codec_init(&source, protocol("q2-rerelease", 1038)));
  qa_app_sim_rerelease_transcoder *convert = qa_app_sim_create_rerelease_native_message_transcoder(protocol("q2-kex", 2023));
  qa_q2_server_message_reader target;
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = 12448; options.inventory_slots = 256;
  CHECK(qa_q2_server_message_reader_init(&target, protocol("q2-kex", 2023), options));
  qa_q2_server_event sound;
  memset(&sound, 0, sizeof sound);
  sound.kind = QA_Q2_SERVER_SOUND;
  sound.sound.flags = 0; sound.sound.index = 300; sound.sound.entity = 2000; sound.sound.channel = 1;
  sound.sound.has_position = true; sound.sound.position = (qa_vec3){1.9, -50000, 3.25};
  sound.sound.volume = 1; sound.sound.attenuation = 1; sound.sound.delay_seconds = 0;
  size_t sound_length = 0, command_length = 0;
  uint8_t *sound_bytes = qa_q2_encode_server_event(&source, &sound, &sound_length);
  qa_q2_server_event command;
  memset(&command, 0, sizeof command);
  command.kind = QA_Q2_SERVER_COMMAND_TEXT;
  command.text = "echo \"native record\"\n";
  uint8_t *command_bytes = qa_q2_encode_server_event(&source, &command, &command_length);
  uint8_t *batch = malloc(sound_length + command_length);
  memcpy(batch, sound_bytes, sound_length);
  memcpy(batch + sound_length, command_bytes, command_length);
  size_t converted_length = 0;
  uint8_t *converted = qa_app_sim_rerelease_transcode(convert, batch, sound_length + command_length, &converted_length);
  qa_q2_server_record *records = NULL;
  size_t record_count = 0;
  CHECK(qa_q2_server_message_reader_read(&target, converted, converted_length, &records, &record_count));
  CHECK(record_count == 2);
  if (record_count == 2) {
    const qa_q2_server_event *event = &records[0].event;
    CHECK(event->kind == QA_Q2_SERVER_SOUND);
    CHECK(event->sound.has_position);
    CHECK(event->sound.position.x == (double)(float)1.9 && event->sound.position.y == -50000 && event->sound.position.z == 3.25);
    CHECK(event->sound.index == 300); CHECK(event->sound.entity == 2000);
    CHECK(records[1].event.kind == QA_Q2_SERVER_COMMAND_TEXT);
    CHECK(strcmp(records[1].event.text, "echo \"native record\"\n") == 0);
  }
  qa_q2_server_records_free(records, record_count);
  free(converted); free(batch); free(sound_bytes); free(command_bytes);
  qa_q2_server_message_reader_destroy(&target);
  qa_app_sim_rerelease_transcoder_destroy(convert);
  qa_q2_wire_codec_destroy(&source);
  qa_caught_error caught;
  CHECK(!qa_try(classic_transcoder, NULL, &caught));
  CHECK(strstr(caught.message, "protocol1038 or KEX2023") != NULL);
}

static void test_info(void) {
  qa_app_sim_info info;
  qa_app_sim_info_parse_q2(&info, "\\name\\one\\name\\two\\skin\\male/grunt\\odd");
  CHECK(info.count == 2);
  CHECK(strcmp(qa_app_sim_info_get(&info, "name"), "one") == 0);
  qa_app_sim_info_set(&info, "name_1", "seat");
  qa_app_sim_info_fold_seat(&info, 1);
  CHECK(strcmp(qa_app_sim_info_get(&info, "name"), "seat") == 0);
  char *text = qa_app_sim_info_text(&info);
  CHECK(strcmp(text, "\\name\\seat\\skin\\male/grunt\\name_1\\seat") == 0);
  free(text);
  qa_app_sim_info_clear(&info);
  CHECK(strcmp(qa_app_sim_network_gamedir("/corpus/q2/rerelease/baseq2"), "baseq2") == 0);
  CHECK(strcmp(qa_app_sim_network_gamedir("baseq2"), "baseq2") == 0);
}

/* ---- Q2 guest hosts over sentinel contexts ----------------------------------------- */
static int world_sentinel, sim_sentinel, content_sentinel, mounts_sentinel, print_sentinel, identity_sentinel;
#define SENTINEL(ctx, expected) CHECK((ctx) == (void *)&(expected))
/* The host.discovery value the Q2 server network reads. */
typedef struct guest_discovery_view { void *ctx; qa_q2_status (*status)(void *ctx); qa_q2_host_info (*info)(void *ctx); } guest_discovery_view;
typedef struct fake_world_state {
  qa_identity_owner *owner;
  char *configs[64]; bool has_config[64];
  qa_q2_config_string_entry entries[64]; size_t entry_count;
  qa_app_sim_q2_guest_client clients[4]; size_t client_count;
  char userinfo[4][512];
  bool allow; char last_connect[512]; char last_social[64];
  int begun, disconnected, thought, userinfo_calls, commands;
  char last_command[256]; size_t last_argc;
  qa_q2_entity_state states[2]; qa_q2_rerelease_entity_state rr_states[2];
  qa_app_sim_q2_guest_message messages[3]; uint8_t bytes[3][4];
  bool retired;
} fake_world_state;
static fake_world_state world_state;
static void w_set_configstring(void *ctx, double index, const char *value) {
  SENTINEL(ctx, world_sentinel);
  size_t i = (size_t)index;
  free(world_state.configs[i]);
  world_state.configs[i] = strdup(value);
  world_state.has_config[i] = true;
}
static const qa_q2_config_string_entry *w_configstrings(void *ctx, size_t *out_count) {
  SENTINEL(ctx, world_sentinel);
  world_state.entry_count = 0;
  for (size_t i = 0; i < 64; i++)
    if (world_state.has_config[i]) {
      world_state.entries[world_state.entry_count].index = (double)i;
      world_state.entries[world_state.entry_count++].value = world_state.configs[i];
    }
  *out_count = world_state.entry_count;
  return world_state.entries;
}
static bool w_is_retired(void *ctx) { SENTINEL(ctx, world_sentinel); return world_state.retired; }
static const qa_app_sim_q2_guest_client *w_clients(void *ctx, size_t *out_count) {
  SENTINEL(ctx, world_sentinel); *out_count = world_state.client_count; return world_state.clients;
}
static double w_player_ping(void *ctx, double slot) { SENTINEL(ctx, world_sentinel); return 40 + slot; }
static double stats_storage[20];
static void w_player_state(void *ctx, double slot, qa_q2_player_state *classic, qa_q2_rerelease_player_state *rerelease) {
  SENTINEL(ctx, world_sentinel);
  for (int i = 0; i < 20; i++) stats_storage[i] = 0;
  stats_storage[14] = 7 * slot;
  if (classic) { memset(classic, 0, sizeof *classic); classic->view.stats.values = stats_storage; classic->view.stats.count = 20; }
  if (rerelease) { memset(rerelease, 0, sizeof *rerelease); rerelease->view.stats.values = stats_storage; rerelease->view.stats.count = 20; }
}
static const void *w_entity_states(void *ctx, size_t *out_count) { SENTINEL(ctx, world_sentinel); *out_count = 2; return world_state.states; }
static const void *w_rr_entity_states(void *ctx, size_t *out_count) { SENTINEL(ctx, world_sentinel); *out_count = 2; return world_state.rr_states; }
static qa_q2_entity_state w_entity_state(void *ctx, double slot) {
  SENTINEL(ctx, world_sentinel);
  qa_q2_entity_state state; memset(&state, 0, sizeof state); state.number = slot; return state;
}
static qa_app_sim_q2_guest_entity_info w_entity_info(void *ctx, double slot) {
  SENTINEL(ctx, world_sentinel);
  qa_app_sim_q2_guest_entity_info info; memset(&info, 0, sizeof info); info.has_owner_slot = slot == 2; info.owner_slot = 1; return info;
}
static qa_app_sim_q2_guest_connection w_connect(void *ctx, double slot, const char *userinfo, const char *social_id, bool bot) {
  SENTINEL(ctx, world_sentinel);
  CHECK(!bot);
  snprintf(world_state.last_connect, sizeof world_state.last_connect, "%s", userinfo);
  snprintf(world_state.last_social, sizeof world_state.last_social, "%s", social_id);
  qa_app_sim_q2_guest_connection result = {world_state.allow, world_state.allow ? userinfo : "\\name\\x\\rejmsg\\Banned."};
  if (world_state.allow) {
    world_state.clients[world_state.client_count].slot = slot;
    snprintf(world_state.userinfo[world_state.client_count], 512, "%s", userinfo);
    world_state.clients[world_state.client_count].userinfo = world_state.userinfo[world_state.client_count];
    world_state.client_count++;
  }
  return result;
}
static void w_begin(void *ctx, double slot) { SENTINEL(ctx, world_sentinel); (void)slot; world_state.begun++; }
static void w_disconnect(void *ctx, double slot) { SENTINEL(ctx, world_sentinel); (void)slot; world_state.disconnected++; }
static void w_think(void *ctx, double slot, const qa_user_command *command) {
  SENTINEL(ctx, world_sentinel); (void)slot; (void)command; world_state.thought++;
}
static void w_userinfo(void *ctx, double slot, const char *value) { SENTINEL(ctx, world_sentinel); (void)slot; (void)value; world_state.userinfo_calls++; }
static void w_command(void *ctx, double slot, const char *const *argv, size_t argc, const char *args) {
  SENTINEL(ctx, world_sentinel); (void)slot; (void)args;
  world_state.commands++; world_state.last_argc = argc;
  snprintf(world_state.last_command, sizeof world_state.last_command, "%s", argc ? argv[0] : "");
}
static const qa_app_sim_q2_guest_message *w_raw_messages(void *ctx, size_t *out_count) {
  SENTINEL(ctx, world_sentinel); *out_count = 3; return world_state.messages;
}
static double n_add(void *ctx, double a, double b) { (void)ctx; return a + b; }
static double n_sub(void *ctx, double a, double b) { (void)ctx; return a - b; }
static double n_mul(void *ctx, double a, double b) { (void)ctx; return a * b; }
static double n_div(void *ctx, double a, double b) { (void)ctx; return a / b; }
static double n_sqrt(void *ctx, double a) { (void)ctx; return sqrt(a); }

/* SharedSimulation surface over sentinel ctx. */
typedef struct fake_sim_state {
  qa_identity_owner *owner;
  int notified_admitted, notified_userinfo, observed, disconnected, registered;
  qa_actor_id last_actor;
} fake_sim_state;
static fake_sim_state sim_state;
static qa_owned_actor s_register_q2_native_client(void *ctx, qa_client_id client) {
  SENTINEL(ctx, sim_sentinel);
  sim_state.registered++;
  qa_owned_actor actor; memset(&actor, 0, sizeof actor);
  actor.id = qa_identity_owner_actor(sim_state.owner, client.slot + 10, 0);
  return actor;
}
static qa_client_id native_clients[1]; static qa_actor_id native_actors[1];
static size_t s_q2_native_players(void *ctx, const qa_client_id **clients, const qa_actor_id **actors) {
  SENTINEL(ctx, sim_sentinel);
  native_clients[0] = qa_identity_owner_client(sim_state.owner, 3, 0);
  native_actors[0] = qa_identity_owner_actor(sim_state.owner, 13, 0);
  *clients = native_clients; *actors = native_actors;
  return 1;
}
static void s_notify(void *ctx, const char *kind, qa_actor_id actor) {
  SENTINEL(ctx, sim_sentinel);
  if (strcmp(kind, "admitted") == 0) sim_state.notified_admitted++;
  if (strcmp(kind, "userinfo") == 0) sim_state.notified_userinfo++;
  sim_state.last_actor = actor;
}
static void s_observe(void *ctx, const qa_actor_command *input) {
  SENTINEL(ctx, sim_sentinel);
  CHECK(input->source.kind == QA_COMMAND_SOURCE_REMOTE_CLIENT);
  sim_state.observed++;
}
static void s_disconnect(void *ctx, qa_actor_id actor) { SENTINEL(ctx, sim_sentinel); (void)actor; sim_state.disconnected++; }
static qa_app_sim_network_simulation fake_simulation(const qa_executable_recipe *recipe, const qa_app_simulation_options *options) {
  qa_app_sim_network_simulation s; memset(&s, 0, sizeof s);
  s.ctx = &sim_sentinel; s.recipe = recipe; s.options = options;
  s.register_q2_native_client = s_register_q2_native_client; s.q2_native_players = s_q2_native_players;
  s.notify_client_event = s_notify; s.observe_client_command = s_observe; s.disconnect_player = s_disconnect;
  return s;
}
static bool m_open(void *ctx, const char *path, uint8_t **out, size_t *length, const qa_resolved_resource_reference **reference) {
  SENTINEL(ctx, mounts_sentinel); (void)path; (void)out; (void)length; (void)reference; return false;
}
static bool m_exists(void *ctx, const char *path) { SENTINEL(ctx, mounts_sentinel); (void)path; return false; }
static void m_invalidate(void *ctx) { SENTINEL(ctx, mounts_sentinel); }
static qa_app_q2_mounts c_for_content(void *ctx, qa_content_id content) {
  SENTINEL(ctx, content_sentinel); (void)content;
  qa_app_q2_mounts mounts = {&mounts_sentinel, m_open, m_exists, m_invalidate};
  return mounts;
}
static uint8_t *c_read(void *ctx, const qa_resolved_resource_reference *reference, size_t *out_length) {
  SENTINEL(ctx, content_sentinel); (void)reference;
  uint8_t *bytes = malloc(4); memcpy(bytes, "IBSP", 4); *out_length = 4; return bytes;
}
static const char *c_content_directory(void *ctx, qa_content_id content) { SENTINEL(ctx, content_sentinel); (void)content; return "/corpus/q2/baseq2"; }
static char printed[512];
static void p_print(void *ctx, const char *text) { SENTINEL(ctx, print_sentinel); snprintf(printed, sizeof printed, "%s", text); }
static int identity_calls; static bool last_identity;
static void p_identity(void *ctx, qa_client_id client, bool has_identity, double seat, const char *social_id) {
  SENTINEL(ctx, identity_sentinel); (void)client; (void)seat; (void)social_id; identity_calls++; last_identity = has_identity;
}
static qa_cvar_registry *q2_cvars(qa_identity_owner *owner) {
  qa_cvar_registry_options options; memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  options.context.session = owner->session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  return qa_cvar_registry_create(&options);
}
static qa_app_sim_q2_guest_world fake_world(qa_cvar_registry *cvars, bool rerelease) {
  qa_app_sim_q2_guest_world w; memset(&w, 0, sizeof w);
  w.ctx = &world_sentinel; w.cvars = cvars; w.max_clients = 2; w.map_path = "maps/base1.bsp"; w.frame_milliseconds = 25;
  w.numeric.add = n_add; w.numeric.subtract = n_sub; w.numeric.multiply = n_mul; w.numeric.divide = n_div; w.numeric.square_root = n_sqrt;
  w.set_configstring = w_set_configstring; w.configstrings = w_configstrings; w.is_retired = w_is_retired; w.clients = w_clients;
  w.player_ping = w_player_ping; w.player_state = w_player_state; w.entity_states = rerelease ? w_rr_entity_states : w_entity_states;
  w.entity_state = w_entity_state; w.entity_info = w_entity_info; w.connect = w_connect; w.begin = w_begin; w.disconnect = w_disconnect;
  w.think = w_think; w.userinfo = w_userinfo; w.command = w_command; w.raw_messages = w_raw_messages;
  return w;
}
static void reset_world_state(void) {
  for (int i = 0; i < 64; i++) free(world_state.configs[i]);
  memset(&world_state, 0, sizeof world_state);
  world_state.allow = true;
  world_state.states[0].number = 1; world_state.states[0].model_indexes[0] = 255;
  world_state.states[1].number = 2; world_state.states[1].effects = 4;
  world_state.rr_states[0].base = world_state.states[0]; world_state.rr_states[1].base = world_state.states[1];
  world_state.rr_states[1].effects = 4;
  for (int i = 0; i < 3; i++) {
    world_state.bytes[i][0] = (uint8_t)(i + 1);
    world_state.messages[i].bytes = world_state.bytes[i]; world_state.messages[i].length = 1; world_state.messages[i].reliable = true;
  }
  world_state.messages[0].unicast = true; world_state.messages[0].slot = 1;
  world_state.messages[1].unicast = true; world_state.messages[1].slot = 2;
  world_state.messages[2].unicast = false; world_state.messages[2].scope = "all";
}
typedef struct admit_call { qa_app_q2_server_host *host; const qa_net_address *from; const qa_q2_connect_request *request; qa_app_q2_admission result; } admit_call;
static void call_admit(void *ctx) { admit_call *call = ctx; call->result = call->host->admit(call->host->ctx, call->from, call->request); }

static void test_classic_guest_host(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "guest-host"));
  sim_state.owner = &owner;
  reset_world_state();
  qa_session_mode mode = {QA_SESSION_MODE_HEADLESS};
  qa_engine_session *session = qa_engine_session_create(&owner, mode);
  qa_cvar_registry *cvars = q2_cvars(&owner);
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(cvars, "sv_airaccelerate", "0", 0, &snapshot); qa_cvar_read_free(&snapshot);
  qa_executable_recipe recipe; memset(&recipe, 0, sizeof recipe);
  recipe.movement.provider = "q2:movement"; recipe.character.definition.provider = "q1:character";
  recipe.map.entities.content = "q2:classic:baseq2:test"; recipe.map.geometry.requested_path = "maps/base1.bsp";
  qa_app_simulation_options sim_options; memset(&sim_options, 0, sizeof sim_options); sim_options.max_clients = 2;
  qa_app_sim_network_simulation simulation = fake_simulation(&recipe, &sim_options);
  qa_app_sim_network_content content; memset(&content, 0, sizeof content);
  content.ctx = &content_sentinel; content.recipe = &recipe; content.world.kind = QA_DECODED_WORLD_Q1;
  content.for_content = c_for_content; content.read = c_read; content.content_directory = c_content_directory;
  qa_app_sim_q2_guest_world world = fake_world(cvars, false);
  qa_app_sim_q2_server_binding_options options; memset(&options, 0, sizeof options);
  options.session = session; options.simulation = &simulation; options.content = &content;
  options.protocol = protocol("q2-classic", 34); options.print = p_print; options.print_ctx = &print_sentinel;
  qa_app_q2_server_host host = qa_app_sim_create_classic_q2_application_server_host(options, &world);
  char *mapname = qa_cvar_registry_variable_string(cvars, "mapname");
  CHECK(strcmp(mapname, "base1") == 0);
  free(mapname);
  CHECK(world_state.has_config[31] && world_state.has_config[30] && strcmp(world_state.configs[30], "2") == 0);
  CHECK(strcmp(world_state.configs[29], "0") == 0);
  const qa_wire_admission *support = host.supports_source_wire(host.ctx);
  CHECK(!support->supported && support->reason_count == 2);
  CHECK(strcmp(support->reasons[1], "Guest native wire requires Q2 character state") == 0);
  qa_net_address from; memset(&from, 0, sizeof from); from.kind = QA_NET_IPV4;
  from.ipv4[0] = 10; from.ipv4[3] = 7; from.port = 27910;
  qa_q2_connect_request request; memset(&request, 0, sizeof request);
  request.protocol = protocol("q2-classic", 34); request.userinfo = "\\name\\guest\\ip\\spoofed";
  qa_app_q2_admission admitted = host.admit(host.ctx, &from, &request);
  CHECK(admitted.accepted); CHECK(admitted.player.source_entity == 1); CHECK(sim_state.registered == 1);
  CHECK(strcmp(world_state.last_connect, "\\name\\guest\\ip\\10.0.0.7:27910") == 0);
  world_state.allow = false;
  admitted = host.admit(host.ctx, &from, &request);
  CHECK(!admitted.accepted && strcmp(admitted.reason, "Banned.") == 0);
  world_state.allow = true;
  qa_app_q2_player player = host.carried_player(host.ctx, qa_identity_owner_client(&owner, 3, 0));
  CHECK(player.source_entity == 4 && player.actor.slot == 13);
  qa_app_q2_player first = {qa_identity_owner_client(&owner, 0, 0), qa_identity_owner_actor(&owner, 10, 0), 1};
  /* The session's first client carries generation 0 at slot 0 for this owner. */
  first.client = qa_session_client_id(qa_engine_session_client_at(session, 0));
  host.begin(host.ctx, first);
  CHECK(world_state.begun == 1 && sim_state.notified_admitted == 1);
  host.userinfo(host.ctx, first, "\\name\\renamed");
  CHECK(world_state.userinfo_calls == 1 && sim_state.notified_userinfo == 1);
  const char *args[2] = {"hello", "world"};
  host.command(host.ctx, first, "say", args, 2);
  CHECK(world_state.commands == 1 && strcmp(world_state.last_command, "say") == 0 && world_state.last_argc == 3);
  qa_q2_wire_usercmd command; memset(&command, 0, sizeof command); command.msec = 16;
  CHECK(host.input(host.ctx, first, &command, 5) == NULL);
  CHECK(sim_state.observed == 1 && world_state.thought == 1);
  size_t count = 9;
  CHECK(host.events(host.ctx, first, NULL, NULL, 0, &count) == NULL && count == 0);
  qa_cvar_registry_set(cvars, "sv_airaccelerate", "1", true, &snapshot); qa_cvar_read_free(&snapshot);
  host.observe(host.ctx, NULL, NULL, 0);
  CHECK(strcmp(world_state.configs[29], "1") == 0);
  const qa_app_q2_raw_message *raw = host.raw_messages(host.ctx, first, &count);
  CHECK(count == 2 && raw[0].bytes[0] == 1 && raw[1].bytes[0] == 3);
  raw = host.local_messages(host.ctx, first, &count);
  CHECK(count == 2);
  raw = host.local_messages(host.ctx, first, &count);
  CHECK(count == 0);
  qa_app_q2_game_state state = host.game_state(host.ctx, first, NULL);
  const qa_q2_server_data_result *data = state.data;
  CHECK(strcmp(data->gamedir, "baseq2") == 0 && data->clientnum == 0 && strcmp(data->levelname, "base1") == 0 && data->server_fps == 10);
  CHECK(((const qa_q2_baseline_list *)state.baselines)->count == 2);
  const char *expanded = host.expand_client_command(host.ctx, "echo $mapname");
  CHECK(expanded != NULL && strcmp(expanded, "echo base1") == 0);
  free((void *)expanded);
  host.print(host.ctx, "printed");
  CHECK(strcmp(printed, "printed") == 0);
  const guest_discovery_view *discovery = host.discovery;
  qa_q2_status status = discovery->status(discovery->ctx);
  CHECK(status.player_count == 1 && strcmp(status.players[0].name, "guest") == 0 && status.players[0].score == 7 && status.players[0].ping == 41);
  qa_q2_host_info info = discovery->info(discovery->ctx);
  CHECK(strcmp(info.map, "base1") == 0 && info.players == 1 && info.max_players == 2);
  host.disconnect(host.ctx, first, "bye");
  CHECK(sim_state.disconnected == 1);
  qa_caught_error caught;
  struct { qa_app_q2_server_host *host; qa_app_q2_player player; } retired = {&host, first};
  (void)retired;
  admit_call call = {&host, &from, &request, {0}};
  world_state.client_count = 2; world_state.clients[1].slot = 2; world_state.clients[1].userinfo = "";
  CHECK(qa_try(call_admit, &call, &caught) && !call.result.accepted && strcmp(call.result.reason, "Server is full") == 0);
  qa_app_sim_classic_q2_application_server_host_destroy(host);
  qa_cvar_registry_destroy(cvars);
  qa_engine_session_destroy(session);
  qa_identity_owner_destroy(&owner);
  reset_world_state();
}

static void test_rerelease_guest_host(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "rerelease-host"));
  sim_state.owner = &owner;
  reset_world_state();
  /* Native FLOAT/1038 command-text records for the transcoder. */
  qa_q2_wire_codec source;
  CHECK(qa_q2_wire_codec_init(&source, protocol("q2-rerelease", 1038)));
  qa_q2_server_event command_text; memset(&command_text, 0, sizeof command_text);
  command_text.kind = QA_Q2_SERVER_COMMAND_TEXT; command_text.text = "echo native\n";
  size_t native_length = 0;
  uint8_t *native = qa_q2_encode_server_event(&source, &command_text, &native_length);
  for (int i = 0; i < 3; i++) { world_state.messages[i].bytes = native; world_state.messages[i].length = native_length; }
  qa_session_mode mode = {QA_SESSION_MODE_HEADLESS};
  qa_engine_session *session = qa_engine_session_create(&owner, mode);
  qa_cvar_registry *cvars = q2_cvars(&owner);
  qa_cvar_snapshot snapshot;
  qa_cvar_registry_register(cvars, "sv_airaccelerate", "0", 0, &snapshot); qa_cvar_read_free(&snapshot);
  qa_cvar_registry_register(cvars, "sv_novis", "0", 0, &snapshot); qa_cvar_read_free(&snapshot);
  qa_executable_recipe recipe; memset(&recipe, 0, sizeof recipe);
  recipe.movement.provider = "q2:movement"; recipe.character.definition.provider = "q2:character";
  recipe.map.entities.content = "q2:rerelease:baseq2:test";
  qa_app_simulation_options sim_options; memset(&sim_options, 0, sizeof sim_options); sim_options.max_clients = 2;
  qa_app_sim_network_simulation simulation = fake_simulation(&recipe, &sim_options);
  qa_app_sim_network_content content; memset(&content, 0, sizeof content);
  content.ctx = &content_sentinel; content.recipe = &recipe; content.world.kind = QA_DECODED_WORLD_Q1;
  content.for_content = c_for_content; content.read = c_read; content.content_directory = c_content_directory;
  qa_app_sim_q2_guest_world world = fake_world(cvars, true);
  qa_app_sim_q2_server_binding_options options; memset(&options, 0, sizeof options);
  options.session = session; options.simulation = &simulation; options.content = &content;
  options.protocol = protocol("q2-kex", 2023); options.print = p_print; options.print_ctx = &print_sentinel;
  options.player_identity = p_identity; options.player_identity_ctx = &identity_sentinel;
  qa_app_q2_server_host host = qa_app_sim_create_rerelease_native_q2_application_server_host(options, &world);
  const qa_wire_admission *support = host.supports_source_wire(host.ctx);
  CHECK(!support->supported && support->reason_count == 1);
  qa_net_address from; memset(&from, 0, sizeof from); from.kind = QA_NET_IPV4; from.port = 1;
  qa_q2_connect_request request; memset(&request, 0, sizeof request);
  request.protocol = protocol("q2-rerelease", 1038); request.userinfo = "\\name_1\\seat\\name\\guest";
  qa_app_q2_admission admitted = host.admit(host.ctx, &from, &request);
  CHECK(!admitted.accepted && strcmp(admitted.reason, "Native game and requested wire editions differ") == 0);
  request.protocol = protocol("q2-kex", 2023); request.has_split_seat = true; request.split_seat = 1;
  char *social = "steam:1"; request.has_social_ids = true; request.social_ids = &social; request.social_id_count = 1;
  admitted = host.admit(host.ctx, &from, &request);
  CHECK(admitted.accepted && identity_calls == 1 && last_identity);
  CHECK(strcmp(world_state.last_connect, "\\name_1\\seat\\name\\seat\\ip\\0.0.0.0:1") == 0);
  CHECK(strcmp(world_state.last_social, "steam:1") == 0);
  qa_app_q2_player first = admitted.player;
  host.observe(host.ctx, NULL, NULL, 0);
  size_t count = 0;
  const qa_app_q2_raw_message *wire = host.raw_messages(host.ctx, first, &count);
  CHECK(count == 2);
  qa_q2_server_message_reader kex;
  qa_q2_server_message_options reader_options; memset(&reader_options, 0, sizeof reader_options);
  reader_options.max_config_strings = 12448; reader_options.inventory_slots = 256;
  CHECK(qa_q2_server_message_reader_init(&kex, protocol("q2-kex", 2023), reader_options));
  qa_q2_server_record *records = NULL; size_t record_count = 0;
  CHECK(qa_q2_server_message_reader_read(&kex, wire[0].bytes, wire[0].length, &records, &record_count));
  CHECK(record_count == 1 && records[0].event.kind == QA_Q2_SERVER_COMMAND_TEXT && strcmp(records[0].event.text, "echo native\n") == 0);
  qa_q2_server_records_free(records, record_count);
  qa_q2_server_message_reader_destroy(&kex);
  const qa_app_q2_raw_message *sources = host.source_messages(host.ctx, first, &count);
  CHECK(count == 2 && sources[0].length == native_length && memcmp(sources[0].bytes, native, native_length) == 0);
  qa_q2_wire_usercmd command; memset(&command, 0, sizeof command); command.server_frame = 77;
  CHECK(host.input(host.ctx, first, &command, 5) == NULL && world_state.thought == 1);
  qa_app_q2_game_state state = host.game_state(host.ctx, first, NULL);
  CHECK(((const qa_q2_server_data_result *)state.data)->server_fps == 40);
  host.disconnect(host.ctx, first, "bye");
  CHECK(identity_calls == 2 && !last_identity);
  qa_app_sim_rerelease_native_q2_application_server_host_destroy(host);
  free(native);
  qa_q2_wire_codec_destroy(&source);
  qa_cvar_registry_destroy(cvars);
  qa_engine_session_destroy(session);
  qa_identity_owner_destroy(&owner);
  reset_world_state();
}

static qa_q2_product_runtime *s_no_q2(void *ctx) { SENTINEL(ctx, sim_sentinel); return NULL; }
static qa_comp_q1_source_composition *s_no_q1(void *ctx) { SENTINEL(ctx, sim_sentinel); return NULL; }
static qa_app_q3_source_runtime *s_no_q3(void *ctx) { SENTINEL(ctx, sim_sentinel); return NULL; }
typedef struct create_call { qa_app_sim_q2_server_binding_options q2; qa_app_sim_unified_host_options unified;
  qa_app_sim_q1_server_binding_options q1; qa_app_sim_qw_server_binding_options qw; } create_call;
static void call_q1_create(void *ctx) { create_call *call = ctx; (void)qa_app_sim_create_q1_application_server_host(call->q1); }
static void call_qw_create(void *ctx) { create_call *call = ctx; (void)qa_app_sim_create_qw_application_server_host(call->qw); }
static void call_q2_create(void *ctx) { create_call *call = ctx; (void)qa_app_sim_create_q2_application_server_host(call->q2); }
static void call_unified_create(void *ctx) { create_call *call = ctx; (void)qa_app_sim_create_unified_application_server_host(call->unified); }
static void test_host_requirements(void) {
  qa_executable_recipe recipe; memset(&recipe, 0, sizeof recipe);
  qa_app_simulation_options sim_options; memset(&sim_options, 0, sizeof sim_options);
  qa_app_sim_network_simulation simulation = fake_simulation(&recipe, &sim_options);
  simulation.q2_source = s_no_q2; simulation.q1_source = s_no_q1; simulation.q3_source = s_no_q3;
  create_call call; memset(&call, 0, sizeof call);
  call.q2.simulation = &simulation; call.unified.simulation = &simulation;
  qa_caught_error caught;
  CHECK(!qa_try(call_q2_create, &call, &caught));
  CHECK(strcmp(caught.message, "Q2 server network host requires the Q2 source game provider") == 0);
  CHECK(!qa_try(call_unified_create, &call, &caught));
  CHECK(strcmp(caught.message, "Unified hosting currently requires a TypeScript Q1, Q2 or Q3 source game") == 0);
  qa_app_sim_network_content content; memset(&content, 0, sizeof content);
  call.q1.simulation = &simulation; call.q1.content = &content;
  CHECK(!qa_try(call_q1_create, &call, &caught));
  CHECK(strcmp(caught.message, "Q1 network requires the Q1 source game") == 0);
  call.qw.simulation = &simulation; call.qw.content = &content;
  CHECK(!qa_try(call_qw_create, &call, &caught));
  CHECK(strcmp(caught.message, "Native QW wire requires its admitted QuakeWorld source and 32 reserved players") == 0);
}

int main(void) {
  test_unified_audience();
  test_unified_audience_type_error();
  test_unified_models();
  test_rerelease_transcoder();
  test_info();
  test_classic_guest_host();
  test_rerelease_guest_host();
  test_host_requirements();
  printf("test_sim_network: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
