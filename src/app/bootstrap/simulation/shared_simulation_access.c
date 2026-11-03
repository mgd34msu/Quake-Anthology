/* The access structs the network adapters and bot shells take in place of
 * the TS SharedSimulation / Q2 guest world objects (network.h, bots.h,
 * network_q2_guest.h, unified_prediction.h, player_movement.h), filled from
 * the real qa_app_shared_simulation. Every entry is the TS member of the
 * same name; ctx is always the simulation (or the native guest world).
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/player_state.h"
#include "qa/app/bootstrap/simulation/bot_mover_connections.h"
#include "qa/app/bootstrap/simulation/bot_prediction.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/foundation/support.h"
#include "qa/content/q2/base/player/index.h"
#include "qa/llm/errors.h"
#include "qa/app/bootstrap/simulation/prediction/presentation.h"
#include "qa/app/bootstrap/simulation/player_jump.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/q3/base/records.h"
#include "qa/world/collision/index.h"
#include "qa/app/bootstrap/remote_access.h"
#include "qa/content/mounts.h"

static qa_app_shared_simulation *ss(void *ctx) { return ctx; }

/* ---- qa_app_unified_prediction_simulation ---- */
static bool ss_prediction_player(void *ctx, qa_actor_id actor, qa_app_unified_prediction_player *out) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(ss(ctx), actor);
  if (!player) return false;
  memset(out, 0, sizeof *out);
  out->state = player->state; out->arsenal = player->arsenal; out->animation = player->animation;
  out->environment = qa_app_sim_movement_player_prediction_environment(player);
  out->bounds = player->bounds; out->view_angles = player->view_angles; out->view_height = player->view_height;
  out->ground = player->ground; out->water_level = player->water_level; out->water_type = player->water_type;
  out->profile = qa_app_sim_movement_player_prediction_profile(player);
  out->standing_bounds = player->standing_bounds;
  out->character_kind = qa_game_family_name(player->character);
  return true;
}
static double ss_time(void *ctx) { return qa_app_shared_sim_time_seconds(ss(ctx)); }
static void ss_observations(void *ctx, void (*visit)(void *visit_ctx, qa_actor_id actor), void *visit_ctx) {
  size_t count = 0; const qa_actor_observation *observations = qa_session_actor_registry_observations(ss(ctx)->actors, &count);
  for (size_t i = 0; i < count; i++) visit(visit_ctx, observations[i].id);
}
static bool ss_linked_actor(void *ctx, qa_actor_id actor, qa_spatial_actor *out) { return qa_shared_linked_actor(ss(ctx)->scene, actor, out); }

/* ---- qa_app_sim_network_simulation ---- */
static qa_q2_product_runtime *ss_q2_source(void *ctx) { return qa_app_shared_sim_q2_source(ss(ctx)); }
static qa_cvar_registry *ss_q2_server_cvars(void *ctx) { return qa_app_shared_sim_q2_server_cvars(ss(ctx)); }
static bool ss_q2_movement_config(void *ctx, qa_app_sim_q2_movement_config *out) { return qa_app_shared_sim_q2_movement_config(ss(ctx), out); }
static bool ss_q2_player_view(void *ctx, qa_actor_id actor, qa_q2_base_player_view *out) {
  return qa_app_shared_sim_q2_player_view(ss(ctx), actor, out);
}
static qa_comp_q1_source_composition *ss_q1_source(void *ctx) {
  return (ss(ctx)->strategy->as_q1(ss(ctx)) != NULL) ? ss(ctx)->source.q1_composition : NULL;
}
static qa_app_q3_source_runtime *ss_q3_source(void *ctx) { return (ss(ctx)->strategy->as_q3(ss(ctx)) != NULL) ? ss(ctx)->source.q3 : NULL; }
static qa_app_q3_qvm_server_game *ss_q3_guest(void *ctx) { return (ss(ctx)->strategy->as_q3_qvm(ss(ctx)) != NULL) ? ss(ctx)->source.qvm : NULL; }
static const qa_actor_id *ss_players(void *ctx, size_t *out_count) { return qa_app_shared_sim_players(ss(ctx), out_count); }
static qa_app_sim_movement_player *ss_movement_player(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_movement_player(ss(ctx), actor); }
static qa_app_sim_player_admission ss_admit_player(void *ctx, qa_client_id client) { return qa_app_shared_sim_admit_player(ss(ctx), client); }
static void ss_disconnect_player(void *ctx, qa_actor_id actor) { qa_app_shared_sim_disconnect_player(ss(ctx), actor); }
static void ss_notify_client_event(void *ctx, const char *kind, qa_actor_id actor) { qa_app_shared_sim_notify_client_event(ss(ctx), kind, actor); }
static void ss_observe_client_command(void *ctx, const qa_actor_command *input) { qa_app_shared_sim_observe_client_command(ss(ctx), input); }
static void ss_player_command(void *ctx, qa_actor_id actor, const char *name, const char *const *args, size_t count) {
  qa_app_shared_sim_player_command(ss(ctx), actor, name, args, count);
}
static qa_owned_actor ss_register_q2_native_client(void *ctx, qa_client_id client) { return qa_app_shared_sim_register_q2_native_client(ss(ctx), client); }
static size_t ss_q2_native_players(void *ctx, const qa_client_id **out_clients, const qa_actor_id **out_actors) {
  return qa_app_shared_sim_q2_native_players(ss(ctx), out_clients, out_actors);
}
static const qa_app_simulation_presentation *ss_presentations(void *ctx, size_t *out_count) { return qa_app_shared_sim_presentations(ss(ctx), out_count); }
static qa_app_sim_player_view ss_player_view(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_player_view(ss(ctx), actor); }
static qa_app_sim_player_ui ss_player_ui(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_player_ui(ss(ctx), actor); }
static const qa_q3_character_view *ss_character_views(void *ctx, size_t *out_count) { return qa_app_shared_sim_character_views(ss(ctx), out_count); }
static const qa_world_text *ss_world_text(void *ctx, size_t *out_count) { return qa_app_shared_sim_world_text(ss(ctx), out_count); }
static bool ss_q1_paused(void *ctx) { return qa_app_shared_sim_q1_paused(ss(ctx)); }
static const char *ss_toggle_q1_pause(void *ctx, const qa_actor_id *actor) { return qa_app_shared_sim_toggle_q1_pause(ss(ctx), actor); }
static const char *ss_light_style(void *ctx, double index) { return qa_app_sim_simulation_events_light_style(ss(ctx)->events, index); }
static const qa_app_sim_presentation_event *const *ss_persistent_events(void *ctx, size_t *out_count) {
  return qa_app_shared_sim_persistent_events(ss(ctx), out_count);
}
static qa_save_value *const *ss_persistent_presentation(void *ctx, size_t *out_count) {
  return qa_app_shared_sim_persistent_presentation(ss(ctx), out_count);
}
static bool ss_event_resource(void *ctx, qa_resource_id id, qa_resolved_resource_reference *out) {
  return qa_app_shared_sim_event_resource(ss(ctx), id, out);
}
static void ss_register_resource(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource) {
  qa_app_sim_simulation_events_register_resource(ss(ctx)->events, content, path, resource);
}
static qa_app_sim_network_restart_plan ss_source_restart_plan(void *ctx) { return qa_app_shared_sim_source_restart_plan(ss(ctx)); }
static void ss_queue_quakeworld_action(void *ctx, qa_client_id client, void (*action)(void *action_ctx), void *action_ctx) {
  qa_app_shared_sim_queue_quake_world_action(ss(ctx), client, action, action_ctx);
}
static void ss_queue_quakeworld_commands(void *ctx, qa_client_id client, const qa_qw_user_command *commands, size_t count, double sequence) {
  qa_app_shared_sim_queue_quake_world_commands(ss(ctx), client, commands, count, sequence);
}
static qa_actor_id ss_reserve_netquake_client(void *ctx, qa_client_id client) { return qa_app_shared_sim_reserve_net_quake_client(ss(ctx), client); }

