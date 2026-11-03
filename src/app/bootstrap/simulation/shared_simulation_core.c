/* Port of src/app/bootstrap/simulation/runtime.ts, SharedSimulation core
 * region: the free helpers (270-280), createModClients/sourceClientView/
 * activeClientOutputs (495-569), the constructor (587-962), static load
 * (964-1043), close (8673-8697) and the actor infrastructure members the
 * constructor's closures read (schedule .. classname, 3961-4092).
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/monster_navigation.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/simulation/quakec_character_animation.h"
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/content/catalog/timing.h"
#include "qa/content/mounts.h"
#include "qa/core/numeric.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/formats/q1_map.h"
#include "qa/persistence/save_image.h"
#include "qa/world/actors/body.h"
#include "qa/formats/q2_map.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/runtime.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/settings/server/profile.h"
#include "qa/settings/server/cvars.h"
#include "qa/settings/server/selection.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/compat/q2/rerelease/module.h"
#include "qa/content/q3/base/combat_bridge.h"
/* TS leaves an unregistered Q3 bridge policy to the GC; the simulation frees it with itself. */
static void ss_release_q3_bridge_policy(void *ctx) {
  qa_combat_policy policy;
  memset(&policy, 0, sizeof policy);
  policy.ctx = ctx;
  qa_q3_base_combat_bridge_policy_destroy(policy);
}

const qa_vec3 qa_ss_zero = {0, 0, 0};

static noreturn void ss_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *ss_grow(void *ptr, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return ptr;
  size_t next = *capacity ? *capacity * 2 : 8;
  while (next < need) next *= 2;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}

void *qa_app_shared_sim_keep(qa_app_shared_simulation *sim, size_t size) {
  void *allocation = calloc(1, size ? size : 1);
  if (!allocation) qa_range_error("Out of memory");
  sim->allocations = ss_grow(sim->allocations, &sim->allocation_capacity, sim->allocation_count + 1, sizeof *sim->allocations);
  sim->allocations[sim->allocation_count++] = allocation;
  return allocation;
}
void qa_app_shared_sim_keep_owned(qa_app_shared_simulation *sim, void *value, void (*release)(void *value)) {
  sim->owned = ss_grow(sim->owned, &sim->owned_capacity, sim->owned_count + 1, sizeof *sim->owned);
  sim->owned[sim->owned_count].value = value; sim->owned[sim->owned_count].release = release; sim->owned_count++;
}

/* ---- typed views of the C option payloads ---- */
const qa_app_sim_native_q2_guest_options *qa_app_shared_sim_q2_guest(const qa_app_shared_simulation *sim) {
  return sim->options.has_q2_guest ? &sim->options.q2_guest : NULL;
}
static const qa_resolved_execution_module *ss_q2_guest_execution(const qa_app_sim_native_q2_guest_options *guest) {
  if (qa_ss_is(guest->edition, "classic")) return &((const qa_app_sim_prepared_classic_guest *)guest->prepared)->execution;
  return &((const qa_app_sim_prepared_rerelease_guest *)guest->prepared)->execution;
}
bool qa_app_shared_sim_owns_client(const qa_app_shared_simulation *sim, qa_client_id client) {
  return qa_identity_owner_owns_client(&sim->options.identity, client);
}

/* TS isDeepStrictEqual over an ExecutionModule. */
static bool ss_reference_equal(qa_provider_reference a, qa_provider_reference b) {
  return qa_ss_is(a.provider, b.provider) && qa_ss_is(a.content, b.content);
}
static bool ss_text_equal(const char *a, const char *b) { return a == b || qa_ss_is(a, b); }
static bool ss_resource_equal(const qa_resolved_resource_reference *a, const qa_resolved_resource_reference *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  return ss_text_equal(a->id, b->id) && ss_text_equal(a->requested_path, b->requested_path)
      && ss_text_equal(a->digest, b->digest) && a->byte_length == b->byte_length
      && a->provenance.kind == b->provenance.kind && ss_text_equal(a->provenance.member_path, b->provenance.member_path)
      && a->provenance.member_index == b->provenance.member_index
      && a->resolution.kind == b->resolution.kind && ss_text_equal(a->resolution.plan, b->resolution.plan)
      && a->resolution.rank == b->resolution.rank && ss_text_equal(a->resolution.prefix, b->resolution.prefix)
      && ss_text_equal(a->resolution.source_prefix, b->resolution.source_prefix)
      && ss_text_equal(a->resolution.target_path, b->resolution.target_path);
}
static bool ss_api_equal(qa_game_api_identity a, qa_game_api_identity b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
  case QA_GAME_API_QUAKEC: return a.as.quakec.kind == b.as.quakec.kind && a.as.quakec.program_version == b.as.quakec.program_version
      && a.as.quakec.system_crc == b.as.quakec.system_crc;
  case QA_GAME_API_Q2_GAME: return a.as.q2_game.kind == b.as.q2_game.kind && a.as.q2_game.version == b.as.q2_game.version;
  case QA_GAME_API_Q2_CGAME: return a.as.q2_cgame.version == b.as.q2_cgame.version;
  case QA_GAME_API_Q3: return a.as.q3.kind == b.as.q3.kind && a.as.q3.version == b.as.q3.version;
  }
  return false;
}
static bool ss_execution_equal(const qa_execution_module *a, const qa_execution_module *b) {
  return a->kind == b->kind && ss_reference_equal(a->owner, b->owner) && ss_text_equal(a->role, b->role)
      && ss_api_equal(a->api, b->api) && ss_text_equal(a->implementation, b->implementation)
      && ss_resource_equal(a->artifact, b->artifact)
      && ss_text_equal(a->profile.kind, b->profile.kind) && ss_text_equal(a->profile.image, b->profile.image)
      && a->profile.pointer_bytes == b->profile.pointer_bytes && ss_text_equal(a->profile.call, b->profile.call);
}
/* TS isDeepStrictEqual(saved.recipe, options.recipe): both recipes are
 * serialized by the save-image encoder (TS encodeCheckpointValue of the
 * plain object) inside otherwise identical images; equal bytes are equal
 * plain values. */
static bool ss_recipe_equal(const qa_executable_recipe *a, const qa_executable_recipe *b) {
  qa_save_image left; memset(&left, 0, sizeof left);
  left.schema_version = 3; left.frame.time.kind = QA_SOURCE_TIME_SECONDS; left.frame.elapsed.kind = QA_SOURCE_TIME_SECONDS;
  qa_save_image right = left;
  left.recipe = *a; right.recipe = *b;
  /* isDeepStrictEqual ignores key insertion order. */
  left.recipe.launch_key_order = false; right.recipe.launch_key_order = false;
  uint8_t *left_bytes = NULL, *right_bytes = NULL; size_t left_length = 0, right_length = 0;
  if (!qa_encode_save_image(&left, &left_bytes, &left_length) || !qa_encode_save_image(&right, &right_bytes, &right_length))
    qa_range_error("Out of memory");
  bool equal = left_length == right_length && memcmp(left_bytes, right_bytes, left_length) == 0;
  free(left_bytes); free(right_bytes);
  return equal;
}

/* TS options.world: the decoded ApplicationWorld. */
static void ss_resolve_world(qa_app_shared_simulation *sim) {
  const qa_app_application_world *world = sim->options.world;
  if (!world || !world->value) ss_fail("Simulation requires a decoded application world");
  sim->world_kind = world->kind;
  if (qa_ss_is(world->kind, "q1-bsp")) {
    sim->q1_map = world->value;
    sim->world.kind = QA_DECODED_WORLD_Q1; sim->world.as.q1 = &sim->q1_map->world;
    sim->world_entities = sim->q1_map->world.entities;
  } else if (qa_ss_is(world->kind, "q2-bsp")) {
    const qa_q2_decoded_map *map = world->value;
    sim->world.kind = QA_DECODED_WORLD_Q2; sim->world.as.q2 = &map->world;
    sim->world_entities = map->world.entities;
  } else if (qa_ss_is(world->kind, "q3-bsp")) {
    const qa_q3_world_geometry *geometry = world->value;
    sim->world.kind = QA_DECODED_WORLD_Q3; sim->world.as.q3 = geometry;
    sim->world_entities = geometry->entities;
  } else ss_fail("Simulation requires a decoded BSP world");
  if (!sim->world_entities) sim->world_entities = "";
}

/* TS source.game.records.player(slot) of the Q3 QVM source. */
static qa_q3_player_state ss_qvm_player(qa_app_shared_simulation *sim, double slot) {
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(sim->source.qvm), slot, &state, stats, persistent, powerups, ammo);
  state.stats = (qa_double_view){NULL, 0}; state.persistent = state.stats; state.powerups = state.stats; state.ammo = state.stats;
  return state;
}
static const qa_qvm_input_definition *ss_q3_guest_input(const qa_app_shared_simulation *sim) {
  const qa_app_sim_q3_guest_options *guest = sim->options.q3_guest;
  return guest ? qa_app_prepared_q3_primary_input(guest->prepared) : NULL;
}
static qa_app_sim_rerelease_guest_services *ss_rerelease_services(qa_app_shared_simulation *sim) {
  return qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world);
}
static qa_app_sim_classic_guest_services *ss_classic_services(qa_app_shared_simulation *sim) {
  return qa_app_sim_classic_guest_world_services(sim->source.classic_world);
}

/* ---- events / presentation ---- */
void qa_app_shared_sim_print(qa_app_shared_simulation *sim, const char *text, const qa_actor_id *actor) {
  qa_network_event event; memset(&event, 0, sizeof event);
  event.kind = QA_NETWORK_EVENT_PRINT; event.level = 2; event.text = text;
  qa_actor_id none; memset(&none, 0, sizeof none);
  qa_app_sim_simulation_events_message(sim->events, event, actor != NULL, actor ? *actor : none, false, 0);
}

/* ---- SimulationEvents super (PresentationState) ---- */
static bool ss_super_resource_for(void *ctx, qa_content_id content, const char *path, qa_resource_id *out) {
  return qa_app_presentation_state_resource_for(((qa_app_shared_simulation *)ctx)->presentation, content, path, out);
}
static void ss_super_emit(void *ctx, bool has_owner, qa_presentation_owner owner, qa_content_id content,
    qa_app_sim_source_event_input source, qa_source_time time, bool has_recipient, qa_actor_id recipient,
    qa_app_sim_presentation_event *out) {
  qa_app_presentation_state_emit_owned(((qa_app_shared_simulation *)ctx)->presentation, has_owner, owner, content, source, time,
                                       has_recipient, recipient, out);
}
static void ss_super_assert_consumed(void *ctx) {
  qa_app_presentation_state_assert_output_consumed(((qa_app_shared_simulation *)ctx)->presentation);
}
static void ss_super_register_resource(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource) {
  qa_app_presentation_state_register_resource(((qa_app_shared_simulation *)ctx)->presentation, content, path, resource);
}
static const char *ss_super_light_style(void *ctx, double style) {
  return qa_app_presentation_state_light_style(((qa_app_shared_simulation *)ctx)->presentation, style);
}
static qa_source_time ss_super_now(void *ctx) { return ((qa_app_shared_simulation *)ctx)->source_frame.time; }
static bool ss_super_source_slot(void *ctx, qa_actor_id actor, double *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(((qa_app_shared_simulation *)ctx)->actors, actor);
  if (!source.has_value) return false;
  *out = (double)source.slot;
  return true;
}
static bool ss_super_alive(void *ctx, qa_owned_actor actor) {
  qa_owned_actor resolved;
  return qa_session_actor_registry_resolve_owned(((qa_app_shared_simulation *)ctx)->actors, actor.id, &resolved);
}
/* SimulationEvents.emitOwned as the PresentationState virtual (presentation_state.h). */
static qa_app_sim_presentation_event ss_events_emit_owned_override(void *ctx, const qa_presentation_owner *owner,
    qa_content_id content, const qa_app_sim_source_presentation_event *source, bool has_time, qa_source_time time,
    bool has_recipient, qa_actor_id recipient) {
  qa_presentation_owner none; memset(&none, 0, sizeof none);
  return qa_app_sim_simulation_events_emit_owned(((qa_app_shared_simulation *)ctx)->events, owner != NULL, owner ? *owner : none,
      content, *source, has_time, time, has_recipient, recipient);
}
/* new SimulationEvents(bodies, now, clientFor, sourceSlot, { content, acceptedContents, entities, alive }) (725). */
void qa_app_shared_sim_bind_presentation_super(qa_app_shared_simulation *sim, qa_app_sim_simulation_events_options *events) {
  const qa_resolved_mount_plan *mounts = &sim->recipe.mounts;
  sim->accepted_contents = calloc(mounts->mount_count ? mounts->mount_count : 1, sizeof *sim->accepted_contents);
  if (!sim->accepted_contents) qa_range_error("Out of memory");
  for (size_t i = 0; i < mounts->mount_count; i++) {
    const qa_content_mount *mount = &mounts->mounts[i];
    qa_content_id content = mount->kind == QA_CONTENT_MOUNT_ARCHIVE ? mount->as.archive.identity.content : mount->as.loose.identity.content;
    bool seen = false; /* new Set(...) */
    for (size_t j = 0; j < sim->accepted_content_count; j++) if (qa_ss_is(sim->accepted_contents[j], content)) seen = true;
    if (!seen) sim->accepted_contents[sim->accepted_content_count++] = content;
  }
  qa_app_presentation_fog_options fog = {sim->recipe.map.entities.content, sim->accepted_contents, sim->accepted_content_count,
      qa_ss_is(sim->world_kind, "q1-bsp") ? sim->world_entities : "", sim, ss_super_alive};
  sim->presentation = qa_app_presentation_state_create_source(ss_super_now, ss_super_source_slot, sim, &fog);
  if (!sim->presentation) qa_range_error("Out of memory");
  events->resource_for = ss_super_resource_for; events->emit_super = ss_super_emit;
  events->assert_super_output_consumed = ss_super_assert_consumed;
  events->register_resource_super = ss_super_register_resource; events->light_style_super = ss_super_light_style;
}
void qa_app_shared_sim_release_presentation_super(qa_app_shared_simulation *sim) {
  if (sim->presentation) qa_app_presentation_state_destroy(sim->presentation);
  sim->presentation = NULL;
  free(sim->accepted_contents); sim->accepted_contents = NULL;
}
/* simulation.events.capture().persistent, as the network adapters read it (they keep the "q1" records): the capture
 * runs for its checks (an unsupported persistent event throws), and its records are the persistent presentation sorted
 * by sequence; local media records carry no simulation event. */
const qa_app_sim_presentation_event *const *qa_app_shared_sim_persistent_events(qa_app_shared_simulation *sim, size_t *out_count) {
  qa_save_value_free(qa_app_presentation_state_capture(sim->presentation));
  return qa_app_presentation_state_persistent_presentation(sim->presentation, out_count);
}
/* simulation.events.persistentPresentation(), as unified event trees. */
qa_save_value *const *qa_app_shared_sim_persistent_presentation(qa_app_shared_simulation *sim, size_t *out_count) {
  return qa_app_presentation_state_persistent_presentation_trees(sim->presentation, out_count);
}
/* simulation.events.resource(id): false is TS null. */
bool qa_app_shared_sim_event_resource(qa_app_shared_simulation *sim, qa_resource_id id, qa_resolved_resource_reference *out) {
  const qa_resolved_resource_reference *found = qa_app_presentation_state_resource(sim->presentation, id);
  if (found == NULL) return false;
  *out = *found;
  return true;
}
void qa_app_shared_sim_events_retire(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_presentation_state_retire_actor(sim->presentation, actor);
}
void qa_app_shared_sim_events_finish_owner_restore(qa_app_shared_simulation *sim) {
  qa_app_presentation_state_finish_owner_restore(sim->presentation);
}
void qa_app_shared_sim_emit(qa_app_shared_simulation *sim, qa_content_id content, qa_app_sim_source_event_input source,
    const qa_source_time *time, const qa_actor_id *recipient) {
  qa_actor_id none; memset(&none, 0, sizeof none);
  qa_app_sim_simulation_events_emit(sim->events, content, source, time != NULL, time ? *time : sim->source_frame.time,
                                    recipient != NULL, recipient ? *recipient : none);
}
const char *qa_app_shared_sim_intern(qa_app_shared_simulation *sim, const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = qa_app_shared_sim_keep(sim, length);
  memcpy(copy, text, length);
  return copy;
}

/* ---- createModClients (495-537) ---- */
static qa_mod_client_output_lease ss_claim_outputs(void *ctx, qa_provider_id owner,
    const qa_mod_client_output_channel *channels, size_t channel_count);
