/* Port of src/app/bootstrap/network/remote-q1.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/remote_q1.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q1_client.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/contracts/world.h"
#include "qa/core/commands/text.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/constants.h"
#include "qa/foundation/js.h"
#include "qa/text/localization_key.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
static char *copy_text(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = xmalloc(n);
  memcpy(out, s, n);
  return out;
}
typedef struct weapon_def { double bit; const char *name, *ammo; } weapon_def;
static const weapon_def weapons[] = {
  {4096, "axe", NULL}, {1, "shotgun", "shells"}, {2, "supershotgun", "shells"},
  {4, "nailgun", "nails"}, {8, "supernailgun", "nails"},
  {16, "grenadelauncher", "rockets"}, {32, "rocketlauncher", "rockets"},
  {64, "lightning", "cells"},
};
#define WEAPON_COUNT (sizeof weapons / sizeof weapons[0])
static const char *sound_channel(double value, const char **out_name) {
  static const char *channels[] = {"auto", "weapon", "voice", "item", "body", NULL, NULL, NULL};
  /* range-check the number before the cast: NaN or 1e300 must be rejected, not cast */
  int64_t index = value >= 0 && value <= 7 ? (int64_t)value : -1;
  if (index < 0 || index > 7 || value != (double)index)
    qa_raise("Error", 0, 0, "Invalid NetQuake sound channel");
  *out_name = channels[index];
  return *out_name;
}
typedef struct entity_entry { double number; qa_q1_extended_entity_state state; } entity_entry;
struct qa_app_q1_remote {
  qa_app_q1_remote_options options;
  qa_app_remote_world_content *world;
  double ordinal, sequence, frame_number, pending_impulse;
  double seconds, previous_seconds, received_at, fraction, max_clients, view_entity;
  qa_vec3 view_angles;
  bool has_demo_seconds; double demo_seconds;
  bool has_demo_angles; qa_vec3 demo_previous, demo_current;
  bool has_data; qa_q1_client_data data;
  double weapon_alpha;
  struct { double number; qa_actor_id actor; } *actors; size_t actor_count, actor_cap;
  entity_entry *current; size_t current_count, current_cap;
  entity_entry *previous; size_t previous_count, previous_cap;
  qa_q1_extended_entity_state *statics; size_t static_count, static_cap;
  char **models; size_t model_count;
  char **sounds; size_t sound_count;
  struct { char *key; qa_resolved_resource_reference reference; } *resources;
  size_t resource_count, resource_cap;
  struct { double index; char *value; } *styles; size_t style_count, style_cap;
  /* Admitted events are owned copies (qa_app_sim_presentation_event_copy). */
  qa_app_sim_presentation_event *events; size_t event_count, event_cap;
  qa_simulation_event *sounds_pending; size_t sound_head, sound_pending_count, sound_cap;
  qa_actor_id *sound_actors; size_t sound_actor_cap;
  qa_simulation_output published; bool has_published;
  qa_body_snapshot *published_bodies;
  qa_actor_observation *published_actors;
  qa_inventory_snapshot published_inventory[1];
  qa_actor_configuration published_configuration[1];
  qa_scene_light_style *published_styles; size_t published_style_count;
  qa_scene_snapshot_world published_world;
  qa_simulation_event *published_events; size_t published_event_count;
  qa_simulation_event *drained_sounds; /* drainPresentationEvents' taken sounds */
  qa_app_q1_client_table *scoreboard;
  qa_netquake_message *records; size_t record_count;
  qa_app_simulation_presentation *presentations; size_t presentation_cap;
  qa_app_sim_presentation_event *drained; size_t drained_count;
  qa_inventory_entry ui_inventory[4];
  qa_app_sim_player_ui_item ui_items[8];
  char *ui_item_ids[8];
  char *ui_ammo_id, *ui_weapon_id;
  qa_weapon_hud_status ui_status;
};
static const qa_executable_recipe *recipe_of(qa_app_q1_remote *remote) {
  return remote->options.content_recipe(remote->options.ctx,
      qa_app_remote_world_loaded(remote->world));
}
static qa_content_id source_content(qa_app_q1_remote *remote) {
  const qa_executable_recipe *recipe = recipe_of(remote);
  for (size_t i = 0; i < recipe->execution_count; i++) {
    const qa_resolved_execution_module *execution = &recipe->execution[i];
    if (execution->kind == QA_EXECUTION_MODULE_QUAKEC)
      return execution->owner.content;
  }
  return recipe->map.entities.content;
}
static qa_actor_id actor_of(qa_app_q1_remote *remote, double number) {
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].number == number) return remote->actors[i].actor;
  if (remote->ordinal >= 65536) qa_range_error("Remote actor registry is full");
  double slot = remote->ordinal++;
  qa_actor_id value = remote->options.identity_actor(remote->options.ctx, slot,
      remote->options.next_generation(remote->options.ctx, slot));
  if (remote->actor_count == remote->actor_cap) {
    remote->actor_cap = remote->actor_cap ? remote->actor_cap * 2 : 64;
    remote->actors = realloc(remote->actors, remote->actor_cap * sizeof *remote->actors);
    if (!remote->actors) qa_range_error("out of memory");
  }
  remote->actors[remote->actor_count].number = number;
  remote->actors[remote->actor_count].actor = value;
  remote->actor_count++;
  return value;
}
static bool player_of(qa_app_q1_remote *remote, qa_app_q1_player *out) {
  if (remote->view_entity == 0) return false;
  out->client = remote->options.client;
  out->actor = actor_of(remote, remote->view_entity);
  out->source_entity = remote->view_entity;
  return true;
}
static void events_release(qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) qa_app_sim_presentation_event_release(&events[i]);
}
/* TS events.admit(event): EventRing<SimulationPresentationEvent>(8192) throws when full; the ring
 * keeps an owned copy of the borrowed view. */
static void event_admit(qa_app_q1_remote *remote, const qa_app_sim_presentation_event *event) {
  if (remote->event_count >= 8192)
    qa_range_error("Event ring capacity exhausted (8192 undrained records); drain before admitting more");
  if (remote->event_count == remote->event_cap) {
    size_t new_cap = remote->event_cap ? remote->event_cap * 2 : 64;
    qa_app_sim_presentation_event *grown = realloc(remote->events, new_cap * sizeof *grown);
    if (!grown) qa_range_error("out of memory");
    remote->events = grown;
    remote->event_cap = new_cap;
  }
  if (!qa_app_sim_presentation_event_copy(&remote->events[remote->event_count], event))
    qa_range_error("out of memory");
  remote->event_count++;
}
/* TS emit(event, sourceEntity = null): { kind: 'q1', event, content: recipe.map.entities.content,
 * seconds, sequence: this.sequence++, sourceEntity }. */