qa_app_sim_network_simulation qa_app_shared_sim_network_simulation(qa_app_shared_simulation *sim) {
  qa_app_sim_network_simulation access; memset(&access, 0, sizeof access);
  access.ctx = sim;
  access.session = sim->session; access.recipe = &sim->recipe; access.options = &sim->options;
  access.actors = sim->actors; access.bodies = qa_app_sim_shared_physics_bodies(sim->physics); access.scene = sim->scene;
  access.inventory = sim->inventory;
  access.prediction = (qa_app_unified_prediction_simulation){sim, ss_prediction_player, ss_time, ss_observations, ss_linked_actor};
  access.q2_source = ss_q2_source; access.q2_server_cvars = ss_q2_server_cvars; access.q2_movement_config = ss_q2_movement_config;
  access.q2_player_view = ss_q2_player_view; access.q1_source = ss_q1_source; access.q3_source = ss_q3_source;
  access.q3_guest = ss_q3_guest;
  access.players = ss_players; access.movement_player = ss_movement_player; access.admit_player = ss_admit_player;
  access.disconnect_player = ss_disconnect_player; access.notify_client_event = ss_notify_client_event;
  access.observe_client_command = ss_observe_client_command; access.player_command = ss_player_command;
  access.register_q2_native_client = ss_register_q2_native_client; access.q2_native_players = ss_q2_native_players;
  access.presentations = ss_presentations; access.player_view = ss_player_view; access.player_ui = ss_player_ui;
  access.character_views = ss_character_views; access.world_text = ss_world_text; access.time_seconds = ss_time;
  access.q1_paused = ss_q1_paused; access.toggle_q1_pause = ss_toggle_q1_pause; access.light_style = ss_light_style;
  access.persistent_events = ss_persistent_events; access.persistent_presentation = ss_persistent_presentation;
  access.event_resource = ss_event_resource; access.register_resource = ss_register_resource;
  access.source_restart_plan = ss_source_restart_plan;
  access.queue_quakeworld_action = ss_queue_quakeworld_action; access.queue_quakeworld_commands = ss_queue_quakeworld_commands;
  access.reserve_netquake_client = ss_reserve_netquake_client;
  return access;
}

/* ---- qa_app_sim_network_content over LoadedApplicationContent ---- */
typedef struct ss_content { qa_app_loaded_content *content; qa_app_q3_package_mount *q3_mounts; } ss_content;
/* The MountedContent members createQ2ApplicationDownloads reads (open, exists, invalidateListings). */
static bool ss_q2_mount_open(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length,
    const qa_resolved_resource_reference **out_reference) {
  qa_content_opened_resource opened; memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_length = opened.byte_length;
  if (out_reference) *out_reference = opened.reference;
  return true;
}
static bool ss_q2_mount_exists(void *ctx, const char *path) { return qa_content_mounted_exists(ctx, path); }
static void ss_q2_mount_invalidate(void *ctx) { qa_content_mounted_invalidate_listings(ctx); }
/* await options.content.forContent(content) */
static qa_app_q2_mounts ss_for_content(void *ctx, qa_content_id content) {
  qa_content_mounted *mounted = (qa_content_mounted *)qa_app_loaded_content_for_content(((ss_content *)ctx)->content, content);
  qa_app_q2_mounts mounts = {mounted, ss_q2_mount_open, ss_q2_mount_exists, ss_q2_mount_invalidate};
  return mounts;
}
static bool ss_content_resolve(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference *out) {
  const qa_content_mounted *mounted = qa_app_loaded_content_for_content(((ss_content *)ctx)->content, content);
  const qa_resolved_resource_reference *reference = NULL;
  if (!mounted || !qa_content_mounted_resolve((qa_content_mounted *)mounted, path, &reference) || !reference) return false;
  *out = *reference;
  return true;
}
static uint8_t *ss_content_read(void *ctx, const qa_resolved_resource_reference *reference, size_t *out_length) {
  return qa_content_mounted_read_reference((qa_content_mounted *)qa_app_loaded_content_mounts(((ss_content *)ctx)->content), reference, out_length);
}
static uint8_t *ss_content_read_path(void *ctx, const char *path, size_t *out_length) {
  return qa_content_mounted_read_path((qa_content_mounted *)qa_app_loaded_content_mounts(((ss_content *)ctx)->content), path, out_length);
}
static bool ss_content_resolve_path(void *ctx, const char *path, qa_resolved_resource_reference *out) {
  const qa_resolved_resource_reference *reference = NULL;
  if (!qa_content_mounted_resolve((qa_content_mounted *)qa_app_loaded_content_mounts(((ss_content *)ctx)->content), path, &reference) || !reference) return false;
  *out = *reference;
  return true;
}
static bool ss_content_open(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length, qa_resolved_resource_reference *out_reference) {
  qa_content_opened_resource opened; memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open((qa_content_mounted *)qa_app_loaded_content_mounts(((ss_content *)ctx)->content), path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_length = opened.byte_length;
  if (out_reference && opened.reference) *out_reference = *opened.reference;
  return true;
}
/* content.catalog.product(content).expectation.contentDirectory */
static const char *ss_content_directory(void *ctx, qa_content_id content) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(((ss_content *)ctx)->content);
  return qa_content_catalog_product(catalog, content)->expectation.content_directory;
}
/* Q3ApplicationPackages.open(content): content.mounts.plan.defaultOrder as package mounts (the rows stay valid until
 * the next call or the simulation's destruction). */
static const qa_app_q3_package_mount *ss_q3_package_mounts(void *ctx, size_t *out_count) {
  ss_content *view = ctx;
  free(view->q3_mounts);
  view->q3_mounts = qa_app_q3_package_mounts_of(qa_app_loaded_content_catalog(view->content),
      qa_app_loaded_content_mounts(view->content), out_count);
  return view->q3_mounts;
}
/* Q3ApplicationPackages.collect(content): every openedResources entry of every content.openedMounts() mount, with
 * whether its mount's product family is q3. */