static const qa_mod_client_identity *ss_mod_clients_clients(void *ctx, size_t *out_count);
static bool ss_mod_clients_for_actor(void *ctx, qa_actor_id actor, qa_client_id *out) {
  qa_app_shared_simulation *sim = ctx;
  return qa_session_actor_registry_is_live(sim->actors, actor) && qa_app_shared_sim_player_client(sim, actor, out);
}
static bool ss_mod_clients_actor(void *ctx, qa_client_id client, qa_actor_id *out) {
  qa_app_shared_simulation *sim = ctx;
  if (!qa_app_shared_sim_owns_client(sim, client)) return false;
  size_t count = 0; const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
  for (size_t i = 0; i < count; i++) {
    qa_client_id found;
    if (qa_session_actor_registry_is_live(sim->actors, players[i]) && qa_app_shared_sim_player_client(sim, players[i], &found)
        && qa_same_client(found, client)) { *out = players[i]; return true; }
  }
  return false;
}
static const char *ss_mod_clients_userinfo(void *ctx, qa_client_id client) {
  qa_app_shared_simulation *sim = ctx;
  const char *value = qa_app_shared_sim_source_player_userinfo(sim, qa_app_shared_sim_require_mod_client(sim, client));
  return value ? value : "";
}
static void ss_mod_clients_set_userinfo(void *ctx, qa_client_id client, const char *value) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_shared_sim_store_player_userinfo(sim, qa_app_shared_sim_require_mod_client(sim, client), value);
}
static const qa_mod_client_command_receipt *ss_mod_clients_command(void *ctx, qa_client_id client) {
  qa_app_shared_simulation *sim = ctx;
  qa_actor_id actor = qa_app_shared_sim_require_mod_client(sim, client);
  return qa_ss_map_get(&sim->mod_client_commands, &actor);
}
static bool ss_mod_clients_grounded(void *ctx, qa_client_id client) {
  qa_app_shared_simulation *sim = ctx;
  qa_actor_id actor = qa_app_shared_sim_require_mod_client(sim, client);
  bool guest;
  if (sim->strategy->guest_grounded(sim, actor, &guest)) return guest;
  qa_movement_state state = qa_app_sim_movement_player_read_state(qa_app_shared_sim_require_player(sim, actor));
  if (state.kind == QA_MOVEMENT_STATE_Q2) return (qa_js_to_int32(state.as.q2.flags) & 4) != 0;
  if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) return (qa_js_to_int32(state.as.q2_rerelease.flags) & 4) != 0;
  if (state.kind == QA_MOVEMENT_STATE_Q1) return (qa_js_to_int32(state.as.q1.flags) & 512) != 0;
  qa_trace_hit ground = state.kind == QA_MOVEMENT_STATE_QW ? state.as.qw.ground : state.as.q3.ground;
  return ground.kind != QA_TRACE_HIT_NONE;
}
static qa_mod_client_player_view ss_mod_clients_player_view(void *ctx, qa_client_id client) {
  qa_app_shared_simulation *sim = ctx;
  qa_actor_id actor = qa_app_shared_sim_require_mod_client(sim, client);
  qa_mod_client_player_view original = qa_app_shared_sim_source_client_view(sim, actor);
  qa_mod_client_movement_outputs output;
  bool active = qa_app_shared_sim_active_client_outputs(sim, actor, &output);
  /* output?.viewOffset !== undefined && (output.stance !== false || !original.crouched) */
  if (active && output.view_offset != NULL && (!output.has_stance || output.stance || !original.crouched))
    original.view_offset = *output.view_offset;
  return original;
}
static void ss_mod_clients_drop(void *ctx, qa_client_id client, const char *reason, qa_content_id content) {
  qa_app_shared_simulation *sim = ctx;
  qa_actor_id actor = qa_app_shared_sim_require_mod_client(sim, client);
  qa_ss_mod_client_drop drop = {client, actor, reason, content};
  qa_event_ring_admit(&sim->mod_client_drops, &drop);
}
static qa_mod_client_unsubscribe ss_mod_clients_subscribe(void *ctx, qa_mod_client_listener_fn listener, void *listener_ctx) {
  return qa_app_shared_sim_subscribe_listener(ctx, listener, listener_ctx);
}
typedef struct ss_application_unsubscribe { qa_mod_client_applications *applications; int64_t id; } ss_application_unsubscribe;
static void ss_unsubscribe_application(void *ctx) {
  ss_application_unsubscribe *entry = ctx;
  qa_mod_client_applications_unsubscribe(entry->applications, entry->id);
  free(entry);
}
static qa_mod_client_unsubscribe ss_mod_clients_subscribe_application(void *ctx,
    qa_mod_client_application_listener_fn listener, void *listener_ctx) {
  qa_app_shared_simulation *sim = ctx;
  ss_application_unsubscribe *entry = malloc(sizeof *entry);
  if (!entry) qa_range_error("Out of memory");
  entry->applications = sim->mod_client_applications;
  entry->id = qa_mod_client_applications_subscribe(sim->mod_client_applications, listener, listener_ctx);
  return (qa_mod_client_unsubscribe){entry, ss_unsubscribe_application};
}
qa_mod_client_services qa_app_shared_sim_create_mod_clients(qa_app_shared_simulation *sim) {
  qa_mod_client_services services; memset(&services, 0, sizeof services);
  services.ctx = sim;
  services.maximum = sim->options.max_clients;
  services.claim_outputs = ss_claim_outputs;
  services.clients = ss_mod_clients_clients;
  services.for_actor = ss_mod_clients_for_actor;
  services.actor = ss_mod_clients_actor;
  services.userinfo = ss_mod_clients_userinfo;
  services.set_userinfo = ss_mod_clients_set_userinfo;
  services.command = ss_mod_clients_command;
  services.grounded = ss_mod_clients_grounded;
  services.player_view = ss_mod_clients_player_view;
  services.drop = ss_mod_clients_drop;
  services.subscribe = ss_mod_clients_subscribe;
  services.subscribe_application = ss_mod_clients_subscribe_application;
  return services;
}
/* claimOutputs: the lease closes its own application subscription. */
typedef struct ss_output_claim {
  qa_app_shared_simulation *sim;
  qa_mod_client_output_lease lease;
  int64_t stop;
} ss_output_claim;
static void ss_claim_publish(void *ctx, qa_actor_id actor, const qa_mod_client_output *outputs, size_t count) {
  ss_output_claim *claim = ctx; claim->lease.publish(claim->lease.ctx, actor, outputs, count);
}
static void ss_claim_release(void *ctx, qa_actor_id actor) {
  ss_output_claim *claim = ctx; claim->lease.release(claim->lease.ctx, actor);
}
static void ss_claim_close(void *ctx) {
  ss_output_claim *claim = ctx;
  claim->lease.close(claim->lease.ctx);
  qa_mod_client_applications_unsubscribe(claim->sim->mod_client_applications, claim->stop);
  free(claim);
}
static void ss_claim_noop_listener(void *ctx, qa_mod_client_application_event *event) { (void)ctx; (void)event; }
static bool ss_channels_include(const qa_mod_client_output_channel *channels, size_t count, qa_mod_client_output_channel name) {
  for (size_t i = 0; i < count; i++) if (channels[i] == name) return true;
  return false;
}
static qa_mod_client_output_lease ss_claim_outputs(void *ctx, qa_provider_id owner,
    const qa_mod_client_output_channel *channels, size_t channel_count) {
  qa_app_shared_simulation *sim = ctx;
  qa_clock_profile_kind movement = QA_CLOCK_Q1_NETQUAKE; bool has_movement = false;
  for (size_t i = 0; i < sim->options.recipe.timing_count; i++)
    if (qa_ss_is(sim->options.recipe.timing[i].provider, sim->options.recipe.movement.provider)) {
      movement = sim->options.recipe.timing[i].clock.kind; has_movement = true; break;
    }
  sim->strategy->check_output_claim(sim, channels, channel_count, has_movement, movement);
  ss_output_claim *claim = malloc(sizeof *claim);
  if (!claim) qa_range_error("Out of memory");
  claim->sim = sim;
  claim->lease = qa_mod_client_outputs_claim(sim->mod_client_outputs, owner, channels, channel_count);
  claim->stop = qa_mod_client_applications_subscribe(sim->mod_client_applications, ss_claim_noop_listener, NULL);
  return (qa_mod_client_output_lease){claim, ss_claim_publish, ss_claim_release, ss_claim_close};
}
/* SourceStrategy.checkOutputClaim per class (each override calls super first). */
#define SS_STANCE_CLAIM_ERROR "Selected original Quake movement has no crouch-request interface; authored collision changes require an explicit source boundary"
void qa_ss_loading_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count,
    bool has_movement, qa_clock_profile_kind movement) {
  (void)sim;
  if (ss_channels_include(channels, channel_count, QA_MOD_CLIENT_OUTPUT_STANCE) && has_movement && movement == QA_CLOCK_Q1_NETQUAKE)
    ss_fail(SS_STANCE_CLAIM_ERROR);
}
void qa_ss_quakec_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count,
    bool has_movement, qa_clock_profile_kind movement) {
  qa_ss_loading_check_output_claim(sim, channels, channel_count, has_movement, movement);
  if (ss_channels_include(channels, channel_count, QA_MOD_CLIENT_OUTPUT_STANCE) && has_movement && movement == QA_CLOCK_Q1_QUAKEWORLD
      && qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "quakeworld"))
    ss_fail(SS_STANCE_CLAIM_ERROR);
}
void qa_ss_q2_native_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count,
    bool has_movement, qa_clock_profile_kind movement) {
  qa_ss_loading_check_output_claim(sim, channels, channel_count, has_movement, movement);
  if (ss_channels_include(channels, channel_count, QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) && qa_ss_is(sim->source.edition, "rerelease")
      && !qa_compat_q2_rerelease_guest_module_require_world_profile(qa_compat_q2_rerelease_q2_guest_host_get_module(
             qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_world_source(sim->source.rerelease_world))))->movement.has_body)
    ss_fail("Selected original rerelease movement has no admitted body trace interface");
}
void qa_ss_q3_qvm_check_output_claim(qa_app_shared_simulation *sim, const qa_mod_client_output_channel *channels, size_t channel_count,
    bool has_movement, qa_clock_profile_kind movement) {
  qa_ss_loading_check_output_claim(sim, channels, channel_count, has_movement, movement);
  const qa_app_sim_q3_guest_options *guest = sim->options.q3_guest;
  if (ss_channels_include(channels, channel_count, QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE) && (!ss_q3_guest_input(sim) || !ss_q3_guest_input(sim)->has_movement_modes))
    ss_fail("Original QVM movement modes require an artifact-qualified input declaration");
  if (ss_channels_include(channels, channel_count, QA_MOD_CLIENT_OUTPUT_BODY_SHAPE)
      && (!guest || !qa_app_prepared_q3_primary_weapons(guest->prepared) || !qa_app_prepared_q3_primary_weapons(guest->prepared)->equipment_movement.has_body_trace))
    ss_fail("Selected original QVM movement has no admitted body trace interface");
}
/* SourceStrategy.guestGrounded / guestClientView (Q2Native, Q3Qvm). */
bool qa_ss_q2_native_guest_grounded(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  double slot = (double)qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  *out = qa_ss_is(sim->source.edition, "classic")
      ? qa_app_sim_classic_guest_services_player_grounded(ss_classic_services(sim), slot, actor)
      : qa_app_sim_rerelease_guest_services_player_grounded(ss_rerelease_services(sim), slot, actor);
  return true;
}
bool qa_ss_q3_qvm_guest_grounded(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  double slot;
  if (!qa_app_q3_guest_records_slot(qa_app_q3_qvm_server_game_records(sim->source.qvm), actor, &slot))
    ss_fail("QVM movement state requires a source client");
  *out = ss_qvm_player(sim, slot).ground_entity_number != 1023;
  return true;
}
bool qa_ss_q2_native_guest_client_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 *out_view_offset, bool *out_crouched) {
  double slot = (double)qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  qa_compat_q2_native_input_motion_view motion = qa_ss_is(sim->source.edition, "classic")
      ? qa_app_sim_classic_guest_services_player_view(ss_classic_services(sim), slot, actor)
      : qa_app_sim_rerelease_guest_services_player_view(ss_rerelease_services(sim), slot, actor);
  *out_view_offset = motion.view_offset; *out_crouched = motion.crouched;
  return true;
}
bool qa_ss_q3_qvm_guest_client_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 *out_view_offset, bool *out_crouched) {
  double slot;
  if (!qa_app_q3_guest_records_slot(qa_app_q3_qvm_server_game_records(sim->source.qvm), actor, &slot))
    ss_fail("QVM view requires a live source client");
  qa_q3_player_state state = ss_qvm_player(sim, slot);
  *out_view_offset = (qa_vec3){0, 0, state.view_height};
  *out_crouched = (qa_js_to_int32(state.movement_flags) & 1) != 0;
  return true;
}
/* SourceStrategy.outputsSuspended per class. */
static bool ss_outputs_suspended_base(qa_app_sim_movement_player *player) { return player != NULL && (player->intermission || player->has_cutscene); }
bool qa_ss_quakec_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  if (ss_outputs_suspended_base(player)) return true;
  bool intermission = false; qa_app_sim_player_view local;
  return (qa_app_sim_quakec_source_local_client_intermission(sim->source.quakec, actor, &intermission) && intermission)
      || qa_app_sim_quakec_source_local_client_view(sim->source.quakec, actor, &local);
}
bool qa_ss_q2_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  if (ss_outputs_suspended_base(player)) return true;
  const qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor);
  return state != NULL && state->has_chase_target;
}
bool qa_ss_q2_native_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)player; /* TS Q2NativeStrategy.outputsSuspended(actor) */
  double slot = (double)qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  double mode = qa_ss_is(sim->source.edition, "classic")
      ? qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot).movement.type
      : qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot).movement.type;
  return mode == (qa_ss_is(sim->source.edition, "classic") ? 4 : 6);
}
bool qa_ss_q3_qvm_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)player; /* TS Q3QvmStrategy.outputsSuspended(actor) */
  const qa_qvm_input_definition *input = ss_q3_guest_input(sim);
  double slot;
  if (!qa_app_q3_guest_records_slot(qa_app_q3_qvm_server_game_records(sim->source.qvm), actor, &slot)) return true;
  if (input == NULL) return false;
  double type = ss_qvm_player(sim, slot).movement_type;
  for (size_t i = 0; i < input->intermission_count; i++) if (input->intermission[i] == type) return true;
  return false;
}
static const qa_mod_client_identity *ss_mod_clients_clients(void *ctx, size_t *out_count) {
  qa_app_shared_simulation *sim = ctx;
  static __thread qa_mod_client_identity *storage; static __thread size_t capacity;
  size_t count = 0, written = 0; const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
  storage = ss_grow(storage, &capacity, count + 1, sizeof *storage);
  for (size_t i = 0; i < count; i++) {
    qa_client_id client; qa_body_state body;
    if (!qa_app_shared_sim_player_client(sim, players[i], &client) || !qa_app_shared_sim_body(sim, players[i], &body)) continue;
    storage[written++] = (qa_mod_client_identity){client, players[i]};
  }
  *out_count = written;
  return storage;
}

/* sourceClientView (538-551). */
qa_mod_client_player_view qa_app_shared_sim_source_client_view(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_mod_client_player_view view; memset(&view, 0, sizeof view);
  if (sim->strategy->guest_client_view(sim, actor, &view.view_offset, &view.crouched)) return view;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_movement_state state = qa_app_sim_movement_player_read_state(player);
  qa_app_sim_player_view source_view; qa_body_state body;
  bool has_view = qa_app_shared_sim_source_player_view(sim, actor, &source_view);
  bool has_body = qa_app_shared_sim_body(sim, actor, &body);
  if (!has_view || !has_body) ss_fail("Client view requires a live movement owner");
  double height = state.kind == QA_MOVEMENT_STATE_Q3 ? state.as.q3.view_height
      : state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE ? state.as.q2_rerelease.view_height : source_view.view_height;
  view.view_offset = (qa_vec3){source_view.origin.x - body.origin.x, source_view.origin.y - body.origin.y,
      source_view.origin.z + height - body.origin.z};
  view.crouched = state.kind == QA_MOVEMENT_STATE_Q3 ? (qa_js_to_int32(state.as.q3.movement_flags) & 1) != 0
      : state.kind == QA_MOVEMENT_STATE_Q2 ? (qa_js_to_int32(state.as.q2.flags) & 1) != 0
      : state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE ? (qa_js_to_int32(state.as.q2_rerelease.flags) & 1) != 0
      : state.kind == QA_MOVEMENT_STATE_QW && body.bounds.max.z < player->standing_bounds.max.z;
  return view;
}

/* activeClientOutputs (552-569). */
bool qa_app_shared_sim_active_client_outputs(qa_app_shared_simulation *sim, qa_actor_id actor, qa_mod_client_movement_outputs *out) {
  if (!qa_mod_client_outputs_read(sim->mod_client_outputs, actor, out)) return false;
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  if ((combat.has_value ? combat.value.health : 0) <= 0) return false;
  return !sim->strategy->outputs_suspended(sim, actor, qa_app_shared_sim_player(sim, actor));
}

