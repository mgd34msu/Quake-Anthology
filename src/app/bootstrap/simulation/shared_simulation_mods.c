/* SharedSimulation.load's SessionMods.open (runtime.ts, `simulation.modOwner =
 * await SessionMods.open({...})`): the ModHostServices over the shared
 * owners, the engine fragment with typed views (world/session/mod_engine.h),
 * and the PresentationState owner binding adapted to
 * qa_session_mod_bound_events. SessionMods.open is synchronous in C.
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/world/session/mod_match.h"

static void md_oom(const void *p) { if (p == NULL) qa_range_error("Out of memory"); }

/* The C views of options.preparedMods / enabledMods / modTravel /
 * modCommands / modFiles: prepared_mods[i] is a const qa_prepared_mod *,
 * enabled_mods a qa_app_sim_mod_selections, mod_travel a
 * qa_mod_travel_checkpoint, mod_commands a qa_mod_commands and mod_files a
 * qa_mod_user_files (types.h keeps them as const void *). */
size_t qa_app_sim_enabled_mod_count(const void *enabled_mods) {
  return enabled_mods == NULL ? 0 : ((const qa_app_sim_mod_selections *)enabled_mods)->count;
}
size_t qa_app_sim_mod_travel_count(const void *mod_travel) {
  return mod_travel == NULL ? 0 : ((const qa_mod_travel_checkpoint *)mod_travel)->mod_count;
}

/* Everything the closures read lives as long as the simulation. */
typedef struct md_host {
  qa_app_shared_simulation *sim;
  bool restoring, travelling;
  qa_source_weapon_services weapons;
  qa_source_match_services match;
  qa_session_mod_engine_services engine;
  qa_session_mod_engine_scene scene;
  qa_session_mod_engine_physics physics;
  qa_session_mod_engine_camera camera;
  qa_q1_client_visibility_scene visibility;
  qa_q1_visibility_client client;
  qa_actor_id *area; size_t area_capacity;
  qa_app_sim_native_mod_host_context native;
  qa_mod_host_services services;
  qa_session_mods_presentation_option presentation;
  qa_app_presentation_owner_binding **bindings; size_t binding_count;
  qa_mod_selection *enabled; size_t enabled_count;
  qa_prepared_mod *prepared;
} md_host;
static void md_dispose(void *ctx) {
  md_host *host = ctx;
  for (size_t i = 0; i < host->binding_count; i++) qa_app_presentation_owner_binding_free(host->bindings[i]);
  free(host->bindings); free(host->area); free(host->enabled); free(host->prepared); free(host);
}

/* ---- services.weapons ---- */
typedef struct md_unbind { qa_app_shared_simulation *sim; qa_actor_id actor; int handle; } md_unbind;
static void md_weapon_unbind(void *ctx) {
  md_unbind *unbind = ctx;
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&unbind->sim->weapon_slots, &unbind->actor);
  if (slot != NULL) qa_app_sim_weapon_slot_unbind(*slot, unbind->handle);
}
static qa_source_weapon_unbind md_weapon_bind(void *ctx, qa_owned_actor actor, qa_source_weapon_binding binding) {
  qa_app_shared_simulation *sim = ((md_host *)ctx)->sim;
  qa_session_actor_registry_assert_owned(sim->actors, actor);
  if (!qa_ss_map_has(&sim->weapon_slots, &actor.id)) qa_app_shared_sim_bind_weapon_slot(sim, actor.id);
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor.id);
  if (slot == NULL) qa_raise("Error", 0, 0, "Source weapon slot was not admitted");
  md_unbind *unbind = qa_app_shared_sim_keep(sim, sizeof *unbind);
  unbind->sim = sim; unbind->actor = actor.id; unbind->handle = qa_app_sim_weapon_slot_bind(*slot, binding);
  return (qa_source_weapon_unbind){unbind, md_weapon_unbind};
}
static bool md_weapon_selected(void *ctx, qa_actor_id actor, qa_provider_id provider) {
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&((md_host *)ctx)->sim->weapon_slots, &actor);
  return slot != NULL && qa_app_sim_weapon_slot_selected(*slot, provider);
}
static bool md_weapon_presented(void *ctx, qa_actor_id actor, qa_provider_id provider) {
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&((md_host *)ctx)->sim->weapon_slots, &actor);
  return slot != NULL && qa_app_sim_weapon_slot_presented(*slot, provider);
}
static bool md_weapon_request(void *ctx, qa_actor_id actor, qa_weapon_reference weapon) {
  return qa_app_shared_sim_request_weapon(((md_host *)ctx)->sim, actor, weapon);
}

