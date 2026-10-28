/* Port of src/app/bootstrap/network/remote.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/remote.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_effects.h"
#include "qa/app/bootstrap/network/q2_remote_view.h"
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/app/bootstrap/q2_damage_blend.h"
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/prediction/runtime.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/contracts/world.h"
#include "qa/core/math.h"
#include "qa/core/md4.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/adapters.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/solid.h"
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
qa_bounds qa_app_q2_remote_entity_bounds(double solid, bool long_solid) {
  return qa_q2_unpack_solid(solid, long_solid ? QA_Q2_SOLID_R1Q2 : QA_Q2_SOLID_SHORT);
}
static qa_vec3 vec_of(const qa_q2_wire_vec3 value) {
  qa_vec3 out = {value[0], value[1], value[2]};
  return out;
}
static qa_vec3 interpolate(qa_vec3 from, qa_vec3 to, double fraction) {
  qa_vec3 out = {from.x + (to.x - from.x) * fraction,
      from.y + (to.y - from.y) * fraction, from.z + (to.z - from.z) * fraction};
  return out;
}
static double interp_angle(double a, double b, double fraction) {
  double delta = fmod(b - a + 540, 360);
  if (delta < 0) delta += 360;
  return a + (delta - 180) * fraction;
}
static qa_vec3 interpolate_angles(qa_vec3 from, qa_vec3 to, double fraction) {
  qa_vec3 out = {interp_angle(from.x, to.x, fraction),
      interp_angle(from.y, to.y, fraction), interp_angle(from.z, to.z, fraction)};
  return out;
}

/* Retained-record copy: deep-copies the owned fields remote
 * records consume (raw span, text, inventory counts, download
 * bytes); other event payloads are plain values in the struct. */