/* ---- actor infrastructure (3961-4092, 6803-6843, 7465) ---- */
void qa_app_shared_sim_assert_open(const qa_app_shared_simulation *sim) {
  if (sim->closed) ss_fail("Simulation is closed");
}
/* schedule(actor, dueSeconds) */
void qa_app_shared_sim_schedule(qa_app_shared_simulation *sim, qa_owned_actor actor, double due_seconds) {
  qa_provider_id execution_provider = actor.owner;
  qa_provider_id found;
  if (qa_app_shared_sim_execution_provider(sim, actor.id, &found)) execution_provider = found;
  /* The foreign adjunct's provider is synthesized at runtime and has no
   * recipe timing entry; its clock is the native Q2 clock for its edition. */
  qa_clock_profile_kind kind;
  if (sim->has_foreign_world && qa_ss_is(execution_provider, sim->foreign_world.reference.provider))
    kind = qa_ss_is(sim->foreign_world.edition, "classic") ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE;
  else if (sim->has_foreign_q1_world && qa_ss_is(execution_provider, sim->foreign_q1_world.reference.provider))
  {
    qa_provider_timing native;
    if (!qa_native_provider_timing(sim->foreign_q1_world.reference, QA_GAME_FAMILY_Q1,
                                   qa_ss_is(sim->foreign_q1_world.edition, "rerelease"), &native)) qa_range_error("Out of memory");
    kind = native.clock.kind;
    qa_native_provider_timing_free(&native);
  }
  else kind = qa_app_sim_provider_timing(sim->recipe, execution_provider).clock.kind;
  bool milliseconds = kind == QA_CLOCK_Q2_RERELEASE || kind == QA_CLOCK_Q3;
  qa_think_timing timing; memset(&timing, 0, sizeof timing);
  timing.due = (qa_source_time){milliseconds ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS,
                                milliseconds ? due_seconds * 1000 : due_seconds};
  if (!qa_ss_is(execution_provider, actor.owner)) { timing.has_execution_provider = true; timing.execution_provider = execution_provider; }
  timing.boundary = QA_THINK_DURING_PHYSICS;
  timing.order = (qa_invocation_order){actor.owner, actor.id, sim->attack_sequence++};
  qa_frame_scheduler_schedule(sim->scheduler, actor, "world:think", timing);
}
/* executionProvider(actor): false = TS null. */
bool qa_app_shared_sim_execution_provider(qa_app_shared_simulation *sim, qa_actor_id actor, qa_provider_id *out) {
  const qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (!entry) return false;
  switch (entry->kind) {
  case QA_APP_SIM_ACTOR_EXECUTION_Q3: case QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE: *out = entry->provider; return true;
  case QA_APP_SIM_ACTOR_EXECUTION_QUAKEC: *out = sim->recipe.map.entities.provider; return true;
  case QA_APP_SIM_ACTOR_EXECUTION_Q1: *out = qa_q1_services_provider(entry->q1_services); return true;
  case QA_APP_SIM_ACTOR_EXECUTION_Q2: *out = qa_q2_entity_services_game(entry->q2_services)->options.provider; return true;
  }
  return false;
}
double qa_app_shared_sim_source_order(qa_app_shared_simulation *sim, qa_actor_id a, qa_actor_id b) {
  double left[2], right[2];
  qa_app_shared_sim_source_position(sim, a, left); qa_app_shared_sim_source_position(sim, b, right);
  /* left[0] - right[0] || left[1] - right[1]: a NaN or zero first term falls through. */
  double first = left[0] - right[0];
  return first != 0 && !isnan(first) ? first : left[1] - right[1];
}
void qa_app_shared_sim_source_position(qa_app_shared_simulation *sim, qa_actor_id actor, double out[2]) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, actor);
  qa_provider_id provider = "world:unknown";
  if (source.has_value) provider = source.provider;
  else {
    qa_actor_observation_result observed = qa_session_actor_registry_observe(sim->actors, actor);
    if (observed.has_value) provider = observed.value.owner;
  }
  double slot = source.has_value ? (double)source.slot : (double)actor.slot;
  if (sim->recipe.ordering.kind == QA_FRAME_ORDERING_MIXED) {
    double index = -1;
    for (size_t i = 0; i < sim->recipe.ordering.provider_count; i++)
      if (qa_ss_is(sim->recipe.ordering.providers[i], provider)) { index = (double)i; break; }
    out[0] = index; out[1] = slot;
    return;
  }
  if (!qa_ss_is(provider, sim->recipe.map.entities.provider)) { out[0] = 1; out[1] = (double)actor.slot; return; }
  out[0] = 0; out[1] = slot;
}
static void ss_no_solid(qa_app_sim_shared_solid *out, const char *family) {
  memset(out, 0, sizeof *out);
  out->family = family; out->solid = "none"; out->has_model = false; out->owner = NULL;
}
bool qa_app_shared_sim_collision(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_shared_solid *out) {
  if (sim->selected_monsters && !qa_app_sim_selected_monsters_active(sim->selected_monsters, actor.id)) {
    ss_no_solid(out, qa_game_family_name(qa_app_sim_provider_family(sim->recipe.map.entities.provider)));
    return true;
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player && (player->intermission || player->has_cutscene)) {
    ss_no_solid(out, qa_game_family_name(qa_app_sim_provider_family(player->profile.id)));
    return true;
  }
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &actor);
  if (player && q2 && (*q2)->state.gibbed) { ss_no_solid(out, "q2"); return true; }
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &actor);
  if (player && q1 && qa_ss_is(qa_q1_character_actor_presentation(*q1).solid, "none")) { ss_no_solid(out, "q1"); return true; }
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) { *out = qa_app_sim_actor_collision(entry); return true; }
  if (player) {
    ss_no_solid(out, qa_game_family_name(qa_app_sim_provider_family(player->profile.id)));
    out->solid = "box";
    return true;
  }
  if (!entry) return false;
  *out = qa_app_sim_actor_collision(entry);
  return true;
}
static bool ss_q3_execution(const qa_app_sim_actor_execution *entry) {
  return entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3 || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q3_SOURCE;
}
void qa_app_shared_sim_register_actor_execution(qa_app_shared_simulation *sim, qa_app_sim_actor_execution entry) {
  qa_owned_actor actor = ss_q3_execution(&entry) || entry.kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC ? entry.actor
      : entry.kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? entry.q1_entity->actor : entry.q2_entity->actor;
  qa_app_sim_actor_execution *previous = qa_ss_map_get(&sim->actor_executions, &actor.id);
  qa_session_actor_registry_assert_owned(sim->actors, actor);
  if (previous) {
    /* TS compares entry objects by identity for q3/quakec owners and the
     * services object otherwise; C entries are values, so the q3/quakec
     * identity is the same actor, kind and source binding. */
    bool different = ss_q3_execution(previous) || ss_q3_execution(&entry) || previous->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC
        || entry.kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC
        ? !(previous->kind == entry.kind && previous->quakec == entry.quakec && previous->ctx == entry.ctx && previous->step == entry.step)
        : (entry.kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? (void *)previous->q1_services != (void *)entry.q1_services
                                                       : (void *)previous->q2_services != (void *)entry.q2_services);
    if (different) ss_fail("Actor already has another source execution owner");
  }
  qa_ss_map_set(&sim->actor_executions, &actor.id, &entry);
}
/* contents(point, family) */
double qa_app_shared_sim_contents(qa_app_shared_simulation *sim, qa_vec3 point, const char *family) {
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = point;
  query.target.is_model = false;
  query.pass_actor = NULL;
  if (qa_ss_is(family, "q1")) { query.policy.kind = "q1"; query.policy.move = "normal"; query.policy.has_hull = false; }
  else { query.policy.kind = "q2"; query.policy.contents_mask = -1; query.policy.leaf_contents = "merged"; }
  query.numeric = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).numeric;
  qa_point_contents_result result = qa_shared_point_contents(sim->scene, query);
  return qa_ss_is(result.kind, "q2") ? result.merged : result.contents;
}
bool qa_app_shared_sim_world_actor(qa_app_shared_simulation *sim, qa_actor_id *out) {
  qa_owned_actor actor;
  int64_t slot = strncmp(sim->recipe.map.entities.provider, "q3:", 3) == 0 ? 1022 : 0;
  if (!qa_session_actor_registry_at_source(sim->actors, sim->recipe.map.entities.provider, slot, &actor)) return false;
  *out = actor.id;
  return true;
}
qa_app_sim_movement_player *qa_app_shared_sim_player_state(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_app_sim_movement_player **player = qa_ss_map_get(&sim->player_states, &actor);
  return player ? *player : NULL;
}
/* player(actor) */
qa_app_sim_movement_player *qa_app_shared_sim_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) return NULL;
  return qa_app_shared_sim_player_state(sim, owned);
}
/* movementPlayerOf(actor) */
qa_app_sim_movement_player *qa_app_shared_sim_movement_player_of(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player) return player;
  qa_app_sim_movement_player **native = qa_ss_map_get(&sim->native_movement_players, &actor);
  return native ? *native : NULL;
}
/* requirePlayer(actor) */
qa_app_sim_movement_player *qa_app_shared_sim_require_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  qa_app_sim_movement_player *player = qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)
      ? qa_app_shared_sim_movement_player_of(sim, owned) : NULL;
  if (!player) ss_fail("Actor is not an admitted player");
  return player;
}
/* clientIdentities() */
static int ss_client_slot_order(const void *a, const void *b) {
  const qa_client_id *left = a, *right = b;
  return (left->slot > right->slot) - (left->slot < right->slot);
}
const qa_client_id *qa_app_shared_sim_client_identities(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_client_id *storage; static __thread size_t capacity;
  const qa_client_id *own = sim->strategy->connected_clients(sim, out_count);
  if (own != NULL) return own;
  size_t count = 0;
  storage = ss_grow(storage, &capacity, sim->player_states.count + 1, sizeof *storage);
  for (size_t i = 0; i < sim->player_states.count; i++)
    storage[count++] = (*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i))->client;
  /* Array.prototype.sort is stable; slots are distinct per session. */
  qsort(storage, count, sizeof *storage, ss_client_slot_order);
  *out_count = count;
  return storage;
}
/* players() */
typedef struct ss_slot_actor { double slot; size_t index; qa_actor_id actor; } ss_slot_actor;
static int ss_slot_actor_order(const void *a, const void *b) {
  const ss_slot_actor *left = a, *right = b;
  if (left->slot != right->slot) return left->slot < right->slot ? -1 : 1;
  return (left->index > right->index) - (left->index < right->index); /* stable */
}
const qa_actor_id *qa_app_shared_sim_players(qa_app_shared_simulation *sim, size_t *out_count) {
  const qa_actor_id *own = sim->strategy->player_actors(sim, out_count);
  if (own != NULL) return own;
  double revision = sim->player_states.revision;
  if (!sim->has_ordered_players || sim->ordered_players_revision != revision) {
    size_t total = sim->player_states.count;
    ss_slot_actor *sorted = calloc(total ? total : 1, sizeof *sorted);
    qa_actor_id *ordered = calloc(total ? total : 1, sizeof *ordered);
    if (!sorted || !ordered) qa_range_error("Out of memory");
    for (size_t i = 0; i < total; i++) {
      qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
      sorted[i] = (ss_slot_actor){(double)player->client.slot, i, player->actor.id};
    }
    qsort(sorted, total, sizeof *sorted, ss_slot_actor_order);
    for (size_t i = 0; i < total; i++) ordered[i] = sorted[i].actor;
    free(sorted);
    free(sim->ordered_players);
    sim->ordered_players = ordered; sim->ordered_player_count = total;
    sim->ordered_players_revision = revision; sim->has_ordered_players = true;
  }
  *out_count = sim->ordered_player_count;
  return sim->ordered_players;
}
/* playerClient(actor) */
bool qa_app_shared_sim_player_client(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id *out) {
  int own = sim->strategy->client_of(sim, actor, out);
  if (own >= 0) return own == 1;
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(sim, actor);
  if (!player) return false;
  *out = player->client;
  return true;
}
/* classname(actor) */
const char *qa_app_shared_sim_classname(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  qa_client_id client;
  if (qa_app_shared_sim_player_client(sim, actor, &client)) return "player";
  if (entry && ss_q3_execution(entry)) return "q3:projectile";
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) return qa_app_sim_quakec_source_classname(entry->quakec->ctx, actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) return entry->q1_entity->classname;
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) return entry->q2_entity->classname;
  return "";
}
bool qa_app_shared_sim_body(qa_app_shared_simulation *sim, qa_actor_id actor, qa_body_state *out) {
  qa_body_state_result read = qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(sim->physics), actor);
  if (!read.has_value) return false;
  *out = read.value;
  return true;
}
bool qa_app_shared_sim_resolve_owned(qa_app_shared_simulation *sim, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(sim->actors, actor, out);
}
/* get q1PhysicsEdition (1097). */
const char *qa_app_shared_sim_q1_physics_edition(const qa_app_shared_simulation *sim) {
  const char *content = sim->recipe.engine_behavior.content;
  return content && strncmp(content, "q1:rerelease:", 13) == 0 ? "rerelease" : "classic";
}
/* initializeServerSettings (6803-6812). */
void qa_app_shared_sim_initialize_server_settings(qa_app_shared_simulation *sim, qa_cvar_registry *cvars) {
  if (sim->scene->geometry.kind == QA_DECODED_WORLD_Q3) {
    if (!sim->source_collision_settings) {
      sim->source_collision_settings = calloc(1, sizeof *sim->source_collision_settings);
      if (!sim->source_collision_settings) qa_range_error("Out of memory");
    }
    qa_collision_map_settings_init(sim->source_collision_settings, cvars);
    qa_collision_map_settings_register_map(sim->source_collision_settings);
    if (!sim->options.restore) qa_shared_bind_collision_settings(sim->scene, sim->source_collision_settings);
  }
  const qa_server_profile *profile = sim->options.server_profile;
  if (!profile) return;
  qa_cvar_server_owner owner; qa_cvar_server_settings_owner_init(&owner, cvars, true);
  size_t count = 0;
  const qa_server_setting_definition **definitions = qa_server_definitions_for_recipe(&sim->recipe, &count);
  qa_bound_server_setting *bindings = calloc(count ? count : 1, sizeof *bindings);
  if (!bindings) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) bindings[i] = (qa_bound_server_setting){definitions[i], owner.owner};
  qa_apply_server_profile(profile, bindings, count);
  free(bindings);
}

/* ---- constructor closures ---- */
static bool ss_take_kill_velocity(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  qa_q2_entity *entity = entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 ? entry->q2_entity : NULL;
  if (!entity || (qa_js_to_int32(entity->flags) & 0x800000) == 0) return false;
  entity->flags = qa_js_to_int32(entity->flags) & ~0x800000;
  return true;
}
static double ss_stop_speed(void *ctx) {
  qa_app_shared_simulation *sim = ctx;
  return sim->strategy->stop_speed(sim);
}
static double ss_physics_source_order(void *ctx, qa_actor_id a, qa_actor_id b) { return qa_app_shared_sim_source_order(ctx, a, b); }
static bool ss_physics_world_actor(void *ctx, qa_actor_id *out) { return qa_app_shared_sim_world_actor(ctx, out); }
static bool ss_physics_collision(void *ctx, qa_owned_actor actor, qa_app_sim_shared_solid *out) { return qa_app_shared_sim_collision(ctx, actor, out); }
static bool ss_physics_motion(void *ctx, qa_owned_actor actor, qa_q2_motion *out) {
  qa_app_shared_simulation *sim = ctx;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) return false;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player) {
    memset(out, 0, sizeof *out);
    out->actor = actor; out->velocity = body.velocity; out->angular_velocity = qa_ss_zero;
    qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &actor);
    qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &actor);
    const char *q1_movement = q1 ? qa_q1_character_actor_presentation(*q1).movement : NULL;
    out->kind = qa_ss_is(q1_movement, "bounce") ? "bounce" : qa_ss_is(q1_movement, "toss") ? "toss"
        : q2 && (*q2)->state.dead ? ((*q2)->state.gibbed ? "bounce" : "toss") : "step";
    out->gravity = 1; out->gravity_vector = (qa_vec3){0, 0, -1}; out->clip_mask = 0x6000003; out->has_owner = false;
    return true;
  }
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (!entry) return false;
  qa_q2_motion motion = qa_app_sim_actor_motion(entry, body);
  if (!motion.kind) return false;
  if (sim->selected_monsters == NULL || qa_app_sim_selected_monsters_active(sim->selected_monsters, actor.id)) { *out = motion; return true; }
  motion.kind = "stationary"; motion.velocity = qa_ss_zero; motion.angular_velocity = qa_ss_zero;
  *out = motion;
  return true;
}
static bool ss_physics_flags(void *ctx, qa_owned_actor actor, qa_app_sim_shared_physics_flags *out) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  memset(out, 0, sizeof *out);
  if (entry) *out = qa_app_sim_actor_flags(entry);
  /* `{ ...actorFlags(entry), ...{waterLevel, waterType}, player, dead }`: keys keep their first position. */
  qa_app_sim_shared_physics_flags extra; memset(&extra, 0, sizeof extra);
  if (player && !(entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1)) {
    extra.has_water_level = true; extra.water_level = player->water_level;
    extra.has_water_type = true; extra.water_type = player->water_type;
  }
  extra.has_player = true; extra.player = player != NULL;
  qa_combat_state_result combat = sim->combat ? qa_gameplay_authority_read(sim->combat, actor.id) : (qa_combat_state_result){0};
  extra.has_dead = true; extra.dead = (combat.has_value ? combat.value.health : 1) <= 0;
  static const unsigned char order[] = {QA_APP_SIM_FLAG_WATER_LEVEL, QA_APP_SIM_FLAG_WATER_TYPE, QA_APP_SIM_FLAG_PLAYER, QA_APP_SIM_FLAG_DEAD};
  qa_app_sim_physics_flags_order(&extra, order, sizeof order);
  qa_app_sim_physics_flags_merge(out, &extra);
  return true;
}
static void ss_physics_write_flags(void *ctx, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player) {
    if (changes.has_water_level) player->water_level = changes.water_level;
    if (changes.has_water_type) player->water_type = changes.water_type;
  }
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (entry) qa_app_sim_write_actor_flags(entry, changes);
}
static void ss_physics_event(void *ctx, qa_app_sim_physics_event event) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &event.actor);
  qa_game_family family = !entry ? qa_app_sim_provider_family(sim->recipe.map.entities.provider)
      : entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC || entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 ? QA_GAME_FAMILY_Q1
      : entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 ? QA_GAME_FAMILY_Q2 : QA_GAME_FAMILY_Q3;
  qa_content_id content = entry ? entry->content : sim->recipe.map.entities.content;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  qa_q1_event q1; qa_q2_presentation_event q2;
  if (family == QA_GAME_FAMILY_Q1) {
    memset(&q1, 0, sizeof q1);
    q1.kind = "sound"; q1.has_actor = true; q1.actor = event.actor;
    q1.path = qa_ss_is(event.kind, "land") ? "demon/dland2.wav" : "misc/h2ohit1.wav";
    q1.sound_channel = "auto"; q1.volume = 1; q1.attenuation = 1;
    source.kind = "q1"; source.q1 = &q1;
  } else {
    memset(&q2, 0, sizeof q2);
    q2.kind = "sound"; q2.has_actor = true; q2.actor = event.actor; q2.origin = event.origin;
    q2.path = qa_ss_is(event.kind, "land") ? "world/land.wav" : "misc/h2ohit1.wav";
    q2.channel = 0; q2.volume = 1; q2.attenuation = 1; q2.reliable = false; q2.loop = "once";
    source.kind = "q2"; source.q2 = &q2;
  }
  qa_actor_id none; memset(&none, 0, sizeof none);
  qa_app_sim_simulation_events_emit(sim->events, content, source, false, sim->source_frame.time, false, none);
}
static void ss_physics_q1_water_transition(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) qa_q1_check_water_transition(entry->q1_services, entry->q1_entity);
  else if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC) entry->quakec->check_water_transition(entry->quakec->ctx, actor);
}
static void ss_physics_write_angular_velocity(void *ctx, qa_owned_actor actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_QUAKEC)
    qa_app_sim_quakec_source_write_angular_velocity(entry->quakec->ctx, actor, velocity);
  else if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) entry->q1_entity->angular_velocity = velocity;
  else if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) entry->q2_entity->angular_velocity = velocity;
}
static void ss_physics_on_blocked(void *ctx, qa_owned_actor actor, qa_actor_id other) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor.id);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1)
    qa_q1_call_blocked(entry->q1_services, entry->q1_entity, entry->q1_entity->blocked, other);
  else if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2 && entry->q2_entity->blocked.fn)
    entry->q2_entity->blocked.fn(entry->q2_entity->blocked.ctx, entry->q2_entity, qa_q2_entity_services_game(entry->q2_services), other);
}