static void emit(qa_app_q1_remote *remote, const qa_q1_event *payload, bool has_source_entity,
    double source_entity) {
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = "q1";
  event.q1 = payload;
  event.content = recipe_of(remote)->map.entities.content;
  event.seconds = remote->seconds;
  event.sequence = remote->sequence++;
  event.has_source_entity = has_source_entity;
  event.source_entity = source_entity;
  event_admit(remote, &event);
}
/* The stamp of the other kinds: { kind, ..., content, seconds, sequence: this.sequence++ }. */
static qa_app_sim_presentation_event event_stamp(qa_app_q1_remote *remote, const char *kind,
    qa_content_id content) {
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  event.content = content;
  event.seconds = remote->seconds;
  event.sequence = remote->sequence++;
  return event;
}
static qa_q1_event q1_event(const char *kind) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = kind;
  return event;
}
/* TS emit({ kind: 'message', player, text, center, args? }); localized NULL leaves args absent. */
static void emit_message(qa_app_q1_remote *remote, qa_actor_id player, const char *text, bool center,
    const qa_localization_key_text *localized) {
  qa_q1_event event = q1_event("message");
  event.has_player = true;
  event.player = player;
  event.text = text;
  event.center = center;
  qa_q1_msg_arg *args = NULL;
  if (localized) {
    args = malloc((localized->arg_count ? localized->arg_count : 1) * sizeof *args);
    if (!args) qa_range_error("out of memory");
    for (size_t i = 0; i < localized->arg_count; i++)
      args[i] = (qa_q1_msg_arg){false, 0, localized->args[i]};
    event.args = args;
    event.arg_count = localized->arg_count;
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, args);
  emit(remote, &event, false, 0);
  qa_cleanup_pop(&guard);
  free(args);
}
static const entity_entry *entity_find(const entity_entry *entries, size_t count, double number) {
  for (size_t i = 0; i < count; i++)
    if (entries[i].number == number) return &entries[i];
  return NULL;
}
static void entity_set(entity_entry **entries, size_t *count, size_t *cap,
    qa_q1_extended_entity_state state) {
  for (size_t i = 0; i < *count; i++)
    if ((*entries)[i].number == state.base.number) {
      (*entries)[i].state = state;
      return;
    }
  if (*count == *cap) {
    *cap = *cap ? *cap * 2 : 32;
    *entries = realloc(*entries, *cap * sizeof **entries);
    if (!*entries) qa_range_error("out of memory");
  }
  (*entries)[*count].number = state.base.number;
  (*entries)[*count].state = state;
  (*count)++;
}
static qa_q1_extended_entity_state sampled(qa_app_q1_remote *remote,
    qa_q1_extended_entity_state state) {
  const entity_entry *old = entity_find(remote->previous, remote->previous_count, state.base.number);
  if (state.step || !old) return state;
  double dx = fabs(state.base.origin.x - old->state.base.origin.x);
  double dy = fabs(state.base.origin.y - old->state.base.origin.y);
  double dz = fabs(state.base.origin.z - old->state.base.origin.z);
  double max_delta = dx > dy ? dx : dy;
  if (dz > max_delta) max_delta = dz;
  if (max_delta > 100) return state;
  state.base.origin = qa_lerp3_double(old->state.base.origin, state.base.origin, remote->fraction);
  state.base.angles = qa_lerp_angles_wrapped(old->state.base.angles, state.base.angles, remote->fraction);
  return state;
}
static void bonus_flash(qa_app_q1_remote *remote) {
  qa_actor_id actor = actor_of(remote, remote->view_entity);
  const entity_entry *entity = entity_find(remote->current, remote->current_count, remote->view_entity);
  qa_q1_event event = q1_event("effect");
  event.effect = "pickup";
  event.has_actor = true;
  event.actor = actor;
  event.has_origin = true;
  event.origin = entity ? entity->state.base.origin : (qa_vec3){0, 0, 0};
  event.amount = 1;
  emit(remote, &event, true, remote->view_entity);
}
static void publish_remote(qa_app_q1_remote *remote) {
  qa_app_q1_player player;
  if (!player_of(remote, &player) || !remote->has_data
      || !entity_find(remote->current, remote->current_count, remote->view_entity)) return;
  const qa_executable_recipe *recipe = recipe_of(remote);
  double time = remote->previous_seconds
      + (remote->seconds - remote->previous_seconds) * remote->fraction;
  free(remote->published_bodies);
  free(remote->published_actors);
  remote->published_bodies = xmalloc(remote->current_count * sizeof *remote->published_bodies);
  remote->published_actors = xmalloc(remote->current_count * sizeof *remote->published_actors);
  for (size_t i = 0; i < remote->current_count; i++) {
    qa_q1_extended_entity_state state = remote->current[i].state;
    qa_body_snapshot *body = &remote->published_bodies[i];
    body->actor = actor_of(remote, state.base.number);
    body->body.origin = sampled(remote, state).base.origin;
    body->body.angles = sampled(remote, state).base.angles;
    body->body.velocity = state.base.number == remote->view_entity
        ? remote->data.velocity : (qa_vec3){0, 0, 0};
    body->body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    body->body.ground = NULL;
    remote->published_actors[i].id = body->actor;
    remote->published_actors[i].owner = recipe->map.entities.provider;
    remote->published_actors[i].definition = "q1:remote-entity";
  }
  qa_app_sim_player_ui ui = qa_app_q1_remote_player_ui(remote, player.actor);
  remote->published_inventory[0].actor = player.actor;
  remote->published_inventory[0].entries = ui.inventory;
  remote->published_inventory[0].entry_count = ui.inventory_count;
  remote->published_configuration[0].actor = player.actor;
  remote->published_configuration[0].movement = recipe->movement;
  remote->published_configuration[0].character = recipe->character;
  remote->published_configuration[0].weapons = recipe->weapons;
  remote->published_configuration[0].inventory = recipe->inventory;
  free(remote->published_styles);
  remote->published_styles = xmalloc(remote->style_count * sizeof *remote->published_styles);
  remote->published_style_count = remote->style_count;
  for (size_t i = 0; i < remote->style_count; i++) {
    const char *pattern = remote->styles[i].value;
    size_t length = strlen(pattern);
    double value = length == 0 ? 256
        : ((double)(unsigned char)pattern[(size_t)floor(time * 10) % length] - 97) * 22;
    qa_scene_light_style *style = &remote->published_styles[i];
    style->is_q2 = false;
    style->style = remote->styles[i].index;
    style->value = value;
    style->rgb = (qa_vec3){0, 0, 0};
    style->white = 0;
  }
  memset(&remote->published, 0, sizeof remote->published);
  remote->published.snapshot.session = remote->options.session;
  remote->published.snapshot.frame.frame = remote->frame_number;
  remote->published.snapshot.frame.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, time};
  double elapsed = remote->seconds - remote->previous_seconds;
  remote->published.snapshot.frame.elapsed =
      (qa_source_time){QA_SOURCE_TIME_SECONDS, elapsed < 0 ? 0 : elapsed};
  remote->published.snapshot.frame.phase = QA_FRAME_PHASE_FRAME_EXIT;
  remote->published.snapshot.actors = remote->published_actors;
  remote->published.snapshot.actor_count = remote->current_count;
  remote->published.snapshot.bodies = remote->published_bodies;
  remote->published.snapshot.body_count = remote->current_count;
  remote->published.snapshot.inventories = remote->published_inventory;
  remote->published.snapshot.inventory_count = 1;
  remote->published.snapshot.configurations = remote->published_configuration;
  remote->published.snapshot.configuration_count = 1;
  remote->published.snapshot.scene.session = remote->options.session;
  remote->published.snapshot.scene.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, time};
  memset(&remote->published_world, 0, sizeof remote->published_world);
  remote->published_world.resource = recipe->map.geometry;
  if (remote->options.content_world) {
    const qa_scene_snapshot_world *world = remote->options.content_world(
        remote->options.ctx, qa_app_remote_world_loaded(remote->world));
    remote->published_world = *world;
    remote->published_world.resource = recipe->map.geometry;
  }
  remote->published.snapshot.scene.world = &remote->published_world;
  remote->published.snapshot.scene.light_styles = remote->published_styles;
  remote->published.snapshot.scene.light_style_count = remote->published_style_count;
  free(remote->published_events);
  remote->published_events = xmalloc(remote->sound_pending_count * sizeof *remote->published_events);
  remote->published_event_count = remote->sound_pending_count;
  for (size_t i = 0; i < remote->sound_pending_count; i++)
    remote->published_events[i] = remote->sounds_pending[
        (remote->sound_head + i) % (remote->sound_cap ? remote->sound_cap : 1)];
  remote->published.events = remote->published_events;
  remote->published.event_count = remote->published_event_count;
  remote->has_published = true;
  qa_simulation_output output = remote->published;
  output.events = NULL;
  output.event_count = 0;
  remote->options.publish(remote->options.ctx, &output);
}
qa_app_q1_remote *qa_app_q1_remote_create(qa_app_q1_remote_options options) {
  qa_app_q1_remote *remote = calloc(1, sizeof *remote);
  if (!remote) return NULL;
  remote->options = options;
  remote->fraction = 1;
  remote->world = qa_app_remote_world_content_create(options.world_host, options.content);
  if (!remote->world) { free(remote); return NULL; }
  remote->scoreboard = qa_app_q1_client_table_create();
  if (!remote->scoreboard) {
    qa_app_remote_world_content_destroy(remote->world);
    free(remote);
    return NULL;
  }
  return remote;
}
void qa_app_q1_remote_destroy(qa_app_q1_remote *remote) {
  if (!remote) return;
  qa_app_remote_world_content_destroy(remote->world);
  qa_app_q1_client_table_destroy(remote->scoreboard);
  free(remote->actors);
  free(remote->current);
  free(remote->previous);
  free(remote->statics);
  for (size_t i = 0; i < remote->model_count; i++) free(remote->models[i]);
  free(remote->models);
  for (size_t i = 0; i < remote->sound_count; i++) free(remote->sounds[i]);
  free(remote->sounds);
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].key);
  free(remote->resources);
  for (size_t i = 0; i < remote->style_count; i++) free(remote->styles[i].value);
  free(remote->styles);
  events_release(remote->events, remote->event_count);
  free(remote->events);
  free(remote->sounds_pending);
  free(remote->sound_actors);
  free(remote->published_bodies);
  free(remote->published_actors);
  free(remote->published_styles);
  free(remote->published_events);
  free(remote->drained_sounds);
  for (size_t i = 0; i < remote->record_count; i++) qa_netquake_message_destroy(&remote->records[i]);
  free(remote->records);
  free(remote->presentations);
  events_release(remote->drained, remote->drained_count);
  free(remote->drained);
  for (size_t i = 0; i < 8; i++) free(remote->ui_item_ids[i]);
  free(remote->ui_ammo_id);
  free(remote->ui_weapon_id);
  free(remote);
}
qa_client_id qa_app_q1_remote_client(const qa_app_q1_remote *remote) {
  return remote->options.client;
}
void *qa_app_q1_remote_scene(qa_app_q1_remote *remote) {
  return qa_app_remote_world_scene(remote->world);
}
const qa_simulation_output *qa_app_q1_remote_output(const qa_app_q1_remote *remote) {
  return remote->has_published ? &remote->published : NULL;
}
const qa_netquake_message *qa_app_q1_remote_source_records(const qa_app_q1_remote *remote,
    size_t *out_count) {
  *out_count = remote->record_count;
  return remote->records;
}
bool qa_app_q1_remote_player(const qa_app_q1_remote *remote, qa_app_q1_player *out) {
  return player_of((qa_app_q1_remote *)remote, out);
}
bool qa_app_q1_remote_player_slot(qa_app_q1_remote *remote, qa_actor_id actor, double *out) {
  for (double i = 1; i <= remote->max_clients; i++) {
    for (size_t j = 0; j < remote->actor_count; j++)
      if (remote->actors[j].number == i && qa_same_actor(remote->actors[j].actor, actor)) {
        *out = i - 1;
        return true;
      }
  }
  return false;
}
bool qa_app_q1_remote_is_player(qa_app_q1_remote *remote, qa_actor_id actor) {
  double slot = 0;
  return qa_app_q1_remote_player_slot(remote, actor, &slot);
}
size_t qa_app_q1_remote_scoreboard(const qa_app_q1_remote *remote,
    const qa_app_q1_client_row **out_rows) {
  return qa_app_q1_client_table_rows(remote->scoreboard, out_rows);
}
static void message_copy(const qa_netquake_message *in, qa_netquake_message *out) {
  *out = *in;
  out->text = in->text ? copy_text(in->text) : NULL;
  out->level = in->level ? copy_text(in->level) : NULL;
  out->str_value = in->str_value ? copy_text(in->str_value) : NULL;
  out->models = xmalloc(in->model_count * sizeof *out->models);
  for (size_t i = 0; i < in->model_count; i++) out->models[i] = copy_text(in->models[i]);
  out->sounds = xmalloc(in->sound_count * sizeof *out->sounds);
  for (size_t i = 0; i < in->sound_count; i++) out->sounds[i] = copy_text(in->sounds[i]);
}
void qa_app_q1_remote_receive(qa_app_q1_remote *remote,
    const qa_netquake_message *messages, size_t count, double now,
    qa_app_q1_remote_assert_fn assert_current, void *assert_ctx) {
  if (assert_current) assert_current(assert_ctx);
  for (size_t i = 0; i < remote->record_count; i++) qa_netquake_message_destroy(&remote->records[i]);
  free(remote->records);
  remote->records = xmalloc(count * sizeof *remote->records);
  remote->record_count = count;
  for (size_t i = 0; i < count; i++) message_copy(&messages[i], &remote->records[i]);
  for (size_t i = 0; i < count; i++) {
    const qa_netquake_message *message = &messages[i];
    const char *kind = message->kind ? message->kind : "";
    if (strcmp(kind, "server-info") == 0) {
      const char *map = message->model_count ? message->models[0] : NULL;
      size_t map_length = map ? strlen(map) : 0;
      /* startsWith('maps/') && endsWith('.bsp') */
      if (!map || strncmp(map, "maps/", 5) != 0 || map_length < 4 || strcmp(map + map_length - 4, ".bsp") != 0)
        qa_raise("Error", 0, 0, "NetQuake server has no world model");
      qa_app_q1_remote_world_info world = {map,
          (const char *const *)message->models, message->model_count,
          (const char *const *)message->sounds, message->sound_count};
      void *loaded = remote->options.load_content(remote->options.ctx, world,
          assert_current, assert_ctx);
      if (assert_current) assert_current(assert_ctx);
      qa_app_remote_world_set_loaded(remote->world, loaded);
      remote->actor_count = 0;
      remote->ordinal = 0;
      remote->current_count = 0;
      remote->previous_count = 0;
      remote->static_count = 0;
      for (size_t j = 0; j < remote->style_count; j++) free(remote->styles[j].value);
      remote->style_count = 0;
      qa_app_q1_client_table_destroy(remote->scoreboard);
      remote->scoreboard = qa_app_q1_client_table_create();
      if (!remote->scoreboard) qa_range_error("out of memory");
      events_release(remote->events, remote->event_count);
      remote->event_count = 0;
      remote->sound_head = 0;
      remote->sound_pending_count = 0;
      for (size_t j = 0; j < remote->model_count; j++) free(remote->models[j]);
      free(remote->models);
      remote->models = xmalloc(message->model_count * sizeof *remote->models);
      remote->model_count = message->model_count;
      for (size_t j = 0; j < message->model_count; j++) remote->models[j] = copy_text(message->models[j]);
      for (size_t j = 0; j < remote->sound_count; j++) free(remote->sounds[j]);
      free(remote->sounds);
      remote->sounds = xmalloc(message->sound_count * sizeof *remote->sounds);
      remote->sound_count = message->sound_count;
      for (size_t j = 0; j < message->sound_count; j++) remote->sounds[j] = copy_text(message->sounds[j]);
      remote->max_clients = message->max_clients;
      remote->view_entity = 0;
      remote->pending_impulse = 0;
      remote->view_angles = (qa_vec3){0, 0, 0};
      remote->has_demo_angles = false;
      remote->has_demo_seconds = false;
      remote->has_data = false;
      remote->weapon_alpha = 0;
      remote->has_published = false;
      remote->seconds = 0;
      remote->previous_seconds = 0;
    } else if (strcmp(kind, "time") == 0) {
      remote->previous_seconds = remote->seconds;
      remote->seconds = message->seconds;
      free(remote->previous);
      remote->previous = remote->current;
      remote->previous_count = remote->current_count;
      remote->previous_cap = remote->current_cap;
      remote->current = NULL;
      remote->current_count = 0;
      remote->current_cap = 0;
      remote->received_at = remote->options.presentation_time
          ? remote->options.presentation_time(remote->options.ctx) : now;
      remote->fraction = 1;
      remote->frame_number++;
    } else if (strcmp(kind, "entity") == 0) {
      entity_set(&remote->current, &remote->current_count, &remote->current_cap, message->state);
    } else if (strcmp(kind, "static") == 0) {
      /* Native Quake stops at 128 statics; extended protocols raise the figure, so this is a
       * generous ceiling rather than the native one. */
      if (remote->static_count >= 1024) qa_raise("Error", 0, 0, "Too many static entities");
      if (remote->static_count == remote->static_cap) {
        remote->static_cap = remote->static_cap ? remote->static_cap * 2 : 32;
        remote->statics = realloc(remote->statics, remote->static_cap * sizeof *remote->statics);
        if (!remote->statics) qa_range_error("out of memory");
      }
      remote->statics[remote->static_count++] = message->state;
    } else if (strcmp(kind, "set-view") == 0) {
      remote->view_entity = message->entity;
    } else if (strcmp(kind, "set-angle") == 0) {
      remote->view_angles = message->angles;
      qa_app_q1_player player;
      if (player_of(remote, &player)) {
        qa_app_sim_presentation_event event;
        memset(&event, 0, sizeof event);
        event.kind = "view-reset";
        event.view_reset.reason = "source";
        event.view_reset.actor = player.actor;
        event.view_reset.angles = message->angles;
        event.sequence = remote->sequence++;
        event.seconds = remote->seconds;
        event.content = recipe_of(remote)->map.entities.content;
        event_admit(remote, &event);
      }
    } else if (strcmp(kind, "client-data") == 0) {
      remote->data = message->data;
      remote->has_data = true;
      remote->weapon_alpha = message->weapon_alpha;
    } else if (strcmp(kind, "light-style") == 0) {
      bool found = false;
      for (size_t j = 0; j < remote->style_count; j++)
        if (remote->styles[j].index == message->index) {
          free(remote->styles[j].value);
          remote->styles[j].value = copy_text(message->text ? message->text : "");
          found = true;
        }
      if (!found) {
        if (remote->style_count == remote->style_cap) {
          remote->style_cap = remote->style_cap ? remote->style_cap * 2 : 32;
          remote->styles = realloc(remote->styles, remote->style_cap * sizeof *remote->styles);
          if (!remote->styles) qa_range_error("out of memory");
        }
        remote->styles[remote->style_count].index = message->index;
        remote->styles[remote->style_count].value = copy_text(message->text ? message->text : "");
        remote->style_count++;
      }
    } else if (strcmp(kind, "cd-track") == 0) {
      qa_app_sim_presentation_event event = event_stamp(remote, "music",
          recipe_of(remote)->map.entities.content);
      event.music.kind = "cd-track";
      event.music.track = message->track;
      event_admit(remote, &event);
    } else if (strcmp(kind, "name") == 0 || strcmp(kind, "colors") == 0
        || strcmp(kind, "frags") == 0 || strcmp(kind, "ping") == 0
        || strcmp(kind, "social") == 0 || strcmp(kind, "player-info") == 0) {
      /* TS updateQ1ClientMetadata(scoreboard, message), then { kind: 'q1-client', event: message, ... }.
       * NetQuake name/social/player-info carry their text in `value` (str_value). */
      bool text = strcmp(kind, "name") == 0 || strcmp(kind, "social") == 0
          || strcmp(kind, "player-info") == 0;
      qa_app_sim_q1_client_metadata_event metadata = {0};
      metadata.kind = kind;
      metadata.slot = message->slot;
      if (text) metadata.text = message->str_value ? message->str_value : "";
      else metadata.value = message->value;
      qa_app_update_q1_client_metadata(remote->scoreboard, &metadata);
      qa_app_sim_presentation_event event = event_stamp(remote, "q1-client", source_content(remote));
      event.q1_client.kind = kind;
      event.q1_client.slot = message->slot;
      if (text) event.q1_client.text = message->str_value ? message->str_value : "";
      else event.q1_client.value = message->value;
      event_admit(remote, &event);
    } else if (strcmp(kind, "skybox") == 0) {
      qa_app_sim_presentation_event event = event_stamp(remote, "q1-sky", source_content(remote));
      event.q1_sky.kind = "skybox";
      event.q1_sky.name = message->text ? message->text : "";
      event_admit(remote, &event);
    } else if (strcmp(kind, "print") == 0) {
      const char *text = message->text ? message->text : "";
      qa_localization_key_text localized;
      bool has_localized = qa_decode_localization_key(text, &localized);
      qa_app_q1_player player;
      if (has_localized && player_of(remote, &player)) {
        emit_message(remote, player.actor, localized.key, false, &localized);
        qa_localization_key_text_release(&localized);
      } else {
        if (has_localized) qa_localization_key_text_release(&localized);
        char *plain = qa_plain_localization_text(text);
        qa_cleanup guard;
        qa_cleanup_push(&guard, free, plain);
        remote->options.print(remote->options.ctx, plain);
        qa_cleanup_pop(&guard);
        free(plain);
      }
    } else if (strcmp(kind, "center-print") == 0) {
      qa_app_q1_player player;
      if (player_of(remote, &player)) {
        const char *text = message->text ? message->text : "";
        qa_localization_key_text localized;
        if (qa_decode_localization_key(text, &localized)) {
          emit_message(remote, player.actor, localized.key, true, &localized);
          qa_localization_key_text_release(&localized);
        } else {
          char *plain = qa_plain_localization_text(text);
          emit_message(remote, player.actor, plain, true, NULL);
          free(plain);
        }
      }
    } else if (strcmp(kind, "sound") == 0 || strcmp(kind, "static-sound") == 0) {
      bool is_static = strcmp(kind, "static-sound") == 0;
      int64_t index = (int64_t)message->index - 1;
      if (index < 0 || (size_t)index >= remote->sound_count)
        qa_raise("Error", 0, 0, "Unknown NetQuake sound index %s", QA_JS_NUMBER(message->index));
      const char *path = remote->sounds[index];
      if (is_static) {
        qa_q1_event event = q1_event("ambient");
        event.path = path;
        event.has_origin = true;
        event.origin = message->origin;
        event.volume = message->volume / 255;
        event.attenuation = message->attenuation;
        emit(remote, &event, false, 0);
      } else {
        qa_q1_event event = q1_event("sound");
        event.has_actor = true;
        event.actor = actor_of(remote, message->entity);
        event.path = path;
        sound_channel(message->channel, &event.sound_channel);
        event.has_origin = true;
        event.origin = message->origin;
        event.volume = message->volume / 255;
        event.attenuation = message->attenuation;
        emit(remote, &event, true, message->entity);
        const qa_resolved_resource_reference *resource = NULL;
        for (size_t j = 0; j < remote->resource_count; j++)
          if (strncmp(remote->resources[j].key, "sound/", 6) == 0 && strcmp(remote->resources[j].key + 6, path) == 0)
            resource = &remote->resources[j].reference;
        if (!resource) qa_raise("Error", 0, 0, "Unresolved NetQuake sound %s", path);
        if (remote->sound_pending_count >= 8192)
          qa_range_error("Event ring capacity exhausted (8192 undrained records); drain before admitting more");
        if (remote->sound_pending_count == remote->sound_cap) {
          size_t new_cap = remote->sound_cap ? remote->sound_cap * 2 : 64;
          qa_simulation_event *grown = xmalloc(new_cap * sizeof *grown);
          qa_actor_id *grown_actors = xmalloc(new_cap * sizeof *grown_actors);
          for (size_t j = 0; j < remote->sound_pending_count; j++) {
            grown[j] = remote->sounds_pending[(remote->sound_head + j) % remote->sound_cap];
            grown_actors[j] = remote->sound_actors[(remote->sound_head + j) % remote->sound_cap];
            if (grown[j].payload.actor) grown[j].payload.actor = &grown_actors[j];
          }
          free(remote->sounds_pending);
          free(remote->sound_actors);
          remote->sounds_pending = grown;
          remote->sound_actors = grown_actors;
          remote->sound_cap = new_cap;
          remote->sound_head = 0;
        }
        size_t slot_index = (remote->sound_head + remote->sound_pending_count) % remote->sound_cap;
        qa_simulation_event *sound = &remote->sounds_pending[slot_index];
        memset(sound, 0, sizeof *sound);
        sound->sequence = remote->sequence++;
        sound->time = (qa_source_time){QA_SOURCE_TIME_SECONDS, remote->seconds};
        sound->audience.kind = "client";
        sound->audience.client = remote->options.client;
        sound->payload.kind = QA_SIM_EVENT_SOUND;
        sound->payload.resource = resource->id;
        if (message->entity != 0) {
          remote->sound_actors[slot_index] = actor_of(remote, message->entity);
          sound->payload.actor = &remote->sound_actors[slot_index];
        }
        sound->payload.origin = message->origin;
        sound->payload.channel = message->channel;
        sound->payload.volume = message->volume / 255;
        sound->payload.attenuation = message->attenuation;
        remote->sound_pending_count++;
      }
    } else if (strcmp(kind, "stop-sound") == 0) {
      qa_q1_event event = q1_event("stop-sound");
      event.has_actor = true;
      event.actor = actor_of(remote, message->entity);
      event.channel = message->channel;
      emit(remote, &event, true, message->entity);
    } else if (strcmp(kind, "particle") == 0) {
      qa_q1_event event = q1_event("particles");
      event.has_origin = true;
      event.origin = message->origin;
      event.direction = message->direction;
      event.color = message->color_value;
      event.count = message->count;
      emit(remote, &event, false, 0);
    } else if (strcmp(kind, "temporary-entity") == 0) {
      const qa_temporary_entity *effect = &message->effect;
      if (effect->kind == QA_TE_EXPLOSION_COLORS) {
        qa_q1_event event = q1_event("colored-explosion");
        event.has_origin = true;
        event.origin = effect->origin;
        event.color_start = effect->color_start;
        event.color_length = effect->color_length;
        emit(remote, &event, false, 0);
      } else if (effect->kind == QA_TE_BEAM) {
        qa_q1_event event = q1_event("beam");
        event.beam_style = effect->type == 5 ? "lightning1" : effect->type == 6 ? "lightning2"
            : effect->type == 9 ? "lightning3" : "grapple";
        event.has_actor = true;
        event.actor = actor_of(remote, effect->entity);
        event.start = effect->start;
        event.end = effect->end;
        emit(remote, &event, false, 0);
      } else {
        const char *effect_kind = effect->type == 0 ? "spike" : effect->type == 1 ? "superspike"
            : effect->type == 2 ? "gunshot" : effect->type == 3 ? "explosion"
            : effect->type == 4 ? "tar-explosion" : effect->type == 7 ? "wizard-spike"
            : effect->type == 8 ? "knight-spike" : effect->type == 10 ? "lava-splash"
            : effect->type == 11 ? "teleport" : NULL;
        if (!effect_kind) qa_raise("Error", 0, 0, "Unsupported NetQuake temporary entity %s", QA_JS_NUMBER(effect->type));
        qa_q1_event event = q1_event("effect");
        event.effect = effect_kind;
        event.has_actor = false; /* TS actor: null */
        event.has_origin = true;
        event.origin = effect->origin;
        event.amount = effect->count;
        emit(remote, &event, false, 0);
      }
    } else if (strcmp(kind, "bonus-flash") == 0) {
      bonus_flash(remote);
    } else if (strcmp(kind, "stufftext") == 0) {
      char *pending = copy_text(message->text ? message->text : "");
      char *cursor = pending;
      while (cursor && *cursor) {
        size_t offset = qa_command_separator_offset(cursor, QA_COMMAND_DIALECT_Q1_NETQUAKE);
        char saved = cursor[offset];
        cursor[offset] = '\0';
        qa_command_tokens tokens;
        if (!qa_tokenize_command(cursor, QA_COMMAND_DIALECT_Q1_NETQUAKE, QA_COMMAND_TEXT_SOURCE, &tokens))
          qa_range_error("out of memory");
        char *name = qa_ascii_fold(tokens.argc ? tokens.argv[0] : "");
        if (!name) qa_range_error("out of memory");
        /* line.trim() === 'reconnect' (JS whitespace), without altering the printed line. */
        const char *first = cursor;
        while (*first == ' ' || (*first >= '\t' && *first <= '\r') || (unsigned char)*first == 0xa0) first++;
        size_t trimmed_length = strlen(first);
        while (trimmed_length && (first[trimmed_length - 1] == ' '
            || (first[trimmed_length - 1] >= '\t' && first[trimmed_length - 1] <= '\r')
            || (unsigned char)first[trimmed_length - 1] == 0xa0))
          trimmed_length--;
        bool reconnect = trimmed_length == 9 && strncmp(first, "reconnect", 9) == 0;
        if (strcmp(name, "bf") == 0) bonus_flash(remote);
        else if (reconnect) remote->has_published = false;
        else if (name[0] != '\0') {
          char *line = xmalloc(strlen(cursor) + 32);
          snprintf(line, strlen(cursor) + 32, "Unhandled server command: %s\n", cursor);
          remote->options.print(remote->options.ctx, line);
          free(line);
        }
        free(name);
        qa_command_tokens_destroy(&tokens);
        cursor[offset] = saved;
        cursor = saved ? cursor + offset + 1 : NULL;
      }
      free(pending);
    }
  }
  publish_remote(remote);
}
const qa_world_text *qa_app_q1_remote_world_text(qa_app_q1_remote *remote, size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
qa_app_sim_player_view qa_app_q1_remote_player_view(qa_app_q1_remote *remote, qa_actor_id actor) {
  qa_app_q1_player player;
  if (!player_of(remote, &player) || !qa_same_actor(player.actor, actor) || !remote->has_data)
    qa_raise("Error", 0, 0, "Remote Q1 player has no clientdata");
  const entity_entry *entity = entity_find(remote->current, remote->current_count, remote->view_entity);
  qa_app_sim_player_view view = {0};
  view.origin = entity ? sampled(remote, entity->state).base.origin : (qa_vec3){0, 0, 0};
  view.angles = remote->view_angles;
  view.view_height = remote->data.view_height;
  view.has_kick_angles = true;
  view.kick_angles = remote->data.punch_angles;
  view.has_pitch_drift = true;
  view.pitch_drift_grounded = remote->data.on_ground;
  view.pitch_drift_ideal_pitch = remote->data.ideal_pitch;
  view.pitch_drift_disabled = remote->has_demo_seconds;
  return view;
}
qa_app_sim_player_ui qa_app_q1_remote_player_ui(qa_app_q1_remote *remote, qa_actor_id actor) {
  qa_app_q1_remote_player_view(remote, actor);
  if (!remote->has_data) qa_raise("Error", 0, 0, "No Q1 clientdata");
  const qa_q1_client_data *data = &remote->data;
  const weapon_def *weapon = NULL;
  for (size_t i = 0; i < WEAPON_COUNT && weapon == NULL; i++) /* Array.find: first match */
    if (weapons[i].bit == data->active_weapon
        || (strcmp(weapons[i].name, "axe") == 0 && data->active_weapon == 0
            && data->weapon_model >= 1 && (size_t)(data->weapon_model - 1) < remote->model_count
            && strcmp(remote->models[(size_t)data->weapon_model - 1], "progs/v_axe.mdl") == 0))
      weapon = &weapons[i];
  const qa_executable_recipe *recipe = recipe_of(remote);
  qa_app_sim_player_ui ui = {0};
  ui.health = data->health;
  ui.armor.powered.kind = "none";
  if (data->armor == 0) ui.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  else {
    ui.armor.regular.kind = QA_REGULAR_ARMOR_Q1;
    ui.armor.regular.points = data->armor;
    ui.armor.regular.absorption = (qa_js_to_int32(data->items) & 32768) != 0 ? 0.8
        : (qa_js_to_int32(data->items) & 16384) != 0 ? 0.6 : 0.3;
    ui.armor.regular.item = "q1:armor";
  }
  free(remote->ui_weapon_id);
  remote->ui_weapon_id = NULL;
  free(remote->ui_ammo_id);
  remote->ui_ammo_id = NULL;
  if (weapon) {
    remote->ui_weapon_id = xmalloc(strlen(weapon->name) + 12);
    snprintf(remote->ui_weapon_id, strlen(weapon->name) + 12, "q1:weapon/%s", weapon->name);
    ui.active_weapon = remote->ui_weapon_id;
  }
  if (weapon && weapon->ammo) {
    remote->ui_ammo_id = xmalloc(strlen(weapon->ammo) + 10);
    snprintf(remote->ui_ammo_id, strlen(weapon->ammo) + 10, "q1:ammo/%s", weapon->ammo);
    ui.has_ammo = true;
    ui.ammo_item = remote->ui_ammo_id;
    ui.ammo_count = data->ammo;
  }
  ui.arsenal_warning = "none";
  if (weapon && recipe->weapons.count > 0 && ui.active_weapon) {
    qa_weapon_hud_status *status = &remote->ui_status;
    memset(status, 0, sizeof *status);
    status->source = recipe->weapons.values[0];
    status->item = ui.active_weapon;
    status->label = weapon->name;
    if (!weapon->ammo) status->ammo_is_finite = false;
    else {
      status->ammo_is_finite = true;
      status->ammo_item = remote->ui_ammo_id;
      status->ammo_count = data->ammo;
      status->has_ammo_to_start = data->ammo > 0;
    }
    ui.weapon_status = status;
  }
  static const char *ammo_names[4] = {"shells", "nails", "rockets", "cells"};
  double counts[4] = {data->shells, data->nails, data->rockets, data->cells};
  static char ammo_ids[4][24];
  for (size_t i = 0; i < 4; i++) {
    snprintf(ammo_ids[i], sizeof ammo_ids[i], "q1:ammo/%s", ammo_names[i]);
    remote->ui_inventory[i].item = ammo_ids[i];
    remote->ui_inventory[i].count = counts[i];
    remote->ui_inventory[i].capacity = counts[i];
  }
  ui.inventory = remote->ui_inventory;
  ui.inventory_count = 4;
  for (size_t i = 0; i < WEAPON_COUNT; i++) {
    free(remote->ui_item_ids[i]);
    remote->ui_item_ids[i] = xmalloc(strlen(weapons[i].name) + 12);
    snprintf(remote->ui_item_ids[i], strlen(weapons[i].name) + 12, "q1:weapon/%s", weapons[i].name);
    qa_app_sim_player_ui_item *item = &remote->ui_items[i];
    memset(item, 0, sizeof *item);
    item->id = remote->ui_item_ids[i];
    item->label = weapons[i].name;
    item->kind = "weapon";
    item->source_ordinal = (double)i + 1;
    item->owned = (qa_js_to_int32(data->items) & (int64_t)weapons[i].bit) != 0;
    double ammo = 0;
    if (weapons[i].ammo)
      for (size_t j = 0; j < 4; j++)
        if (strcmp(ammo_names[j], weapons[i].ammo) == 0) ammo = counts[j];
    item->has_ammo = !weapons[i].ammo || ammo > 0;
    item->has_count = weapons[i].ammo != NULL;
    item->count = ammo;
    item->warning_count = 0;
  }
  ui.items = remote->ui_items;
  ui.item_count = WEAPON_COUNT;
  return ui;
}
const void *qa_app_q1_remote_character_views(qa_app_q1_remote *remote, size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
const qa_app_simulation_presentation *qa_app_q1_remote_presentations(qa_app_q1_remote *remote,
    size_t *out_count) {
  size_t needed = remote->current_count + remote->static_count + 1;
  if (remote->presentation_cap < needed) {
    remote->presentations = realloc(remote->presentations, needed * sizeof *remote->presentations);
    if (!remote->presentations) qa_range_error("out of memory");
    remote->presentation_cap = needed;
  }
  const qa_executable_recipe *recipe = recipe_of(remote);
  const qa_app_q1_client_row *rows = NULL;
  size_t row_count = qa_app_q1_client_table_rows(remote->scoreboard, &rows);
  size_t count = 0;
  for (size_t pass = 0; pass < 2; pass++) {
    size_t total = pass == 0 ? remote->current_count : remote->static_count;
    for (size_t i = 0; i < total; i++) {
      qa_q1_extended_entity_state state = pass == 0
          ? sampled(remote, remote->current[i].state) : remote->statics[i];
      double number = pass == 0 ? state.base.number : 65536 + (double)i;
      if (state.base.model_index == 0) continue;
      int64_t model_index = (int64_t)state.base.model_index - 1;
      const char *path = model_index >= 0 && (size_t)model_index < remote->model_count
          ? remote->models[model_index] : NULL;
      if (!path) qa_raise("Error", 0, 0, "Unknown NetQuake model %s", QA_JS_NUMBER(state.base.model_index));
      qa_app_simulation_presentation *item = &remote->presentations[count++];
      memset(item, 0, sizeof *item);
      item->actor = actor_of(remote, number);
      item->content = recipe->map.entities.content;
      item->family = QA_GAME_FAMILY_Q1;
      item->path = path;
      item->frame = state.base.frame;
      item->old_frame = state.base.frame;
      item->skin = state.base.skin;
      item->effects = state.base.effects;
      item->origin = state.base.origin;
      item->angles = state.base.angles;
      item->has_alpha = true;
      item->alpha = qa_q1_entalpha_decode(state.alpha);
      item->scale = qa_q1_entscale_decode(state.scale);
      item->visible = number != remote->view_entity;
      if (state.base.color_map > 0 && state.base.color_map <= remote->max_clients) {
        for (size_t j = 0; j < row_count; j++)
          if (rows[j].slot == state.base.color_map - 1) {
            item->has_player_colors = true;
            double top = (double)((qa_js_to_int32(rows[j].colors) >> 4) & 15);
            double bottom = (double)(qa_js_to_int32(rows[j].colors) & 15);
            item->player_color_top = top > 13 ? 13 : top;
            item->player_color_bottom = bottom > 13 ? 13 : bottom;
          }
      }
    }
  }
  qa_app_q1_player player;
  if (player_of(remote, &player) && remote->has_data && remote->data.weapon_model != 0) {
    int64_t model_index = (int64_t)remote->data.weapon_model - 1;
    const char *path = model_index >= 0 && (size_t)model_index < remote->model_count
        ? remote->models[model_index] : NULL;
    if (!path) qa_raise("Error", 0, 0, "Unknown Q1 weapon model");
    qa_app_sim_player_view view = qa_app_q1_remote_player_view(remote, player.actor);
    qa_app_simulation_presentation *item = &remote->presentations[count++];
    memset(item, 0, sizeof *item);
    item->actor = player.actor;
    item->content = recipe->map.entities.content;
    item->family = QA_GAME_FAMILY_Q1;
    item->path = path;
    item->frame = remote->data.weapon_frame;
    item->old_frame = remote->data.weapon_frame;
    item->origin = (qa_vec3){view.origin.x, view.origin.y, view.origin.z + view.view_height};
    item->has_view_origin = true;
    item->view_origin = item->origin;
    item->has_view_angles = true;
    item->view_angles = view.angles;
    item->angles = view.angles;
    item->has_alpha = true;
    item->alpha = qa_q1_entalpha_decode(remote->weapon_alpha);
    item->scale = 1;
    item->visible = remote->data.health > 0 && (qa_js_to_int32(remote->data.items) & 524288) == 0;
    item->view_weapon = true;
  }
  *out_count = count;
  return remote->presentations;
}
void qa_app_q1_remote_register_resource(qa_app_q1_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource) {
  (void)content;
  for (size_t i = 0; i < remote->resource_count; i++)
    if (strcmp(remote->resources[i].key, path) == 0) {
      remote->resources[i].reference = resource;
      return;
    }
  if (remote->resource_count == remote->resource_cap) {
    remote->resource_cap = remote->resource_cap ? remote->resource_cap * 2 : 16;
    remote->resources = realloc(remote->resources, remote->resource_cap * sizeof *remote->resources);
    if (!remote->resources) qa_range_error("out of memory");
  }
  remote->resources[remote->resource_count].key = copy_text(path);
  remote->resources[remote->resource_count].reference = resource;
  remote->resource_count++;
}
static void select_weapon(qa_app_q1_remote *remote, qa_actor_id actor, const char *requested) {
  qa_app_sim_player_ui ui = qa_app_q1_remote_player_ui(remote, actor);
  for (size_t i = 0; i < ui.item_count; i++)
    if (ui.items[i].owned && (strcmp(ui.items[i].id, requested) == 0
        || strcmp(ui.items[i].label, requested) == 0)) {
      remote->pending_impulse = ui.items[i].source_ordinal;
      return;
    }
  qa_raise("Error", 0, 0, "Native Q1 weapon is not owned: %s", requested);
}
qa_q1_user_command qa_app_q1_remote_command(qa_app_q1_remote *remote,
    const qa_actor_command *command) {
  if (command->command.kind != QA_USER_COMMAND_Q1)
    qa_raise("Error", 0, 0, "Native Q1 requires NetQuake input");
  qa_app_q1_remote_player_view(remote, command->actor);
  remote->view_angles = command->command.as.q1.view_angles;
  if (command->arsenal && command->arsenal->weapon) {
    qa_app_sim_player_ui ui = qa_app_q1_remote_player_ui(remote, command->actor);
    if (!ui.active_weapon || strcmp(command->arsenal->weapon, ui.active_weapon) != 0)
      select_weapon(remote, command->actor, command->arsenal->weapon);
  }
  qa_q1_user_command result = command->command.as.q1;
  /* command.impulse || pendingImpulse: 0 and NaN are falsy. */
  result.impulse = result.impulse != 0 && !isnan(result.impulse) ? result.impulse : remote->pending_impulse;
  remote->pending_impulse = 0;
  return result;
}
void qa_app_q1_remote_player_command(qa_app_q1_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count) {
  qa_app_q1_remote_player_view(remote, actor);
  /* [name, ...args].some(/["\n\r;]/): every argument, however many. */
  for (size_t i = 0; i <= count; i++)
    for (const char *p = i == 0 ? name : args[i - 1]; *p; p++)
      if (*p == '"' || *p == '\n' || *p == '\r' || *p == ';')
        qa_raise("Error", 0, 0, "Invalid native command delimiter");
  if (strcmp(name, "use") == 0) {
    size_t total = 1;
    for (size_t i = 0; i < count; i++) total += strlen(args[i]);
    char *joined = xmalloc(total);
    size_t used = 0;
    for (size_t i = 0; i < count; i++)
      for (const char *p = args[i]; *p; p++) {
        if (*p == ' ') continue;
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        joined[used++] = c;
      }
    joined[used] = '\0';
    select_weapon(remote, actor, joined);
    free(joined);
    return;
  }
  if (strcmp(name, "weapnext") == 0 || strcmp(name, "weapprev") == 0) {
    qa_app_sim_player_ui ui = qa_app_q1_remote_player_ui(remote, actor);
    const qa_app_sim_player_ui_item *owned[8];
    size_t owned_count = 0;
    int64_t current = -1;
    for (size_t i = 0; i < ui.item_count; i++)
      if (ui.items[i].owned && ui.items[i].has_ammo) {
        if (ui.active_weapon && strcmp(ui.items[i].id, ui.active_weapon) == 0)
          current = (int64_t)owned_count;
        owned[owned_count++] = &ui.items[i];
      }
    if (owned_count > 0) {
      int64_t offset = strcmp(name, "weapnext") == 0 ? 1 : (int64_t)owned_count - 1;
      const qa_app_sim_player_ui_item *next = owned[(current + offset) % (int64_t)owned_count];
      remote->pending_impulse = next->source_ordinal;
    }
    return;
  }
  size_t total = strlen(name) + 1;
  for (size_t i = 0; i < count; i++) total += strlen(args[i]) + 3;
  char *line = xmalloc(total);
  size_t used = (size_t)snprintf(line, total, "%s", name);
  for (size_t i = 0; i < count; i++)
    used += (size_t)snprintf(line + used, total - used, " \"%s\"", args[i]);
  remote->options.send_command(remote->options.ctx, line);
  free(line);
}
const qa_simulation_output *qa_app_q1_remote_sample_presentation(qa_app_q1_remote *remote,
    double now) {
  if (remote->has_demo_seconds) return qa_app_q1_remote_sample_demo(remote, remote->demo_seconds);
  if (remote->options.presentation_time)
    now = remote->options.presentation_time(remote->options.ctx);
  if (remote->seconds - remote->previous_seconds > 0.1)
    remote->previous_seconds = remote->seconds - 0.1;
  double duration = remote->seconds - remote->previous_seconds;
  if (duration < 0) duration = 0;
  remote->fraction = duration == 0 ? 1
      : qa_js_max(0, qa_js_min(1, (now - remote->received_at) / (duration * 1000)));
  publish_remote(remote);
  return remote->has_published ? &remote->published : NULL;
}
double qa_app_q1_remote_recorded_seconds(const qa_app_q1_remote *remote) {
  return remote->seconds;
}
void qa_app_q1_remote_set_demo_view_angles(qa_app_q1_remote *remote,
    qa_vec3 current, bool interpolate) {
  remote->demo_previous = interpolate && remote->has_demo_angles ? remote->demo_current : current;
  remote->demo_current = current;
  remote->has_demo_angles = true;
}
const qa_simulation_output *qa_app_q1_remote_sample_demo(qa_app_q1_remote *remote, double seconds) {
  remote->demo_seconds = seconds;
  remote->has_demo_seconds = true;
  if (remote->seconds - remote->previous_seconds > 0.1)
    remote->previous_seconds = remote->seconds - 0.1;
  double duration = remote->seconds - remote->previous_seconds;
  remote->fraction = duration <= 0 ? 1
      : qa_js_max(0, qa_js_min(1, (seconds - remote->previous_seconds) / duration));
  if (remote->has_demo_angles)
    remote->view_angles = qa_lerp_angles_wrapped(remote->demo_previous, remote->demo_current,
        remote->fraction);
  publish_remote(remote);
  return remote->has_published ? &remote->published : NULL;
}
const qa_app_sim_presentation_event *qa_app_q1_remote_drain_events(qa_app_q1_remote *remote,
    size_t *out_count) {
  *out_count = 0;
  if (!remote->has_published) return NULL;
  /* soundsPending.take(cursor): every sound admitted since the last drain, read now. */
  free(remote->drained_sounds);
  remote->drained_sounds = xmalloc((remote->sound_pending_count ? remote->sound_pending_count : 1) * sizeof *remote->drained_sounds);
  for (size_t i = 0; i < remote->sound_pending_count; i++)
    remote->drained_sounds[i] = remote->sounds_pending[(remote->sound_head + i) % remote->sound_cap];
  qa_simulation_output output = remote->published;
  output.events = remote->drained_sounds;
  output.event_count = remote->sound_pending_count;
  remote->options.publish(remote->options.ctx, &output);
  remote->sound_head = (remote->sound_head + remote->sound_pending_count)
      % (remote->sound_cap ? remote->sound_cap : 1);
  remote->sound_pending_count = 0;
  /* events.take(cursor) then retire: the taken events move to the remote's drained set, which
   * it owns until the next drain or destroy. */
  events_release(remote->drained, remote->drained_count);
  free(remote->drained);
  remote->drained = remote->events;
  remote->drained_count = remote->event_count;
  remote->events = NULL;
  remote->event_count = 0;
  remote->event_cap = 0;
  *out_count = remote->drained_count;
  return remote->drained;
}
void qa_app_q1_remote_disconnected(qa_app_q1_remote *remote, const char *reason) {
  remote->options.disconnected(remote->options.ctx, reason);
}
static void host_receive(void *ctx, qa_netquake_message *messages, size_t count, double now) {
  qa_app_q1_remote_receive(ctx, messages, count, now, NULL, NULL);
}
static qa_q1_user_command host_command(void *ctx, const void *actor_command) {
  return qa_app_q1_remote_command(ctx, actor_command);
}
static void host_disconnected(void *ctx, const char *reason) {
  qa_app_q1_remote_disconnected(ctx, reason);
}
qa_app_q1_client_host qa_app_q1_remote_as_client_host(qa_app_q1_remote *remote) {
  qa_app_q1_client_host host = {0};
  host.ctx = remote;
  host.receive = host_receive;
  host.command = host_command;
  host.disconnected = host_disconnected;
  return host;
}