static void ss_q3_collect_opened(void *ctx, void (*visit)(void *visit_ctx, const qa_resolved_resource_reference *reference,
    bool is_q3), void *visit_ctx) {
  qa_app_loaded_content *loaded = ((ss_content *)ctx)->content;
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(loaded);
  size_t capacity = 16, count = 0;
  const void **opened = NULL;
  for (;;) {
    free(opened);
    opened = calloc(capacity, sizeof *opened);
    if (opened == NULL) qa_range_error("Out of memory");
    count = qa_app_loaded_content_opened_mounts(loaded, opened, capacity);
    if (count < capacity) break;
    capacity *= 2;
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, opened);
  for (size_t i = 0; i < count; i++) {
    size_t resources = 0;
    const qa_resolved_resource_reference *rows = qa_content_mounted_opened_resources(opened[i], &resources);
    for (size_t j = 0; j < resources; j++) {
      const qa_resolved_resource_reference *reference = &rows[j];
      qa_content_id product = reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? reference->provenance.archive_mount->identity.content : reference->provenance.loose_mount->identity.content;
      visit(visit_ctx, reference, qa_content_catalog_product(catalog, product)->expectation.family == QA_GAME_FAMILY_Q3);
    }
  }
  qa_cleanup_pop(&guard);
  free(opened);
}
static void ss_content_dispose(void *ctx) { free(((ss_content *)ctx)->q3_mounts); }
qa_app_sim_network_content qa_app_shared_sim_network_content(qa_app_shared_simulation *sim, qa_app_loaded_content *content) {
  ss_content *view = qa_app_shared_sim_keep(sim, sizeof *view);
  view->content = content;
  view->q3_mounts = NULL;
  qa_app_shared_sim_own(sim, ss_content_dispose, view);
  qa_app_sim_network_content access; memset(&access, 0, sizeof access);
  access.ctx = view;
  access.recipe = qa_app_loaded_content_recipe(content);
  access.world = sim->world;
  access.for_content = ss_for_content; access.resolve = ss_content_resolve; access.read = ss_content_read;
  access.read_path = ss_content_read_path; access.resolve_path = ss_content_resolve_path; access.open = ss_content_open;
  access.content_directory = ss_content_directory;
  access.q3_package_mounts = ss_q3_package_mounts;
  access.q3_collect_opened = ss_q3_collect_opened;
  /* content.q3Product?.restriction.kind === "demo" */
  qa_q3_application_product q3_product;
  access.q3_demo_restricted = qa_app_loaded_content_q3_product(content, &q3_product) && q3_product.restriction.kind == QA_Q3_MOUNT_DEMO;
  access.has_prepared_quakec = sim->options.prepared_quakec != NULL;
  return access;
}