/* SimulationEvents host: now / clientFor / sourceSlot, and the
 * PresentationState super (presentation-state.ts) members. */
static qa_source_time ss_events_now(void *ctx) { return ((qa_app_shared_simulation *)ctx)->source_frame.time; }
static bool ss_events_client_for(void *ctx, qa_actor_id actor, qa_client_id *out) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(ctx, actor);
  if (!player) return false;
  *out = player->client;
  return true;
}
static bool ss_events_source_slot(void *ctx, qa_actor_id actor, double *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(((qa_app_shared_simulation *)ctx)->actors, actor);
  if (!source.has_value) return false;
  *out = (double)source.slot;
  return true;
}

/* GameplayAuthority hooks. */
static const char *ss_combat_team(void *ctx, qa_actor_id actor, const char *original) {
  return qa_app_shared_sim_canonical_team(ctx, actor, original);
}
static bool ss_combat_damage_allowed(void *ctx, qa_damage_request request) {
  qa_app_shared_simulation *sim = ctx;
  return sim->selected_q3_source == NULL || !qa_app_q3_selected_source_blocks_damage(sim->selected_q3_source, request);
}
static void ss_combat_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) {
  qa_app_shared_simulation *sim = ctx;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) ss_fail("Damaged actor has no body");
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, movement);
  const qa_numeric_operations *numeric = qa_create_numeric_operations(&timing.numeric);
  qa_vec3 velocity;
  velocity.x = numeric->add(numeric->ctx, body.velocity.x, impulse.x);
  velocity.y = numeric->add(numeric->ctx, body.velocity.y, impulse.y);
  velocity.z = numeric->add(numeric->ctx, body.velocity.z, impulse.z);
  body.velocity = velocity;
  qa_shared_body_table_write(qa_app_sim_shared_physics_bodies(sim->physics), actor, body);
}
static void ss_combat_before_reaction(void *ctx, qa_owned_actor actor, qa_damage_decision decision) {
  qa_app_shared_simulation *sim = ctx;
  qa_ss_map_set(&sim->last_attack, &actor, &decision.request.attack);
  sim->strategy->reaction_start(sim, actor, &decision);
  if (sim->selected_q3_source) {
    qa_q3_base_combat_bridge *bridge = qa_app_q3_selected_source_bridge(sim->selected_q3_source);
    qa_q3_damage_call call;
    if (qa_q3_base_combat_bridge_current_call(bridge, &call)) qa_q3_base_combat_bridge_before_reaction(bridge, decision);
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (qa_ss_is(decision.reaction, "death")) {
    if (sim->selected_q3_source) qa_app_q3_selected_source_died(sim->selected_q3_source, actor.id);
    if (player) {
      qa_app_sim_movement_player_set_flight(player, false);
      if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor.id);
      qa_app_shared_sim_step_hand_grenade(sim, player->actor.id, "dead");
    }
  }
  sim->strategy->reaction_end(sim, actor, &decision);
}
/* Q2Strategy.reactionEnd */
void qa_ss_q2_reaction_end(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *in) {
  qa_damage_decision decision = *in;
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  {
    qa_q2_game_services *game = sim->source.product->services;
    qa_q2_entity *entity = game->entity(game->ctx, &actor.id);
    if (entity) {
      /* entity.lastAttack = decision.request.attack: the entity borrows a
       * per-actor copy the simulation keeps until the actor is released. */
      qa_attack_provenance **stored = qa_ss_map_get(&sim->q2_entity_attacks, &actor.id);
      qa_attack_provenance *copy = stored ? *stored : malloc(sizeof *copy);
      if (!copy) qa_range_error("Out of memory");
      *copy = decision.request.attack;
      if (!stored) qa_ss_map_set(&sim->q2_entity_attacks, &actor.id, &copy);
      entity->last_attack = copy;
      if (player) qa_q2_product_runtime_before_reaction(sim->source.product, actor.id, copy);
    }
  }
  if (qa_ss_is(decision.reaction, "death") && player && player->character != QA_GAME_FAMILY_Q2) {
    qa_q2_game_services *game = sim->source.product->services;
    qa_q2_entity *entity = game->entity(game->ctx, &actor.id);
    if (entity) {
      qa_death_reaction death; memset(&death, 0, sizeof death);
      death.pain.attack = &decision.request.attack; death.pain.self = actor;
      death.pain.attacker = decision.request.attack.attacker; death.inflictor = decision.request.attack.inflictor;
      death.pain.damage = decision.applied_damage; death.pain.kick = decision.request.knockback; death.point = decision.request.point;
      qa_q2_players_record_death(sim->source.product->players, entity, game, death);
    }
  }
}
static void ss_combat_confirmed(void *ctx, qa_damage_outcome outcome) {
  qa_app_shared_simulation *sim = ctx;
  if (qa_ss_is(outcome.kind, "committed")) {
    qa_owned_actor actor;
    if (qa_session_actor_registry_resolve_owned(sim->actors, outcome.decision.request.target, &actor)) {
      qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &actor);
      if (character) qa_q2_character_actor_record_damage(*character, outcome.decision);
      sim->strategy->damage_committed(sim, actor, &outcome.decision);
    }
  }
  qa_simulation_event_payload payload; memset(&payload, 0, sizeof payload);
  payload.kind = QA_SIM_EVENT_DAMAGE; payload.outcome = outcome;
  qa_actor_id none; memset(&none, 0, sizeof none);
  qa_app_sim_simulation_events_append(sim->events, payload, false, none);
}
/* QuakeCStrategy.damageCommitted / Q2Strategy.damageCommitted */
void qa_ss_quakec_damage_committed(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player && player->character == QA_GAME_FAMILY_Q2 && decision->applied_damage > 0) {
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor.id);
    player->animation = qa_app_sim_quakec_character_animation(player->animation,
        (combat.has_value ? combat.value.health : 0) <= 0 ? "death" : "pain",
        player->bounds.max.z < player->standing_bounds.max.z);
  }
}
void qa_ss_q2_damage_committed(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision) {
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_entity *entity = game->entity(game->ctx, &actor.id);
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (entity && player && player->character == QA_GAME_FAMILY_Q2)
    qa_q2_players_record_damage(sim->source.product->players, entity, game, *decision);
  if (decision->has_feedback && qa_ss_is(decision->feedback_kind, "q2") && decision->feedback_power_armor > 0 && !player)
    qa_q2m_monsters_note_power_armor(&sim->source.product->monsters, actor.id, game->host->now(game->host->ctx));
  qa_q3_character_actor **q3 = qa_ss_map_get(&sim->characters, &actor);
  qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor.id);
  if (q3 && state && state->dead)
    state->respawn_time = qa_js_max(state->respawn_time, qa_q3_character_actor_get_respawn_eligible_after(*q3) / 1000);
}
static bool ss_original_pickup_eligible(void *ctx, qa_original_pickup_offer offer) {
  qa_app_shared_simulation *sim = ctx;
  return sim->selected_q3_source == NULL || qa_app_q3_selected_source_pickup_allowed(sim->selected_q3_source, offer);
}

/* FrameScheduler options. */
static void ss_world_think(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  qa_actor_callback_table_think(((qa_app_shared_simulation *)ctx)->callbacks, self, frame);
}
static qa_think_callback_result ss_scheduler_resolve(void *ctx, qa_provider_id provider, qa_callback_id callback) {
  (void)provider;
  qa_think_callback_result result; memset(&result, 0, sizeof result);
  if (qa_ss_is(callback, "world:think")) { result.has_value = true; result.value.ctx = ctx; result.value.call = ss_world_think; }
  return result;
}
static bool ss_scheduler_source_slot(void *ctx, qa_actor_id actor, int64_t *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(((qa_app_shared_simulation *)ctx)->actors, actor);
  if (!source.has_value) return false;
  *out = source.slot;
  return true;
}
static bool ss_scheduler_execution_provider(void *ctx, qa_actor_id actor, qa_provider_id *out) {
  return qa_app_shared_sim_execution_provider(ctx, actor, out);
}

/* actors.onRelease (746-761). */
static void ss_on_release(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ctx;
  qa_actor_id id = actor.id;
  qa_mod_client_applications_release(sim->mod_client_applications, id);
  qa_mod_client_outputs_release(sim->mod_client_outputs, id);
  qa_ss_map_delete(&sim->mod_client_commands, &id); qa_ss_map_delete(&sim->mod_client_admissions, &id);
  qa_app_shared_sim_events_retire(sim, id);
  qa_ss_map_delete(&sim->actor_executions, &id);
  qa_ss_map_delete(&sim->monster_missions, &id);
  qa_ss_map_delete(&sim->lmctf_teams, &id); qa_ss_map_delete(&sim->lmctf_health, &id); qa_ss_map_delete(&sim->lmctf_regen_at, &id);
  qa_ss_map_delete(&sim->deathball_teams, &id); qa_ss_map_delete(&sim->tag_health, &id);
  qa_ss_map_delete(&sim->native_equipment_players, &id); qa_ss_map_delete(&sim->native_equipment_alive, &id);
  qa_ss_map_delete(&sim->native_equipment_velocity, &id); qa_ss_map_delete(&sim->native_weapon_requests, &id);
  qa_ss_dispose *binding = qa_ss_map_get(&sim->selected_pickup_bindings, &id);
  if (binding) binding->dispose(binding->ctx);
  qa_ss_map_delete(&sim->selected_pickup_bindings, &id);
  if (sim->has_selected_arsenal) {
    if (sim->selected_arsenal.q1) qa_app_q1_selected_arsenal_remove(sim->selected_arsenal.q1, id);
    if (sim->selected_arsenal.q2) qa_app_q2_selected_arsenal_remove(sim->selected_arsenal.q2, id);
    if (sim->selected_arsenal.q3) qa_app_q3_selected_arsenal_remove(sim->selected_arsenal.q3, id);
  }
  qa_frame_scheduler_cancel(sim->scheduler, actor); qa_ss_map_delete(&sim->native_weapon_times, &id);
  if (sim->native_primary_weapons) qa_compat_q2_native_primary_weapons_release(sim->native_primary_weapons, id);
  if (sim->native_primary_inventory) qa_compat_q2_native_primary_inventory_release(sim->native_primary_inventory, id);
  qa_ss_map_delete(&sim->restored_native_inventory_selections, &id);
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &id);
  if (slot) qa_app_sim_weapon_slot_destroy(*slot);
  qa_ss_map_delete(&sim->weapon_slots, &id);
  qa_app_sim_movement_player **player = qa_ss_map_get(&sim->player_states, &actor);
  if (player) qa_app_sim_movement_player_destroy(*player);
  qa_ss_map_delete(&sim->player_states, &actor);
  qa_app_sim_movement_player **native = qa_ss_map_get(&sim->native_movement_players, &actor);
  if (native) qa_app_sim_movement_player_destroy(*native);
  qa_ss_map_delete(&sim->native_movement_players, &actor);
  qa_ss_map_delete(&sim->native_character_commands, &id); qa_ss_map_delete(&sim->native_character_health, &id);
  qa_ss_map_delete(&sim->native_mirrored_combat, &id); qa_ss_map_delete(&sim->native_exact_motions, &id);
  qa_ss_map_delete(&sim->native_ground_planes, &id); qa_ss_map_delete(&sim->native_move_carry, &id);
  qa_ss_map_delete(&sim->native_mirrored_health, &id); qa_ss_map_delete(&sim->native_mirror_write_reported, &id);
  qa_app_shared_sim_players_release(sim, actor); /* B3 owners: character actors, entry carry */
  qa_ss_map_delete(&sim->characters, &actor); qa_ss_map_delete(&sim->character_starts, &actor);
  qa_ss_map_delete(&sim->q3_arsenals, &actor); qa_ss_map_delete(&sim->q3_commands, &actor);
  qa_ss_map_delete(&sim->q1_characters, &actor); qa_ss_map_delete(&sim->q2_views, &id);
  qa_ss_map_delete(&sim->q2_characters, &actor); qa_ss_map_delete(&sim->character_ticks, &actor);
  qa_ss_map_delete(&sim->entry_carry, &actor); qa_ss_map_delete(&sim->detached_models, &actor);
  qa_ss_map_delete(&sim->source_models, &id); qa_ss_map_delete(&sim->source_model_read_order, &id);
  qa_ss_map_delete(&sim->view_models, &id);
  qa_ss_map_delete(&sim->last_attack, &actor);
  qa_attack_provenance **attack = qa_ss_map_get(&sim->q2_entity_attacks, &id);
  if (attack) free(*attack);
  qa_ss_map_delete(&sim->q2_entity_attacks, &id);
  qa_app_sim_q1_player_punch_release(sim->q1_punch, actor);
  qa_app_sim_source_pickup_cargo_release(sim->dropped_pickups, id);
}

/* Q1ClientVisibility client(slot) (788-799). */
static qa_q1_visibility_client ss_q1_visibility_client(void *ctx, int64_t slot) {
  qa_app_shared_simulation *sim = ctx;
  qa_q1_visibility_client client; memset(&client, 0, sizeof client);
  qa_actor_id actor; bool has_actor = false;
  if (slot == 0) has_actor = qa_app_shared_sim_world_actor(sim, &actor);
  else {
    size_t count = 0; const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
    for (size_t i = 0; i < count; i++) {
      qa_client_id found;
      if (qa_app_shared_sim_player_client(sim, players[i], &found) && found.slot == slot - 1) { actor = players[i]; has_actor = true; break; }
    }
  }
  qa_q1_client_eye eye; bool has_eye = has_actor && qa_app_shared_sim_q1_visibility_eye(sim, actor, &eye);
  client.has_actor = has_actor; if (has_actor) client.actor = actor;
  client.free = !has_actor || !qa_session_actor_registry_is_live(sim->actors, actor);
  qa_combat_state_result combat = has_actor ? qa_gameplay_authority_read(sim->combat, actor) : (qa_combat_state_result){0};
  client.health = !has_actor ? 0 : combat.has_value ? combat.value.health : 0;
  client.notarget = has_actor && qa_app_shared_sim_monster_target_notarget(sim, actor);
  client.origin = has_eye ? eye.origin : qa_ss_zero;
  client.view_offset = has_eye ? eye.view_offset : qa_ss_zero;
  return client;
}
static double ss_scene_point_leaf(void *ctx, qa_vec3 point) { return qa_shared_point_leaf(ctx, point); }
static double ss_scene_leaf_cluster(void *ctx, double leaf) { return qa_shared_leaf_cluster(ctx, leaf); }
static bool ss_scene_cluster_visible(void *ctx, double from, double to, const char *kind) { return qa_shared_cluster_visible(ctx, from, to, kind); }
static size_t ss_world_leaf_count(const qa_decoded_world *world) {
  return world->kind == QA_DECODED_WORLD_Q1 ? world->as.q1->leaf_count
      : world->kind == QA_DECODED_WORLD_Q2 ? world->as.q2->leaf_count : world->as.q3->leaf_count;
}