static void record_copy_local(const qa_q2_server_record *in, qa_q2_server_record *out) {
  memset(out, 0, sizeof *out);
  out->seat = in->seat;
  out->opcode = in->opcode;
  if (in->raw) {
    out->raw = xmalloc(in->raw_length);
    memcpy(out->raw, in->raw, in->raw_length);
    out->raw_length = in->raw_length;
  }
  out->event = in->event;
  out->event.text = in->event.text ? copy_text(in->event.text) : NULL;
  if (in->event.counts) {
    out->event.counts = xmalloc(in->event.count_count * sizeof *out->event.counts);
    memcpy(out->event.counts, in->event.counts,
        in->event.count_count * sizeof *out->event.counts);
  }
  if (in->event.bytes) {
    out->event.bytes = xmalloc(in->event.bytes_length);
    memcpy(out->event.bytes, in->event.bytes, in->event.bytes_length);
  }
  out->event.indicators = NULL;
  out->event.indicator_count = 0;
  out->event.private_name = in->event.private_name ? copy_text(in->event.private_name) : NULL;
  memset(&out->event.frame, 0, sizeof out->event.frame);
}
static void records_free_local(qa_q2_server_record *records, size_t count) {
  if (!records) return;
  for (size_t i = 0; i < count; i++) {
    free(records[i].raw);
    free(records[i].event.text);
    free(records[i].event.counts);
    free(records[i].event.bytes);
    free(records[i].event.private_name);
  }
  free(records);
}
typedef struct config_entry { double index; char *value; } config_entry;
typedef struct resource_entry { char *key; qa_resolved_resource_reference reference; } resource_entry;
typedef struct event_entry {
  qa_app_sim_presentation_event event;
  qa_app_q2_remote_event payload;
  qa_app_q2_effect_event effect;
  qa_app_q2_beam_event beam;
  char *text, *path;
} event_entry;
struct qa_app_q2_remote {
  qa_app_q2_remote_options options;
  qa_app_q2_download_receiver *downloads;
  const qa_app_q2_remote_mounts *download_content;
  qa_q2_protocol_identity selected_protocol;
  bool strafejump_hack, extended_game;
  qa_app_q2_layout layout;
  struct { double number; qa_actor_id actor; } *actors; size_t actor_count, actor_cap;
  config_entry *configs; size_t config_count, config_cap;
  resource_entry *resources; size_t resource_count, resource_cap;
  event_entry *events; size_t event_head, event_count, event_cap;
  bool has_current, has_previous;
  qa_q2_wire_frame current, previous_frame;
  double stats_current[64], stats_previous[64];
  double received_at, fraction, view_time;
  qa_app_q2_rerelease_view_height view_height;
  bool has_player; qa_app_q2_player player;
  qa_simulation_output published; bool has_published;
  qa_body_snapshot *published_bodies;
  qa_actor_observation *published_actors;
  qa_inventory_snapshot published_inventory[1];
  qa_actor_configuration published_configuration[1];
  qa_scene_light_style *published_styles;
  qa_scene_snapshot_world published_world;
  qa_byte_view published_area_bits;
  double event_sequence, frame_milliseconds;
  double *inventory; size_t inventory_count;
  qa_q2_server_record *last_records; size_t last_record_count;
  char *layout_text;
  qa_app_remote_world_content *world;
  qa_app_selected_movement_prediction *prediction_owner;
  bool has_predicted; qa_app_movement_prediction_result predicted;
  double packet_acknowledged;
  qa_app_q2_mvd_visibility_state *visibility;
  qa_app_simulation_presentation *presentations; size_t presentation_cap;
  char **presentation_paths; size_t presentation_path_cap;
  qa_app_sim_presentation_event *drained; size_t drained_cap;
  qa_weapon_hud_status weapon_status;
  qa_inventory_entry *ui_inventory; size_t ui_inventory_cap;
  qa_app_sim_player_ui_item *ui_items;
  qa_body_snapshot *sample_bodies;
  struct { qa_app_q2_remote *remote; double servercount; bool completed; double revision; } cinematic_state;
  qa_q2_server_message_options message_options;
};
static const qa_executable_recipe *recipe_of(qa_app_q2_remote *remote) {
  void *loaded = qa_app_remote_world_loaded(remote->world);
  return remote->options.content_recipe(remote->options.ctx, loaded);
}
static qa_actor_id actor_of(qa_app_q2_remote *remote, double number) {
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].number == number) return remote->actors[i].actor;
  double generation = remote->options.next_generation(remote->options.ctx, number);
  qa_actor_id actor = remote->options.identity_actor(remote->options.ctx, number, generation);
  if (remote->actor_count == remote->actor_cap) {
    remote->actor_cap = remote->actor_cap ? remote->actor_cap * 2 : 32;
    remote->actors = realloc(remote->actors, remote->actor_cap * sizeof *remote->actors);
    if (!remote->actors) qa_range_error("out of memory");
  }
  remote->actors[remote->actor_count].number = number;
  remote->actors[remote->actor_count].actor = actor;
  remote->actor_count++;
  return actor;
}
static const char *config_get(qa_app_q2_remote *remote, double index) {
  for (size_t i = 0; i < remote->config_count; i++)
    if (remote->configs[i].index == index) return remote->configs[i].value;
  return NULL;
}
static void config_set(qa_app_q2_remote *remote, double index, const char *value) {
  for (size_t i = 0; i < remote->config_count; i++)
    if (remote->configs[i].index == index) {
      free(remote->configs[i].value);
      remote->configs[i].value = copy_text(value ? value : "");
      return;
    }
  if (remote->config_count == remote->config_cap) {
    remote->config_cap = remote->config_cap ? remote->config_cap * 2 : 64;
    remote->configs = realloc(remote->configs, remote->config_cap * sizeof *remote->configs);
    if (!remote->configs) qa_range_error("out of memory");
  }
  remote->configs[remote->config_count].index = index;
  remote->configs[remote->config_count].value = copy_text(value ? value : "");
  remote->config_count++;
}
static event_entry *event_push(qa_app_q2_remote *remote) {
  if (remote->event_count == remote->event_cap) {
    size_t new_cap = remote->event_cap ? remote->event_cap * 2 : 64;
    event_entry *grown = xmalloc(new_cap * sizeof *grown);
    for (size_t i = 0; i < remote->event_count; i++)
      grown[i] = remote->events[(remote->event_head + i) % remote->event_cap];
    free(remote->events);
    remote->events = grown;
    remote->event_cap = new_cap;
    remote->event_head = 0;
  }
  event_entry *entry = &remote->events[(remote->event_head + remote->event_count) % remote->event_cap];
  memset(entry, 0, sizeof *entry);
  remote->event_count++;
  entry->event.sequence = remote->event_sequence++;
  const qa_executable_recipe *recipe = recipe_of(remote);
  entry->event.content = recipe->map.geometry_content;
  entry->event.event = &entry->payload;
  return entry;
}
static bool is_rerelease(qa_app_q2_remote *remote) {
  const char *kind = remote->selected_protocol.kind;
  return kind && (strcmp(kind, "q2-rerelease") == 0 || strcmp(kind, "q2-kex") == 0);
}
static qa_app_q2_remote_player native_player(qa_app_q2_remote *remote,
    const qa_q2_wire_frame *frame, bool previous) {
  qa_app_q2_remote_player player = {0};
  player.rerelease = is_rerelease(remote);
  double *stats = previous ? remote->stats_previous : remote->stats_current;
  if (player.rerelease) qa_q2_to_rerelease_player(&frame->player, stats, &player.modern);
  else qa_q2_to_player(&frame->player, stats, &player.classic);
  return player;
}
static qa_vec3 player_origin(qa_app_q2_remote *remote, const qa_q2_wire_frame *frame, bool previous) {
  qa_app_q2_remote_player player = native_player(remote, frame, previous);
  if (player.rerelease) return player.modern.movement.origin;
  qa_vec3 out = {(double)player.classic.movement.origin_eighths.values[0] / 8,
      (double)player.classic.movement.origin_eighths.values[1] / 8,
      (double)player.classic.movement.origin_eighths.values[2] / 8};
  return out;
}
static const qa_app_movement_prediction_snapshot *predicted_player(qa_app_q2_remote *remote) {
  if (!remote->has_predicted) return NULL;
  const char *status = remote->predicted.status;
  if (status && (strcmp(status, "predicted") == 0 || strcmp(status, "disabled") == 0))
    return &remote->predicted.player;
  return NULL;
}
static qa_vec3 sampled_player_origin(qa_app_q2_remote *remote, const qa_q2_wire_frame *frame) {
  const qa_app_movement_prediction_snapshot *predicted = predicted_player(remote);
  if (predicted) return qa_app_sim_movement_origin(predicted->state);
  qa_vec3 origin = player_origin(remote, frame, false);
  if (!remote->has_previous) return origin;
  qa_vec3 before = player_origin(remote, &remote->previous_frame, true);
  double dx = fabs(origin.x - before.x), dy = fabs(origin.y - before.y), dz = fabs(origin.z - before.z);
  double max_delta = dx > dy ? dx : dy;
  if (dz > max_delta) max_delta = dz;
  return max_delta > 256 ? origin : interpolate(before, origin, remote->fraction);
}
static void require_player(qa_app_q2_remote *remote, qa_actor_id actor) {
  if (!remote->has_player || !qa_same_actor(remote->player.actor, actor) || !remote->has_current)
    qa_range_error("Remote Q2 player has no decoded frame");
}
static const qa_app_q2_remote_mounts *downloads_content(void *ctx) {
  qa_app_q2_remote *remote = ctx;
  if (!remote->download_content) qa_range_error("Q2 downloads require prepared server content");
  return remote->download_content;
}
static const char *state_gamedir(const void *data) {
  return ((const qa_q2_server_data_result *)data)->gamedir;
}
static const char *state_config_string(const void *config_strings, double index) {
  const qa_app_q2_config_snapshot *snapshot = config_strings;
  for (size_t i = 0; i < snapshot->count; i++)
    if (snapshot->entries[i].index == index) return snapshot->entries[i].value;
  return NULL;
}
typedef struct refresh_ctx { qa_app_q2_remote *remote; double revision; } refresh_ctx;
static void refresh_assert(void *ctx) {
  refresh_ctx *refresh = ctx;
  if (qa_app_q2_download_receiver_revision(refresh->remote->downloads) != refresh->revision)
    qa_range_error("Q2 package refresh was retired");
}
static void refresh_downloads(void *ctx) {
  qa_app_q2_remote *remote = ctx;
  refresh_ctx refresh = {remote, qa_app_q2_download_receiver_revision(remote->downloads)};
  const qa_app_q2_remote_mounts *fresh = remote->options.refresh_downloads(remote->options.ctx, refresh_assert, &refresh);
  refresh_assert(&refresh);
  remote->download_content = fresh;
}
static void send_command_text(void *ctx, const char *text) {
  qa_app_q2_remote *remote = ctx;
  remote->options.send_command(remote->options.ctx, text);
}
static void host_print(void *ctx, const char *text) {
  qa_app_q2_remote *remote = ctx;
  remote->options.print(remote->options.ctx, text);
}
qa_app_q2_remote *qa_app_q2_remote_create(qa_app_q2_remote_options options) {
  if (options.protocol.kind && strcmp(options.protocol.kind, "q2-kex-demo") == 0)
    qa_range_error("KEX native live transport is not bound");
  qa_app_q2_remote *remote = calloc(1, sizeof *remote);
  if (!remote) return NULL;
  remote->options = options;
  remote->selected_protocol = options.protocol;
  remote->layout = options.has_recorded_profile
      ? qa_app_q2_mvd_layout(options.recorded_profile)
      : qa_app_q2_application_layout(options.protocol);
  remote->frame_milliseconds = 100;
  remote->fraction = 1;
  remote->layout_text = copy_text("");
  qa_app_q2_rerelease_view_height_init(&remote->view_height);
  remote->world = qa_app_remote_world_content_create(options.world_host, options.content);
  if (!remote->world) { free(remote->layout_text); free(remote); return NULL; }
  qa_app_q2_state_access state_access = {state_gamedir, state_config_string};
  remote->downloads = qa_app_q2_download_receiver_create(downloads_content, remote,
      send_command_text, remote, host_print, remote,
      options.refresh_downloads ? refresh_downloads : NULL, remote,
      options.download_permission, state_access);
  if (!remote->downloads) {
    qa_app_remote_world_content_destroy(remote->world);
    free(remote->layout_text);
    free(remote);
    return NULL;
  }
  return remote;
}
void qa_app_q2_remote_destroy(qa_app_q2_remote *remote) {
  if (!remote) return;
  qa_app_q2_download_receiver_destroy(remote->downloads);
  qa_app_remote_world_content_destroy(remote->world);
  if (remote->prediction_owner) qa_app_selected_movement_prediction_destroy(remote->prediction_owner);
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
  }
  if (remote->has_current) qa_q2_wire_frame_free(&remote->current);
  if (remote->has_previous) qa_q2_wire_frame_free(&remote->previous_frame);
  free(remote->actors);
  for (size_t i = 0; i < remote->config_count; i++) free(remote->configs[i].value);
  free(remote->configs);
  for (size_t i = 0; i < remote->resource_count; i++) free(remote->resources[i].key);
  free(remote->resources);
  for (size_t i = 0; i < remote->event_count; i++) {
    event_entry *entry = &remote->events[(remote->event_head + i) % remote->event_cap];
    free(entry->text);
    free(entry->path);
  }
  free(remote->events);
  free(remote->published_bodies);
  free(remote->published_actors);
  free(remote->published_styles);
  free(remote->inventory);
  records_free_local(remote->last_records, remote->last_record_count);
  free(remote->layout_text);
  if (remote->visibility) qa_app_q2_mvd_visibility_destroy(remote->visibility);
  free(remote->presentations);
  for (size_t i = 0; i < remote->presentation_path_cap; i++) free(remote->presentation_paths[i]);
  free(remote->presentation_paths);
  free(remote->drained);
  free(remote->ui_inventory);
  free(remote->ui_items);
  free(remote->sample_bodies);
  free(remote);
}
qa_app_q2_download_receiver *qa_app_q2_remote_downloads(qa_app_q2_remote *remote) {
  return remote->downloads;
}
size_t qa_app_q2_remote_download_progress(qa_app_q2_remote *remote,
    qa_app_client_download_progress *out, size_t capacity) {
  return qa_app_q2_download_receiver_progress(remote->downloads, out, capacity);
}
void qa_app_q2_remote_cancel_downloads(qa_app_q2_remote *remote) {
  qa_app_q2_download_receiver_cancel(remote->downloads);
}
void qa_app_q2_remote_retry_downloads(qa_app_q2_remote *remote) {
  qa_app_q2_download_receiver_retry(remote->downloads);
}
qa_client_id qa_app_q2_remote_client(const qa_app_q2_remote *remote) {
  return remote->options.client;
}
qa_q2_protocol_identity qa_app_q2_remote_protocol(const qa_app_q2_remote *remote) {
  return remote->selected_protocol;
}
void qa_app_q2_remote_message_options(const qa_app_q2_remote *remote,
    qa_q2_server_message_options *out) {
  memset(out, 0, sizeof *out);
  out->max_config_strings = remote->layout.max_config_strings;
  out->inventory_slots = 256;
}
const char *qa_app_q2_remote_userinfo(qa_app_q2_remote *remote) {
  return remote->options.userinfo(remote->options.ctx);
}
static qa_shared_scene_queries *visibility_scene(void *ctx) {
  qa_app_q2_remote *remote = ctx;
  return qa_app_remote_world_scene(remote->world);
}
static const char *visibility_model_path(void *ctx, double index) {
  qa_app_q2_remote *remote = ctx;
  return config_get(remote, remote->layout.models + index);
}
qa_q2_mvd_visibility qa_app_q2_remote_mvd_visibility(qa_app_q2_remote *remote) {
  if (!remote->options.has_recorded_profile)
    qa_range_error("MVD presentation requires its recorded profile");
  if (!remote->visibility) {
    qa_app_q2_mvd_visibility_host host = {0};
    host.ctx = remote;
    host.profile = remote->options.recorded_profile;
    host.scene = visibility_scene;
    host.model_path = visibility_model_path;
    remote->visibility = qa_app_q2_mvd_visibility_create(host);
    if (!remote->visibility) qa_range_error("out of memory");
  }
  return qa_app_q2_mvd_visibility_vtable(remote->visibility);
}
void qa_app_q2_remote_select_recorded_view(qa_app_q2_remote *remote, double clientnum) {
  const char *maximum_text = config_get(remote, remote->layout.max_clients);
  double maximum = maximum_text ? strtod(maximum_text, NULL) : NAN;
  if (clientnum != trunc(clientnum) || clientnum < 0 || maximum != trunc(maximum)
      || clientnum >= maximum)
    qa_range_error("Recorded viewpoint is outside its admitted player range");
  if (!remote->has_player || remote->player.source_entity != clientnum + 1) {
    if (remote->has_current) {
      qa_q2_wire_frame_free(&remote->current);
      remote->has_current = false;
    }
    if (remote->has_previous) {
      qa_q2_wire_frame_free(&remote->previous_frame);
      remote->has_previous = false;
    }
    qa_app_q2_rerelease_view_height_reset(&remote->view_height);
  }
  remote->player.client = remote->options.client;
  remote->player.actor = actor_of(remote, clientnum + 1);
  remote->player.source_entity = clientnum + 1;
  remote->has_player = true;
  if (remote->prediction_owner) {
    qa_app_selected_movement_prediction_destroy(remote->prediction_owner);
    remote->prediction_owner = NULL;
  }
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
    remote->has_predicted = false;
  }
}
qa_app_q2_layout qa_app_q2_remote_config_layout(const qa_app_q2_remote *remote) {
  return remote->layout;
}
bool qa_app_q2_remote_player_of(const qa_app_q2_remote *remote, qa_app_q2_player *out) {
  if (!remote->has_player) return false;
  *out = remote->player;
  return true;
}
const qa_simulation_output *qa_app_q2_remote_output(const qa_app_q2_remote *remote) {
  return remote->has_published ? &remote->published : NULL;
}
const qa_q2_server_record *qa_app_q2_remote_source_records(const qa_app_q2_remote *remote,
    size_t *out_count) {
  *out_count = remote->last_record_count;
  return remote->last_records;
}
const char *qa_app_q2_remote_native_layout(const qa_app_q2_remote *remote) {
  return remote->layout_text;
}
void *qa_app_q2_remote_scene(qa_app_q2_remote *remote) {
  return qa_app_remote_world_scene(remote->world);
}
bool qa_app_q2_remote_is_player(qa_app_q2_remote *remote, qa_actor_id actor) {
  if (remote->has_player && qa_same_actor(remote->player.actor, actor)) return true;
  if (!remote->has_current) return false;
  const char *maximum_text = config_get(remote, remote->layout.max_clients);
  double maximum = maximum_text ? strtod(maximum_text, NULL) : NAN;
  if (maximum != trunc(maximum) || maximum < 1 || maximum > 256)
    qa_range_error("Remote Q2 frame has no valid advertised client range");
  for (size_t i = 0; i < remote->current.entity_count; i++) {
    double number = remote->current.entities[i].number;
    if (number < 1 || number > maximum) continue;
    for (size_t j = 0; j < remote->actor_count; j++)
      if (remote->actors[j].number == number && qa_same_actor(remote->actors[j].actor, actor))
        return true;
  }
  return false;
}
typedef struct server_data_ctx {
  qa_app_q2_remote *remote; double revision;
  qa_app_q2_remote_assert_fn assert_current; void *assert_ctx;
} server_data_ctx;
static void server_data_assert(void *ctx) {
  server_data_ctx *data = ctx;
  data->assert_current(data->assert_ctx);
  if (qa_app_q2_download_receiver_revision(data->remote->downloads) != data->revision)
    qa_range_error("Q2 server content preparation was retired");
}
static void cinematic_ended(void *ctx) {
  qa_app_q2_remote *remote = ctx;
  if (remote->cinematic_state.completed) return;
  remote->cinematic_state.completed = true;
  char line[96];
  snprintf(line, sizeof line, "nextserver %.17g\n", remote->cinematic_state.servercount);
  remote->options.send_command(remote->options.ctx, line);
}
void qa_app_q2_remote_server_data(qa_app_q2_remote *remote, const void *data_value,
    qa_app_q2_remote_assert_fn assert_current, void *assert_ctx) {
  const qa_q2_server_data_result *data = data_value;
  if (remote->options.has_cinematic) remote->options.cinematic.stop(remote->options.cinematic.ctx);
  server_data_ctx context = {remote, qa_app_q2_download_receiver_revision(remote->downloads),
      assert_current, assert_ctx};
  const qa_app_q2_remote_mounts *prepared = remote->options.prepare_server_data(remote->options.ctx, data,
      server_data_assert, &context);
  server_data_assert(&context);
  remote->download_content = prepared;
  if (data->clientnum < 0) {
    if (!remote->options.has_cinematic)
      qa_range_error("Q2 cinematic serverdata requires the shared media owner");
    if (remote->has_current) {
      qa_q2_wire_frame_free(&remote->current);
      remote->has_current = false;
    }
    qa_app_q2_rerelease_view_height_reset(&remote->view_height);
    remote->view_time = 0;
    if (remote->has_previous) {
      qa_q2_wire_frame_free(&remote->previous_frame);
      remote->has_previous = false;
    }
    remote->has_published = false;
    remote->has_player = false;
    if (remote->prediction_owner) {
      qa_app_selected_movement_prediction_destroy(remote->prediction_owner);
      remote->prediction_owner = NULL;
    }
    if (remote->has_predicted) {
      qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
      free(remote->predicted.effects);
      remote->has_predicted = false;
    }
    remote->cinematic_state.remote = remote;
    remote->cinematic_state.servercount = data->servercount;
    remote->cinematic_state.completed = false;
    remote->options.cinematic.start(remote->options.cinematic.ctx, data->levelname,
        cinematic_ended, remote);
    server_data_assert(&context);
  }
}
static void player_info(qa_app_q2_remote *remote, double index, const char *value, double seconds) {
  if (index < remote->layout.player_skins || index >= remote->layout.player_skins + 256) return;
  double slot = index - remote->layout.player_skins;
  const char *split = strchr(value, '\\');
  event_entry *entry = event_push(remote);
  entry->event.kind = "q2-player";
  entry->event.event_kind = "userinfo";
  entry->event.seconds = seconds;
  entry->event.has_source_entity = true;
  entry->event.source_entity = slot + 1;
  entry->event.actor = actor_of(remote, slot + 1);
  entry->payload.kind = QA_APP_Q2_REMOTE_USERINFO;
  entry->payload.has_actor = true;
  entry->payload.actor = entry->event.actor;
  entry->payload.slot = slot;
  if (split) {
    size_t length = (size_t)(split - value);
    entry->text = xmalloc(length + 1);
    memcpy(entry->text, value, length);
    entry->text[length] = '\0';
    entry->payload.name = entry->text;
    entry->payload.skin = split + 1;
    entry->path = copy_text(split + 1);
    entry->payload.skin = entry->path;
  } else {
    entry->payload.name = value;
    entry->payload.skin = "";
  }
}
void qa_app_q2_remote_game_state(qa_app_q2_remote *remote, qa_app_q2_game_state state) {
  const qa_q2_server_data_result *data = state.data;
  qa_q2_protocol_identity offered = remote->options.protocol;
  if (offered.kind && strcmp(offered.kind, "q2-r1q2") == 0) {
    qa_q2_protocol_identity negotiated = offered;
    qa_q2_negotiated_r1q2_protocol(offered, data->has_r1q2_version,
        data->r1q2_version, &negotiated);
    remote->selected_protocol = negotiated;
  }
  remote->strafejump_hack = offered.kind && strcmp(offered.kind, "q2-r1q2") == 0
      ? (data->has_r1q2_strafejump_hack && data->r1q2_strafejump_hack)
      : (data->has_q2pro_strafejump_hack && data->q2pro_strafejump_hack);
  remote->extended_game = false;
  if (offered.kind && strcmp(offered.kind, "q2-q2pro") == 0) {
    qa_q2pro_features features = {0};
    features.revision = data->has_q2pro_version ? data->q2pro_version : offered.revision;
    features.flags = data->has_wire_flags ? data->wire_flags : 0;
    remote->extended_game = qa_q2pro_extensions(features);
  }
  double revision = qa_app_q2_download_receiver_revision(remote->downloads);
  void *content = remote->options.load_content
      ? remote->options.load_content(remote->options.ctx, &state)
      : qa_app_remote_world_loaded(remote->world);
  if (qa_app_q2_download_receiver_revision(remote->downloads) != revision) return;
  const qa_app_q2_config_snapshot *snapshot = state.config_strings;
  const char *path = NULL;
  const char *checksum = NULL;
  for (size_t i = 0; i < snapshot->count; i++) {
    if (snapshot->entries[i].index == remote->layout.models + 1) path = snapshot->entries[i].value;
    if (snapshot->entries[i].index == remote->layout.map_checksum) checksum = snapshot->entries[i].value;
  }
  const qa_executable_recipe *recipe = remote->options.content_recipe(remote->options.ctx, content);
  const char *requested = recipe->map.geometry.requested_path;
  if (!path || !requested || strcmp(path, requested) != 0)
    qa_range_error("Q2 server map %s requires application content replacement",
        path ? path : "<missing>");
  size_t map_length = 0;
  const uint8_t *map_bytes = remote->options.content_read(remote->options.ctx, content,
      &recipe->map.geometry, &map_length);
  if (qa_app_q2_download_receiver_revision(remote->downloads) != revision) return;
  if (!checksum || (uint32_t)strtod(checksum, NULL) != qa_block_checksum(map_bytes, map_length))
    qa_range_error("Q2 server map checksum differs from mounted content");
  if (data->clientnum < 0)
    qa_range_error("Q2 remote multi-seat/cinematic serverdata requires its source presentation binding");
  qa_app_remote_world_set_loaded(remote->world, content);
  if (remote->download_content && remote->options.rebind_download_content)
    remote->download_content = remote->options.rebind_download_content(
        remote->options.ctx, remote->download_content, content);
  remote->actor_count = 0;
  for (size_t i = 0; i < remote->config_count; i++) free(remote->configs[i].value);
  remote->config_count = 0;
  if (remote->has_current) {
    qa_q2_wire_frame_free(&remote->current);
    remote->has_current = false;
  }
  qa_app_q2_rerelease_view_height_reset(&remote->view_height);
  remote->view_time = 0;
  if (remote->has_previous) {
    qa_q2_wire_frame_free(&remote->previous_frame);
    remote->has_previous = false;
  }
  remote->has_published = false;
  if (remote->prediction_owner) {
    qa_app_selected_movement_prediction_destroy(remote->prediction_owner);
    remote->prediction_owner = NULL;
  }
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
    remote->has_predicted = false;
  }
  free(remote->inventory);
  remote->inventory = NULL;
  remote->inventory_count = 0;
  free(remote->layout_text);
  remote->layout_text = copy_text("");
  for (size_t i = 0; i < remote->event_count; i++) {
    event_entry *entry = &remote->events[(remote->event_head + i) % remote->event_cap];
    free(entry->text);
    free(entry->path);
  }
  remote->event_head = 0;
  remote->event_count = 0;
  for (size_t i = 0; i < snapshot->count; i++)
    config_set(remote, snapshot->entries[i].index, snapshot->entries[i].value);
  remote->player.client = remote->options.client;
  remote->player.actor = actor_of(remote, data->clientnum + 1);
  remote->player.source_entity = data->clientnum + 1;
  remote->has_player = true;
  remote->frame_milliseconds = 1000 / (data->has_server_fps ? data->server_fps : 10);
  for (size_t i = 0; i < remote->config_count; i++)
    player_info(remote, remote->configs[i].index, remote->configs[i].value, 0);
}
const qa_world_text *qa_app_q2_remote_world_text(qa_app_q2_remote *remote, size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
qa_app_sim_player_view qa_app_q2_remote_player_view(qa_app_q2_remote *remote, qa_actor_id actor) {
  require_player(remote, actor);
  const qa_q2_wire_frame *frame = &remote->current;
  qa_vec3 origin = sampled_player_origin(remote, frame);
  const qa_q2_wire_frame *previous = remote->has_previous ? &remote->previous_frame : NULL;
  double fov = !previous ? frame->player.fov
      : previous->player.fov + (frame->player.fov - previous->player.fov) * remote->fraction;
  qa_app_q2_remote_player native = native_player(remote, frame, false);
  qa_app_q2_remote_player prior = native;
  bool has_prior = previous != NULL;
  if (has_prior) prior = native_player(remote, previous, true);
  qa_app_sim_player_view view = {0};
  view.has_field_of_view = true;
  view.field_of_view = fov;
  if (native.rerelease) {
    view.has_damage_blend = true;
    view.damage_blend = qa_app_interpolate_q2_damage_blend(
        has_prior && prior.rerelease ? &prior.modern.damage_blend : NULL,
        native.modern.damage_blend, remote->fraction);
  }
  qa_vec3 offset = !has_prior
      ? (native.rerelease ? native.modern.view.view_offset : native.classic.view.view_offset)
      : interpolate(prior.rerelease ? prior.modern.view.view_offset : prior.classic.view.view_offset,
          native.rerelease ? native.modern.view.view_offset : native.classic.view.view_offset,
          remote->fraction);
  double height = qa_app_q2_remote_view_height(&native);
  double prior_height = !has_prior ? height : qa_app_q2_remote_view_height(&prior);
  bool has_source_height = native.rerelease;
  double source_height = has_source_height
      ? qa_app_q2_rerelease_view_height_sample(&remote->view_height, height, remote->view_time) : 0;
  const qa_app_movement_prediction_snapshot *predicted = predicted_player(remote);
  if (predicted) {
    qa_app_q2_remote_view_position position = qa_app_q2_remote_view_position_at(
        &native, origin, offset, has_source_height ? source_height : predicted->view_height);
    view.origin = position.origin;
    view.view_height = position.view_height;
    view.angles = predicted->view_angles;
    return view;
  }
  qa_app_q2_remote_view_position position = qa_app_q2_remote_view_position_at(&native, origin, offset,
      has_source_height ? source_height : prior_height + (height - prior_height) * remote->fraction);
  view.origin = position.origin;
  view.view_height = position.view_height;
  view.angles = !previous
      ? (native.rerelease ? native.modern.view.view_angles : native.classic.view.view_angles)
      : interpolate_angles(vec_of(previous->player.viewangles), vec_of(frame->player.viewangles),
          remote->fraction);
  return view;
}
static double stat_at(const qa_q2_wire_frame *frame, size_t index) {
  return index < QA_Q2_MAX_STATS_STORAGE ? frame->player.stats[index] : 0;
}
typedef struct count_ctx { const qa_q2_wire_frame *frame; } count_ctx;
static double item_count(void *ctx, qa_item_id item) {
  (void)item;
  count_ctx *count = ctx;
  return stat_at(count->frame, 3);
}
static char *normalized_name(const char *value) {
  char *out = xmalloc(strlen(value) + 1);
  size_t used = 0;
  for (const char *p = value; *p; p++) {
    if (*p == ' ') continue;
    char c = *p;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    out[used++] = c;
  }
  out[used] = '\0';
  return out;
}
qa_app_sim_player_ui qa_app_q2_remote_player_ui(qa_app_q2_remote *remote, qa_actor_id actor) {
  require_player(remote, actor);
  const qa_q2_wire_frame *frame = &remote->current;
  const char *weapon_model = config_get(remote, remote->layout.models + frame->player.gunindex);
  const qa_q2w_base_definition *weapon = NULL;
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++)
    if (weapon_model && qa_q2w_base_weapons[i].view_model
        && strcmp(qa_q2w_base_weapons[i].view_model, weapon_model) == 0)
      weapon = &qa_q2w_base_weapons[i];
  const qa_executable_recipe *recipe = recipe_of(remote);
  if (recipe->weapons.count == 0)
    qa_range_error("Remote Q2 presentation requires its selected weapon provider");
  if (remote->ui_inventory_cap < remote->inventory_count) {
    remote->ui_inventory = realloc(remote->ui_inventory,
        remote->inventory_count * sizeof *remote->ui_inventory);
    if (!remote->ui_inventory) qa_range_error("out of memory");
    remote->ui_inventory_cap = remote->inventory_count;
  }
  size_t entry_count = 0;
  for (size_t ordinal = 0; ordinal < remote->inventory_count; ordinal++) {
    double count = remote->inventory[ordinal];
    const char *label = config_get(remote, remote->layout.items + (double)ordinal);
    if (count == 0 || !label) continue;
    char *normalized = normalized_name(label);
    const qa_q2w_base_definition *definition = NULL;
    for (size_t i = 0; i < qa_q2w_base_weapon_count; i++)
      if (qa_q2w_base_weapons[i].name && strcmp(qa_q2w_base_weapons[i].name, normalized) == 0)
        definition = &qa_q2w_base_weapons[i];
    free(normalized);
    if (!definition) continue;
    remote->ui_inventory[entry_count].item = definition->item;
    remote->ui_inventory[entry_count].count = count;
    remote->ui_inventory[entry_count].capacity = count;
    entry_count++;
  }
  qa_app_sim_player_ui ui = {0};
  ui.health = stat_at(frame, 1);
  double armor = stat_at(frame, 5);
  if (armor == 0) {
    ui.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  } else {
    ui.armor.regular.kind = QA_REGULAR_ARMOR_Q2;
    ui.armor.regular.points = armor;
    ui.armor.regular.item = "q2:remote-armor";
  }
  ui.armor.powered.kind = "none";
  count_ctx count = {frame};
  char label[128] = "";
  qa_weapon_hud_status status = {0};
  qa_provider_reference source = recipe->weapons.values[0];
  if (qa_app_q2_weapon_status(weapon, item_count, &count, source, label, sizeof label, &status)) {
    remote->weapon_status = status;
    ui.weapon_status = &remote->weapon_status;
  }
  ui.arsenal_warning = "none";
  ui.active_weapon = weapon ? weapon->item : NULL;
  if (weapon && weapon->ammo) {
    ui.has_ammo = true;
    ui.ammo_item = weapon->ammo;
    ui.ammo_count = stat_at(frame, 3);
  }
  ui.inventory = remote->ui_inventory;
  ui.inventory_count = entry_count;
  if (!remote->ui_items) {
    remote->ui_items = xmalloc(qa_q2w_base_weapon_count * sizeof *remote->ui_items);
  }
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++) {
    const qa_q2w_base_definition *definition = &qa_q2w_base_weapons[i];
    qa_app_sim_player_ui_item *item = &remote->ui_items[i];
    memset(item, 0, sizeof *item);
    item->id = definition->item;
    item->label = definition->name;
    item->kind = "weapon";
    item->source_ordinal = (double)i;
    bool owned = weapon == definition;
    for (size_t j = 0; j < entry_count; j++)
      if (remote->ui_inventory[j].item == definition->item) owned = true;
    item->owned = owned;
    item->has_ammo = !definition->ammo || stat_at(frame, 3) >= definition->quantity;
    item->has_count = definition->ammo != NULL;
    item->count = stat_at(frame, 3);
    item->warning_count = definition->warning;
  }
  ui.items = remote->ui_items;
  ui.item_count = qa_q2w_base_weapon_count;
  return ui;
}
const qa_q3_character_view *qa_app_q2_remote_character_views(qa_app_q2_remote *remote,
    size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
const qa_app_simulation_presentation *qa_app_q2_remote_presentations(qa_app_q2_remote *remote,
    size_t *out_count) {
  *out_count = 0;
  if (!remote->has_current || !remote->has_player) return NULL;
  const qa_q2_wire_frame *current = &remote->current;
  const qa_executable_recipe *recipe = recipe_of(remote);
  size_t needed = current->entity_count + 1;
  if (remote->presentation_cap < needed) {
    remote->presentations = realloc(remote->presentations, needed * sizeof *remote->presentations);
    if (!remote->presentations) qa_range_error("out of memory");
    remote->presentation_cap = needed;
  }
  if (remote->presentation_path_cap < needed * 2) {
    for (size_t i = 0; i < remote->presentation_path_cap; i++) free(remote->presentation_paths[i]);
    free(remote->presentation_paths);
    remote->presentation_paths = calloc(needed * 2, sizeof(char *));
    if (!remote->presentation_paths) qa_range_error("out of memory");
    remote->presentation_path_cap = needed * 2;
  }
  for (size_t i = 0; i < remote->presentation_path_cap; i++) {
    free(remote->presentation_paths[i]);
    remote->presentation_paths[i] = NULL;
  }
  size_t count = 0;
  size_t path_index = 0;
  for (size_t i = 0; i < current->entity_count; i++) {
    const qa_q2_wire_entity *entity = &current->entities[i];
    const char *path = config_get(remote, remote->layout.models + entity->modelindex);
    const char *skin_path = NULL;
    if (entity->modelindex == 255) {
      const char *value = config_get(remote,
          remote->layout.player_skins + (double)((int64_t)entity->skinnum & 255));
      if (!value) value = "player\\male/grunt";
      const char *appearance = strchr(value, '\\');
      appearance = appearance ? appearance + 1 : value;
      const char *slash = strchr(appearance, '/');
      char model[64], skin[64];
      if (!slash) {
        snprintf(model, sizeof model, "male");
        snprintf(skin, sizeof skin, "grunt");
      } else {
        size_t model_length = (size_t)(slash - appearance);
        if (model_length >= sizeof model) model_length = sizeof model - 1;
        memcpy(model, appearance, model_length);
        model[model_length] = '\0';
        snprintf(skin, sizeof skin, "%s", slash + 1);
      }
      char *model_path = xmalloc(strlen(model) + 24);
      snprintf(model_path, strlen(model) + 24, "players/%s/tris.md2", model);
      remote->presentation_paths[path_index] = model_path;
      path = remote->presentation_paths[path_index++];
      char *skin_file = xmalloc(strlen(model) + strlen(skin) + 24);
      snprintf(skin_file, strlen(model) + strlen(skin) + 24, "players/%s/%s.pcx", model, skin);
      remote->presentation_paths[path_index] = skin_file;
      skin_path = remote->presentation_paths[path_index++];
    }
    if (!path || entity->modelindex == 0) continue;
    const qa_q2_wire_entity *prior = NULL;
    if (remote->has_previous)
      for (size_t j = 0; j < remote->previous_frame.entity_count; j++)
        if (remote->previous_frame.entities[j].number == entity->number)
          prior = &remote->previous_frame.entities[j];
    bool continuous = false;
    if (prior && prior->modelindex == entity->modelindex && entity->event != 6 && entity->event != 7) {
      double max_delta = 0;
      double deltas[3] = {fabs(entity->origin[0] - prior->origin[0]),
          fabs(entity->origin[1] - prior->origin[1]), fabs(entity->origin[2] - prior->origin[2])};
      for (size_t j = 0; j < 3; j++)
        if (deltas[j] > max_delta) max_delta = deltas[j];
      continuous = max_delta <= 512;
    }
    qa_app_simulation_presentation *item = &remote->presentations[count++];
    memset(item, 0, sizeof *item);
    item->actor = actor_of(remote, entity->number);
    item->content = recipe->map.geometry_content;
    item->family = QA_GAME_FAMILY_Q2;
    item->path = path;
    item->frame = entity->frame;
    item->old_frame = continuous ? prior->frame : entity->frame;
    item->has_back_lerp = true;
    item->back_lerp = continuous ? 1 - remote->fraction : 0;
    item->skin = entity->modelindex == 255 ? 0 : entity->skinnum;
    item->has_skin_path = skin_path != NULL;
    item->skin_path = skin_path;
    item->effects = entity->effects;
    item->render_flags = entity->renderfx;
    item->origin = continuous ? interpolate(vec_of(prior->origin), vec_of(entity->origin), remote->fraction)
        : vec_of(entity->origin);
    item->angles = continuous ? interpolate_angles(vec_of(prior->angles), vec_of(entity->angles), remote->fraction)
        : vec_of(entity->angles);
    item->scale = entity->scale != 0 ? entity->scale : 1;
    item->visible = true;
  }
  const char *gun_path = config_get(remote, remote->layout.models + current->player.gunindex);
  if (gun_path && current->player.gunindex != 0) {
    qa_app_sim_player_view view = qa_app_q2_remote_player_view(remote, remote->player.actor);
    qa_vec3 offset = vec_of(current->player.gunoffset);
    qa_app_simulation_presentation *item = &remote->presentations[count++];
    memset(item, 0, sizeof *item);
    item->actor = remote->player.actor;
    item->content = recipe->map.geometry_content;
    item->family = QA_GAME_FAMILY_Q2;
    item->path = gun_path;
    item->frame = current->player.gunframe;
    item->old_frame = remote->has_previous
        && remote->previous_frame.player.gunindex == current->player.gunindex
        ? remote->previous_frame.player.gunframe : current->player.gunframe;
    item->has_back_lerp = true;
    item->back_lerp = 1 - remote->fraction;
    item->skin = current->player.gunskin;
    item->origin = (qa_vec3){view.origin.x + offset.x, view.origin.y + offset.y,
        view.origin.z + view.view_height + offset.z};
    item->has_view_origin = true;
    item->view_origin = (qa_vec3){view.origin.x, view.origin.y, view.origin.z + view.view_height};
    item->has_view_angles = true;
    item->view_angles = view.angles;
    item->angles = view.angles;
    item->scale = 1;
    item->visible = true;
    item->view_weapon = true;
  }
  *out_count = count;
  return remote->presentations;
}
void qa_app_q2_remote_register_resource(qa_app_q2_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource) {
  char *key = xmalloc(strlen(content) + strlen(path) + 2);
  snprintf(key, strlen(content) + strlen(path) + 2, "%s/%s", content, path);
  for (size_t i = 0; i < remote->resource_count; i++)
    if (strcmp(remote->resources[i].key, key) == 0) {
      remote->resources[i].reference = resource;
      free(key);
      return;
    }
  if (remote->resource_count == remote->resource_cap) {
    remote->resource_cap = remote->resource_cap ? remote->resource_cap * 2 : 16;
    remote->resources = realloc(remote->resources, remote->resource_cap * sizeof *remote->resources);
    if (!remote->resources) qa_range_error("out of memory");
  }
  remote->resources[remote->resource_count].key = key;
  remote->resources[remote->resource_count].reference = resource;
  remote->resource_count++;
}
void qa_app_q2_remote_player_command(qa_app_q2_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count) {
  require_player(remote, actor);
  const char *values[64];
  values[0] = name;
  for (size_t i = 0; i < count && i < 63; i++) values[i + 1] = args[i];
  size_t total = strlen(name) + 1;
  for (size_t i = 0; i <= count && i < 64; i++) {
    const char *value = values[i];
    for (const char *p = value; *p; p++)
      if (*p == '"' || *p == '\n' || *p == '\r' || *p == ';')
        qa_range_error("Q2 console argument contains a command delimiter");
    if (i) total += strlen(value) + 3;
  }
  char *line = xmalloc(total);
  size_t used = (size_t)snprintf(line, total, "%s", name);
  for (size_t i = 0; i < count; i++)
    used += (size_t)snprintf(line + used, total - used, " \"%s\"", args[i]);
  remote->options.send_command(remote->options.ctx, line);
  free(line);
}
qa_q2_wire_usercmd qa_app_q2_remote_wire_command(qa_app_q2_remote *remote,
    const qa_actor_command *command) {
  require_player(remote, command->actor);
  if (command->command.kind != QA_USER_COMMAND_Q2
      && command->command.kind != QA_USER_COMMAND_Q2_RERELEASE)
    qa_range_error("Native Q2 client requires Q2 movement commands");
  qa_app_q2_remote_player player = native_player(remote, &remote->current, false);
  return qa_app_q2_remote_command(command->command.kind == QA_USER_COMMAND_Q2_RERELEASE,
      &command->command.as.q2, &command->command.as.q2_rerelease, &player);
}
static bool is_brush_hit(void *ctx, qa_trace_hit hit) {
  qa_app_q2_remote *remote = ctx;
  if (hit.kind == QA_TRACE_HIT_WORLD) return true;
  if (hit.kind != QA_TRACE_HIT_ACTOR || !remote->has_current) return false;
  for (size_t i = 0; i < remote->current.entity_count; i++) {
    const qa_q2_wire_entity *entity = &remote->current.entities[i];
    bool matches = false;
    for (size_t j = 0; j < remote->actor_count; j++)
      if (remote->actors[j].number == entity->number
          && qa_same_actor(remote->actors[j].actor, hit.actor)) matches = true;
    if (matches && entity->solid == 31) return true;
  }
  return false;
}
static void receive_prediction(qa_app_q2_remote *remote, const qa_q2_wire_frame *frame) {
  if (!remote->has_player) return;
  qa_app_q2_remote_player native = native_player(remote, frame, false);
  void *loaded = qa_app_remote_world_loaded(remote->world);
  const qa_executable_recipe *recipe = recipe_of(remote);
  qa_movement_profile profile = qa_app_sim_movement_profile(*recipe);
  const char *kind = profile.kind ? profile.kind : "";
  if (strcmp(kind, "q2-classic") != 0 && strcmp(kind, "q2-rerelease") != 0) return;
  bool profile_rerelease = strcmp(kind, "q2-rerelease") == 0;
  if (profile_rerelease != native.rerelease)
    qa_range_error("Q2 server movement API differs from selected prediction profile");
  const char *air_text = config_get(remote, remote->layout.air_accelerate);
  profile.air_accelerate = air_text ? strtod(air_text, NULL) : 0;
  qa_app_sim_player_ui ui = qa_app_q2_remote_player_ui(remote, remote->player.actor);
  qa_app_movement_prediction_snapshot snapshot = {0};
  snapshot.sequence = remote->packet_acknowledged;
  snapshot.command_time_milliseconds = frame->server_frame * remote->frame_milliseconds;
  if (native.rerelease) {
    snapshot.state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
    snapshot.state.as.q2_rerelease = native.modern.movement;
    snapshot.view_angles = native.modern.view.view_angles;
    snapshot.view_offset = native.modern.view.view_offset;
  } else {
    snapshot.state.kind = QA_MOVEMENT_STATE_Q2;
    snapshot.state.as.q2 = native.classic.movement;
    snapshot.view_angles = native.classic.view.view_angles;
    snapshot.view_offset = native.classic.view.view_offset;
  }
  snapshot.view_height = qa_app_q2_remote_view_height(&native);
  snapshot.bounds = qa_app_q2_remote_body_bounds(&native);
  snapshot.environment.health = native.rerelease
      ? (native.modern.view.stats.count > 1 ? native.modern.view.stats.values[1] : 0)
      : (native.classic.view.stats.count > 1 ? native.classic.view.stats.values[1] : 0);
  snapshot.environment.gravity_multiplier = 1;
  snapshot.arsenal.provider = recipe->inventory.provider;
  snapshot.arsenal.active_weapon = ui.active_weapon;
  snapshot.arsenal.ammo = ui.inventory;
  snapshot.arsenal.ammo_count = ui.inventory_count;
  snapshot.arsenal.state.kind = "q2";
  snapshot.animation.provider = recipe->character.definition.provider;
  snapshot.animation.state.kind = "q2";
  if (!remote->prediction_owner) {
    qa_app_movement_prediction_options options = {0};
    options.probe.actor = remote->options.owned_actor(remote->options.ctx,
        remote->player.actor, &recipe->map.entities);
    options.seat = remote->options.seat;
    options.probe.recipe = recipe;
    options.probe.profile = profile;
    options.probe.standing_bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
    options.probe.standing_view_height = 22;
    options.probe.host_ctx = remote;
    options.probe.is_brush = is_brush_hit;
    (void)loaded;
    remote->prediction_owner = qa_app_selected_movement_prediction_create(options, snapshot);
    if (!remote->prediction_owner) qa_range_error("out of memory");
  } else qa_app_selected_movement_prediction_receive(remote->prediction_owner, snapshot);
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
  }
  remote->predicted = qa_app_selected_movement_prediction_replay(remote->prediction_owner, NULL, NULL);
  remote->has_predicted = true;
}
static void link_solids(qa_app_q2_remote *remote, const qa_q2_wire_frame *frame,
    const qa_body_snapshot *bodies, size_t body_count) {
  if (!remote->options.scene_link) return;
  void *scene = qa_app_remote_world_scene(remote->world);
  for (size_t i = 0; i < remote->actor_count; i++)
    remote->options.scene_unlink(remote->options.ctx, scene, remote->actors[i].actor);
  for (size_t i = 0; i < frame->entity_count; i++) {
    const qa_q2_wire_entity *entity = &frame->entities[i];
    if (entity->solid == 0) continue;
    const qa_body_snapshot *body = NULL;
    qa_actor_id actor = actor_of(remote, entity->number);
    for (size_t j = 0; j < body_count; j++)
      if (qa_same_actor(bodies[j].actor, actor)) body = &bodies[j];
    if (!body) continue;
    const char *path = config_get(remote, remote->layout.models + entity->modelindex);
    bool has_model = false;
    double model = 0;
    if (entity->solid == 31 && path && path[0] == '*') {
      char *end = NULL;
      model = strtod(path + 1, &end);
      has_model = end != path + 1 && model == trunc(model);
    }
    if (entity->solid == 31 && !has_model) continue;
    qa_bounds bounds = has_model
        ? remote->options.scene_model_bounds(remote->options.ctx, scene, model)
        : body->body.bounds;
    qa_vec3 origin = body->body.origin;
    double rx = fabs(bounds.min.x) > fabs(bounds.max.x) ? fabs(bounds.min.x) : fabs(bounds.max.x);
    double ry = fabs(bounds.min.y) > fabs(bounds.max.y) ? fabs(bounds.min.y) : fabs(bounds.max.y);
    double rz = fabs(bounds.min.z) > fabs(bounds.max.z) ? fabs(bounds.min.z) : fabs(bounds.max.z);
    double radius = sqrt(rx * rx + ry * ry + rz * rz);
    bool rotated = has_model && (body->body.angles.x != 0 || body->body.angles.y != 0
        || body->body.angles.z != 0);
    qa_bounds linked = rotated ? (qa_bounds){{-radius, -radius, -radius}, {radius, radius, radius}} : bounds;
    qa_linked_body linked_body = {0};
    linked_body.actor = body->actor;
    linked_body.state = body->body;
    linked_body.state.bounds = bounds;
    linked_body.link_count = frame->server_frame;
    linked_body.absolute_bounds = (qa_bounds){
        {origin.x + linked.min.x - 1, origin.y + linked.min.y - 1, origin.z + linked.min.z - 1},
        {origin.x + linked.max.x + 1, origin.y + linked.max.y + 1, origin.z + linked.max.z + 1}};
    qa_actor_collision collision = {0};
    collision.family = "q2";
    collision.shape_kind = has_model ? "model" : "box";
    collision.shape_model = model;
    collision.contents = has_model ? 1 : 0x2000000;
    collision.role = "solid";
    collision.monster = !has_model;
    remote->options.scene_link(remote->options.ctx, scene, linked_body, collision);
  }
}
void qa_app_q2_remote_frame(qa_app_q2_remote *remote, qa_q2_wire_frame frame,
    const qa_q2_server_record *records, size_t count, double now_milliseconds) {
  for (size_t i = 0; i < count; i++)
    if (records[i].event.kind == QA_Q2_SERVER_CONFIG_STRING)
      config_set(remote, records[i].event.index, records[i].event.text);
  if (!remote->has_player) {
    qa_q2_wire_frame_free(&frame);
    qa_range_error("Q2 frame precedes application signon");
  }
  qa_app_q2_remote_player native = native_player(remote, &frame, false);
  bool continuous = true;
  if (native.rerelease) {
    continuous = false;
    if (remote->has_current && remote->current.valid) {
      qa_app_q2_remote_player prior = native_player(remote, &remote->current, false);
      if (prior.rerelease) {
        double event = 0;
        for (size_t i = 0; i < frame.entity_count; i++)
          if (frame.entities[i].number == remote->player.source_entity)
            event = frame.entities[i].event;
        continuous = qa_app_q2_rerelease_view_continuous(&prior.modern, &native.modern,
            remote->current.server_frame, frame.server_frame, event);
      }
    }
  }
  double previous_frame_number = remote->has_current ? remote->current.server_frame : 0;
  bool had_previous = remote->has_current;
  if (remote->has_previous) qa_q2_wire_frame_free(&remote->previous_frame);
  if (remote->has_current && continuous) {
    remote->previous_frame = remote->current;
    remote->has_previous = true;
    memcpy(remote->stats_previous, remote->stats_current, sizeof remote->stats_previous);
  } else {
    if (remote->has_current) qa_q2_wire_frame_free(&remote->current);
    remote->has_previous = false;
  }
  remote->current = frame;
  remote->has_current = true;
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
    remote->has_predicted = false;
  }
  remote->fraction = 1;
  double candidate_time = (frame.server_frame - 1) * remote->frame_milliseconds;
  if (candidate_time > remote->view_time) remote->view_time = candidate_time;
  remote->received_at = remote->options.presentation_time
      ? remote->options.presentation_time(remote->options.ctx) : now_milliseconds;
  qa_app_sim_player_view view = qa_app_q2_remote_player_view(remote, remote->player.actor);
  qa_vec3 velocity = {0, 0, 0};
  if (native.rerelease) velocity = native.modern.movement.velocity;
  else {
    velocity = (qa_vec3){(double)native.classic.movement.velocity_eighths.values[0] / 8,
        (double)native.classic.movement.velocity_eighths.values[1] / 8,
        (double)native.classic.movement.velocity_eighths.values[2] / 8};
  }
  free(remote->published_bodies);
  free(remote->published_actors);
  size_t body_count = 0;
  remote->published_bodies = xmalloc((frame.entity_count + 1) * sizeof *remote->published_bodies);
  qa_q2_solid_encoding encoding = QA_Q2_SOLID_SHORT;
  qa_q2_get_solid_encoding(remote->selected_protocol, remote->extended_game, &encoding);
  for (size_t i = 0; i < frame.entity_count; i++) {
    const qa_q2_wire_entity *entity = &frame.entities[i];
    if (entity->number == remote->player.source_entity) continue;
    qa_body_snapshot *body = &remote->published_bodies[body_count++];
    body->actor = actor_of(remote, entity->number);
    body->body.origin = vec_of(entity->origin);
    body->body.angles = vec_of(entity->angles);
    body->body.velocity = (qa_vec3){0, 0, 0};
    body->body.bounds = qa_q2_unpack_solid(entity->solid, encoding);
    body->body.ground = NULL;
  }
  qa_body_snapshot *player_body = &remote->published_bodies[body_count++];
  player_body->actor = remote->player.actor;
  player_body->body.origin = player_origin(remote, &frame, false);
  player_body->body.angles = view.angles;
  player_body->body.velocity = velocity;
  player_body->body.bounds = qa_app_q2_remote_body_bounds(&native);
  player_body->body.ground = NULL;
  double time = frame.server_frame * remote->frame_milliseconds;
  const qa_executable_recipe *recipe = recipe_of(remote);
  free(remote->published_styles);
  remote->published_styles = xmalloc(256 * sizeof *remote->published_styles);
  size_t style_count = 0;
  for (size_t style = 0; style < 256; style++) {
    const char *pattern = config_get(remote, remote->layout.lights + (double)style);
    if (!pattern) continue;
    size_t length = strlen(pattern);
    double value = length == 0 ? 1
        : ((double)(unsigned char)pattern[(size_t)floor(time / 100) % length] - 97) / 12;
    qa_scene_light_style *entry = &remote->published_styles[style_count++];
    entry->is_q2 = true;
    entry->style = (double)style;
    entry->value = value;
    entry->rgb = (qa_vec3){value, value, value};
    entry->white = value * 3;
  }
  remote->published_actors = xmalloc(body_count * sizeof *remote->published_actors);
  for (size_t i = 0; i < body_count; i++) {
    remote->published_actors[i].id = remote->published_bodies[i].actor;
    remote->published_actors[i].owner = recipe->map.entities.provider;
    remote->published_actors[i].definition = "q2:remote-entity";
  }
  qa_app_sim_player_ui ui = qa_app_q2_remote_player_ui(remote, remote->player.actor);
  remote->published_inventory[0].actor = remote->player.actor;
  remote->published_inventory[0].entries = ui.inventory;
  remote->published_inventory[0].entry_count = ui.inventory_count;
  remote->published_configuration[0].actor = remote->player.actor;
  remote->published_configuration[0].movement = recipe->movement;
  remote->published_configuration[0].character = recipe->character;
  remote->published_configuration[0].weapons = recipe->weapons;
  remote->published_configuration[0].inventory = recipe->inventory;
  memset(&remote->published, 0, sizeof remote->published);
  remote->published.snapshot.session = remote->options.session;
  remote->published.snapshot.frame.frame = frame.server_frame;
  remote->published.snapshot.frame.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, time};
  remote->published.snapshot.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS,
      !had_previous ? remote->frame_milliseconds
          : (frame.server_frame - previous_frame_number) * remote->frame_milliseconds};
  remote->published.snapshot.frame.phase = QA_FRAME_PHASE_FRAME_EXIT;
  remote->published.snapshot.actors = remote->published_actors;
  remote->published.snapshot.actor_count = body_count;
  remote->published.snapshot.bodies = remote->published_bodies;
  remote->published.snapshot.body_count = body_count;
  remote->published.snapshot.inventories = remote->published_inventory;
  remote->published.snapshot.inventory_count = 1;
  remote->published.snapshot.configurations = remote->published_configuration;
  remote->published.snapshot.configuration_count = 1;
  remote->published.snapshot.scene.session = remote->options.session;
  remote->published.snapshot.scene.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, time};
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
  remote->published.snapshot.scene.light_style_count = style_count;
  remote->published_area_bits.bytes = frame.area_bits;
  remote->published_area_bits.length = frame.area_bits_length;
  remote->published.snapshot.scene.area_bits = &remote->published_area_bits;
  remote->has_published = true;
  remote->options.publish(remote->options.ctx, &remote->published);
  link_solids(remote, &frame, remote->published_bodies, body_count);
  if (!remote->options.has_recorded_profile) receive_prediction(remote, &frame);
  double seconds = time / 1000;
  for (size_t i = 0; i < frame.entity_count; i++) {
    const qa_q2_wire_entity *entity = &frame.entities[i];
    if (entity->event == 0) continue;
    event_entry *entry = event_push(remote);
    entry->event.kind = "q2";
    entry->event.event_kind = "entity-event";
    entry->event.seconds = seconds;
    entry->event.has_source_entity = true;
    entry->event.source_entity = entity->number;
    entry->payload.kind = QA_APP_Q2_REMOTE_ENTITY_EVENT;
    entry->payload.has_actor = true;
    entry->payload.actor = actor_of(remote, entity->number);
    entry->event.actor = entry->payload.actor;
    entry->payload.event = entity->event;
  }
  if (had_previous) {
    /* Previous frame entities were moved/freed above; loop-sound
     * stop/start diffing uses the retained previous frame. */
    const qa_q2_wire_frame *previous = remote->has_previous ? &remote->previous_frame : NULL;
    if (previous) {
      for (size_t i = 0; i < previous->entity_count; i++) {
        const qa_q2_wire_entity *entity = &previous->entities[i];
        if (entity->sound == 0) continue;
        bool present = false;
        for (size_t j = 0; j < frame.entity_count; j++)
          if (frame.entities[j].number == entity->number && frame.entities[j].sound == entity->sound)
            present = true;
        if (present) continue;
        event_entry *entry = event_push(remote);
        entry->event.kind = "q2";
        entry->event.event_kind = "sound";
        entry->event.seconds = seconds;
        entry->event.has_source_entity = true;
        entry->event.source_entity = entity->number;
        entry->payload.kind = QA_APP_Q2_REMOTE_SOUND_STOP;
        entry->payload.has_actor = true;
        entry->payload.actor = actor_of(remote, entity->number);
        entry->event.actor = entry->payload.actor;
        entry->payload.origin = vec_of(entity->origin);
        { const char *sound_path = config_get(remote, remote->layout.sounds + entity->sound); entry->path = copy_text(sound_path ? sound_path : ""); }
        entry->payload.path = entry->path;
      }
      for (size_t i = 0; i < frame.entity_count; i++) {
        const qa_q2_wire_entity *entity = &frame.entities[i];
        if (entity->sound == 0) continue;
        bool present = false;
        for (size_t j = 0; j < previous->entity_count; j++)
          if (previous->entities[j].number == entity->number
              && previous->entities[j].sound == entity->sound) present = true;
        if (present) continue;
        const char *path = config_get(remote, remote->layout.sounds + entity->sound);
        if (!path) qa_range_error("Q2 loop sound %.17g has no configstring", entity->sound);
        event_entry *entry = event_push(remote);
        entry->event.kind = "q2";
        entry->event.event_kind = "sound";
        entry->event.seconds = seconds;
        entry->event.has_source_entity = true;
        entry->event.source_entity = entity->number;
        entry->payload.kind = QA_APP_Q2_REMOTE_SOUND_START;
        entry->payload.has_actor = true;
        entry->payload.actor = actor_of(remote, entity->number);
        entry->event.actor = entry->payload.actor;
        entry->payload.origin = vec_of(entity->origin);
        entry->payload.path = path;
        entry->payload.volume = 1;
        entry->payload.attenuation = 1;
      }
    }
  }
  (void)records;
  (void)count;
}
static const qa_simulation_output *sample_frame(qa_app_q2_remote *remote, double fraction) {
  if (!remote->has_current || !remote->has_published || !remote->has_player) return NULL;
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  remote->fraction = fraction;
  double time = (remote->current.server_frame - 1 + fraction) * remote->frame_milliseconds;
  remote->view_time = time;
  size_t presentation_count = 0;
  const qa_app_simulation_presentation *presentations =
      qa_app_q2_remote_presentations(remote, &presentation_count);
  qa_app_sim_player_view view = qa_app_q2_remote_player_view(remote, remote->player.actor);
  double prior_time = remote->published.snapshot.frame.time.kind == QA_SOURCE_TIME_MILLISECONDS
      ? remote->published.snapshot.frame.time.value
      : remote->published.snapshot.frame.time.value * 1000;
  free(remote->sample_bodies);
  remote->sample_bodies = xmalloc(remote->published.snapshot.body_count * sizeof *remote->sample_bodies);
  for (size_t i = 0; i < remote->published.snapshot.body_count; i++) {
    remote->sample_bodies[i] = remote->published.snapshot.bodies[i];
    qa_body_snapshot *body = &remote->sample_bodies[i];
    if (qa_same_actor(body->actor, remote->player.actor)) {
      body->body.origin = sampled_player_origin(remote, &remote->current);
      body->body.angles = view.angles;
      const qa_app_movement_prediction_snapshot *predicted = predicted_player(remote);
      qa_app_q2_remote_player native = native_player(remote, &remote->current, false);
      body->body.bounds = predicted ? predicted->bounds : qa_app_q2_remote_body_bounds(&native);
    } else {
      for (size_t j = 0; j < presentation_count; j++)
        if (qa_same_actor(presentations[j].actor, body->actor) && !presentations[j].view_weapon) {
          body->body.origin = presentations[j].origin;
          body->body.angles = presentations[j].angles;
        }
    }
  }
  remote->published.snapshot.bodies = remote->sample_bodies;
  remote->published.snapshot.frame.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, time};
  double elapsed = time - prior_time;
  remote->published.snapshot.frame.elapsed =
      (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, elapsed < 0 ? 0 : elapsed};
  remote->published.snapshot.scene.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, time};
  remote->options.publish(remote->options.ctx, &remote->published);
  return &remote->published;
}
const qa_simulation_output *qa_app_q2_remote_sample_presentation(qa_app_q2_remote *remote,
    double now_milliseconds) {
  if (remote->options.presentation_time)
    now_milliseconds = remote->options.presentation_time(remote->options.ctx);
  return sample_frame(remote, (now_milliseconds - remote->received_at) / remote->frame_milliseconds);
}
const qa_simulation_output *qa_app_q2_remote_sample_demo(qa_app_q2_remote *remote,
    double recorded_milliseconds) {
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
    remote->has_predicted = false;
  }
  double base = ((remote->has_current ? remote->current.server_frame : 0) - 1)
      * remote->frame_milliseconds;
  return sample_frame(remote, (recorded_milliseconds - base) / remote->frame_milliseconds);
}
void qa_app_q2_remote_records(qa_app_q2_remote *remote,
    const qa_q2_server_record *records, size_t count) {
  records_free_local(remote->last_records, remote->last_record_count);
  remote->last_records = NULL;
  remote->last_record_count = 0;
  if (count) {
    remote->last_records = xmalloc(count * sizeof *remote->last_records);
    for (size_t i = 0; i < count; i++) {
      record_copy_local(&records[i], &remote->last_records[i]);
    }
    remote->last_record_count = count;
  }
  double seconds = (remote->has_current ? remote->current.server_frame : 0)
      * remote->frame_milliseconds / 1000;
  for (size_t i = 0; i < count; i++) {
    const qa_q2_server_event *event = &records[i].event;
    if (event->kind == QA_Q2_SERVER_CONFIG_STRING) {
      config_set(remote, event->index, event->text);
      if (remote->has_player) player_info(remote, event->index, event->text ? event->text : "", seconds);
    } else if (event->kind == QA_Q2_SERVER_PRINT && event->level == 3 && remote->has_player) {
      event_entry *entry = event_push(remote);
      entry->event.kind = "q2-player";
      entry->event.event_kind = "print";
      entry->event.seconds = seconds;
      entry->event.has_source_entity = true;
      entry->event.source_entity = remote->player.source_entity;
      entry->payload.kind = QA_APP_Q2_REMOTE_PRINT;
      entry->payload.has_actor = true;
      entry->payload.actor = remote->player.actor;
      entry->text = copy_text(event->text ? event->text : "");
      entry->payload.text = entry->text;
    } else if (event->kind == QA_Q2_SERVER_INVENTORY) {
      free(remote->inventory);
      remote->inventory = xmalloc(event->count_count * sizeof *remote->inventory);
      for (size_t j = 0; j < event->count_count; j++) remote->inventory[j] = event->counts[j];
      remote->inventory_count = event->count_count;
    } else if (event->kind == QA_Q2_SERVER_LAYOUT) {
      free(remote->layout_text);
      remote->layout_text = copy_text(event->text ? event->text : "");
    } else if (event->kind == QA_Q2_SERVER_TEMPORARY_ENTITY) {
      event_entry *entry = NULL;
      qa_app_q2_effect_event effect = {0};
      if (qa_app_q2_effect_from_wire(&event->temp, &effect)) {
        entry = event_push(remote);
        entry->effect = effect;
        entry->event.kind = "q2";
        entry->event.event_kind = "effect";
        entry->payload.kind = QA_APP_Q2_REMOTE_EFFECT;
        entry->payload.effect = &entry->effect;
      } else {
        qa_app_q2_beam_event beam = {0};
        if (qa_app_q2_beam_from_wire(&event->temp, &beam)) {
          entry = event_push(remote);
          entry->beam = beam;
          entry->event.kind = "q2-weapon";
          entry->event.event_kind = "beam";
          entry->payload.kind = QA_APP_Q2_REMOTE_BEAM;
          entry->payload.effect = &entry->beam;
        }
      }
      if (entry) entry->event.seconds = seconds;
    } else if (event->kind == QA_Q2_SERVER_SOUND) {
      const char *path = config_get(remote, remote->layout.sounds + event->sound.index);
      if (!path) qa_range_error("Q2 sound %.17g has no configstring", event->sound.index);
      qa_vec3 origin = {0, 0, 0};
      if (event->sound.has_position) origin = event->sound.position;
      else if (remote->has_current)
        for (size_t j = 0; j < remote->current.entity_count; j++)
          if (remote->current.entities[j].number == event->sound.entity)
            origin = vec_of(remote->current.entities[j].origin);
      event_entry *entry = event_push(remote);
      entry->event.kind = "q2";
      entry->event.event_kind = "sound";
      entry->event.seconds = seconds;
      entry->event.has_source_entity = true;
      entry->event.source_entity = event->sound.entity;
      entry->payload.kind = QA_APP_Q2_REMOTE_SOUND_ONCE;
      if (event->sound.entity != 0) {
        entry->payload.has_actor = true;
        entry->payload.actor = actor_of(remote, event->sound.entity);
        entry->event.actor = entry->payload.actor;
      }
      entry->payload.origin = origin;
      entry->payload.path = path;
      entry->payload.volume = event->sound.volume;
      entry->payload.attenuation = event->sound.attenuation;
      entry->payload.channel = event->sound.channel;
    } else if (event->kind == QA_Q2_SERVER_MUZZLE_FLASH) {
      if (event->monster) {
        const qa_q2_wire_entity *entity = NULL;
        if (remote->has_current)
          for (size_t j = 0; j < remote->current.entity_count; j++)
            if (remote->current.entities[j].number == event->flash_entity)
              entity = &remote->current.entities[j];
        if (!entity) continue;
        qa_angle_vectors axes = qa_math_angle_vectors_rule(vec_of(entity->angles), QA_ANGLE_RULE_NATIVE);
        qa_vec3 offset = qa_q2m_muzzle_offset(QA_Q2F_CLASSIC, event->flash);
        qa_vec3 origin = vec_of(entity->origin);
        event_entry *entry = event_push(remote);
        entry->event.kind = "q2";
        entry->event.event_kind = "monster-muzzleflash";
        entry->event.seconds = seconds;
        entry->event.has_source_entity = true;
        entry->event.source_entity = event->flash_entity;
        entry->payload.kind = QA_APP_Q2_REMOTE_MONSTER_MUZZLEFLASH;
        entry->payload.has_actor = true;
        entry->payload.actor = actor_of(remote, event->flash_entity);
        entry->event.actor = entry->payload.actor;
        entry->payload.flash = event->flash;
        entry->payload.origin = (qa_vec3){
            origin.x + axes.forward.x * offset.x + axes.right.x * offset.y,
            origin.y + axes.forward.y * offset.x + axes.right.y * offset.y,
            origin.z + axes.forward.z * offset.x + axes.right.z * offset.y + offset.z};
        entry->payload.direction = axes.forward;
      } else {
        event_entry *entry = event_push(remote);
        entry->event.kind = "q2-weapon";
        entry->event.event_kind = "muzzleflash";
        entry->event.seconds = seconds;
        entry->event.has_source_entity = true;
        entry->event.source_entity = event->flash_entity;
        entry->payload.kind = QA_APP_Q2_REMOTE_MUZZLEFLASH;
        entry->payload.has_actor = true;
        entry->payload.actor = actor_of(remote, event->flash_entity);
        entry->event.actor = entry->payload.actor;
        entry->payload.flash = event->flash;
        entry->payload.silenced = event->silenced;
      }
    } else if (event->kind == QA_Q2_SERVER_CENTER_PRINT && remote->has_player) {
      event_entry *entry = event_push(remote);
      entry->event.kind = "q2";
      entry->event.event_kind = "centerprint";
      entry->event.seconds = seconds;
      entry->event.has_source_entity = true;
      entry->event.source_entity = remote->player.source_entity;
      entry->payload.kind = QA_APP_Q2_REMOTE_CENTERPRINT;
      entry->payload.has_actor = true;
      entry->payload.actor = remote->player.actor;
      entry->event.actor = remote->player.actor;
      entry->text = copy_text(event->text ? event->text : "");
      entry->payload.text = entry->text;
    }
  }
}
const qa_app_sim_presentation_event *qa_app_q2_remote_drain_events(qa_app_q2_remote *remote,
    size_t *out_count) {
  if (remote->drained_cap < remote->event_count) {
    remote->drained = realloc(remote->drained, remote->event_count * sizeof *remote->drained);
    if (!remote->drained) qa_range_error("out of memory");
    remote->drained_cap = remote->event_count;
  }
  /* Drained events alias ring payloads; ring storage is retained
   * (entries are not freed on drain, matching take+retire reuse). */
  for (size_t i = 0; i < remote->event_count; i++)
    remote->drained[i] = remote->events[(remote->event_head + i) % remote->event_cap].event;
  *out_count = remote->event_count;
  remote->event_head = (remote->event_head + remote->event_count) % (remote->event_cap ? remote->event_cap : 1);
  remote->event_count = 0;
  return remote->drained;
}
void qa_app_q2_remote_prediction_acknowledged(qa_app_q2_remote *remote,
    double sequence, double now_milliseconds) {
  (void)now_milliseconds;
  remote->packet_acknowledged = sequence;
}
void qa_app_q2_remote_prediction_sent(qa_app_q2_remote *remote,
    double sequence, const qa_q2_wire_usercmd *command, double now_milliseconds) {
  if (!remote->prediction_owner) return;
  qa_app_prediction_command entry = {0};
  entry.sequence = sequence;
  entry.time_milliseconds = now_milliseconds;
  bool rerelease = is_rerelease(remote);
  if (rerelease) {
    entry.command.kind = QA_USER_COMMAND_Q2_RERELEASE;
    qa_q2_to_rerelease_command(command, remote->has_current ? remote->current.server_frame : 0,
        &entry.command.as.q2_rerelease);
  } else {
    entry.command.kind = QA_USER_COMMAND_Q2;
    qa_q2_to_command(command, &entry.command.as.q2);
  }
  qa_app_selected_movement_prediction_submit(remote->prediction_owner, entry);
  if (remote->has_predicted) {
    qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
    free(remote->predicted.effects);
  }
  remote->predicted = qa_app_selected_movement_prediction_replay(remote->prediction_owner, NULL, NULL);
  remote->has_predicted = true;
}
void qa_app_q2_remote_disconnected(qa_app_q2_remote *remote, const char *reason) {
  if (remote->options.has_cinematic) remote->options.cinematic.stop(remote->options.cinematic.ctx);
  char *line = xmalloc(strlen(reason) + 2);
  snprintf(line, strlen(reason) + 2, "%s\n", reason);
  remote->options.print(remote->options.ctx, line);
  free(line);
  remote->options.disconnected(remote->options.ctx, reason);
}
void qa_app_q2_remote_print(qa_app_q2_remote *remote, const char *text) {
  remote->options.print(remote->options.ctx, text);
}
static const void *host_downloads_of(void *ctx) { return ctx; }
static void host_server_data(void *ctx, const void *data,
    void (*assert_current)(void *), void *assert_ctx) {
  qa_app_q2_remote *remote = ctx;
  qa_app_q2_remote_server_data(remote, data,
      (qa_app_q2_remote_assert_fn)assert_current, assert_ctx);
}
static const char *host_userinfo(void *ctx) {
  return qa_app_q2_remote_userinfo(ctx);
}
static void host_prediction_sent(void *ctx, double sequence, const void *command, double now) {
  qa_app_q2_remote_prediction_sent(ctx, sequence, command, now);
}
static void host_prediction_acknowledged(void *ctx, double sequence, double now) {
  qa_app_q2_remote_prediction_acknowledged(ctx, sequence, now);
}
static void host_game_state(void *ctx, qa_app_q2_game_state state) {
  qa_app_q2_remote_game_state(ctx, state);
}
static void host_frame(void *ctx, const void *frame, const void *const *records,
    size_t count, double now) {
  qa_app_q2_remote *remote = ctx;
  qa_q2_wire_frame copy = {0};
  if (!qa_q2_wire_frame_clone((const qa_q2_wire_frame *)frame, &copy)) qa_range_error("out of memory");
  const qa_q2_server_record **list = (const qa_q2_server_record **)records;
  qa_q2_server_record *flat = xmalloc(count * sizeof *flat);
  for (size_t i = 0; i < count; i++) {
    record_copy_local(list[i], &flat[i]);
  }
  qa_app_q2_remote_frame(remote, copy, flat, count, now);
  records_free_local(flat, count);
}
static void host_records(void *ctx, const void *const *records, size_t count) {
  qa_app_q2_remote *remote = ctx;
  const qa_q2_server_record **list = (const qa_q2_server_record **)records;
  qa_q2_server_record *flat = xmalloc(count * sizeof *flat);
  for (size_t i = 0; i < count; i++) {
    record_copy_local(list[i], &flat[i]);
  }
  qa_app_q2_remote_records(remote, flat, count);
  records_free_local(flat, count);
}
static const void *host_command(void *ctx, const void *actor_command) {
  qa_app_q2_remote *remote = ctx;
  static _Thread_local qa_q2_wire_usercmd wire;
  wire = qa_app_q2_remote_wire_command(remote, actor_command);
  return &wire;
}
static void host_disconnected(void *ctx, const char *reason) {
  qa_app_q2_remote_disconnected(ctx, reason);
}
static void host_print_text(void *ctx, const char *text) { qa_app_q2_remote_print(ctx, text); }
qa_app_q2_client_host qa_app_q2_remote_as_client_host(qa_app_q2_remote *remote) {
  qa_app_q2_client_host host = {0};
  host.ctx = remote;
  host.downloads = remote->downloads;
  host.protocol = remote->selected_protocol;
  host.message_options = &remote->message_options;
  qa_app_q2_remote_message_options(remote, &remote->message_options);
  host.server_data = host_server_data;
  host.userinfo = host_userinfo;
  host.prediction_sent = host_prediction_sent;
  host.prediction_acknowledged = host_prediction_acknowledged;
  host.game_state = host_game_state;
  host.frame = host_frame;
  host.records = host_records;
  host.command = host_command;
  host.disconnected = host_disconnected;
  host.print = host_print_text;
  (void)host_downloads_of;
  return host;
}