/* ---- qa_app_sim_q2_guest_world over the native source world ---- */
static bool ss_classic(qa_app_shared_simulation *sim) { return qa_ss_is(sim->source.edition, "classic"); }
static void ss_g_set_configstring(void *ctx, double index, const char *value) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) qa_app_sim_classic_guest_services_set_configstring(qa_app_sim_classic_guest_world_services(sim->source.classic_world), index, value);
  else qa_app_sim_rerelease_guest_services_set_configstring(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world), index, value);
}
static const qa_q2_config_string_entry *ss_g_configstrings(void *ctx, size_t *out_count) {
  static __thread qa_q2_config_string_entry *storage; static __thread size_t capacity;
  qa_app_shared_simulation *sim = ss(ctx);
  size_t count = 0;
  const qa_app_sim_configstring *values = ss_classic(sim) ? qa_app_sim_classic_guest_world_configstrings(sim->source.classic_world, &count)
                                                          : qa_app_sim_rerelease_guest_world_configstrings(sim->source.rerelease_world, &count);
  if (count + 1 > capacity) {
    capacity = count + 16;
    storage = realloc(storage, capacity * sizeof *storage);
    if (!storage) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < count; i++) storage[i] = (qa_q2_config_string_entry){values[i].index, (char *)values[i].value};
  *out_count = count;
  return storage;
}
static bool ss_g_is_retired(void *ctx) {
  qa_app_shared_simulation *sim = ss(ctx);
  return ss_classic(sim) ? qa_app_sim_classic_guest_world_is_retired(sim->source.classic_world)
                         : qa_app_sim_rerelease_guest_world_is_retired(sim->source.rerelease_world);
}
static const qa_app_sim_q2_guest_client *ss_g_clients(void *ctx, size_t *out_count) {
  static __thread qa_app_sim_q2_guest_client *storage; static __thread size_t capacity;
  qa_app_shared_simulation *sim = ss(ctx);
  size_t count = 0;
  const qa_app_sim_classic_guest_client *clients = ss_classic(sim) ? qa_app_sim_classic_guest_world_clients(sim->source.classic_world, &count)
                                                                   : qa_app_sim_rerelease_guest_world_clients(sim->source.rerelease_world, &count);
  if (count + 1 > capacity) {
    capacity = count + 16;
    storage = realloc(storage, capacity * sizeof *storage);
    if (!storage) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < count; i++) storage[i] = (qa_app_sim_q2_guest_client){clients[i].slot, clients[i].userinfo};
  *out_count = count;
  return storage;
}
static double ss_g_player_ping(void *ctx, double slot) {
  qa_app_shared_simulation *sim = ss(ctx);
  return ss_classic(sim) ? qa_app_sim_classic_guest_world_player_ping(sim->source.classic_world, slot)
                         : qa_app_sim_rerelease_guest_world_player_ping(sim->source.rerelease_world, slot);
}
static void ss_g_player_state(void *ctx, double slot, qa_q2_player_state *classic, qa_q2_rerelease_player_state *rerelease) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) { if (classic) *classic = qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot); }
  else if (rerelease) *rerelease = qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot);
}
static const void *ss_g_entity_states(void *ctx, size_t *out_count) {
  qa_app_shared_simulation *sim = ss(ctx);
  return ss_classic(sim) ? (const void *)qa_app_sim_classic_guest_world_entity_states(sim->source.classic_world, out_count)
                         : (const void *)qa_app_sim_rerelease_guest_world_entity_states(sim->source.rerelease_world, out_count);
}
static qa_q2_entity_state ss_g_entity_state(void *ctx, double slot) {
  qa_app_shared_simulation *sim = ss(ctx);
  return ss_classic(sim) ? qa_app_sim_classic_guest_world_entity_state(sim->source.classic_world, slot)
                         : qa_app_sim_rerelease_guest_world_entity_state(sim->source.rerelease_world, slot).base;
}
static qa_app_sim_q2_guest_entity_info ss_g_entity_info(void *ctx, double slot) {
  qa_app_shared_simulation *sim = ss(ctx);
  qa_app_sim_q2_guest_entity_info info; memset(&info, 0, sizeof info);
  if (ss_classic(sim)) {
    static __thread double clusters[16];
    qa_app_sim_classic_guest_entity_info entity = qa_app_sim_classic_guest_world_entity_info(sim->source.classic_world, slot);
    info.server_flags = entity.server_flags; info.areas[0] = entity.areas[0]; info.areas[1] = entity.areas[1];
    info.has_clusters = !entity.clusters_null;
    memcpy(clusters, entity.clusters, sizeof clusters);
    info.clusters = clusters; info.cluster_count = entity.cluster_count;
    info.first_cluster = entity.first_cluster; info.headnode = entity.headnode;
    info.has_owner_slot = entity.has_owner_slot; info.owner_slot = entity.owner_slot;
  } else {
    qa_app_sim_rerelease_entity_info entity = qa_app_sim_rerelease_guest_world_entity_info(sim->source.rerelease_world, slot);
    info.server_flags = entity.server_flags; info.areas[0] = entity.areas[0]; info.areas[1] = entity.areas[1];
    info.has_clusters = !entity.clusters_null; info.clusters = entity.clusters; info.cluster_count = entity.cluster_count;
    info.first_cluster = entity.first_cluster; info.headnode = entity.headnode;
    info.has_owner_slot = entity.has_owner_slot; info.owner_slot = entity.owner_slot;
  }
  return info;
}
static qa_app_sim_q2_guest_connection ss_g_connect(void *ctx, double slot, const char *userinfo, const char *social_id, bool bot) {
  qa_app_shared_simulation *sim = ss(ctx);
  qa_app_sim_classic_guest_connect result = ss_classic(sim) ? qa_app_sim_classic_guest_world_connect(sim->source.classic_world, slot, userinfo)
      : qa_app_sim_rerelease_guest_world_connect(sim->source.rerelease_world, slot, userinfo, social_id, bot);
  return (qa_app_sim_q2_guest_connection){result.allowed, result.userinfo};
}
static void ss_g_begin(void *ctx, double slot) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) qa_app_sim_classic_guest_world_begin(sim->source.classic_world, slot);
  else qa_app_sim_rerelease_guest_world_begin(sim->source.rerelease_world, slot);
}
static void ss_g_disconnect(void *ctx, double slot) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) qa_app_sim_classic_guest_world_disconnect(sim->source.classic_world, slot);
  else qa_app_sim_rerelease_guest_world_disconnect(sim->source.rerelease_world, slot);
}
static void ss_g_think(void *ctx, double slot, const qa_user_command *command) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) qa_app_sim_classic_guest_world_think(sim->source.classic_world, slot, command->as.q2, NULL);
  else qa_app_sim_rerelease_guest_world_think(sim->source.rerelease_world, slot, command->as.q2_rerelease, NULL);
}
static void ss_g_userinfo(void *ctx, double slot, const char *value) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) qa_app_sim_classic_guest_world_userinfo(sim->source.classic_world, slot, value);
  else qa_app_sim_rerelease_guest_world_userinfo(sim->source.rerelease_world, slot, value);
}
static void ss_g_command(void *ctx, double slot, const char *const *argv, size_t argc, const char *args) {
  qa_app_shared_simulation *sim = ss(ctx);
  if (ss_classic(sim)) qa_app_sim_classic_guest_world_command(sim->source.classic_world, slot, argv, argc, args);
  else qa_app_sim_rerelease_guest_world_command(sim->source.rerelease_world, slot, argv, argc, args);
}
static const qa_app_sim_q2_guest_message *ss_g_raw_messages(void *ctx, size_t *out_count) {
  static __thread qa_app_sim_q2_guest_message *storage; static __thread size_t capacity;
  qa_app_shared_simulation *sim = ss(ctx);
  size_t count = 0;
  const qa_app_sim_classic_guest_message *messages = ss_classic(sim) ? qa_app_sim_classic_guest_world_raw_messages(sim->source.classic_world, &count)
                                                                     : qa_app_sim_rerelease_guest_world_raw_messages(sim->source.rerelease_world, &count);
  if (count + 1 > capacity) {
    capacity = count + 16;
    storage = realloc(storage, capacity * sizeof *storage);
    if (!storage) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < count; i++)
    storage[i] = (qa_app_sim_q2_guest_message){messages[i].audience.unicast, messages[i].audience.slot, messages[i].audience.origin,
                                               messages[i].audience.scope, messages[i].reliable, messages[i].bytes, messages[i].length};
  *out_count = count;
  return storage;
}
qa_app_sim_q2_guest_world qa_app_shared_sim_q2_guest_world(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q2_native(sim) == NULL)) qa_raise("Error", 0, 0, "Native Quake II source is unavailable");
  qa_app_sim_q2_guest_world world; memset(&world, 0, sizeof world);
  world.ctx = sim;
  if (ss_classic(sim)) {
    const qa_app_sim_classic_guest_services_options *options =
        qa_app_sim_classic_guest_services_options_of(qa_app_sim_classic_guest_world_services(sim->source.classic_world));
    world.cvars = options->cvars; world.max_clients = options->max_clients; world.numeric = options->numeric;
    world.map_path = options->map_path; world.frame_milliseconds = 100;
  } else {
    const qa_app_sim_rerelease_guest_services_options *options =
        qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world));
    world.cvars = options->base.cvars; world.max_clients = options->base.max_clients; world.numeric = options->base.numeric;
    world.map_path = options->base.map_path; world.frame_milliseconds = options->frame_milliseconds;
  }
  world.set_configstring = ss_g_set_configstring; world.configstrings = ss_g_configstrings; world.is_retired = ss_g_is_retired;
  world.clients = ss_g_clients; world.player_ping = ss_g_player_ping; world.player_state = ss_g_player_state;
  world.entity_states = ss_g_entity_states; world.entity_state = ss_g_entity_state; world.entity_info = ss_g_entity_info;
  world.connect = ss_g_connect; world.begin = ss_g_begin; world.disconnect = ss_g_disconnect; world.think = ss_g_think;
  world.userinfo = ss_g_userinfo; world.command = ss_g_command; world.raw_messages = ss_g_raw_messages;
  return world;
}

/* ---- qa_app_sim_bot_simulation ---- */
static const char *ss_source_entity_text(void *ctx) { return qa_app_shared_sim_source_entity_text(ss(ctx)); }
static const qa_client_id *ss_client_identities(void *ctx, size_t *out_count) { return qa_app_shared_sim_client_identities(ss(ctx), out_count); }
static bool ss_bot_entity(void *ctx, qa_actor_id actor, qa_app_sim_bot_entity *out) { return qa_app_shared_sim_bot_entity(ss(ctx), actor, out); }
static qa_owned_actor ss_prepare_bot_client(void *ctx, qa_client_id client) { return qa_app_shared_sim_prepare_bot_client(ss(ctx), client); }
static void ss_set_source_player_identity(void *ctx, qa_actor_id actor, const qa_app_sim_player_identity *identity) {
  qa_app_shared_sim_set_source_player_identity(ss(ctx), actor, identity);
}
static const qa_app_sim_source_objective *ss_source_objectives(void *ctx, size_t *out_count) { return qa_app_shared_sim_source_objectives(ss(ctx), out_count); }
static const qa_app_sim_bot_q1_source *ss_bot_q1_source(void *ctx) {
  static __thread qa_app_sim_bot_q1_source view;
  qa_app_shared_simulation *sim = ss(ctx);
  if ((sim->strategy->as_q1(sim) == NULL)) return NULL;
  view = (qa_app_sim_bot_q1_source){sim->source.q1_game, sim->source.q1_composition, sim->source.q1_cvars};
  return &view;
}
static const qa_app_sim_bot_q3_source *ss_bot_q3_source(void *ctx) {
  static __thread qa_app_sim_bot_q3_source view;
  qa_app_shared_simulation *sim = ss(ctx);
  if ((sim->strategy->as_q3(sim) == NULL)) return NULL;
  view = (qa_app_sim_bot_q3_source){sim->source.q3, sim->source.q3_owner, qa_app_q3_source_runtime_host(sim->source.q3)};
  return &view;
}
static bool ss_bot_q3_guest(void *ctx) { return (ss(ctx)->strategy->as_q3_qvm(ss(ctx)) != NULL); }
static qa_q1_services *ss_q1_weapon_source(void *ctx) { return qa_app_shared_sim_q1_weapon_source(ss(ctx)); }
static bool ss_q2_weapon_source(void *ctx, qa_q2_game_services **out_game, qa_q2w_weapons **out_weapons) {
  return qa_app_shared_sim_q2_weapon_source(ss(ctx), out_game, out_weapons);
}
static qa_app_q3_selected_arsenal *ss_selected_q3_weapon_source(void *ctx) { return qa_app_shared_sim_selected_q3_weapon_source(ss(ctx)); }
/* player-movement.ts createPlayerMovementPrediction reads: source.pool.at(slot) +
 * readQ3MovementState(entity, source.records), readQ3ArsenalRuntime. */