/* SimulationWeaponBehaviors host (801-809). */
static const qa_app_sim_qc_weapon_target *ss_behavior_targets(void *ctx, size_t *out_count) {
  return qa_app_shared_sim_weapon_behavior_targets(ctx, out_count);
}
static qa_vec3 ss_behavior_aim(void *ctx, qa_actor_id actor, double speed) {
  (void)speed;
  qa_vec3 forward = {0, 0, 0};
  qa_app_sim_player_view view = qa_app_shared_sim_player_view(ctx, actor);
  qa_donor_angle_vectors(view.angles, &forward, NULL, NULL);
  return forward;
}
static void ss_behavior_print(void *ctx, const qa_actor_id *actor, const char *text) { qa_app_shared_sim_print(ctx, text, actor); }
static qa_app_sim_qvm_weapon_behavior_source *ss_behavior_qvm(void *ctx, qa_app_prepared_weapon_behavior entry) {
  return qa_app_shared_sim_create_qvm_weapon_behavior(ctx, entry);
}
static qa_app_sim_rerelease_weapon_behavior_source *ss_behavior_native(void *ctx, qa_app_prepared_weapon_behavior entry,
    void (*next_frame)(void *frame_ctx), void *frame_ctx) {
  return qa_app_shared_sim_create_native_weapon_behavior(ctx, entry, next_frame, frame_ctx);
}

/* Q1PlayerPunch / ModMatchState / SourcePickupCargo bindings. */
static bool ss_punch_resolve_owned(void *ctx, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(((qa_app_shared_simulation *)ctx)->actors, actor, out);
}
static bool ss_punch_resolve_saved(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_saved(((qa_app_shared_simulation *)ctx)->actors, saved, out);
}
static bool ss_punch_source(void *ctx, qa_actor_id actor, qa_app_sim_q1_punch_owner *out) {
  return qa_app_shared_sim_q1_punch_owner(ctx, actor, out);
}
static qa_source_match_player_result ss_match_primary(void *ctx, qa_actor_id actor) {
  return qa_app_shared_sim_primary_match_player(ctx, actor);
}
static bool ss_cargo_is_live(void *ctx, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(((qa_app_shared_simulation *)ctx)->actors, actor);
}
static bool ss_cargo_source_of(void *ctx, qa_actor_id actor, qa_provider_id *out_provider, double *out_slot) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(((qa_app_shared_simulation *)ctx)->actors, actor);
  if (!source.has_value) return false;
  *out_provider = source.provider; *out_slot = (double)source.slot;
  return true;
}
static bool ss_cargo_at_source(void *ctx, qa_provider_id provider, double slot, qa_actor_id *out) {
  qa_owned_actor actor;
  if (!qa_session_actor_registry_at_source(((qa_app_shared_simulation *)ctx)->actors, provider, (int64_t)slot, &actor)) return false;
  *out = actor.id;
  return true;
}
static bool ss_cargo_resolve_saved(void *ctx, qa_saved_actor_id saved, qa_actor_id *out) {
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_saved(((qa_app_shared_simulation *)ctx)->actors, saved, &actor)) return false;
  *out = actor.id;
  return true;
}
static bool ss_mod_applications_live(void *ctx, qa_mod_client_identity identity) {
  qa_app_shared_simulation *sim = ctx;
  qa_client_id client;
  return qa_session_actor_registry_is_live(sim->actors, identity.actor) && qa_app_shared_sim_owns_client(sim, identity.client)
      && qa_app_shared_sim_player_client(sim, identity.actor, &client) && qa_same_client(client, identity.client);
}
static bool ss_mod_outputs_live(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ctx;
  qa_client_id client;
  return qa_session_actor_registry_is_live(sim->actors, actor) && qa_app_shared_sim_player_client(sim, actor, &client);
}