/* ---- services top level ---- */
static void md_damage_context(void *ctx, qa_provider_id source, double *out_sequence, qa_provider_id *out_weapon_provider,
    qa_provider_id *out_combat_provider, qa_provider_id *out_inventory_provider, qa_provider_id *out_movement_provider) {
  qa_app_shared_simulation *sim = ((md_host *)ctx)->sim;
  *out_sequence = sim->attack_sequence++;
  *out_weapon_provider = source; *out_combat_provider = sim->recipe.combat.provider;
  *out_inventory_provider = sim->recipe.inventory.provider; *out_movement_provider = sim->recipe.movement.provider;
}
static qa_actor_id md_reference_saved(void *ctx, qa_saved_actor_id actor) {
  md_host *host = ctx;
  if (!host->restoring && host->travelling) qa_raise("Error", 0, 0, "Retained mod state cannot reference actors from the previous world");
  return qa_session_actor_registry_reference_saved(host->sim->actors, actor, host->restoring ? "checkpoint" : "current");
}
static qa_source_time md_time(void *ctx) { return ((md_host *)ctx)->sim->source_frame.time; }

/* ---- services.engine ---- */
static void md_events_emit(void *ctx, const qa_session_mod_emit *emit) {
  qa_app_sim_source_presentation_event source = qa_app_sim_presentation_event_source(&emit->event);
  qa_app_shared_sim_emit(((md_host *)ctx)->sim, emit->event.content, source, emit->has_time ? &emit->time : NULL,
      emit->event.has_recipient ? &emit->event.recipient : NULL);
}
static void md_events_register_resource(void *ctx, const qa_session_mod_resource_registration *resource) {
  qa_app_shared_sim_register_resource(((md_host *)ctx)->sim, resource->content, resource->path, resource->resource);
}
static void md_print(void *ctx, const char *text) {
  qa_network_event event; memset(&event, 0, sizeof event);
  event.kind = QA_NETWORK_EVENT_PRINT; event.level = 2; event.text = text;
  qa_app_sim_simulation_events_message(((md_host *)ctx)->sim->events, event, false, (qa_actor_id){0}, false, 0);
}
static void md_message(void *ctx, qa_network_event event, const qa_actor_id *actor) {
  qa_app_sim_simulation_events_message(((md_host *)ctx)->sim->events, event, actor != NULL, actor != NULL ? *actor : (qa_actor_id){0}, false, 0);
}
static bool md_world(void *ctx, qa_actor_id *out) { return qa_app_shared_sim_world_actor(((md_host *)ctx)->sim, out); }
static const char *md_classname(void *ctx, qa_actor_id actor) { return qa_app_shared_sim_classname(((md_host *)ctx)->sim, actor); }
static bool md_inline_model(void *ctx, double index, qa_bounds *out_bounds, qa_content_id *out_content, const char **out_family) {
  qa_app_shared_simulation *sim = ((md_host *)ctx)->sim;
  *out_bounds = qa_shared_model_bounds(sim->scene, index);
  *out_content = sim->recipe.map.geometry_content;
  *out_family = sim->world.kind == QA_DECODED_WORLD_Q1 ? "q1" : sim->world.kind == QA_DECODED_WORLD_Q2 ? "q2" : "q3";
  return true;
}
static const qa_actor_id *md_area_entities(void *ctx, qa_bounds bounds, size_t *out_count) {
  md_host *host = ctx;
  size_t count = 0;
  const qa_spatial_actor *actors = qa_shared_query_actors(host->sim->scene, bounds, NULL, &count);
  if (count > host->area_capacity) {
    qa_actor_id *grown = realloc(host->area, count * sizeof *grown);
    md_oom(grown);
    host->area = grown; host->area_capacity = count;
  }
  for (size_t i = 0; i < count; i++) host->area[i] = actors[i].body.actor;
  *out_count = count;
  return host->area;
}
/* clients.at(slot): the Q1VisibilityClient view of the player in client slot - 1. */
static const void *md_clients_at(void *ctx, double slot) {
  md_host *host = ctx;
  qa_app_shared_simulation *sim = host->sim;
  size_t count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(sim, &count);
  bool has_actor = false; qa_actor_id actor = {0};
  for (size_t i = 0; i < count && !has_actor; i++) {
    qa_client_id client;
    if (qa_app_shared_sim_player_client(sim, players[i], &client) && (double)client.slot == slot - 1) { has_actor = true; actor = players[i]; }
  }
  qa_q1_visibility_client *out = &host->client;
  memset(out, 0, sizeof *out);
  out->has_actor = has_actor; out->actor = actor;
  qa_body_state_result body = {0};
  if (has_actor) body = qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(sim->physics), actor);
  out->free = !has_actor || !body.has_value;
  if (has_actor) {
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
    out->health = combat.has_value ? combat.value.health : 0;
    out->notarget = qa_app_shared_sim_monster_target_notarget(sim, actor);
  }
  if (body.has_value) {
    out->origin = body.value.origin;
    qa_app_sim_player_view view = qa_app_shared_sim_player_view(sim, actor);
    out->view_offset = (qa_vec3){view.origin.x - body.value.origin.x, view.origin.y - body.value.origin.y,
        view.origin.z + view.view_height - body.value.origin.z};
  }
  return out;
}
static double md_point_leaf(void *ctx, qa_vec3 point) { return qa_shared_point_leaf(ctx, point); }
static double md_leaf_cluster(void *ctx, double leaf) { return qa_shared_leaf_cluster(ctx, leaf); }
static bool md_cluster_visible(void *ctx, double from, double to, const char *kind) { return qa_shared_cluster_visible(ctx, from, to, kind); }
static const qa_actor_id *md_players(void *ctx, size_t *out_count) { return qa_app_shared_sim_players(((md_host *)ctx)->sim, out_count); }
static void md_camera(void *ctx, qa_actor_id actor, qa_vec3 *out_origin, qa_vec3 *out_angles) {
  qa_app_sim_player_view view = qa_app_shared_sim_player_view(((md_host *)ctx)->sim, actor);
  *out_origin = (qa_vec3){view.origin.x, view.origin.y, view.origin.z + view.view_height};
  *out_angles = view.angles;
}
static int md_bind_source(void *ctx, qa_owned_actor actor, qa_app_sim_source_body_physics source) {
  return qa_app_sim_shared_physics_bind_source(((md_host *)ctx)->sim->physics, actor, source);
}
static void md_unbind_source(void *ctx, int handle) { qa_app_sim_shared_physics_unbind_source(((md_host *)ctx)->sim->physics, handle); }
static void md_set_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  qa_app_sim_shared_physics_set_collision(((md_host *)ctx)->sim->physics, actor, collision);
}
static const char *md_step(void *ctx, qa_owned_actor actor, double elapsed) {
  return qa_app_sim_shared_physics_step(((md_host *)ctx)->sim->physics, actor, elapsed);
}
static void md_touch_triggers(void *ctx, qa_owned_actor actor) { qa_app_sim_shared_physics_touch_triggers(((md_host *)ctx)->sim->physics, actor); }
static qa_q1_pusher_services md_q1_pusher_services(void *ctx, qa_app_sim_q1_pusher_env *env) {
  env->physics = ((md_host *)ctx)->sim->physics;
  return qa_app_sim_shared_physics_q1_pusher_services(env);
}
static bool md_read_q1_pusher(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  return qa_app_sim_shared_physics_read_q1_pusher(((md_host *)ctx)->sim->physics, actor, out);
}
static void md_write_q1_pusher(void *ctx, qa_q1_physics_entity entity) { qa_app_sim_shared_physics_write_q1_pusher(((md_host *)ctx)->sim->physics, entity); }