static bool ss_q3_baseline(void *ctx, qa_client_id client, qa_movement_state *out_state, double *out_weapon, bool *has_weapon) {
  qa_app_shared_simulation *sim = ss(ctx);
  if ((sim->strategy->as_q3(sim) == NULL) || !sim->source.q3_owner) return false;
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(qa_app_q3_source_subsystems_owner_pool(sim->source.q3_owner), (double)client.slot);
  if (!entity) return false;
  static __thread qa_actor_id jump_pad;
  out_state->kind = QA_MOVEMENT_STATE_Q3;
  out_state->as.q3 = qa_app_read_q3_movement_state(entity, qa_app_q3_source_subsystems_owner_records(sim->source.q3_owner), &jump_pad);
  /* weapon: entity?.client?.ps.weapon ?? (arsenal fallback in the caller) */
  *has_weapon = entity->client != NULL;
  if (entity->client) *out_weapon = entity->client->ps.weapon;
  return true;
}
static bool ss_q3_arsenal_runtime(void *ctx, qa_client_id client, const char **out_product, qa_q3_arsenal_runtime_state *out) {
  qa_app_shared_simulation *sim = ss(ctx);
  if ((sim->strategy->as_q3(sim) == NULL) || !sim->source.q3_owner) return false;
  qa_q3_game_entity *entity = qa_q3_entity_pool_at(qa_app_q3_source_subsystems_owner_pool(sim->source.q3_owner), (double)client.slot);
  if (!entity) return false;
  *out_product = qa_app_q3_source_runtime_options(sim->source.q3)->product;
  *out = qa_app_read_q3_arsenal_runtime(entity, *out);
  return true;
}
static qa_combat_state_result ss_combat_read(void *ctx, qa_actor_id actor) { return qa_gameplay_authority_read(ss(ctx)->combat, actor); }
static qa_app_sim_prediction_simulation ss_prediction(void *ctx) {
  qa_app_shared_simulation *sim = ss(ctx);
  qa_app_sim_prediction_simulation prediction; memset(&prediction, 0, sizeof prediction);
  prediction.ctx = sim; prediction.q3_baseline = ss_q3_baseline; prediction.q3_arsenal_runtime = ss_q3_arsenal_runtime;
  prediction.combat_read = ss_combat_read; prediction.scene = qa_shared_as_scene_queries(sim->scene);
  prediction.native_quakeworld = (sim->strategy->as_quakec(sim) != NULL) && qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "quakeworld");
  prediction.time_seconds = qa_app_shared_sim_time_seconds(sim);
  return prediction;
}
qa_app_sim_bot_simulation qa_app_shared_sim_bot_simulation(qa_app_shared_simulation *sim) {
  qa_app_sim_bot_simulation access; memset(&access, 0, sizeof access);
  access.ctx = sim;
  access.session = sim->session; access.recipe = &sim->recipe; access.mode = sim->options.mode;
  access.max_clients = sim->options.max_clients; access.skill = sim->options.skill;
  access.world_model_count = sim->world.kind == QA_DECODED_WORLD_Q1 ? sim->world.as.q1->model_count
      : sim->world.kind == QA_DECODED_WORLD_Q2 ? sim->world.as.q2->model_count : sim->world.as.q3->model_count;
  access.actors = sim->actors; access.bodies = qa_app_sim_shared_physics_bodies(sim->physics); access.physics = sim->physics;
  access.combat = sim->combat; access.inventory = sim->inventory; access.scene = sim->scene; access.random = sim->random;
  access.bot_services = &sim->bot_services;
  access.time_seconds = ss_time; access.source_entity_text = ss_source_entity_text; access.players = ss_players;
  access.client_identities = ss_client_identities; access.movement_player = ss_movement_player; access.player_ui = ss_player_ui;
  access.bot_entity = ss_bot_entity; access.player_command = ss_player_command; access.prepare_bot_client = ss_prepare_bot_client;
  access.disconnect_player = ss_disconnect_player; access.set_source_player_identity = ss_set_source_player_identity;
  access.source_objectives = ss_source_objectives; access.q2_server_cvars = ss_q2_server_cvars;
  access.q1_source = ss_bot_q1_source; access.q2_source = ss_q2_source; access.q3_source = ss_bot_q3_source; access.q3_guest = ss_bot_q3_guest;
  access.q1_weapon_source = ss_q1_weapon_source; access.q2_weapon_source = ss_q2_weapon_source;
  access.selected_q3_weapon_source = ss_selected_q3_weapon_source; access.prediction = ss_prediction;
  return access;
}

/* ---- qa_app_sim_bot_navigation from rereleaseNavigation() ---- */
static qa_save_value *ss_navigation_checkpoint(void *ctx) {
  qa_app_sim_bot_navigation_checkpoint checkpoint;
  if (!qa_app_sim_application_bot_navigation_checkpoint(ctx, &checkpoint)) qa_range_error("Out of memory");
  qa_save_value *value = qa_app_sim_bot_navigation_checkpoint_value(&checkpoint);
  qa_app_sim_bot_navigation_checkpoint_destroy(&checkpoint);
  return value;
}
static void ss_navigation_restore(void *ctx, const qa_save_value *value) {
  qa_app_sim_application_bot_navigation_restore_checkpoint(ctx, value, NULL, NULL);
}
static void ss_navigation_restart(void *ctx) { qa_app_sim_application_bot_navigation_restart_round(ctx); }
bool qa_app_shared_sim_bot_navigation(qa_app_shared_simulation *sim, qa_app_sim_bot_navigation *out) {
  if (!sim->native_navigation) return false;
  memset(out, 0, sizeof *out);
  out->selected = *qa_app_sim_application_bot_navigation_selected(sim->native_navigation);
  out->ctx = sim->native_navigation;
  out->checkpoint = ss_navigation_checkpoint; out->restore_checkpoint = ss_navigation_restore; out->restart_round = ss_navigation_restart;
  return true;
}

/* ---- qa_app_sim_navigation_simulation (navigation.ts createApplicationBotNavigation) ---- */
/* TS simulation.players(): the owned actors of the admitted players with a movement player
 * (navigation.ts flatMaps movementPlayer(actor) ?? []). Borrowed until the next call. */