/* ---- the post-createSource source bindings (811-864) ---- */
static void ss_native_velocity_write(void *ctx, qa_owned_actor actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ctx;
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor.id, &client)) ss_fail("Native movement write requires an admitted player");
  qa_ss_map_set(&sim->native_equipment_velocity, &actor.id, &velocity);
}
static bool ss_native_velocity_read(void *ctx, qa_actor_id actor, qa_vec3 *out) {
  qa_vec3 *velocity = qa_ss_map_get(&((qa_app_shared_simulation *)ctx)->native_equipment_velocity, &actor);
  if (!velocity) return false;
  *out = *velocity;
  return true;
}
static void ss_qvm_velocity_write(void *ctx, qa_owned_actor actor, qa_vec3 velocity) {
  qa_app_shared_simulation *sim = ctx;
  qa_app_q3_guest_records *records = qa_app_q3_qvm_server_game_records(sim->source.qvm);
  double slot = qa_app_q3_guest_records_require_slot(records, actor.id);
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(records, slot, &state, stats, persistent, powerups, ammo);
  state.velocity = velocity;
  state.ground_entity_number = 1023;
  qa_qvm_game_data_write_player_state(qa_app_q3_guest_records_data(records), slot, &state);
}
/* Native weapon input scope (842-856). */
static void ss_native_weapon_scope(void *ctx, qa_mod_client_application_event *event) {
  qa_app_shared_simulation *sim = ctx;
  if (!qa_ss_is(event->application->scope, "client-command")) return;
  if (event->is_before) {
    sim->native_weapon_applications = ss_grow(sim->native_weapon_applications, &sim->native_weapon_application_capacity,
        sim->native_weapon_application_count + 1, sizeof *sim->native_weapon_applications);
    sim->native_weapon_applications[sim->native_weapon_application_count++] = event->application;
    qa_mod_client_application *application = event->application;
    if (sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q3")
        && qa_app_q3_selected_arsenal_has(sim->selected_arsenal.q3, application->identity.actor))
      qa_app_q3_selected_arsenal_observe_holdable_input(sim->selected_arsenal.q3, application->identity.actor,
          application->command, application->has_arsenal ? &application->arsenal : NULL);
  } else {
    if (sim->native_weapon_application_count == 0
        || sim->native_weapon_applications[sim->native_weapon_application_count - 1] != event->application)
      ss_fail("Native weapon input scope changed before completion");
    sim->native_weapon_application_count--;
  }
}
/* QVM weapon decision scope (857-869). */
static void ss_qvm_weapon_decision_pop(void *ctx) { ((qa_app_shared_simulation *)ctx)->qvm_weapon_decision_count--; }
static void ss_qvm_weapon_scope(void *ctx, qa_mod_client_application_event *event) {
  qa_app_shared_simulation *sim = ctx;
  if (!qa_ss_is(event->application->scope, "movement-slice")) return;
  if (event->is_before) {
    sim->qvm_weapon_decisions = ss_grow(sim->qvm_weapon_decisions, &sim->qvm_weapon_decision_capacity,
        sim->qvm_weapon_decision_count + 1, sizeof *sim->qvm_weapon_decisions);
    sim->qvm_weapon_decisions[sim->qvm_weapon_decision_count++] = (qa_ss_qvm_weapon_decision){event->application, false};
    return;
  }
  qa_ss_qvm_weapon_decision *decision = sim->qvm_weapon_decision_count
      ? &sim->qvm_weapon_decisions[sim->qvm_weapon_decision_count - 1] : NULL;
  if (!decision || decision->application != event->application) ss_fail("Original weapon decision lost its input invocation");
  /* try { ... } finally { pop } */
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, ss_qvm_weapon_decision_pop, sim);
  if (qa_ss_is(event->outcome, "completed")) qa_app_shared_sim_qvm_weapon_step(sim, event->application, decision->reached);
  qa_cleanup_pop(&cleanup);
  sim->qvm_weapon_decision_count--;
}
static void ss_track_subscription(qa_app_shared_simulation *sim, int64_t id) {
  int64_t *grown = realloc(sim->mod_client_subscriptions, (sim->mod_client_subscription_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  sim->mod_client_subscriptions = grown;
  sim->mod_client_subscriptions[sim->mod_client_subscription_count++] = id;
}

/* Monster navigation install view. */
static double ss_nav_time_seconds(void *ctx) { return qa_app_shared_sim_time_seconds(ctx); }
static qa_q1_services *ss_nav_q1_game(void *ctx) {
  qa_app_shared_simulation *sim = ctx;
  return (sim->strategy->as_q1(sim) != NULL) ? sim->source.q1_game : NULL;
}

/* ---- constructor ---- */
static void ss_init_containers(qa_app_shared_simulation *sim) {
  qa_ss_map_init(&sim->horde_fragged, QA_SS_KEY_ACTOR, sizeof(qa_ss_horde_frag));
  qa_ss_map_init(&sim->tag_health, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->lmctf_teams, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->lmctf_flags, QA_SS_KEY_NUMBER, sizeof(qa_ss_lmctf_flag));
  qa_ss_map_init(&sim->lmctf_runes, QA_SS_KEY_STRING, sizeof(qa_ss_lmctf_rune));
  qa_ss_map_init(&sim->lmctf_health, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->lmctf_regen_at, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->lmctf_spot_flags, QA_SS_KEY_STRING, sizeof(qa_vec3));
  qa_ss_map_init(&sim->deathball_teams, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->deathball_goals, QA_SS_KEY_NUMBER, sizeof(qa_vec3));
  qa_ss_map_init(&sim->actor_executions, QA_SS_KEY_ACTOR, sizeof(qa_app_sim_actor_execution));
  qa_ss_map_init(&sim->player_states, QA_SS_KEY_OWNED, sizeof(qa_app_sim_movement_player *));
  qa_ss_map_init(&sim->native_movement_players, QA_SS_KEY_OWNED, sizeof(qa_app_sim_movement_player *));
  qa_ss_map_init(&sim->native_character_commands, QA_SS_KEY_ACTOR, sizeof(qa_ss_character_command));
  qa_ss_map_init(&sim->native_character_health, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->guest_monster_plans, QA_SS_KEY_STRING, sizeof(qa_app_sim_guest_monster_plan *));
  qa_ss_map_init(&sim->shared_travel_applied, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->native_mirrored_combat, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->native_mirrored_health, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->native_mirror_write_reported, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->native_ground_planes, QA_SS_KEY_ACTOR, sizeof(qa_plane));
  qa_ss_map_init(&sim->native_move_carry, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->native_exact_motions, QA_SS_KEY_ACTOR, sizeof(qa_ss_exact_motion));
  qa_ss_map_init(&sim->q2_characters, QA_SS_KEY_OWNED, sizeof(qa_q2_character_actor *));
  qa_ss_map_init(&sim->character_ticks, QA_SS_KEY_OWNED, sizeof(double));
  qa_ss_map_init(&sim->entry_carry, QA_SS_KEY_OWNED, sizeof(qa_q1_travel_state));
  qa_ss_map_init(&sim->pending_start_items, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->detached_models, QA_SS_KEY_OWNED, sizeof(qa_ss_detached_model));
  qa_ss_map_init(&sim->q1_characters, QA_SS_KEY_OWNED, sizeof(qa_q1_character_actor *));
  qa_ss_map_init(&sim->q1_character_adjuncts, QA_SS_KEY_POINTER, 0);
  qa_ss_map_init(&sim->q2_views, QA_SS_KEY_ACTOR, sizeof(qa_q2_base_player_view));
  qa_ss_map_init(&sim->characters, QA_SS_KEY_OWNED, sizeof(qa_q3_character_actor *));
  qa_ss_map_init(&sim->character_starts, QA_SS_KEY_OWNED, sizeof(qa_ss_optional_actor));
  qa_ss_map_init(&sim->weapon_slots, QA_SS_KEY_ACTOR, sizeof(qa_app_sim_weapon_slot *));
  qa_ss_map_init(&sim->native_equipment_players, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->native_equipment_alive, QA_SS_KEY_ACTOR, sizeof(bool));
  qa_ss_map_init(&sim->native_weapon_requests, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->native_equipment_velocity, QA_SS_KEY_ACTOR, sizeof(qa_vec3));
  qa_ss_map_init(&sim->selected_q3_strings, QA_SS_KEY_NUMBER, sizeof(char *));
  qa_ss_map_init(&sim->primary_command_blocks, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->selected_original_weapons, QA_SS_KEY_STRING, sizeof(qa_ss_optional_item));
  qa_ss_map_init(&sim->q3_arsenals, QA_SS_KEY_OWNED, sizeof(qa_q3_arsenal_runtime_state));
  qa_ss_map_init(&sim->q3_commands, QA_SS_KEY_OWNED, sizeof(qa_actor_command));
  qa_ss_map_init(&sim->source_models, QA_SS_KEY_ACTOR, sizeof(qa_q2_presentation_event));
  qa_ss_map_init(&sim->source_model_read_order, QA_SS_KEY_ACTOR, sizeof(bool));
  qa_ss_map_init(&sim->presentation_snapshot, QA_SS_KEY_ACTOR, sizeof(qa_ss_presentation_pose));
  qa_ss_map_init(&sim->view_models, QA_SS_KEY_ACTOR, sizeof(qa_ss_view_model));
  qa_ss_map_init(&sim->monster_sources, QA_SS_KEY_STRING, sizeof(qa_app_sim_selected_monster_source *));
  qa_ss_map_init(&sim->monster_missions, QA_SS_KEY_ACTOR, sizeof(qa_monster_mission));
  qa_ss_map_init(&sim->settled_q3_sources, QA_SS_KEY_POINTER, 0);
  qa_ss_map_init(&sim->mod_client_admissions, QA_SS_KEY_ACTOR, 0);
  qa_ss_map_init(&sim->mod_client_commands, QA_SS_KEY_ACTOR, sizeof(qa_mod_client_command_receipt));
  qa_ss_map_init(&sim->restored_native_inventory_selections, QA_SS_KEY_ACTOR, sizeof(qa_ss_optional_item));
  qa_ss_map_init(&sim->native_travel_consumed, QA_SS_KEY_CLIENT, 0);
  qa_ss_map_init(&sim->native_weapon_times, QA_SS_KEY_ACTOR, sizeof(double));
  qa_ss_map_init(&sim->selected_pickup_bindings, QA_SS_KEY_ACTOR, sizeof(qa_ss_dispose));
  qa_ss_map_init(&sim->last_attack, QA_SS_KEY_OWNED, sizeof(qa_attack_provenance));
  qa_ss_map_init(&sim->q2_entity_attacks, QA_SS_KEY_ACTOR, sizeof(qa_attack_provenance *));
  qa_ss_map_init(&sim->area_portals, QA_SS_KEY_NUMBER, sizeof(bool));
  qa_ss_map_init(&sim->quake_world_touched, QA_SS_KEY_NUMBER, 0);
  qa_ss_map_init(&sim->source.visited, QA_SS_KEY_STRING, sizeof(void *));
  qa_ss_map_init(&sim->source.clients, QA_SS_KEY_CLIENT, sizeof(qa_owned_actor));
  /* TS field initializers. */
  sim->horde_pending_wave = 1;
  sim->horde_admission_at = -INFINITY;
  sim->horde_admission_players = -1;
  sim->debug_line_frame = -1;
  sim->world_text_frame = -1;
  sim->presentation_ahead = true;
  sim->source_round_settlement.kind = "none";
  sim->source.kind = QA_SS_SOURCE_LOADING;
  qa_ss_set_source_strategy(sim);
  qa_q3_death_animation_sequence_init(&sim->death_animations);
  qa_app_sim_bot_services_init(&sim->bot_services);
  if (!qa_world_debug_line_store_init_default(&sim->debug_line_store)) qa_range_error("Out of memory");
  qa_world_text_store_init(&sim->world_text_store);
  if (!qa_event_ring_init(&sim->transitions, 1024, sizeof(qa_transition_intent))
      || !qa_event_ring_open_cursor(&sim->transitions, &sim->transitions_cursor, false)
      || !qa_event_ring_init(&sim->quake_world_commands, 8192, sizeof(qa_ss_quakeworld_command))
      || !qa_event_ring_open_cursor(&sim->quake_world_commands, &sim->quake_world_cursor, false)
      || !qa_event_ring_init(&sim->mod_client_drops, 1024, sizeof(qa_ss_mod_client_drop))
      || !qa_event_ring_open_cursor(&sim->mod_client_drops, &sim->mod_client_drops_cursor, false))
    qa_range_error("Out of memory");
}

/* Number.isSafeInteger */
static bool ss_safe_integer(double value) {
  return isfinite(value) && trunc(value) == value && fabs(value) <= 9007199254740991.0;
}
static bool ss_has_execution(const qa_executable_recipe *recipe, qa_execution_module_kind kind, const char *role,
                             const qa_resolved_execution_module **out) {
  for (size_t i = 0; i < recipe->execution_count; i++)
    if (recipe->execution[i].kind == kind && (role == NULL || qa_ss_is(recipe->execution[i].role, role))) {
      if (out) *out = &recipe->execution[i];
      return true;
    }
  return false;
}

typedef struct ss_construct { qa_app_shared_simulation *sim; bool loading; const qa_save_image *saved; } ss_construct;
static void ss_construct_source(void *ctx);

/* new SharedSimulation(options, loading) (587-962). */
static qa_app_shared_simulation *ss_create(qa_app_simulation_options options, bool loading) {
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  if (!sim) qa_range_error("Out of memory");
  sim->options = options;
  ss_init_containers(sim);
  /* Field initializers: modClientApplications, modClientOutputs. */
  sim->mod_client_applications = qa_mod_client_applications_create(ss_mod_applications_live, sim);
  sim->mod_client_outputs = qa_mod_client_outputs_create(ss_mod_outputs_live, sim);
  if (!sim->mod_client_applications || !sim->mod_client_outputs) qa_range_error("Out of memory");
  ss_resolve_world(sim);
  if (options.restore) {
    sim->source_items_restore = calloc(1, sizeof *sim->source_items_restore);
    if (!sim->source_items_restore || !qa_prepare_source_item_restore(options.restore, sim->source_items_restore))
      qa_range_error("Out of memory");
    sim->options.restore = &sim->source_items_restore->primary;
    options = sim->options;
  }
  sim->mod_clients = qa_app_shared_sim_create_mod_clients(sim);
  if (!loading && options.recipe.equipment.grapple.is_enabled && qa_ss_is(options.recipe.equipment.grapple.mechanic, "q3-qvm"))
    ss_fail("Authored QVM grapple requires asynchronous loadSimulation");
  const qa_mod_session_checkpoint *restore_mods = options.restore ? options.restore->mods : NULL;
  if (!loading && (options.prepared_mod_count != 0 || qa_app_sim_enabled_mod_count(options.enabled_mods) != 0
      || (restore_mods && restore_mods->mod_count != 0) || qa_app_sim_mod_travel_count(options.mod_travel) != 0))
    ss_fail("Selected gameplay mods require asynchronous loadSimulation");
  const qa_resolved_execution_module *native_module = NULL;
  (void)ss_has_execution(&options.recipe, QA_EXECUTION_MODULE_NATIVE, NULL, &native_module);
  /* nativeModule?.api.kind === "q3-qagame" ? undefined : nativeModule */
  bool native_q3 = native_module != NULL && native_module->api.kind == QA_GAME_API_Q3 && native_module->api.as.q3.kind == QA_Q3_API_QAGAME;
  const qa_resolved_execution_module *native = native_q3 ? NULL : native_module;
  if (native != NULL) {
    const qa_app_sim_native_q2_guest_options *guest = qa_app_shared_sim_q2_guest(sim);
    bool abi = guest && ((native->api.kind == QA_GAME_API_Q2_GAME && native->api.as.q2_game.kind == QA_Q2_GAME_API_CLASSIC
                          && qa_ss_is(native->profile.kind, "windows-i386") && qa_ss_is(guest->edition, "classic"))
                         || (native->api.kind == QA_GAME_API_Q2_GAME && native->api.as.q2_game.kind == QA_Q2_GAME_API_RERELEASE
                          && qa_ss_is(native->profile.kind, "windows-x86-64") && qa_ss_is(guest->edition, "rerelease")));
    if (!qa_ss_is(native->role, "server-game") || !abi || !guest || !ss_execution_equal(native, ss_q2_guest_execution(guest))
        || options.recipe.execution_count != 1 || options.has_travel)
      ss_fail("Native Quake II requires its exact prepared API and ABI");
  } else if (options.has_q2_guest) ss_fail("Prepared Quake II native module differs from selected execution");
  const qa_resolved_execution_module *qvm = NULL;
  (void)ss_has_execution(&options.recipe, QA_EXECUTION_MODULE_QVM, "server-game", &qvm);
  const qa_resolved_execution_module *q3_server = qvm != NULL ? qvm
      : native_q3 && native_module->role != NULL && qa_ss_is(native_module->role, "server-game") ? native_module : NULL;
  if (q3_server != NULL) {
    qa_app_assert_q3_guest_recipe(options.recipe, q3_server);
    const qa_app_sim_q3_guest_options *guest = options.q3_guest;
    if (!guest || !ss_execution_equal(q3_server, &guest->prepared->execution) || options.q3_session
        || (!options.restore && options.restored_client_count != 0)
        || (options.has_initial_source_milliseconds && (!ss_safe_integer(options.initial_source_milliseconds)
            || options.initial_source_milliseconds < 0 || options.initial_source_milliseconds > 0x7fffffff)))
      ss_fail("A Q3 server game requires a prepared native map without travel state");
  } else if (options.q3_guest || ss_has_execution(&options.recipe, QA_EXECUTION_MODULE_QVM, NULL, NULL)) {
    ss_fail("Prepared Q3 guest does not match the selected server execution");
  }
  sim->session = options.identity.session;
  bool q1_travel = options.has_travel && qa_ss_is(options.travel.source_kind, "q1");
  qa_q1_campaign_state_init(&sim->q1_campaign, q1_travel ? options.travel.flags : 0,
      (int)(q1_travel ? options.travel.skill : options.skill));
  sim->recipe = options.recipe;
  qa_app_sim_saved_settings settings; memset(&settings, 0, sizeof settings);
  bool has_settings = false;
  if (options.restore) { settings = qa_app_sim_saved_simulation_settings(*options.restore); has_settings = true; }
  sim->start_items = qa_ss_strdup(!options.restore ? (options.start_items ? options.start_items : "") : settings.start_items);
  const qa_app_sim_native_q2_travel *native_travel = options.native_q2_travel;
  sim->initial_spawn_point = qa_ss_strdup(native_travel
      ? (native_travel->rerelease ? native_travel->rerelease_travel.base.spawn_point : native_travel->classic.base.spawn_point)
      : options.has_travel ? options.travel.spawn_point
      : !options.restore ? (options.initial_spawn_point ? options.initial_spawn_point : "") : settings.initial_spawn_point);
  const qa_resolved_execution_module *quakec = NULL;
  if (ss_has_execution(&options.recipe, QA_EXECUTION_MODULE_QUAKEC, NULL, &quakec)) {
    const qa_app_sim_prepared_quakec_source *prepared = options.prepared_quakec;
    if (!prepared || !ss_execution_equal(quakec, &prepared->execution) || options.recipe.execution_count != 1)
      ss_fail("QuakeC simulation requires the prepared native supported artifact");
    if (options.recipe.enemies.kind == QA_ENEMY_SELECTION_PLAYERS) ss_fail("QuakeC worlds have no bot client admission for player-selected enemies");
    bool quakeworld = quakec->api.kind == QA_GAME_API_QUAKEC && quakec->api.as.quakec.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
    if (quakeworld && options.max_clients > 32) ss_fail("Native QuakeWorld supports at most 32 clients");
    bool travel_qc = options.has_travel && (qa_ss_is(options.travel.source_kind, "netquake") || qa_ss_is(options.travel.source_kind, "quakeworld"));
    if ((options.has_travel && (options.restore || (travel_qc && !qa_ss_is(options.travel.source_kind, quakeworld ? "quakeworld" : "netquake"))))
        || (!options.restore && options.restored_client_count != 0)
        || (!options.original_save_candidate && options.has_initial_source_milliseconds && options.initial_source_milliseconds != 1000))
      ss_fail("QuakeC travel must use the same native ABI and a fresh source clock");
  } else if (options.prepared_quakec) ss_fail("Prepared QuakeC artifact does not match the selected execution");
  if (options.recipe.weapons.count != 1) ss_fail("This source arsenal requires one selected weapon provider");
  sim->weapon_provider = options.recipe.weapons.values[0];
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider);
  const qa_save_image *saved = options.restore;
  if (saved) {
    qa_app_sim_validate_simulation_save(*saved);
    if (settings.skill != options.skill || !qa_ss_is(settings.mode, options.mode) || settings.max_clients != options.max_clients)
      ss_fail("Saved simulation rules differ from construction settings");
    if (!ss_recipe_equal(&saved->recipe, &options.recipe)) ss_fail("Saved simulation recipe differs from the mounted recipe");
    for (size_t i = 0; i < options.restored_client_count; i++)
      if (!qa_app_shared_sim_owns_client(sim, options.restored_clients[i])) ss_fail("Restored client belongs to another session");
    bool mismatch = options.restored_client_count != settings.client_slot_count;
    for (size_t i = 0; i < options.restored_client_count && !mismatch; i++)
      for (size_t j = i + 1; j < options.restored_client_count; j++)
        if (options.restored_clients[i].slot == options.restored_clients[j].slot) mismatch = true;
    for (size_t i = 0; i < settings.client_slot_count && !mismatch; i++)
      for (size_t j = i + 1; j < settings.client_slot_count; j++)
        if (settings.client_slots[i] == settings.client_slots[j]) mismatch = true;
    for (size_t i = 0; i < settings.client_slot_count && !mismatch; i++) {
      bool found = false;
      for (size_t j = 0; j < options.restored_client_count; j++) if ((double)options.restored_clients[j].slot == settings.client_slots[i]) found = true;
      if (!found) mismatch = true;
    }
    if (mismatch) ss_fail("Saved players do not match the restored client slots");
  }
  double initial = !saved ? (options.has_initial_source_milliseconds ? options.initial_source_milliseconds : options.prepared_quakec ? 1000 : 0)
      : qa_ss_seconds(saved->frame.time) * 1000;
  if (!isfinite(initial) || initial < 0) qa_range_error("Initial source time must be finite and nonnegative");
  bool milliseconds = timing.clock.kind == QA_CLOCK_Q2_RERELEASE || timing.clock.kind == QA_CLOCK_Q3;
  qa_source_clock_init(&sim->clock, (qa_source_time){milliseconds ? QA_SOURCE_TIME_MILLISECONDS : QA_SOURCE_TIME_SECONDS,
                                                     milliseconds ? initial : initial / 1000}, saved ? saved->frame.frame : 0);
  sim->host_milliseconds = initial;
  sim->source_scheduling_milliseconds = initial;
  sim->selected_milliseconds = initial;
  sim->source_frame = saved ? saved->frame : qa_source_clock_frame(&sim->clock);
  sim->equipment_frame = sim->source_frame;
  sim->grapple_frame = sim->source_frame;
  sim->random = qa_app_sim_source_random_create(options.seed, timing.clock.kind == QA_CLOCK_Q2_RERELEASE ? "q2-rerelease" : "classic");
  if (!saved) sim->actors = qa_session_actor_registry_create(&sim->options.identity, 65536);
  else {
    qa_provider_checkpoint slots = qa_app_sim_simulation_provider_checkpoint(*saved, "world:source-slots");
    qa_source_actors_result sources;
    if (!qa_read_source_actors_checkpoint(&slots, &sources)) qa_range_error("Out of memory");
    sim->actors = qa_session_actor_registry_restore(&sim->options.identity, saved->actors, saved->actor_count,
        sources.entries, sources.count, 65536);
    qa_source_actors_result_free(&sources);
  }
  if (!sim->actors) qa_range_error("Out of memory");
  sim->source_match = qa_mod_match_state_create(sim->actors, ss_match_primary, sim);
  qa_app_sim_pickup_registry registry = {sim, ss_cargo_is_live, ss_cargo_source_of, ss_cargo_at_source, ss_cargo_resolve_saved};
  sim->dropped_pickups = qa_app_sim_source_pickup_cargo_create(registry, sim->recipe.map.entities.provider);
  qa_app_sim_q1_punch_actors punch_actors = {sim, ss_punch_resolve_owned, ss_punch_resolve_saved};
  sim->q1_punch = qa_app_sim_q1_player_punch_create(punch_actors, ss_punch_source, sim);
  sim->callbacks = qa_actor_callback_table_create(sim->actors);
  sim->scene = qa_create_scene_queries(sim->world);
  if (!sim->source_match || !sim->callbacks || !sim->scene) qa_range_error("Out of memory");
  qa_app_sim_shared_physics_options physics; memset(&physics, 0, sizeof physics);
  physics.ctx = sim; physics.actors = sim->actors; physics.callbacks = sim->callbacks; physics.scene = sim->scene;
  physics.numeric = timing.numeric;
  physics.q2_edition = sim->recipe.map.entities.content && strstr(sim->recipe.map.entities.content, ":rerelease:") ? "rerelease" : "classic";
  physics.take_kill_velocity = ss_take_kill_velocity;
  physics.stop_speed = ss_stop_speed;
  physics.source_order = ss_physics_source_order; physics.world_actor = ss_physics_world_actor;
  physics.get_collision = ss_physics_collision; physics.get_motion = ss_physics_motion;
  physics.get_flags = ss_physics_flags; physics.write_flags = ss_physics_write_flags;
  physics.event = ss_physics_event; physics.q1_water_transition = ss_physics_q1_water_transition;
  physics.write_angular_velocity = ss_physics_write_angular_velocity; physics.on_blocked = ss_physics_on_blocked;
  sim->physics = qa_app_sim_shared_physics_create(physics);
  sim->inventory = qa_shared_inventory_table_create(sim->actors);
  if (!sim->physics || !sim->inventory) qa_range_error("Out of memory");
  qa_app_sim_simulation_events_options events; memset(&events, 0, sizeof events);
  events.ctx = sim; events.bodies = qa_app_sim_shared_physics_bodies(sim->physics);
  events.now = ss_events_now; events.client_for = ss_events_client_for; events.source_slot = ss_events_source_slot;
  qa_app_shared_sim_bind_presentation_super(sim, &events);
  sim->events = qa_app_sim_simulation_events_create(events);
  /* TS SimulationEvents overrides emitOwned; PresentationState's own owner emits dispatch to it virtually. */
  if (sim->events) qa_app_presentation_state_set_emit_owned_override(sim->presentation, ss_events_emit_owned_override, sim);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  hooks.ctx = sim; hooks.team = ss_combat_team; hooks.damage_allowed = ss_combat_damage_allowed;
  hooks.impulse = ss_combat_impulse; hooks.before_reaction = ss_combat_before_reaction; hooks.confirmed = ss_combat_confirmed;
  sim->combat = qa_gameplay_authority_create(sim->actors, sim->callbacks, hooks);
  if (!sim->events || !sim->combat) qa_range_error("Out of memory");
  sim->original_pickups = qa_shared_original_pickup_admission_create(sim->actors, sim->combat, sim->inventory,
      ss_original_pickup_eligible, sim);
  if (!sim->original_pickups) qa_range_error("Out of memory");
  qa_app_shared_sim_register_combat(sim);
  qa_provider_reference foreign_reference; const char *foreign_family = NULL, *foreign_edition = NULL;
  bool foreign = qa_app_shared_sim_foreign_adjunct_identity(sim, &foreign_reference, &foreign_family, &foreign_edition);
  /* The foreign adjunct thinks last in mixed ordering: its world reacts
   * to the primary source's frame, never the reverse. */
  qa_frame_ordering ordering = sim->recipe.ordering;
  if (foreign && ordering.kind == QA_FRAME_ORDERING_MIXED) {
    bool included = false;
    for (size_t i = 0; i < ordering.provider_count; i++) if (qa_ss_is(ordering.providers[i], foreign_reference.provider)) included = true;
    if (!included) {
      sim->scheduler_providers = malloc((ordering.provider_count + 1) * sizeof *sim->scheduler_providers);
      if (!sim->scheduler_providers) qa_range_error("Out of memory");
      memcpy(sim->scheduler_providers, ordering.providers, ordering.provider_count * sizeof *ordering.providers);
      sim->scheduler_providers[ordering.provider_count] = foreign_reference.provider;
      ordering.providers = sim->scheduler_providers; ordering.provider_count++;
    }
  }
  sim->scheduler_clock_count = sim->recipe.timing_count + (foreign ? 1 : 0);
  sim->scheduler_clocks = calloc(sim->scheduler_clock_count ? sim->scheduler_clock_count : 1, sizeof *sim->scheduler_clocks);
  if (!sim->scheduler_clocks) qa_range_error("Out of memory");
  for (size_t i = 0; i < sim->recipe.timing_count; i++)
    sim->scheduler_clocks[i] = (qa_scheduler_clock){sim->recipe.timing[i].provider, sim->recipe.timing[i].clock};
  if (foreign) {
    qa_game_family family = QA_GAME_FAMILY_Q2; qa_game_family_parse(foreign_family, &family);
    qa_provider_timing native;
    if (!qa_native_provider_timing(foreign_reference, family, qa_ss_is(foreign_edition, "rerelease"), &native)) qa_range_error("Out of memory");
    sim->scheduler_clocks[sim->recipe.timing_count] = (qa_scheduler_clock){foreign_reference.provider, native.clock};
    qa_native_provider_timing_free(&native);
  }
  qa_scheduler_options scheduler; memset(&scheduler, 0, sizeof scheduler);
  scheduler.actors = qa_session_actor_registry_as_contract(sim->actors);
  scheduler.ordering = ordering;
  scheduler.clocks = sim->scheduler_clocks; scheduler.clock_count = sim->scheduler_clock_count;
  scheduler.ctx = sim; scheduler.source_slot = ss_scheduler_source_slot;
  scheduler.execution_provider = ss_scheduler_execution_provider; scheduler.resolve = ss_scheduler_resolve;
  sim->scheduler = qa_frame_scheduler_create(scheduler);
  if (!sim->scheduler) qa_range_error("Out of memory");
  sim->release_subscription = qa_session_actor_registry_on_release(sim->actors, ss_on_release, sim);
  qa_app_shared_sim_create_monster_movement(sim, timing.numeric, sim->random, &sim->q1_movement);
  qa_q1_client_visibility_scene visibility = {sim->scene, ss_world_leaf_count(&sim->world),
      ss_scene_point_leaf, ss_scene_leaf_cluster, ss_scene_cluster_visible};
  sim->q1_client_visibility = qa_q1_client_visibility_create((int64_t)options.max_clients, sim, ss_q1_visibility_client, visibility);
  if (!sim->q1_client_visibility) qa_range_error("Out of memory");
  qa_app_sim_weapon_behavior_runtime_host behavior; memset(&behavior, 0, sizeof behavior);
  behavior.actors = sim->actors; behavior.scene = qa_shared_as_scene_queries(sim->scene);
  behavior.mode = options.mode; behavior.seed = options.seed; behavior.ctx = sim;
  behavior.targets = ss_behavior_targets; behavior.aim = ss_behavior_aim; behavior.print = ss_behavior_print;
  behavior.create_qvm = ss_behavior_qvm; behavior.create_native = ss_behavior_native;
  qa_app_prepared_weapon_behavior *prepared = NULL;
  if (options.weapon_behavior_count) {
    prepared = malloc(options.weapon_behavior_count * sizeof *prepared);
    if (!prepared) qa_range_error("Out of memory");
    for (size_t i = 0; i < options.weapon_behavior_count; i++) prepared[i] = *(const qa_app_prepared_weapon_behavior *)options.weapon_behaviors[i];
  }
  sim->weapon_behavior = qa_app_sim_simulation_weapon_behaviors_create(prepared, options.weapon_behavior_count, behavior);
  free(prepared);
  if (!sim->weapon_behavior) qa_range_error("Out of memory");
  /* try { ... } catch (error) { cleanup; throw } (810-961). */
  ss_construct construct = {sim, loading, saved};
  qa_caught_error caught;
  if (!qa_try(ss_construct_source, &construct, &caught)) {
    qa_caught_error cleanup;
    if ((sim->strategy->as_q3_qvm(sim) != NULL)) {
      size_t failures = 1;
      if (!qa_try((void (*)(void *))qa_app_q3_qvm_server_game_discard, sim->source.qvm, &cleanup)) failures++;
      if (!qa_try((void (*)(void *))qa_app_shared_sim_close, sim, &cleanup)) failures++;
      if (failures != 1) qa_raise("AggregateError", 0, 0, "Q3 guest candidate restore and cleanup failed");
      qa_rethrow(&caught);
    }
    if (!qa_try((void (*)(void *))qa_app_shared_sim_close, sim, &cleanup))
      qa_raise("AggregateError", 0, 0, "Source candidate initialization and cleanup failed");
    qa_rethrow(&caught);
  }
  if (has_settings) qa_save_value_free(settings.value), free(settings.client_slots);
  return sim;
}

/* The try block of the constructor (811-950). */
static void ss_construct_source(void *ctx) {
  ss_construct *construct = ctx;
  qa_app_shared_simulation *sim = construct->sim;
  qa_app_simulation_options options = sim->options;
  bool loading = construct->loading;
  const qa_save_image *saved = construct->saved;
  qa_app_shared_sim_create_source(sim, loading, &sim->source);
  qa_ss_set_source_strategy(sim); /* this.setSource(this.createSource(loading)) */
  if (qa_app_shared_sim_foreign_lmctf(sim) && (sim->strategy->as_q1(sim) != NULL))
    qa_app_shared_sim_register_lmctf_q1_haste_rules(sim, sim->source.q1_game);
  if ((sim->strategy->as_q2_native(sim) != NULL)) {
    qa_app_sim_player_velocity writer = {sim, ss_native_velocity_write, ss_native_velocity_read};
    if (qa_ss_is(sim->source.edition, "classic"))
      qa_app_sim_classic_guest_services_set_player_velocity_writer(qa_app_sim_classic_guest_world_services(sim->source.classic_world), writer);
    else qa_app_sim_rerelease_guest_services_set_player_velocity_writer(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world), writer);
  } else if ((sim->strategy->as_q3_qvm(sim) != NULL)) {
    qa_app_q3_guest_records_set_player_velocity_writer(qa_app_q3_qvm_server_game_records(sim->source.qvm), ss_qvm_velocity_write, sim);
    const qa_app_sim_q3_guest_options *guest = options.q3_guest;
    if (qa_app_prepared_q3_primary_input(guest->prepared) != NULL) qa_app_shared_sim_bind_qvm_input(sim);
  }
  if ((sim->strategy->as_q3_qvm(sim) != NULL) && (!qa_ss_is(sim->weapon_provider.content, sim->recipe.map.entities.content)
      || !qa_ss_is(sim->weapon_provider.provider, sim->recipe.map.entities.provider)
      || (sim->recipe.equipment.grapple.is_enabled && !qa_ss_is(sim->recipe.equipment.grapple.mechanic, "q3-qvm"))
      || sim->recipe.equipment.hand_grenades.is_enabled)) {
    qa_owned_actor world;
    if (!qa_session_actor_registry_at_source(sim->actors, sim->recipe.map.entities.provider, 1022, &world))
      world = qa_session_actor_registry_allocate_at_source(sim->actors, sim->recipe.map.entities.provider, 1022, "q3:worldspawn");
    qa_body_state body; memset(&body, 0, sizeof body);
    qa_shared_body_table_create_body(qa_app_sim_shared_physics_bodies(sim->physics), world, body);
  }
  sim->hand_grenades = qa_app_shared_sim_create_hand_grenades(sim);
  sim->grapple = qa_app_shared_sim_create_grapple(sim);
  qa_game_family weapon_family = qa_app_sim_provider_family(sim->weapon_provider.provider);
  const char *supply_family = sim->strategy->supply_family(sim);
  qa_game_family source_family = qa_ss_is(supply_family, "q1") ? QA_GAME_FAMILY_Q1 : qa_ss_is(supply_family, "q2") ? QA_GAME_FAMILY_Q2 : QA_GAME_FAMILY_Q3;
  if (!qa_ss_is(sim->weapon_provider.content, sim->recipe.map.entities.content) || weapon_family != source_family) {
    if (weapon_family == QA_GAME_FAMILY_Q1) qa_app_shared_sim_create_selected_q1_arsenal(sim, &sim->selected_arsenal);
    else if (weapon_family == QA_GAME_FAMILY_Q2) qa_app_shared_sim_create_selected_q2_arsenal(sim, &sim->selected_arsenal);
    else qa_app_shared_sim_create_selected_q3_arsenal(sim, &sim->selected_arsenal);
    sim->has_selected_arsenal = true;
  }
  if ((sim->strategy->as_q2_native(sim) != NULL) && sim->has_selected_arsenal && qa_app_shared_sim_native_primary(sim) != NULL)
    ss_track_subscription(sim, qa_mod_client_applications_subscribe(sim->mod_client_applications, ss_native_weapon_scope, sim));
  if ((sim->strategy->as_q3_qvm(sim) != NULL) && sim->has_selected_arsenal && sim->source.qvm_weapons)
    ss_track_subscription(sim, qa_mod_client_applications_subscribe(sim->mod_client_applications, ss_qvm_weapon_scope, sim));
  if (qa_app_shared_sim_ts_primary(sim)) qa_app_shared_sim_create_foreign_world(sim);
  if ((sim->strategy->as_q2_native(sim) != NULL) || (sim->strategy->as_q3_qvm(sim) != NULL)
      || (sim->strategy->as_q3(sim) != NULL) || (sim->strategy->as_quakec(sim) != NULL)) qa_app_shared_sim_prepare_guest_monsters(sim);
  else qa_app_shared_sim_prepare_selected_monsters(sim);
  if ((sim->strategy->as_q3_qvm(sim) == NULL) && options.monster_navigation) {
    qa_app_sim_monster_navigation_simulation view = {sim, &sim->recipe, sim->scene, ss_nav_time_seconds, ss_nav_q1_game};
    qa_app_sim_application_monster_navigation_install((qa_app_sim_application_monster_navigation *)options.monster_navigation,
        view, &sim->q1_movement);
  }
  if (saved) {
    if ((sim->strategy->as_q2_native(sim) == NULL) || !loading) {
      qa_app_shared_sim_restore(sim, saved, false);
      if (options.has_travel) qa_app_shared_sim_restore_campaign_travel(sim, &options.travel);
      if (sim->source_collision_settings) qa_shared_bind_collision_settings(sim->scene, sim->source_collision_settings);
    }
  } else if ((sim->strategy->as_q1(sim) != NULL)) {
    if (qa_ss_is(sim->world_kind, "q1-bsp")) {
      if (!qa_comp_q1_source_composition_spawn_map(sim->source.q1_composition, sim->q1_map, NULL)) qa_range_error("Out of memory");
    } else {
      size_t entity_count = 0;
      qa_q1_entity *entities = qa_q1_parse_entities(sim->world_entities, &entity_count);
      size_t model_count = sim->world.kind == QA_DECODED_WORLD_Q2 ? sim->world.as.q2->model_count : sim->world.as.q3->model_count;
      qa_bounds *bounds = calloc(model_count ? model_count : 1, sizeof *bounds);
      if (!bounds) qa_range_error("Out of memory");
      for (size_t i = 0; i < model_count; i++) bounds[i] = qa_shared_model_bounds(sim->scene, (double)i);
      char *map_name = qa_ss_map_name(sim->recipe.map.geometry.requested_path);
      qa_q1_spawn_report report; memset(&report, 0, sizeof report);
      if (!qa_comp_q1_source_composition_spawn_entities(sim->source.q1_composition, entities, entity_count,
              bounds, model_count, map_name, NULL, 0, &report)) qa_range_error("Out of memory");
      double *ordinals = calloc(report.foreign_count ? report.foreign_count : 1, sizeof *ordinals);
      if (!ordinals) qa_range_error("Out of memory");
      for (size_t i = 0; i < report.foreign_count; i++) ordinals[i] = report.foreign[i].ordinal;
      qa_app_shared_sim_spawn_foreign_world(sim, ordinals, report.foreign_count, sim->world_entities);
      free(ordinals); free(map_name); free(bounds);
      qa_q1_spawn_report_free(&report); qa_q1_entities_destroy(entities, entity_count);
    }
  } else if ((sim->strategy->as_quakec(sim) != NULL)) {
    if (options.has_travel && (qa_ss_is(options.travel.source_kind, "quakeworld") || qa_ss_is(options.travel.source_kind, "netquake"))) {
      const qa_app_sim_quakec_source_travel *travel = options.travel.rerelease;
      for (size_t i = 0; i < travel->client_count; i++)
        if (!qa_app_shared_sim_owns_client(sim, travel->clients[i].client)) ss_fail("QuakeC travel client belongs to another session");
      qa_app_sim_quakec_source_restore_travel(sim->source.quakec, *travel);
    }
    qa_app_sim_quakec_source_spawn_map(sim->source.quakec);
    if (!options.restore) qa_app_shared_sim_admit_guest_monsters(sim);
  } else if ((sim->strategy->as_q3(sim) != NULL)) {
    qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(sim->source.q3);
    qa_cvar_registry_clear_modified(host->cvars, "sv_maxclients");
    if (qa_app_sim_provider_family(sim->recipe.combat.provider) == QA_GAME_FAMILY_Q3) {
      qa_combat_policy bridge_policy = qa_q3_base_combat_bridge_policy(qa_app_q3_source_runtime_bridge(sim->source.q3));
      qa_app_shared_sim_keep_owned(sim, bridge_policy.ctx, ss_release_q3_bridge_policy);
      sim->dispose_source_combat = qa_gameplay_authority_register(sim->combat, bridge_policy);
      sim->has_dispose_source_combat = true;
    }
    qa_app_q3_source_runtime_load(sim->source.q3);
    if (!options.restore) {
      size_t count = qa_app_q3_source_runtime_foreign_spawns(sim->source.q3, NULL, 0);
      qa_app_q3_foreign_spawn *spawns = calloc(count ? count : 1, sizeof *spawns);
      double *ordinals = calloc(count ? count : 1, sizeof *ordinals);
      if (!spawns || !ordinals) qa_range_error("Out of memory");
      qa_app_q3_source_runtime_foreign_spawns(sim->source.q3, spawns, count);
      for (size_t i = 0; i < count; i++) ordinals[i] = spawns[i].ordinal;
      qa_app_shared_sim_spawn_foreign_world(sim, ordinals, count, qa_app_q3_source_runtime_options(sim->source.q3)->entities);
      free(spawns); free(ordinals);
    }
    if (!options.restore) qa_app_shared_sim_admit_guest_monsters(sim);
    qa_cvar_registry_clear_modified(host->cvars, "g_gametype");
  } else if ((sim->strategy->as_q2_native(sim) != NULL)) {
    if (!options.native_q2_travel && !loading) {
      char *map_name = qa_ss_map_name(sim->recipe.map.geometry.requested_path);
      const char *entities = qa_app_shared_sim_guest_monster_entities(sim, sim->world_entities, sim->source.edition);
      if (qa_ss_is(sim->source.edition, "classic")) {
        qa_app_sim_classic_guest_world_init(sim->source.classic_world);
        qa_app_sim_classic_guest_world_spawn(sim->source.classic_world, map_name, entities, "");
      } else {
        qa_app_sim_rerelease_guest_world_init(sim->source.rerelease_world);
        qa_app_sim_rerelease_guest_world_spawn(sim->source.rerelease_world, map_name, entities, "");
      }
      free(map_name);
    }
    /* Retained travel already spawned the new map during source creation;
     * either way the guest world exists now and replacements can join it. */
    if (!loading && !options.restore) qa_app_shared_sim_admit_guest_monsters(sim);
  } else if ((sim->strategy->as_q2(sim) != NULL)) {
    qa_q2_game_services *game = sim->source.product->services;
    if (options.has_travel && qa_ss_is(options.travel.source_kind, "q2")) game->counters->server_flags = options.travel.server_flags;
    qa_q2_spawn_fields *fields = NULL; size_t field_count = 0;
    if (!qa_q2_parse_entities(sim->world_entities, game->options.edition, &fields, &field_count)) qa_range_error("Out of memory");
    const qa_q2_spawn_fields *worldspawn = NULL;
    for (size_t i = 0; i < field_count; i++) if (qa_ss_is(fields[i].classname, "worldspawn")) { worldspawn = &fields[i]; break; }
    qa_app_shared_sim_set_world_gravity(sim, worldspawn == NULL ? 800 : qa_q2_number_field(worldspawn, "gravity", 800));
    qa_q2_entities_free(fields, field_count);
    qa_q2_spawn_report report = qa_q2_foundation_load(sim->source.product->game, sim->world_entities, NULL, 0);
    qa_q2_product_runtime_after_spawn(sim->source.product);
    if (report.unsupported_count != 0) {
      if (!sim->has_foreign_q1_world) {
        /* `Unimplemented authored Q2 spawns: ${[...new Set(classnames)].join(", ")}` */
        size_t length = 1;
        for (size_t i = 0; i < report.unsupported_count; i++) length += strlen(report.unsupported[i]->classname) + 2;
        char *names = calloc(length, 1);
        if (!names) qa_range_error("Out of memory");
        for (size_t i = 0; i < report.unsupported_count; i++) {
          const char *name = report.unsupported[i]->classname; bool seen = false;
          for (size_t j = 0; j < i; j++) if (qa_ss_is(report.unsupported[j]->classname, name)) seen = true;
          if (seen) continue;
          if (names[0]) strcat(names, ", ");
          strcat(names, name);
        }
        char message[2048];
        snprintf(message, sizeof message, "Unimplemented authored Q2 spawns: %s", names);
        free(names);
        ss_fail("%s", message);
      }
      /* Hand the primary's unclaimed authored entities to the Q1 adjunct;
       * retire its inert placeholders first so each authored entity has
       * exactly one live owner. */
      size_t count = report.unsupported_count;
      double *ordinals = calloc(count, sizeof *ordinals);
      qa_q2_entity **unsupported = calloc(count, sizeof *unsupported);
      if (!ordinals || !unsupported) qa_range_error("Out of memory");
      for (size_t i = 0; i < count; i++) { ordinals[i] = report.unsupported[i]->spawn.ordinal; unsupported[i] = report.unsupported[i]; }
      for (size_t i = 0; i < count; i++)
        if (qa_session_actor_registry_is_live(sim->actors, unsupported[i]->actor.id)) game->remove(game->ctx, unsupported[i]);
      qa_app_shared_sim_spawn_foreign_world(sim, ordinals, count, sim->world_entities);
      free(ordinals); free(unsupported);
    }
  }
  if (!loading) {
    if (sim->pending_shared_restore) {
      qa_shared_world_restore_finish(sim->pending_shared_restore);
      qa_shared_world_restore_assert_complete(sim->pending_shared_restore);
      qa_shared_world_restore_completion_free(sim->pending_shared_restore);
    }
    sim->pending_shared_restore = NULL;
    qa_app_shared_sim_bind_native_input(sim);
    qa_app_shared_sim_restore_native_inventory_cursors(sim, NULL);
  }
}