/* ---- presentation: SimulationEvents.bindOwner/finishOwnerRestore/refreshOwner ---- */
typedef struct md_binding { md_host *host; qa_app_presentation_owner_binding *binding; } md_binding;
static void md_binding_emit(void *ctx, const qa_session_mod_emit *emit) {
  qa_app_sim_source_presentation_event source = qa_app_sim_presentation_event_source(&emit->event);
  qa_app_presentation_owner_binding_emit(((md_binding *)ctx)->binding, emit->event.content, &source, emit->has_time, emit->time,
      emit->event.has_recipient, emit->event.recipient);
}
static void md_binding_register_resource(void *ctx, const qa_session_mod_resource_registration *resource) {
  qa_app_presentation_owner_binding_register_resource(((md_binding *)ctx)->binding, resource->content, resource->path, &resource->resource);
}
static void md_binding_close(void *ctx) { qa_app_presentation_owner_binding_close(((md_binding *)ctx)->binding); }
static qa_session_mod_bound_events md_bind_owner(void *ctx, qa_provider_id provider, qa_content_id content, bool restoring) {
  md_host *host = ctx;
  qa_app_presentation_owner_binding *binding = qa_app_presentation_state_bind_owner(host->sim->presentation, provider, content, restoring);
  md_oom(binding);
  qa_app_presentation_owner_binding **grown = realloc(host->bindings, (host->binding_count + 1) * sizeof *grown);
  md_oom(grown);
  host->bindings = grown; host->bindings[host->binding_count++] = binding;
  md_binding *record = qa_app_shared_sim_keep(host->sim, sizeof *record);
  record->host = host; record->binding = binding;
  qa_session_mod_bound_events out;
  out.resource = (qa_session_resource){record, md_binding_close};
  out.owner = qa_app_presentation_owner_binding_owner(binding);
  out.events = (qa_session_mod_engine_events){record, md_binding_emit, md_binding_register_resource};
  return out;
}
static void md_finish_owner_restore(void *ctx) { qa_app_shared_sim_events_finish_owner_restore(((md_host *)ctx)->sim); }
static void md_refresh_owner(void *ctx, qa_provider_id provider) {
  qa_app_presentation_state_refresh_owner(((md_host *)ctx)->sim->presentation, provider);
}