static const qa_owned_actor *ss_nav_players(void *ctx, size_t *out_count) {
  static __thread qa_owned_actor *owned; static __thread size_t capacity;
  qa_app_shared_simulation *sim = ss(ctx);
  size_t count = 0, kept = 0;
  const qa_actor_id *actors = qa_app_shared_sim_players(sim, &count);
  if (count > capacity) {
    capacity = count;
    owned = realloc(owned, capacity * sizeof *owned);
    if (owned == NULL) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < count; i++) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(sim, actors[i]);
    if (player != NULL) owned[kept++] = player->actor;
  }
  *out_count = kept;
  return owned;
}
/* TS MovementPredictionPlayer view of a MovementPlayer: the player object (identity) and its members. */
static bool ss_nav_movement_player(void *ctx, qa_owned_actor actor, qa_app_sim_nav_player *out) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(ss(ctx), actor.id);
  if (player == NULL) return false;
  memset(out, 0, sizeof *out);
  out->identity = player;
  qa_app_sim_movement_prediction_player *value = &out->value;
  value->client = player->client; value->actor = player->actor;
  value->profile = player->profile; value->standing_bounds = player->standing_bounds; value->bounds = player->bounds;
  value->has_source_movement = player->has_source_movement; value->source_movement = player->source_movement;
  value->character = player->character; value->world_gravity = player->world_gravity;
  value->has_q2_movement_config = qa_app_sim_movement_player_q2_config(player, &value->q2_movement_config);
  value->flight = player->flight;
  value->has_source_environment = player->has_source_environment; value->source_environment = player->source_environment;
  value->gravity_multiplier = player->gravity_multiplier;
  value->movement_speed_multiplier = qa_app_sim_movement_player_speed_multiplier(player);
  value->state = player->state; value->arsenal = player->arsenal; value->animation = player->animation;
  value->view_height = player->view_height;
  value->numeric = player->services.numeric;
  return true;
}
/* The Q3QvmServerGame members guestMovementProjection(guest, player, recipe) reads. */
static qa_app_q3_qvm_server_game *ss_nav_guest(void *ctx) {
  qa_app_shared_simulation *sim = ss(ctx);
  if ((sim->strategy->as_q3_qvm(sim) == NULL)) qa_raise("Error", 0, 0, "Q3 guest movement projection outlived its guest");
  return sim->source.qvm;
}
static qa_q3_player_state ss_nav_guest_player_state(void *ctx, double source_entity) {
  /* The record's arrays stay valid until the next read (guest.records.player(slot) is a live view). */
  static __thread double stats[16], persistent[16], powerups[16], ammo[16];
  qa_q3_player_state state;
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(ss_nav_guest(ctx)), source_entity, &state, stats, persistent, powerups, ammo);
  return state;
}
static qa_owned_actor ss_nav_guest_actor_at(void *ctx, double source_entity) {
  return qa_app_q3_guest_records_actor(qa_app_q3_qvm_server_game_records(ss_nav_guest(ctx)), source_entity);
}
static bool ss_nav_guest_reference_at(void *ctx, double source_entity, qa_actor_id *out) {
  return qa_app_q3_guest_records_reference(qa_app_q3_qvm_server_game_records(ss_nav_guest(ctx)), source_entity, out);
}
static void ss_nav_guest_entity_bounds(void *ctx, double source_entity, qa_vec3 *out_mins, qa_vec3 *out_maxs) {
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(qa_app_q3_qvm_server_game_records(ss_nav_guest(ctx)), source_entity);
  *out_mins = qa_qvm_shared_entity_get_mins(entity);
  *out_maxs = qa_qvm_shared_entity_get_maxs(entity);
}
static double ss_nav_guest_cvar_value(void *ctx, const char *name) {
  return qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(ss_nav_guest(ctx))), name);
}
static bool ss_nav_guest_player_arsenal(void *ctx, qa_actor_id actor, qa_arsenal_state *out) {
  return qa_app_q3_qvm_server_game_player_arsenal(ss_nav_guest(ctx), actor, out);
}
static bool ss_nav_q3_guest(void *ctx, qa_app_q3_guest_movement_host *out_host) {
  if ((ss(ctx)->strategy->as_q3_qvm(ss(ctx)) == NULL)) return false;
  memset(out_host, 0, sizeof *out_host);
  out_host->ctx = ctx;
  out_host->player_state = ss_nav_guest_player_state;
  out_host->actor_at = ss_nav_guest_actor_at;
  out_host->reference_at = ss_nav_guest_reference_at;
  out_host->entity_bounds = ss_nav_guest_entity_bounds;
  out_host->cvar_value = ss_nav_guest_cvar_value;
  out_host->player_arsenal = ss_nav_guest_player_arsenal;
  return true;
}
static const qa_app_q3_player *ss_nav_q3_guest_players(void *ctx, size_t *out_count) {
  static __thread qa_app_q3_player *players; static __thread size_t capacity;
  qa_app_q3_qvm_server_game *game = ss_nav_guest(ctx);
  size_t count = qa_app_q3_qvm_server_game_players(game, players, capacity);
  if (count > capacity) {
    capacity = count;
    players = realloc(players, capacity * sizeof *players);
    if (players == NULL) qa_range_error("Out of memory");
    count = qa_app_q3_qvm_server_game_players(game, players, capacity);
  }
  *out_count = count;
  return players;
}
static qa_q1_services *ss_nav_q1_game(void *ctx) {
  qa_app_shared_simulation *sim = ss(ctx);
  return (sim->strategy->as_q1(sim) != NULL) ? sim->source.q1_game : NULL;
}
static bool ss_nav_q2_source(void *ctx, qa_app_sim_nav_q2_source *out) {
  static __thread qa_q2mp_rogue_movers *expansion_movers[QA_Q2_PRODUCT_MAX_EXPANSIONS];
  qa_q2_product_runtime *runtime = qa_app_shared_sim_q2_source(ss(ctx));
  if (runtime == NULL) return false;
  memset(out, 0, sizeof *out);
  out->game = runtime->services;
  out->base_entities = runtime->base_entities;
  out->movers = runtime->movers;
  for (size_t i = 0; i < runtime->expansion_count; i++)
    expansion_movers[i] = runtime->expansions[i].entities.has_movers ? &runtime->expansions[i].entities.movers : NULL;
  out->expansion_movers = expansion_movers;
  out->expansion_count = runtime->expansion_count;
  return true;
}
static bool ss_nav_q3_source(void *ctx, qa_app_sim_nav_q3_source *out) {
  qa_app_shared_simulation *sim = ss(ctx);
  if ((sim->strategy->as_q3(sim) == NULL)) return false;
  out->records = qa_app_q3_source_subsystems_owner_records(sim->source.q3_owner);
  out->level_time = qa_app_q3_source_runtime_level_time(sim->source.q3);
  return true;
}
/* TS applicationTrainConnections(simulation, profile): the array stays valid until the next call. */
static const qa_navigation_connection *ss_nav_train_connections(void *ctx, qa_navigation_profile profile, size_t *out_count) {
  static __thread qa_app_sim_train_connections connections;
  qa_app_sim_train_connections_free(&connections);
  qa_app_sim_bot_simulation simulation = qa_app_shared_sim_bot_simulation(ss(ctx));
  connections = qa_app_sim_application_train_connections(&simulation, &profile);
  *out_count = connections.count;
  return connections.connections;
}
static qa_bot_travel_prediction_result ss_nav_predict_movement(void *ctx, const qa_app_sim_nav_player *player,
    qa_bot_movement_prediction query, qa_navigation_runtime *navigation) {
  qa_app_sim_bot_simulation simulation = qa_app_shared_sim_bot_simulation(ss(ctx));
  return qa_app_sim_predict_application_bot_movement(&simulation, &player->value, query, navigation, NULL);
}
qa_app_sim_navigation_simulation qa_app_shared_sim_navigation_simulation(qa_app_shared_simulation *sim, const qa_executable_recipe *recipe) {
  qa_app_sim_navigation_simulation access; memset(&access, 0, sizeof access);
  access.ctx = sim;
  access.recipe = recipe;
  access.world = sim->world; access.scene = sim->scene; access.inventory = sim->inventory;
  access.time_seconds = ss_time; access.players = ss_nav_players; access.movement_player = ss_nav_movement_player;
  access.q3_guest = ss_nav_q3_guest; access.q3_guest_players = ss_nav_q3_guest_players; access.q1_game = ss_nav_q1_game;
  access.q2_source = ss_nav_q2_source; access.q3_source = ss_nav_q3_source; access.prediction = ss_prediction;
  access.train_connections = ss_nav_train_connections; access.predict_movement = ss_nav_predict_movement;
  return access;
}