qa_app_shared_simulation *qa_app_shared_simulation_create(qa_app_simulation_options options) {
  return ss_create(options, false);
}

/* static async load(options, nextFrame) (964-1043). */
typedef struct ss_load { qa_app_shared_simulation *sim; qa_app_shared_sim_next_frame_fn next_frame; void *frame_ctx; } ss_load;
static bool ss_any_rerelease_behavior(const qa_app_simulation_options *options) {
  for (size_t i = 0; i < options->weapon_behavior_count; i++)
    if (qa_ss_is(((const qa_app_prepared_weapon_behavior *)options->weapon_behaviors[i])->kind, "rerelease-native")) return true;
  return false;
}
static void ss_shutdown_q3_guest(void *ctx) { qa_app_shared_sim_shutdown_q3_guest(ctx); }
static void ss_close(void *ctx) { qa_app_shared_sim_close(ctx); }
static void ss_load_body(void *ctx) {
  ss_load *load = ctx;
  qa_app_shared_simulation *sim = load->sim;
  const qa_app_simulation_options *options = &sim->options;
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(sim);
  if (options->prepare_rerelease_navigation && ((q2 && q2->services->options.edition == QA_Q2_EDITION_RERELEASE)
      || (options->has_q2_guest && qa_ss_is(options->q2_guest.edition, "rerelease")) || ss_any_rerelease_behavior(options)))
    qa_app_shared_sim_install_rerelease_navigation(sim,
        (qa_app_sim_application_bot_navigation *)options->prepare_rerelease_navigation(options->nav_ctx, (void *)sim));
  if ((sim->strategy->as_q2_native(sim) != NULL)) {
    bool travel = sim->has_pending_native_travel;
    sim->has_pending_native_travel = false;
    if (travel) {
      qa_app_shared_sim_run_pending_native_travel(sim, load->next_frame, load->frame_ctx);
      qa_app_shared_sim_admit_guest_monsters(sim);
    } else if (options->restore) {
      sim->strategy->restore_loading(sim, options->restore, load->next_frame, load->frame_ctx);
      qa_app_shared_sim_restore(sim, options->restore, true);
      if (options->has_travel) qa_app_shared_sim_restore_campaign_travel(sim, &options->travel);
      if (sim->source_collision_settings) qa_shared_bind_collision_settings(sim->scene, sim->source_collision_settings);
    } else {
      char *map_name = qa_ss_map_name(options->recipe.map.geometry.requested_path);
      const char *entities = qa_app_shared_sim_guest_monster_entities(sim, sim->world_entities, sim->source.edition);
      if (qa_ss_is(sim->source.edition, "classic")) {
        qa_app_sim_classic_guest_world_init_loading(sim->source.classic_world, load->next_frame, load->frame_ctx);
        qa_app_sim_classic_guest_world_spawn_loading(sim->source.classic_world, map_name, entities, load->next_frame, load->frame_ctx, "");
      } else {
        qa_app_sim_rerelease_guest_world_init_loading(sim->source.rerelease_world, load->next_frame, load->frame_ctx);
        qa_app_sim_rerelease_guest_world_spawn_loading(sim->source.rerelease_world, map_name, entities, load->next_frame, load->frame_ctx, "");
      }
      free(map_name);
      qa_app_shared_sim_admit_guest_monsters(sim);
    }
  }
  qa_app_shared_sim_bind_native_input(sim);
  qa_app_shared_sim_initialize_qvm_grapple(sim);
  qa_app_sim_simulation_weapon_behaviors_initialize_loading(sim->weapon_behavior, load->next_frame, load->frame_ctx);
  const qa_mod_session_checkpoint *mod_state = options->restore ? options->restore->mods : NULL;
  /* TS options.preparedMods !== undefined: an empty prepared list (non-NULL, count 0) still opens the session mods. */
  if (options->prepared_mods != NULL || qa_app_sim_enabled_mod_count(options->enabled_mods) != 0
      || mod_state || options->mod_travel)
    qa_app_shared_sim_open_mods(sim, mod_state, load->next_frame, load->frame_ctx);
  if (sim->source_items_restore)
    qa_source_item_restore_finish(sim->source_items_restore, sim->actors, sim->inventory);
  qa_app_shared_sim_restore_native_inventory_cursors(sim, NULL);
  for (size_t i = 0; i < sim->weapon_slots.count; i++)
    qa_app_sim_weapon_slot_validate_restore(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i));
  qa_app_shared_sim_events_finish_owner_restore(sim);
  if (sim->pending_shared_restore) {
    qa_shared_world_restore_finish(sim->pending_shared_restore);
    qa_shared_world_restore_assert_complete(sim->pending_shared_restore);
    qa_shared_world_restore_completion_free(sim->pending_shared_restore);
  }
  sim->pending_shared_restore = NULL;
}
qa_app_shared_simulation *qa_app_shared_simulation_load(qa_app_simulation_options options,
    qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx) {
  qa_app_shared_simulation *sim = ss_create(options, true);
  ss_load load = {sim, next_frame, frame_ctx};
  qa_caught_error caught, cleanup;
  if (!qa_try(ss_load_body, &load, &caught)) {
    size_t failures = 1;
    if (!qa_try(ss_shutdown_q3_guest, sim, &cleanup)) failures++;
    if (!qa_try(ss_close, sim, &cleanup)) failures++;
    if (failures > 1) qa_raise("AggregateError", 0, 0, "Simulation loading and cleanup failed");
    qa_rethrow(&caught);
  }
  return sim;
}