static size_t md_world_leaf_count(const qa_decoded_world *world) {
  return world->kind == QA_DECODED_WORLD_Q1 ? world->as.q1->leaf_count
      : world->kind == QA_DECODED_WORLD_Q2 ? world->as.q2->leaf_count : world->as.q3->leaf_count;
}

void qa_app_shared_sim_open_mods(qa_app_shared_simulation *sim, const qa_mod_session_checkpoint *mod_state,
    qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx) {
  const qa_app_simulation_options *options = &sim->options;
  const qa_mod_travel_checkpoint *travel = options->mod_travel;
  md_host *host = calloc(1, sizeof *host);
  md_oom(host);
  host->sim = sim; host->restoring = options->restore != NULL; host->travelling = travel != NULL;
  qa_app_shared_sim_own(sim, md_dispose, host);
  /* enabled = options.enabledMods ?? (modState ?? options.modTravel)?.mods.map(mod => mod.identity.selection) ?? [] */
  if (options->enabled_mods != NULL) {
    const qa_app_sim_mod_selections *enabled = options->enabled_mods;
    host->enabled = calloc(enabled->count ? enabled->count : 1, sizeof *host->enabled);
    md_oom(host->enabled);
    if (enabled->count != 0) memcpy(host->enabled, enabled->items, enabled->count * sizeof *host->enabled);
    host->enabled_count = enabled->count;
  } else if (mod_state != NULL || travel != NULL) {
    size_t count = mod_state != NULL ? mod_state->mod_count : travel->mod_count;
    host->enabled = calloc(count ? count : 1, sizeof *host->enabled);
    md_oom(host->enabled);
    for (size_t i = 0; i < count; i++)
      host->enabled[i] = mod_state != NULL ? mod_state->mods[i].identity.selection : travel->mods[i].identity.selection;
    host->enabled_count = count;
  }
  host->prepared = calloc(options->prepared_mod_count ? options->prepared_mod_count : 1, sizeof *host->prepared);
  md_oom(host->prepared);
  for (size_t i = 0; i < options->prepared_mod_count; i++) host->prepared[i] = *(const qa_prepared_mod *)options->prepared_mods[i];

  host->weapons = (qa_source_weapon_services){host, md_weapon_bind, md_weapon_selected, md_weapon_presented, md_weapon_request};
  host->match = qa_mod_match_state_as_services(sim->source_match);
  host->scene = (qa_session_mod_engine_scene){qa_shared_as_scene_queries(sim->scene), sim->scene};
  host->physics = (qa_session_mod_engine_physics){host, md_bind_source, md_unbind_source, md_set_collision, md_step, md_touch_triggers,
      md_q1_pusher_services, md_read_q1_pusher, md_write_q1_pusher};
  host->camera = (qa_session_mod_engine_camera){host, md_camera};
  host->visibility = (qa_q1_client_visibility_scene){sim->scene, md_world_leaf_count(&sim->world), md_point_leaf, md_leaf_cluster, md_cluster_visible};
  qa_session_mod_engine_services *engine = &host->engine;
  engine->ctx = host;
  engine->print = md_print;
  engine->has_environment = true;
  engine->environment_skill = options->skill; engine->environment_mode = options->mode;
  engine->environment_max_clients = options->max_clients; engine->environment_gravity = qa_app_sim_shared_physics_gravity(sim->physics);
  engine->has_clients = true; engine->clients_maximum = options->max_clients;
  engine->clients_visibility = &host->visibility; engine->clients_at = md_clients_at;
  engine->classname = md_classname; engine->inline_model = md_inline_model; engine->area_entities = md_area_entities;
  engine->message = md_message;
  engine->presentation_map = sim->recipe.map.geometry.requested_path;
  engine->presentation_camera = &host->camera; engine->presentation_players = md_players;
  engine->events = (qa_session_mod_engine_events){host, md_events_emit, md_events_register_resource};
  engine->scene = &host->scene; engine->world = md_world; engine->physics = &host->physics;

  qa_mod_host_services *services = &host->services;
  services->match = &host->match; services->weapons = &host->weapons; services->clients = &sim->mod_clients;
  services->commands = (qa_mod_commands *)options->mod_commands;
  services->files = (qa_mod_user_files *)options->mod_files;
  services->native = qa_app_shared_sim_native_mod_context(sim, &host->native) ? &host->native : NULL;
  services->actors = sim->actors; services->bodies = qa_app_sim_shared_physics_bodies(sim->physics);
  services->combat = sim->combat; services->inventory = sim->inventory;
  services->seed = options->seed; services->callbacks = sim->callbacks;
  services->ctx = host; services->damage_context = md_damage_context; services->reference_saved = md_reference_saved;
  services->engine = engine; services->time = md_time;

  host->presentation = (qa_session_mods_presentation_option){host, md_bind_owner, md_finish_owner_restore, md_refresh_owner};
  qa_session_mods_options open; memset(&open, 0, sizeof open);
  open.presentation = &host->presentation; open.services = services;
  open.prepared = host->prepared; open.prepared_count = options->prepared_mod_count;
  open.enabled = host->enabled; open.enabled_count = host->enabled_count;
  open.operations = (qa_mod_operations){qa_actor_callback_table_think_operation(sim->callbacks), qa_actor_callback_table_touch_operation(sim->callbacks),
      qa_actor_callback_table_use_operation(sim->callbacks), qa_actor_callback_table_pain_operation(sim->callbacks),
      qa_actor_callback_table_die_operation(sim->callbacks), qa_gameplay_authority_damage_operation(sim->combat),
      qa_shared_inventory_table_give_operation(sim->inventory), qa_shared_inventory_table_consume_operation(sim->inventory),
      qa_shared_inventory_table_configure_operation(sim->inventory), qa_shared_inventory_table_adjust_operation(sim->inventory)};
  open.ctx = frame_ctx; open.next_frame = next_frame;
  sim->mod_owner = qa_session_mods_open(&open, mod_state, travel);
  md_oom(sim->mod_owner);
}