/* ---- qa_app_q2_match_simulation (q2-match-reports.ts reads) ---- */
static bool ss_m_has_q2_source(void *ctx) { return qa_app_shared_sim_q2_source(ss(ctx)) != NULL; }
static bool ss_m_has_q2_native(void *ctx) { return qa_app_shared_sim_q2_native(ss(ctx), NULL, NULL); }
static qa_cvar_registry *ss_m_q2_server_cvars(void *ctx) { return qa_app_shared_sim_q2_server_cvars(ss(ctx)); }
/* source.players.intermission (callers check q2Source() first). */
static qa_q2_intermission ss_m_intermission(void *ctx) {
  qa_q2_product_runtime *source = qa_app_shared_sim_q2_source(ss(ctx));
  if (source == NULL) qa_raise("Error", 0, 0, "Q2 match reporting has no source players");
  return qa_q2_players_intermission(source->players);
}
static const char *ss_m_intermission_kind(void *ctx) { return ss_m_intermission(ctx).kind; }
static double ss_m_intermission_started(void *ctx) { return ss_m_intermission(ctx).started; }
/* source.players.states.get(actor): false is TS undefined. */
static bool ss_m_source_player(void *ctx, qa_actor_id actor, bool *spectator_out, double *score_out) {
  qa_q2_product_runtime *source = qa_app_shared_sim_q2_source(ss(ctx));
  qa_q2_base_player_state *state = source == NULL ? NULL : qa_q2_players_get_state(source->players, actor);
  if (state == NULL) return false;
  *spectator_out = state->spectator;
  *score_out = state->score;
  return true;
}
static const char *ss_m_native_edition(void *ctx) { return (ss(ctx)->strategy->as_q2_native(ss(ctx)) != NULL) ? ss(ctx)->source.edition : NULL; }
static qa_module_identity ss_m_native_module(void *ctx) {
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  if (!qa_app_shared_sim_q2_native(ss(ctx), &classic, &rerelease)) qa_raise("Error", 0, 0, "Native Quake II source is unavailable");
  return ss_classic(ss(ctx)) ? qa_app_sim_classic_guest_world_module(classic) : qa_app_sim_rerelease_guest_world_module(rerelease);
}
/* q2NativePlayers().map(player => player.client.slot). */
static size_t ss_m_native_players(void *ctx, const double **slots_out) {
  static __thread double *slots; static __thread size_t capacity;
  const qa_client_id *clients = NULL; const qa_actor_id *actors = NULL;
  size_t count = qa_app_shared_sim_q2_native_players(ss(ctx), &clients, &actors);
  if (count + 1 > capacity) {
    capacity = count + 16;
    slots = realloc(slots, capacity * sizeof *slots);
    if (slots == NULL) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < count; i++) slots[i] = (double)clients[i].slot;
  *slots_out = slots;
  return count;
}
static bool ss_m_player_client_slot(void *ctx, qa_actor_id actor, double *slot_out) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(ss(ctx), actor, &client)) return false;
  *slot_out = (double)client.slot;
  return true;
}
/* native.playerState(entity) for a classic DLL; NULL when the native world is rerelease. */
static const qa_q2_player_state *ss_m_classic_player_state(void *ctx, double entity_number) {
  static __thread qa_q2_player_state state;
  qa_app_sim_classic_guest_world *classic = NULL;
  if (!qa_app_shared_sim_q2_native(ss(ctx), &classic, NULL) || !ss_classic(ss(ctx))) return NULL;
  state = qa_app_sim_classic_guest_world_player_state(classic, entity_number);
  return &state;
}
static const qa_q2_rerelease_player_state *ss_m_rerelease_player_state(void *ctx, double entity_number) {
  static __thread qa_q2_rerelease_player_state state;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  if (!qa_app_shared_sim_q2_native(ss(ctx), NULL, &rerelease) || ss_classic(ss(ctx))) return NULL;
  state = qa_app_sim_rerelease_guest_world_player_state(rerelease, entity_number);
  return &state;
}
/* node:crypto randomUUID(). */
static const char *ss_m_random_uuid(void *ctx) {
  (void)ctx; /* randomUUID reads no simulation state */
  static __thread char uuid[37];
  qa_llm_random_uuid(uuid);
  return uuid;
}
qa_app_q2_match_simulation qa_app_shared_sim_q2_match_simulation(qa_app_shared_simulation *sim) {
  qa_app_q2_match_simulation access; memset(&access, 0, sizeof access);
  access.ctx = sim;
  access.mode = sim->options.mode;
  access.map_path = sim->recipe.map.geometry.requested_path;
  access.has_q2_source = ss_m_has_q2_source; access.has_q2_native = ss_m_has_q2_native;
  access.q2_server_cvars = ss_m_q2_server_cvars; access.intermission_kind = ss_m_intermission_kind;
  access.intermission_started = ss_m_intermission_started; access.source_player = ss_m_source_player;
  access.native_edition = ss_m_native_edition; access.native_module = ss_m_native_module;
  access.native_players = ss_m_native_players; access.player_client_slot = ss_m_player_client_slot;
  access.classic_player_state = ss_m_classic_player_state; access.rerelease_player_state = ss_m_rerelease_player_state;
  access.random_uuid = ss_m_random_uuid;
  return access;
}