/* ---- public members ---- */
qa_session_id qa_app_shared_sim_session(const qa_app_shared_simulation *sim) { return sim->session; }
const qa_executable_recipe *qa_app_shared_sim_recipe(const qa_app_shared_simulation *sim) { return &sim->recipe; }
qa_session_actor_registry *qa_app_shared_sim_actors(qa_app_shared_simulation *sim) { return sim->actors; }
qa_shared_body_table *qa_app_shared_sim_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
double qa_app_shared_sim_time_seconds(const qa_app_shared_simulation *sim) { return qa_ss_seconds(sim->source_frame.time); }
/* TS host.sourceKind: sim.strategy.kind. */
const char *qa_app_shared_sim_source_kind(const qa_app_shared_simulation *sim) { return sim->strategy->kind; }

/* close() (8673-8696). */
typedef struct ss_close_step { qa_app_shared_simulation *sim; int which; } ss_close_step;
static void ss_close_one(void *ctx) {
  ss_close_step *step = ctx; qa_app_shared_simulation *sim = step->sim;
  switch (step->which) {
  case 0: if (sim->mod_owner) qa_session_mods_close(sim->mod_owner); break;
  case 1:
    if (sim->grapple && qa_ss_is(qa_app_sim_grapple_runtime_source(sim->grapple)->kind, "q3-qvm"))
      qa_app_sim_qvm_grapple_source_close(qa_app_sim_grapple_runtime_source(sim->grapple)->q3_game);
    break;
  case 2: qa_app_sim_simulation_weapon_behaviors_close(sim->weapon_behavior); break;
  case 3: if (sim->native_primary_drop) qa_compat_q2_native_primary_drop_close(sim->native_primary_drop); break;
  case 4: if (sim->native_primary_inventory) qa_compat_q2_native_primary_inventory_close(sim->native_primary_inventory); break;
  case 5: if (sim->has_dispose_native_pickup_supply) sim->dispose_native_pickup_supply.dispose(sim->dispose_native_pickup_supply.ctx); break;
  case 6: if (sim->native_primary_commands) qa_compat_q2_native_primary_commands_close(sim->native_primary_commands); break;
  case 7: if (sim->native_primary_weapons) qa_compat_q2_native_primary_weapons_close(sim->native_primary_weapons); break;
  case 8:
    if ((sim->strategy->as_q2_native(sim) != NULL)) {
      if (qa_ss_is(sim->source.edition, "classic")) qa_app_sim_classic_guest_world_close(sim->source.classic_world);
      else qa_app_sim_rerelease_guest_world_close(sim->source.rerelease_world);
    }
    break;
  case 9: if (sim->selected_ammo_timers) qa_app_q3_mapped_ammo_regeneration_close(sim->selected_ammo_timers); break;
  case 10: if (sim->selected_q3_source) qa_app_q3_selected_source_close(sim->selected_q3_source); break;
  case 11: qa_session_actor_registry_close(sim->actors); break;
  case 12: if ((sim->strategy->as_q3(sim) != NULL)) qa_app_q3_source_runtime_close(sim->source.q3); break;
  case 13: if (sim->has_dispose_source_combat) qa_gameplay_authority_unregister(sim->combat, sim->dispose_source_combat); break;
  case 14: qa_frame_scheduler_close(sim->scheduler); break;
  default: break;
  }
}
void qa_app_shared_sim_close(qa_app_shared_simulation *sim) {
  if (sim->checkpoint_in_progress) ss_fail("Await checkpoint completion before closing the simulation");
  if (sim->closed) return;
  if ((sim->strategy->as_q3_qvm(sim) != NULL) && !qa_app_q3_qvm_server_game_is_retired(sim->source.qvm))
    ss_fail("Q3 guest shutdown must be awaited before closing its shared world");
  qa_caught_error first; size_t errors = 0;
  qa_caught_error caught;
  for (int which = 0; which <= 8; which++) {
    ss_close_step step = {sim, which};
    if (which == 1) qa_mod_match_state_close(sim->source_match);
    if (!qa_try(ss_close_one, &step, &caught)) { if (errors++ == 0) first = caught; }
  }
  sim->closed = true;
  qa_mod_client_applications_close(sim->mod_client_applications); qa_mod_client_outputs_close(sim->mod_client_outputs);
  sim->mod_client_listener_count = 0; qa_ss_map_clear(&sim->mod_client_admissions); qa_ss_map_clear(&sim->mod_client_commands);
  qa_event_ring_reset(&sim->mod_client_drops);
  qa_world_debug_line_store_clear(&sim->debug_line_store); sim->debug_line_snapshot = NULL; sim->debug_line_snapshot_count = 0;
  qa_world_text_store_clear(&sim->world_text_store); sim->world_text_snapshot = NULL; sim->world_text_snapshot_count = 0;
  for (int which = 9; which <= 14; which++) {
    ss_close_step step = {sim, which};
    if (!qa_try(ss_close_one, &step, &caught)) { if (errors++ == 0) first = caught; }
  }
  sim->has_dispose_source_combat = false;
  if (errors == 1) qa_rethrow(&first);
  if (errors > 1) qa_raise("AggregateError", 0, 0, "Simulation source and shared owner shutdown failed");
}

void qa_app_shared_simulation_destroy(qa_app_shared_simulation *sim) {
  if (!sim) return;
  /* close() closed the session mods; their owner dies with the simulation. */
  if (sim->mod_owner) qa_session_mods_destroy(sim->mod_owner);
  sim->mod_owner = NULL;
  qa_app_shared_sim_source_runtime_destroy(sim, &sim->source);
  qa_app_shared_sim_worlds_destroy(sim); /* B1 owners: foreign worlds, monster sources, guest plans */
  qa_app_shared_sim_players_destroy(sim); /* B3 owners: character actors, entry carries, Q1 character foundation */
  qa_app_shared_sim_arsenal_destroy(sim); /* B2 owners: selected arsenals, equipment runtimes and their games */
  if (sim->actors) qa_session_actor_registry_off_release(sim->actors, sim->release_subscription);
  for (size_t i = 0; i < sim->player_states.count; i++)
    qa_app_sim_movement_player_destroy(*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i));
  for (size_t i = 0; i < sim->native_movement_players.count; i++)
    qa_app_sim_movement_player_destroy(*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->native_movement_players, i));
  for (size_t i = 0; i < sim->weapon_slots.count; i++)
    qa_app_sim_weapon_slot_destroy(*(qa_app_sim_weapon_slot **)qa_ss_map_value_at(&sim->weapon_slots, i));
  for (size_t i = 0; i < sim->q2_entity_attacks.count; i++) free(*(qa_attack_provenance **)qa_ss_map_value_at(&sim->q2_entity_attacks, i));
  qa_ss_map *maps[] = {&sim->horde_fragged, &sim->tag_health, &sim->lmctf_teams, &sim->lmctf_flags, &sim->lmctf_runes,
    &sim->lmctf_health, &sim->lmctf_regen_at, &sim->lmctf_spot_flags, &sim->deathball_teams, &sim->deathball_goals,
    &sim->actor_executions, &sim->player_states, &sim->native_movement_players, &sim->native_character_commands,
    &sim->native_character_health, &sim->guest_monster_plans, &sim->shared_travel_applied, &sim->native_mirrored_combat,
    &sim->native_mirrored_health, &sim->native_mirror_write_reported, &sim->native_ground_planes, &sim->native_move_carry,
    &sim->native_exact_motions, &sim->q2_characters, &sim->character_ticks, &sim->entry_carry, &sim->pending_start_items,
    &sim->detached_models, &sim->q1_characters, &sim->q1_character_adjuncts, &sim->q2_views, &sim->characters,
    &sim->character_starts, &sim->weapon_slots, &sim->native_equipment_players, &sim->native_equipment_alive,
    &sim->native_weapon_requests, &sim->native_equipment_velocity, &sim->selected_q3_strings, &sim->primary_command_blocks,
    &sim->selected_original_weapons, &sim->q3_arsenals, &sim->q3_commands, &sim->source_models, &sim->source_model_read_order, &sim->presentation_snapshot,
    &sim->view_models, &sim->monster_sources, &sim->monster_missions, &sim->settled_q3_sources, &sim->mod_client_admissions,
    &sim->mod_client_commands, &sim->restored_native_inventory_selections, &sim->native_travel_consumed,
    &sim->native_weapon_times, &sim->selected_pickup_bindings, &sim->last_attack, &sim->q2_entity_attacks, &sim->area_portals,
    &sim->quake_world_touched, &sim->source.visited, &sim->source.clients};
  for (size_t i = 0; i < sizeof maps / sizeof *maps; i++) qa_ss_map_free(maps[i]);
  if (sim->weapon_behavior) qa_app_sim_simulation_weapon_behaviors_destroy(sim->weapon_behavior);
  if (sim->q1_client_visibility) qa_q1_client_visibility_destroy(sim->q1_client_visibility);
  if (sim->scheduler) qa_frame_scheduler_destroy(sim->scheduler);
  if (sim->original_pickups) qa_shared_original_pickup_admission_destroy(sim->original_pickups);
  if (sim->combat) qa_gameplay_authority_destroy(sim->combat);
  if (sim->events) qa_app_sim_simulation_events_destroy(sim->events);
  qa_app_shared_sim_release_presentation_super(sim);
  if (sim->inventory) qa_shared_inventory_table_destroy(sim->inventory);
  if (sim->physics) qa_app_sim_shared_physics_destroy(sim->physics);
  if (sim->scene) qa_shared_scene_queries_destroy(sim->scene);
  if (sim->callbacks) qa_actor_callback_table_destroy(sim->callbacks);
  if (sim->q1_punch) qa_app_sim_q1_player_punch_destroy(sim->q1_punch);
  if (sim->dropped_pickups) qa_app_sim_source_pickup_cargo_destroy(sim->dropped_pickups);
  if (sim->source_match) qa_mod_match_state_destroy(sim->source_match);
  if (sim->mod_client_applications) qa_mod_client_applications_destroy(sim->mod_client_applications);
  if (sim->mod_client_outputs) qa_mod_client_outputs_destroy(sim->mod_client_outputs);
  if (sim->actors) qa_session_actor_registry_destroy(sim->actors);
  if (sim->random) qa_app_sim_source_random_destroy(sim->random);
  if (sim->source_items_restore) { qa_source_item_restore_free(sim->source_items_restore); free(sim->source_items_restore); }
  free(sim->source_collision_settings);
  qa_world_debug_line_store_destroy(&sim->debug_line_store);
  qa_world_text_store_destroy(&sim->world_text_store);
  qa_event_ring_destroy(&sim->transitions); qa_event_ring_destroy(&sim->quake_world_commands); qa_event_ring_destroy(&sim->mod_client_drops);
  free(sim->scheduler_clocks); free(sim->scheduler_providers); free(sim->start_items); free(sim->initial_spawn_point);
  free(sim->pending_guest_monster_report); free(sim->level_change_map); free(sim->mod_client_listeners);
  free(sim->native_weapon_applications); free(sim->qvm_weapon_decisions); free(sim->ordered_players);
  free(sim->mod_client_subscriptions); free(sim->tag_spawns); free(sim->lmctf_spot_health);
  for (size_t i = sim->owned_count; i-- > 0;) sim->owned[i].release(sim->owned[i].value);
  free(sim->owned);
  for (size_t i = 0; i < sim->allocation_count; i++) free(sim->allocations[i]);
  free(sim->allocations);
  free(sim);
}