/* ---- createSimulationPredictionHost's SharedSimulation view (prediction/presentation.ts) ---- */
static bool ss_p_movement_player(void *ctx, qa_actor_id actor, qa_app_simulation_movement_player_view *out) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(ss(ctx), actor);
  if (player == NULL) return false;
  memset(out, 0, sizeof *out);
  out->actor = player->actor; out->last_sequence = player->last_sequence;
  out->state = player->state; out->arsenal = player->arsenal; out->animation = player->animation;
  out->has_source_environment = player->has_source_environment; out->source_environment = player->source_environment;
  out->gravity_multiplier = player->gravity_multiplier; out->world_gravity = player->world_gravity;
  out->bounds = player->bounds; out->standing_bounds = player->standing_bounds;
  out->view_angles = player->view_angles; out->view_height = player->view_height;
  out->ground = player->ground; out->water_level = player->water_level; out->water_type = player->water_type;
  out->profile = player->profile;
  out->character_is_q3 = player->character == QA_GAME_FAMILY_Q3;
  return true;
}
static bool ss_p_has_q3_source(void *ctx) { return qa_app_shared_sim_q3_source(ss(ctx)) != NULL; }
/* source.sourceState().clients.find(value => value.actor.equals(actor))?.state */
static const qa_q3_base_player_state_record *ss_p_source_client_state(void *ctx, qa_actor_id actor) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(ss(ctx));
  if (source == NULL) return NULL;
  qa_app_q3_source_presentation_state *state = qa_app_q3_source_runtime_source_state(source);
  for (size_t i = 0; state != NULL && i < state->client_count; i++)
    if (qa_same_actor(state->clients[i].actor, actor)) return &state->clients[i].state;
  return NULL;
}
/* actorAt: number => source.records.get(number)?.actor.id ?? null */
static bool ss_p_actor_at_slot(void *ctx, double slot, qa_actor_id *out) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(ss(ctx));
  qa_q3_game_entity *entity = source == NULL ? NULL : qa_q3_base_entity_records_get(qa_app_q3_source_runtime_records(source), slot);
  if (entity == NULL) return false;
  *out = qa_q3_game_entity_actor(entity).id;
  return true;
}
/* numberOf: id => source.records.byActor(id)?.slot ?? null */
static bool ss_p_slot_of_actor(void *ctx, qa_actor_id actor, double *out) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(ss(ctx));
  qa_q3_game_entity *entity = source == NULL ? NULL : qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(source), true, actor);
  if (entity == NULL) return false;
  *out = entity->slot;
  return true;
}
/* isBrush: source.records.byActor(hit.actor)?.r.model.kind === "inline" */
static bool ss_p_actor_is_inline_model(void *ctx, qa_actor_id actor) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(ss(ctx));
  qa_q3_game_entity *entity = source == NULL ? NULL : qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(source), true, actor);
  return entity != NULL && entity->r.model.kind == QA_Q3_BASE_MODEL_INLINE;
}
static const char *ss_p_command_kind(qa_user_command_kind kind) {
  return kind == QA_USER_COMMAND_Q1 ? "q1-netquake" : kind == QA_USER_COMMAND_QW ? "q1-quakeworld"
      : kind == QA_USER_COMMAND_Q2 ? "q2-classic" : kind == QA_USER_COMMAND_Q2_RERELEASE ? "q2-rerelease" : "q3";
}
/* relativeQ3SourceCommand(input.source, input.command.kind, presented, delta) */
static qa_q3_base_user_command ss_p_relative_source_command(void *ctx, qa_command_source source, qa_user_command_kind kind,
    qa_q3_base_user_command presented, qa_vec3 delta_angles) {
  (void)ctx; /* TS imports relativeQ3SourceCommand; it reads no simulation state */
  return qa_app_sim_relative_q3_source_command(source, ss_p_command_kind(kind), presented, delta_angles, NULL);
}
/* publishQ3CharacterMovementEvent(output.player.animation.state.kind, event) */
static bool ss_p_publish_event(void *ctx, const char *animation_kind, double event) {
  (void)ctx; /* TS imports publishQ3CharacterMovementEvent; it reads no simulation state */
  qa_game_family family;
  if (!qa_game_family_parse(animation_kind, &family)) qa_range_error("Unknown character animation kind %s", animation_kind);
  return qa_app_sim_publish_q3_character_movement_event(family, event);
}
qa_app_simulation_prediction_source qa_app_shared_sim_prediction_source(qa_app_shared_simulation *sim) {
  qa_app_simulation_prediction_source source; memset(&source, 0, sizeof source);
  source.ctx = sim;
  source.recipe = qa_app_shared_sim_recipe(sim);
  source.scene = qa_shared_as_scene_queries(sim->scene);
  source.movement_player = ss_p_movement_player;
  source.has_q3_source = ss_p_has_q3_source;
  source.source_client_state = ss_p_source_client_state;
  source.actor_at_slot = ss_p_actor_at_slot;
  source.slot_of_actor = ss_p_slot_of_actor;
  source.actor_is_inline_model = ss_p_actor_is_inline_model;
  source.host_ctx = sim;
  source.relative_source_command = ss_p_relative_source_command;
  source.publish_event = ss_p_publish_event;
  /* resolve_controls NULL: the adapter calls resolveQ3ArsenalControls itself, as TS does. */
  return source;
}

/* ---- ApplicationInput / ApplicationSeatUi's Simulation view (contracts Simulation members of SharedSimulation) ---- */
static qa_app_simulation_travel ss_v_capture_travel(void *ctx, const char *spawn_point) { return qa_app_shared_sim_capture_travel(ss(ctx), spawn_point); }
static qa_app_sim_player_admission ss_v_admit_travel(void *ctx, qa_client_id client, qa_app_simulation_travel travel) {
  return qa_app_shared_sim_admit_travel(ss(ctx), client, &travel);
}
static const qa_active_mod_client_presentation *ss_v_mod_sources(void *ctx, size_t *out_count) {
  return qa_app_shared_sim_mod_client_presentation_sources(ss(ctx), out_count);
}
static const qa_transition_intent *ss_v_take_transitions(void *ctx, size_t *out_count) { return qa_app_shared_sim_take_transitions(ss(ctx), out_count); }
qa_app_sim_presentation_access qa_app_shared_sim_presentation_access(qa_app_shared_simulation *sim) {
  qa_app_sim_presentation_access access; memset(&access, 0, sizeof access);
  access.ctx = sim;
  access.mod_client_presentation_sources = ss_v_mod_sources;
  access.world_text = ss_world_text;
  access.player_ui = ss_player_ui;
  access.capture_travel = ss_v_capture_travel;
  access.admit_travel = ss_v_admit_travel;
  access.character_views = ss_character_views;
  access.player_command = ss_player_command;
  access.take_transitions = ss_v_take_transitions;
  access.presentations = ss_presentations;
  /* drain_presentation_events stays NULL: the C SharedSimulation drains event pointers
   * (qa_app_shared_sim_drain_presentation_events), and the application drains it directly. */
  access.register_resource = ss_register_resource;
  access.player_view = ss_player_view;
  access.admit_player = ss_admit_player;
  return access;
}

/* ---- simulation.scene as ApplicationAudio.prepareEnvironment takes it; kept until the simulation is destroyed. ---- */
const qa_scene_queries *qa_app_shared_sim_scene_queries(qa_app_shared_simulation *sim) {
  qa_scene_queries *queries = qa_app_shared_sim_keep(sim, sizeof *queries);
  *queries = qa_shared_as_scene_queries(sim->scene);
  return queries;
}
