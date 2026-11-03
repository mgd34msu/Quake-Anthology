/* Port of src/app/bootstrap/network/remote-qw.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/remote_qw.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/prediction/qw_source_state.h"
#include "qa/contracts/world.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q1/handshake.h"
#include "qa/foundation/js.h"
#include "qa/world/collision/index.h"
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
static qa_q1_extended_entity_state player_entity(qa_qw_player_state state) {
  qa_q1_extended_entity_state out = {0};
  out.base.number = state.number + 1;
  out.base.origin = state.origin;
  out.base.angles = (qa_vec3){-state.command.angles.x / 3, state.command.angles.y, 0};
  out.base.model_index = state.model_index;
  out.base.frame = state.frame;
  out.base.color_map = state.number + 1;
  out.base.skin = state.skin;
  out.base.effects = state.effects;
  out.alpha = 0;
  out.scale = 16;
  out.step = true;
  return out;
}
typedef struct userinfo_entry { double slot; qa_qw_info info; } userinfo_entry;
struct qa_app_qw_remote {
  qa_app_qw_remote_options options;
  qa_app_q1_remote *shared;
  qa_app_remote_world_content *world;
  bool has_data; qa_quakeworld_message data;
  bool has_pending_music; double pending_music_track;
  qa_app_quakeworld_prediction *predictor;
  bool has_predicted; qa_app_movement_prediction_result predicted;
  double acknowledged_sequence;
  struct { double index, value; } *stats; size_t stat_count, stat_cap;
  userinfo_entry *userinfos; size_t userinfo_count, userinfo_cap;
  struct { double slot; qa_indexed_model_skin skin; } *selected_skins;
  size_t selected_count, selected_cap;
  qa_app_qw_player_skins *player_skins;
  bool skin_loading;
  char *skin_signature, *skin_policy_signature;
  double skin_revision;
  bool has_own_player; qa_qw_player_state own_player;
  bool has_camera_players[32]; qa_qw_player_state camera_players[32];
  qa_app_qw_spectator_camera *camera;
  qa_quakeworld_message *records; size_t record_count;
  qa_q1_extended_entity_state *entities; size_t entity_count;
  char **model_names; size_t model_count;
  size_t sound_count;
  bool *available_sounds;
  qa_actor_id *linked; size_t linked_count, linked_cap;
  void *load_ctx;
  double kick;
  bool has_intermission; qa_quakeworld_message intermission;
  bool has_variables; qa_qw_move_variables variables;
  char **skin_names; size_t skin_name_count;
  qa_app_simulation_presentation *presentations; size_t presentation_cap;
  qa_body_snapshot *sample_bodies;
  qa_simulation_output sample_output; /* samplePresentation result: the shared output with this seat's body view */
};
static const qa_executable_recipe *recipe_of(qa_app_qw_remote *remote) {
  return remote->options.shared.content_recipe(remote->options.shared.ctx,
      qa_app_remote_world_loaded(remote->world));
}
static double stat_of(qa_app_qw_remote *remote, double index) {
  for (size_t i = 0; i < remote->stat_count; i++)
    if (remote->stats[i].index == index) return remote->stats[i].value;
  return 0;
}
static void stat_set(qa_app_qw_remote *remote, double index, double value) {
  for (size_t i = 0; i < remote->stat_count; i++)
    if (remote->stats[i].index == index) {
      remote->stats[i].value = value;
      return;
    }
  if (remote->stat_count == remote->stat_cap) {
    remote->stat_cap = remote->stat_cap ? remote->stat_cap * 2 : 32;
    remote->stats = realloc(remote->stats, remote->stat_cap * sizeof *remote->stats);
    if (!remote->stats) qa_range_error("out of memory");
  }
  remote->stats[remote->stat_count].index = index;
  remote->stats[remote->stat_count].value = value;
  remote->stat_count++;
}
static userinfo_entry *userinfo_of(qa_app_qw_remote *remote, double slot, bool create) {
  for (size_t i = 0; i < remote->userinfo_count; i++)
    if (remote->userinfos[i].slot == slot) return &remote->userinfos[i];
  if (!create) return NULL;
  if (remote->userinfo_count == remote->userinfo_cap) {
    remote->userinfo_cap = remote->userinfo_cap ? remote->userinfo_cap * 2 : 32;
    remote->userinfos = realloc(remote->userinfos, remote->userinfo_cap * sizeof *remote->userinfos);
    if (!remote->userinfos) qa_range_error("out of memory");
  }
  userinfo_entry *entry = &remote->userinfos[remote->userinfo_count++];
  entry->slot = slot;
  qa_qw_info_init(&entry->info);
  return entry;
}
static void predicted_clear(qa_app_qw_remote *remote) {
  if (!remote->has_predicted) return;
  qa_app_movement_prediction_snapshot_destroy(&remote->predicted.player);
  free(remote->predicted.effects);
  remote->has_predicted = false;
}
static void camera_send(void *ctx, const char *text) {
  qa_app_qw_remote *remote = ctx;
  remote->options.shared.send_command(remote->options.shared.ctx, text);
}
static qa_app_qw_camera_trace camera_trace(void *ctx, qa_vec3 start, qa_vec3 end) {
  qa_app_qw_remote *remote = ctx;
  void *scene = qa_app_q1_remote_scene(remote->shared);
  qa_app_q1_player player;
  bool has_player = qa_app_q1_remote_player(remote->shared, &player);
  return remote->options.camera_trace(remote->options.shared.ctx, scene, start, end,
      has_player, has_player ? player.actor : (qa_actor_id){0});
}
static void camera_print(void *ctx, const char *text) {
  qa_app_qw_remote *remote = ctx;
  remote->options.shared.print(remote->options.shared.ctx, text);
}
static double camera_zero(void *ctx) { (void)ctx; return 0; }
typedef struct load_ctx {
  qa_app_qw_remote *remote;
  qa_app_q1_remote_options user;
} load_ctx;
static void *shim_ctx(load_ctx *load) { return load->user.ctx; }
static void *shared_load_content(void *ctx, qa_app_q1_remote_world_info world,
    qa_app_q1_remote_assert_fn assert_current, void *assert_ctx) {
  load_ctx *load = ctx;
  void *content = load->user.load_content(shim_ctx(load), world, assert_current, assert_ctx);
  if (assert_current) assert_current(assert_ctx);
  qa_app_remote_world_set_loaded(load->remote->world, content);
  return content;
}
static double shim_next_generation(void *ctx, double slot) {
  load_ctx *load = ctx;
  return load->user.next_generation(shim_ctx(load), slot);
}
static qa_actor_id shim_identity_actor(void *ctx, double slot, double generation) {
  load_ctx *load = ctx;
  return load->user.identity_actor(shim_ctx(load), slot, generation);
}
static const qa_executable_recipe *shim_recipe(void *ctx, void *loaded) {
  load_ctx *load = ctx;
  return load->user.content_recipe(shim_ctx(load), loaded);
}
static const qa_scene_snapshot_world *shim_world(void *ctx, void *loaded) {
  load_ctx *load = ctx;
  return load->user.content_world ? load->user.content_world(shim_ctx(load), loaded) : NULL;
}
static void shim_send_command(void *ctx, const char *text) {
  load_ctx *load = ctx;
  load->user.send_command(shim_ctx(load), text);
}
static void shim_print(void *ctx, const char *text) {
  load_ctx *load = ctx;
  load->user.print(shim_ctx(load), text);
}
static void shim_publish(void *ctx, const qa_simulation_output *output) {
  load_ctx *load = ctx;
  load->user.publish(shim_ctx(load), output);
}
static void shim_disconnected(void *ctx, const char *reason) {
  load_ctx *load = ctx;
  load->user.disconnected(shim_ctx(load), reason);
}
static double shim_presentation_time(void *ctx) {
  load_ctx *load = ctx;
  return load->user.presentation_time(shim_ctx(load));
}
qa_app_qw_remote *qa_app_qw_remote_create(qa_app_qw_remote_options options) {
  qa_app_qw_remote *remote = calloc(1, sizeof *remote);
  if (!remote) return NULL;
  remote->options = options;
  remote->skin_signature = copy_text("");
  remote->skin_policy_signature = copy_text("");
  remote->world = qa_app_remote_world_content_create(options.shared.world_host, options.shared.content);
  if (!remote->world) { free(remote->skin_signature); free(remote->skin_policy_signature); free(remote); return NULL; }
  qa_app_qw_camera_options camera_options = options.has_camera_options
      ? options.camera_options : (qa_app_qw_camera_options){NULL, camera_zero, camera_zero};
  qa_app_qw_camera_host camera_host = {remote, camera_send, camera_trace, camera_print};
  remote->camera = qa_app_qw_spectator_camera_create(camera_options, camera_host);
  if (!remote->camera) {
    qa_app_remote_world_content_destroy(remote->world);
    free(remote->skin_signature);
    free(remote->skin_policy_signature);
    free(remote);
    return NULL;
  }
  remote->player_skins = qa_app_qw_player_skins_create(options.skin_options);
  if (!remote->player_skins) {
    qa_app_qw_spectator_camera_destroy(remote->camera);
    qa_app_remote_world_content_destroy(remote->world);
    free(remote->skin_signature);
    free(remote->skin_policy_signature);
    free(remote);
    return NULL;
  }
  load_ctx *load = xmalloc(sizeof *load);
  load->remote = remote;
  load->user = options.shared;
  qa_app_q1_remote_options shared_options = options.shared;
  shared_options.ctx = load;
  shared_options.load_content = shared_load_content;
  shared_options.next_generation = shim_next_generation;
  shared_options.identity_actor = shim_identity_actor;
  shared_options.content_recipe = shim_recipe;
  shared_options.content_world = options.shared.content_world ? shim_world : NULL;
  shared_options.send_command = shim_send_command;
  shared_options.print = shim_print;
  shared_options.publish = shim_publish;
  shared_options.disconnected = shim_disconnected;
  shared_options.presentation_time = options.shared.presentation_time
      ? shim_presentation_time : NULL;
  remote->shared = qa_app_q1_remote_create(shared_options);
  if (!remote->shared) {
    qa_app_qw_player_skins_destroy(remote->player_skins);
    qa_app_qw_spectator_camera_destroy(remote->camera);
    qa_app_remote_world_content_destroy(remote->world);
    free(load);
    free(remote->skin_signature);
    free(remote->skin_policy_signature);
    free(remote);
    return NULL;
  }
  remote->load_ctx = load;
  return remote;
}
void qa_app_qw_remote_destroy(qa_app_qw_remote *remote) {
  if (!remote) return;
  qa_app_q1_remote_destroy(remote->shared);
  qa_app_remote_world_content_destroy(remote->world);
  qa_app_qw_spectator_camera_destroy(remote->camera);
  qa_app_qw_player_skins_destroy(remote->player_skins);
  if (remote->predictor) qa_app_quakeworld_prediction_destroy(remote->predictor);
  predicted_clear(remote);
  if (remote->has_data) qa_quakeworld_message_destroy(&remote->data);
  if (remote->has_intermission) qa_quakeworld_message_destroy(&remote->intermission);
  free(remote->stats);
  for (size_t i = 0; i < remote->userinfo_count; i++) qa_qw_info_destroy(&remote->userinfos[i].info);
  free(remote->userinfos);
  free(remote->selected_skins);
  free(remote->skin_signature);
  free(remote->skin_policy_signature);
  for (size_t i = 0; i < remote->record_count; i++) qa_quakeworld_message_destroy(&remote->records[i]);
  free(remote->records);
  free(remote->entities);
  for (size_t i = 0; i < remote->model_count; i++) free(remote->model_names[i]);
  free(remote->model_names);
  free(remote->available_sounds);
  free(remote->linked);
  for (size_t i = 0; i < remote->skin_name_count; i++) free(remote->skin_names[i]);
  free(remote->skin_names);
  free(remote->presentations);
  free(remote->sample_bodies);
  free(remote->load_ctx);
  free(remote);
}
qa_app_q1_remote *qa_app_qw_remote_shared(qa_app_qw_remote *remote) { return remote->shared; }
bool qa_app_qw_remote_move_variables(const qa_app_qw_remote *remote, qa_qw_move_variables *out) {
  if (!remote->has_variables) return false;
  *out = remote->variables;
  return true;
}
qa_client_id qa_app_qw_remote_client(const qa_app_qw_remote *remote) {
  return qa_app_q1_remote_client(remote->shared);
}
bool qa_app_qw_remote_player(const qa_app_qw_remote *remote, qa_app_q1_player *out) {
  return qa_app_q1_remote_player(remote->shared, out);
}
const qa_simulation_output *qa_app_qw_remote_output(const qa_app_qw_remote *remote) {
  return qa_app_q1_remote_output(remote->shared);
}
void *qa_app_qw_remote_scene(qa_app_qw_remote *remote) {
  return qa_app_q1_remote_scene(remote->shared);
}
const qa_quakeworld_message *qa_app_qw_remote_source_records(const qa_app_qw_remote *remote,
    size_t *out_count) {
  *out_count = remote->record_count;
  return remote->records;
}
size_t qa_app_qw_remote_scoreboard(const qa_app_qw_remote *remote,
    const qa_app_q1_client_row **out_rows) {
  return qa_app_q1_remote_scoreboard(remote->shared, out_rows);
}
static char *policy_signature(qa_app_qw_remote *remote) {
  qa_app_qw_skin_options *policy = &remote->options.skin_options;
  const char *base = policy->baseskin ? policy->baseskin(policy->ctx) : "";
  const char *all = policy->allskins ? policy->allskins(policy->ctx) : "";
  double noskins = policy->noskins ? policy->noskins(policy->ctx) : 0;
  /* TS joins with NUL; length prefixes keep the fields equally unambiguous in a C string. */
  size_t size = strlen(base) + strlen(all) + 96;
  char *out = xmalloc(size);
  snprintf(out, size, "%s|%zu:%s|%zu:%s", QA_JS_NUMBER(noskins), strlen(base), base, strlen(all), all);
  return out;
}
void qa_app_qw_remote_prepare_skins(qa_app_qw_remote *remote,
    qa_app_qw_remote_assert_fn assert_current, void *assert_ctx) {
  if (assert_current) assert_current(assert_ctx);
  if (remote->skin_loading) return;
  char *policy = policy_signature(remote);
  size_t total = strlen(policy) + 32;
  char *signature = xmalloc(total);
  snprintf(signature, total, "%s|%s", policy, QA_JS_NUMBER(remote->skin_revision));
  if (strcmp(signature, remote->skin_signature) == 0) {
    free(policy);
    free(signature);
    return;
  }
  if (strcmp(policy, remote->skin_policy_signature) != 0) {
    qa_app_qw_player_skins_clear(remote->player_skins);
    free(remote->skin_policy_signature);
    remote->skin_policy_signature = copy_text(policy);
  }
  free(policy);
  remote->selected_count = 0;
  for (size_t i = 0; i < remote->userinfo_count; i++) {
    userinfo_entry *entry = &remote->userinfos[i];
    const char *name = qa_qw_info_get(&entry->info, "name");
    if (!name || name[0] == '\0') continue;
    const char *skin_name = qa_qw_info_get(&entry->info, "skin");
    const qa_indexed_model_skin *skin =
        qa_app_qw_player_skins_select(remote->player_skins, skin_name ? skin_name : "");
    if (assert_current) assert_current(assert_ctx);
    if (skin) {
      if (remote->selected_count == remote->selected_cap) {
        remote->selected_cap = remote->selected_cap ? remote->selected_cap * 2 : 32;
        remote->selected_skins = realloc(remote->selected_skins,
            remote->selected_cap * sizeof *remote->selected_skins);
        if (!remote->selected_skins) qa_range_error("out of memory");
      }
      remote->selected_skins[remote->selected_count].slot = entry->slot;
      remote->selected_skins[remote->selected_count].skin = *skin;
      remote->selected_count++;
    }
  }
  free(remote->skin_signature);
  remote->skin_signature = signature;
}
const char *const *qa_app_qw_remote_skin_names(qa_app_qw_remote *remote, size_t *out_count) {
  for (size_t i = 0; i < remote->skin_name_count; i++) free(remote->skin_names[i]);
  free(remote->skin_names);
  remote->skin_names = NULL;
  remote->skin_name_count = 0;
  qa_app_qw_skin_options *policy = &remote->options.skin_options;
  if (policy->noskins && policy->noskins(policy->ctx) != 0) {
    *out_count = 0;
    return NULL;
  }
  for (size_t i = 0; i < remote->userinfo_count; i++) {
    const char *name = qa_qw_info_get(&remote->userinfos[i].info, "name");
    if (!name || name[0] == '\0') continue;
    const char *skin = qa_qw_info_get(&remote->userinfos[i].info, "skin");
    char *skin_name = qa_app_qw_player_skins_name(remote->player_skins, skin ? skin : "");
    if (!skin_name) qa_range_error("out of memory");
    char *path = xmalloc(strlen(skin_name) + 12);
    snprintf(path, strlen(skin_name) + 12, "skins/%s.pcx", skin_name);
    free(skin_name);
    bool duplicate = false;
    for (size_t j = 0; j < remote->skin_name_count; j++)
      if (strcmp(remote->skin_names[j], path) == 0) duplicate = true;
    if (duplicate) { free(path); continue; }
    remote->skin_names = realloc(remote->skin_names,
        (remote->skin_name_count + 1) * sizeof *remote->skin_names);
    if (!remote->skin_names) qa_range_error("out of memory");
    remote->skin_names[remote->skin_name_count++] = path;
  }
  *out_count = remote->skin_name_count;
  return (const char *const *)remote->skin_names;
}
void qa_app_qw_remote_skins_loading(qa_app_qw_remote *remote, bool value) {
  remote->skin_loading = value;
  remote->selected_count = 0;
  if (!value) {
    qa_app_qw_player_skins_clear(remote->player_skins);
    free(remote->skin_signature);
    remote->skin_signature = copy_text("");
  }
}
void qa_app_qw_remote_skins_prepare(qa_app_qw_remote *remote) {
  qa_app_qw_remote_prepare_skins(remote, NULL, NULL);
}
bool qa_app_qw_remote_take_spectator_teleport(qa_app_qw_remote *remote, qa_vec3 *out) {
  return qa_app_qw_spectator_camera_take_teleport(remote->camera, out);
}
static void message_copy(const qa_quakeworld_message *in, qa_quakeworld_message *out) {
  *out = *in;
  out->text = in->text ? copy_text(in->text) : NULL;
  out->str_value = in->str_value ? copy_text(in->str_value) : NULL;
  out->key = in->key ? copy_text(in->key) : NULL;
  out->game_directory = in->game_directory ? copy_text(in->game_directory) : NULL;
  out->level_name = in->level_name ? copy_text(in->level_name) : NULL;
  out->projectiles = xmalloc(in->projectile_count * sizeof *out->projectiles);
  if (in->projectile_count)
    memcpy(out->projectiles, in->projectiles, in->projectile_count * sizeof *out->projectiles);
  out->names = xmalloc(in->name_count * sizeof *out->names);
  for (size_t i = 0; i < in->name_count; i++) out->names[i] = copy_text(in->names[i]);
  out->entities = xmalloc(in->entity_count * sizeof *out->entities);
  if (in->entity_count)
    memcpy(out->entities, in->entities, in->entity_count * sizeof *out->entities);
  out->download_bytes = NULL;
  if (in->download_bytes) {
    out->download_bytes = xmalloc(in->download_length);
    memcpy(out->download_bytes, in->download_bytes, in->download_length);
  }
}
void qa_app_qw_remote_server_data(qa_app_qw_remote *remote, const qa_quakeworld_message *data) {
  if (remote->has_data) {
    qa_quakeworld_message_destroy(&remote->data);
    remote->has_data = false;
  }
  remote->has_pending_music = false;
  remote->options.prepare_server_data(remote->options.shared.ctx, data);
}
double qa_app_qw_remote_game_state(qa_app_qw_remote *remote, const qa_quakeworld_message *data,
    const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count,
    qa_app_qw_remote_assert_fn assert_current, void *assert_ctx) {
  if (assert_current) assert_current(assert_ctx);
  qa_app_qw_spectator_camera_reset(remote->camera);
  memset(remote->has_camera_players, 0, sizeof remote->has_camera_players);
  qa_app_qw_player_skins_clear(remote->player_skins);
  for (size_t i = 0; i < remote->userinfo_count; i++) qa_qw_info_destroy(&remote->userinfos[i].info);
  remote->userinfo_count = 0;
  remote->selected_count = 0;
  free(remote->skin_signature);
  remote->skin_signature = copy_text("");
  remote->skin_loading = false;
  remote->skin_revision++;
  if (remote->predictor) {
    qa_app_quakeworld_prediction_destroy(remote->predictor);
    remote->predictor = NULL;
  }
  predicted_clear(remote);
  for (size_t i = 0; i < remote->model_count; i++) free(remote->model_names[i]);
  free(remote->model_names);
  remote->model_names = xmalloc(model_count * sizeof *remote->model_names);
  remote->model_count = model_count;
  for (size_t i = 0; i < model_count; i++) remote->model_names[i] = copy_text(models[i]);
  remote->linked_count = 0;
  if (remote->has_data) qa_quakeworld_message_destroy(&remote->data);
  message_copy(data, &remote->data);
  remote->has_data = true;
  remote->variables = data->move_variables;
  remote->has_variables = true;
  remote->stat_count = 0;
  remote->has_own_player = false;
  free(remote->entities);
  remote->entities = NULL;
  remote->entity_count = 0;
  remote->kick = 0;
  if (remote->has_intermission) {
    qa_quakeworld_message_destroy(&remote->intermission);
    remote->has_intermission = false;
  }
  if (model_count == 0) qa_raise("Error", 0, 0, "QW has no world model");
  const char *map = models[0];
  qa_netquake_message server_info = {0};
  server_info.kind = "server-info";
  server_info.protocol = (qa_q1_protocol_identity){"q1-netquake", 15, 0, 0};
  server_info.max_clients = 32;
  server_info.game_type = 1;
  server_info.level = data->level_name;
  server_info.models = (char **)models;
  server_info.model_count = model_count;
  server_info.sounds = (char **)sounds;
  server_info.sound_count = sound_count;
  qa_netquake_message set_view = {0};
  set_view.kind = "set-view";
  set_view.entity = data->player_slot + 1;
  qa_netquake_message initial[2] = {server_info, set_view};
  qa_app_q1_remote_receive(remote->shared, initial, 2, 0,
      (qa_app_q1_remote_assert_fn)assert_current, assert_ctx);
  if (remote->has_pending_music) {
    qa_netquake_message music = {0};
    music.kind = "cd-track";
    music.track = remote->pending_music_track;
    music.loop_track = remote->pending_music_track;
    qa_app_q1_remote_receive(remote->shared, &music, 1, 0, NULL, NULL);
    remote->has_pending_music = false;
  }
  remote->sound_count = sound_count;
  free(remote->available_sounds);
  remote->available_sounds = calloc(sound_count + 1, sizeof(bool));
  if (!remote->available_sounds) qa_range_error("out of memory");
  void *loaded = qa_app_remote_world_loaded(remote->world);
  for (size_t i = 0; i < sound_count; i++) {
    char *path = xmalloc(strlen(sounds[i]) + 7);
    snprintf(path, strlen(sounds[i]) + 7, "sound/%s", sounds[i]);
    if (remote->options.resolve_sound
        && remote->options.resolve_sound(remote->options.shared.ctx, loaded, path))
      remote->available_sounds[i + 1] = true;
    free(path);
    if (assert_current) assert_current(assert_ctx);
  }
  if (assert_current) assert_current(assert_ctx);
  qa_app_q1_remote_world_info world = {map, models, model_count, sounds, sound_count};
  return remote->options.map_checksum(remote->options.shared.ctx, world,
      data->game_directory ? data->game_directory : "");
}
typedef struct translated_list {
  qa_netquake_message *items; size_t count, cap;
} translated_list;
static void translated_free(void *arg) {
  translated_list *list = arg;
  for (size_t i = 0; i < list->count; i++) qa_netquake_message_destroy(&list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}
static void translated_push(translated_list *list, qa_netquake_message message) {
  if (list->count == list->cap) {
    list->cap = list->cap ? list->cap * 2 : 32;
    list->items = realloc(list->items, list->cap * sizeof *list->items);
    if (!list->items) qa_range_error("out of memory");
  }
  list->items[list->count++] = message;
}
static void scoreboard_info(qa_app_qw_remote *remote, double slot, qa_qw_info *info,
    translated_list *translated) {
  if (slot != trunc(slot) || slot < 0 || slot >= 32) qa_raise("Error", 0, 0, "Invalid QW userinfo slot");
  userinfo_entry *entry = userinfo_of(remote, slot, true);
  qa_qw_info_destroy(&entry->info);
  entry->info = *info;
  remote->skin_revision++;
  const char *name = qa_qw_info_get(info, "name");
  qa_netquake_message name_message = {0};
  name_message.kind = "name";
  name_message.slot = slot;
  char trimmed[16] = "";
  snprintf(trimmed, sizeof trimmed, "%s", name ? name : "");
  name_message.str_value = copy_text(trimmed);
  name_message.value = 0;
  translated_push(translated, name_message);
  const char *top = qa_qw_info_get(info, "topcolor");
  const char *bottom = qa_qw_info_get(info, "bottomcolor");
  double top_color = qa_native_atoi(top ? top : "0");
  double bottom_color = qa_native_atoi(bottom ? bottom : "0");
  if (top_color < 0 || top_color > 13) top_color = 13;
  if (bottom_color < 0 || bottom_color > 13) bottom_color = 13;
  qa_netquake_message colors = {0};
  colors.kind = "colors";
  colors.slot = slot;
  colors.value = top_color * 16 + bottom_color;
  translated_push(translated, colors);
}
static void link_solids(qa_app_qw_remote *remote, const qa_qw_player_state *players,
    size_t player_count, const qa_q1_extended_entity_state *nails, size_t nail_count) {
  if (!remote->options.scene_link) return;
  void *scene = qa_app_q1_remote_scene(remote->shared);
  for (size_t i = 0; i < remote->linked_count; i++)
    remote->options.scene_unlink(remote->options.shared.ctx, scene, remote->linked[i]);
  remote->linked_count = 0;
  const qa_simulation_output *output = qa_app_q1_remote_output(remote->shared);
  if (!output) return;
  size_t total = remote->entity_count + nail_count + player_count;
  qa_q1_extended_entity_state *states = xmalloc(total * sizeof *states);
  size_t state_count = 0;
  for (size_t i = 0; i < remote->entity_count; i++) states[state_count++] = remote->entities[i];
  for (size_t i = 0; i < nail_count; i++) states[state_count++] = nails[i];
  for (size_t i = 0; i < player_count; i++) states[state_count++] = player_entity(players[i]);
  /* new Map(entries): a repeated number keeps its first position and takes the last value. */
  size_t unique_count = 0;
  for (size_t i = 0; i < state_count; i++) {
    size_t at = unique_count;
    for (size_t j = 0; j < unique_count; j++)
      if (states[j].base.number == states[i].base.number) at = j;
    states[at] = states[i];
    if (at == unique_count) unique_count++;
  }
  for (size_t index = 0; index < unique_count; index++) {
    if (index >= output->snapshot.body_count) break;
    const qa_body_snapshot *body = &output->snapshot.bodies[index];
    qa_q1_extended_entity_state *state = &states[index];
    if (remote->has_data && state->base.number == remote->data.player_slot + 1) continue;
    const qa_qw_player_state *player = NULL;
    for (size_t i = 0; i < player_count; i++)
      if (players[i].number + 1 == state->base.number) player = &players[i];
    int64_t model_index = (int64_t)state->base.model_index - 1;
    const char *path = model_index >= 0 && (size_t)model_index < remote->model_count
        ? remote->model_names[model_index] : NULL;
    bool has_model = false;
    double model = 0;
    if (path && path[0] == '*') {
      model = qa_js_string_to_number(path + 1); /* Number(path.slice(1)); "*" is 0 */
      has_model = isfinite(model) && model == trunc(model); /* Number.isInteger */
    }
    if (!player && !has_model) continue;
    if (player && (qa_js_to_int32(player->flags) & 512) != 0) continue;
    qa_bounds bounds = has_model
        ? remote->options.scene_model_bounds(remote->options.shared.ctx, scene, model)
        : body->body.bounds;
    qa_vec3 origin = state->base.origin;
    qa_linked_body linked = {0};
    linked.actor = body->actor;
    linked.state = body->body;
    linked.state.origin = origin;
    linked.state.angles = (qa_vec3){0, 0, 0};
    linked.state.bounds = bounds;
    linked.link_count = output->snapshot.frame.frame;
    linked.absolute_bounds = (qa_bounds){
        {origin.x + bounds.min.x - 1, origin.y + bounds.min.y - 1, origin.z + bounds.min.z - 1},
        {origin.x + bounds.max.x + 1, origin.y + bounds.max.y + 1, origin.z + bounds.max.z + 1}};
    qa_actor_collision collision = {0};
    collision.family = "q1";
    collision.shape_kind = has_model ? "model" : "box";
    collision.shape_model = model;
    collision.contents = -2;
    collision.role = "solid";
    collision.monster = player != NULL;
    remote->options.scene_link(remote->options.shared.ctx, scene, linked, collision);
    if (remote->linked_count == remote->linked_cap) {
      remote->linked_cap = remote->linked_cap ? remote->linked_cap * 2 : 32;
      remote->linked = realloc(remote->linked, remote->linked_cap * sizeof *remote->linked);
      if (!remote->linked) qa_range_error("out of memory");
    }
    remote->linked[remote->linked_count++] = body->actor;
  }
  free(states);
}
static qa_app_qw_move_variables move_variables_of(qa_qw_move_variables variables) {
  qa_app_qw_move_variables out = {0};
  out.gravity = variables.gravity;
  out.stop_speed = variables.stop_speed;
  out.max_speed = variables.max_speed;
  out.spectator_max_speed = variables.spectator_max_speed;
  out.accelerate = variables.accelerate;
  out.air_accelerate = variables.air_accelerate;
  out.water_accelerate = variables.water_accelerate;
  out.friction = variables.friction;
  out.water_friction = variables.water_friction;
  out.entity_gravity = variables.entity_gravity;
  return out;
}
/* TS isBrush: hit.kind === "world" || (hit.kind === "actor" && this.scene.spatial.get(hit.actor)?.collision.shape.kind
 * === "model"). The scene is the shared Q1 scene (qa_shared_scene_queries). */
static bool is_brush_hit(void *ctx, qa_trace_hit hit) {
  qa_app_qw_remote *remote = ctx;
  if (hit.kind == QA_TRACE_HIT_WORLD) return true;
  if (hit.kind != QA_TRACE_HIT_ACTOR) return false;
  qa_spatial_actor linked;
  if (!qa_shared_linked_actor(qa_app_q1_remote_scene(remote->shared), hit.actor, &linked)) return false;
  return linked.collision.shape_kind != NULL && strcmp(linked.collision.shape_kind, "model") == 0;
}
static void receive_prediction(qa_app_qw_remote *remote, double now) {
  if (!remote->has_own_player || !remote->has_variables || !remote->has_data) return;
  qa_app_q1_player player;
  if (!qa_app_q1_remote_player(remote->shared, &player)) return;
  if (!qa_app_q1_remote_output(remote->shared)) return;
  const qa_executable_recipe *recipe = recipe_of(remote);
  qa_app_sim_player_view view = qa_app_q1_remote_player_view(remote->shared, player.actor);
  qa_app_sim_player_ui ui = qa_app_q1_remote_player_ui(remote->shared, player.actor);
  qa_bounds bounds = {{-16, -16, -24}, {16, 16, 32}};
  qa_app_movement_prediction_snapshot base = {0};
  base.sequence = remote->acknowledged_sequence;
  base.command_time_milliseconds = now;
  base.state.kind = QA_MOVEMENT_STATE_QW;
  base.state.as.qw.origin = remote->own_player.origin;
  base.state.as.qw.velocity = remote->own_player.velocity;
  base.state.as.qw.angles = view.angles;
  base.state.as.qw.dead = ui.health <= 0;
  base.state.as.qw.spectator = remote->data.spectator ? 1 : 0;
  base.view_angles = view.angles;
  base.view_height = view.view_height;
  base.view_offset = (qa_vec3){0, 0, view.view_height};
  base.bounds = bounds;
  base.environment.health = ui.health;
  base.environment.gravity_multiplier = remote->variables.entity_gravity;
  base.arsenal.provider = recipe->inventory.provider;
  base.arsenal.active_weapon = ui.active_weapon;
  base.arsenal.ammo = ui.inventory;
  base.arsenal.ammo_count = ui.inventory_count;
  base.arsenal.state.kind = "q1";
  base.arsenal.state.frame = remote->own_player.weapon_frame;
  base.arsenal.state.source_weapon = stat_of(remote, 10);
  base.animation.provider = recipe->character.definition.provider;
  base.animation.state.kind = "q1";
  base.animation.state.frame = remote->own_player.frame;
  qa_app_qw_move_variables variables = move_variables_of(remote->variables);
  if (!remote->predictor) {
    qa_app_movement_prediction_options options = {0};
    options.probe.actor = remote->options.owned_actor(remote->options.shared.ctx,
        player.actor, &recipe->map.entities);
    options.seat = remote->options.seat;
    options.probe.recipe = recipe;
    options.probe.profile = qa_app_sim_movement_profile(*recipe);
    options.probe.standing_bounds = bounds;
    options.probe.standing_view_height = 22;
    options.probe.scene = qa_shared_as_scene_queries(qa_app_q1_remote_scene(remote->shared)); /* TS scene: this.scene */
    options.probe.host_ctx = remote;
    options.probe.is_brush = is_brush_hit;
    remote->predictor = qa_app_quakeworld_prediction_create(options, base, variables);
    if (!remote->predictor) qa_range_error("out of memory");
  }
  qa_app_qw_prediction_status status = {ui.health, remote->data.spectator ? 1 : 0};
  qa_app_quakeworld_prediction_receive(remote->predictor, base, remote->own_player, variables, status);
  predicted_clear(remote);
  remote->predicted = qa_app_quakeworld_prediction_replay(remote->predictor);
  remote->has_predicted = true;
}
void qa_app_qw_remote_receive(qa_app_qw_remote *remote,
    const qa_quakeworld_message *messages, size_t count, double now,
    qa_app_qw_remote_assert_fn assert_current, void *assert_ctx) {
  if (assert_current) assert_current(assert_ctx);
  for (size_t i = 0; i < remote->record_count; i++) qa_quakeworld_message_destroy(&remote->records[i]);
  free(remote->records);
  remote->records = xmalloc(count * sizeof *remote->records);
  remote->record_count = count;
  for (size_t i = 0; i < count; i++) message_copy(&messages[i], &remote->records[i]);
  if (!remote->has_data) {
    for (size_t i = 0; i < count; i++)
      if (messages[i].kind && strcmp(messages[i].kind, "cd-track") == 0) {
        remote->pending_music_track = messages[i].track;
        remote->has_pending_music = true;
      }
    return;
  }
  translated_list translated = {0};
  qa_cleanup translated_guard;
  qa_cleanup_push(&translated_guard, translated_free, &translated);
  qa_qw_player_state *players = xmalloc(count * sizeof *players);
  size_t player_count = 0;
  qa_q1_extended_entity_state *nails = xmalloc((count * 64 + 1) * sizeof *nails);
  size_t nail_count = 0, nail_cap = count * 64 + 1;
  bool frame = false;
  for (size_t i = 0; i < count; i++) {
    const qa_quakeworld_message *message = &messages[i];
    const char *kind = message->kind ? message->kind : "";
    if (strcmp(kind, "player") == 0) {
      players[player_count++] = message->player;
      int64_t slot = (int64_t)message->player.number;
      if (slot >= 0 && slot < 32) {
        remote->camera_players[slot] = message->player;
        remote->has_camera_players[slot] = true;
      }
      if (message->player.number == remote->data.player_slot) {
        remote->own_player = message->player;
        remote->has_own_player = true;
      }
    } else if (strcmp(kind, "packet-entities") == 0) {
      free(remote->entities);
      remote->entities = xmalloc(message->entity_count * sizeof *remote->entities);
      remote->entity_count = message->entity_count;
      for (size_t j = 0; j < message->entity_count; j++) {
        qa_nq_wire_entity_from_extended(&message->entities[j].state,
            (qa_q1_wire_entity_state *)&remote->entities[j]);
        remote->entities[j] = message->entities[j].state;
      }
      frame = true;
    } else if (strcmp(kind, "nails") == 0) {
      int64_t model_index = 0;
      for (size_t j = 0; j < remote->model_count; j++)
        if (strcmp(remote->model_names[j], "progs/spike.mdl") == 0) model_index = (int64_t)j + 1;
      if (model_index != 0)
        for (size_t j = 0; j < message->projectile_count; j++) {
          if (nail_count == nail_cap) {
            nail_cap *= 2;
            nails = realloc(nails, nail_cap * sizeof *nails);
            if (!nails) qa_range_error("out of memory");
          }
          qa_q1_extended_entity_state *nail = &nails[nail_count++];
          memset(nail, 0, sizeof *nail);
          nail->base.number = 131072 + (double)j;
          nail->base.origin = message->projectiles[j].origin;
          nail->base.angles = (qa_vec3){message->projectiles[j].pitch, message->projectiles[j].yaw, 0};
          nail->base.model_index = (double)model_index;
          nail->scale = 16;
          nail->step = true;
        }
    } else if (strcmp(kind, "stat") == 0) {
      stat_set(remote, message->index, message->value);
    } else if (strcmp(kind, "kick") == 0) {
      remote->kick = message->degrees;
    } else if (strcmp(kind, "max-speed") == 0) {
      if (remote->has_variables) remote->variables.max_speed = message->value;
    } else if (strcmp(kind, "entity-gravity") == 0) {
      if (remote->has_variables) remote->variables.entity_gravity = message->value;
    } else if (strcmp(kind, "userinfo") == 0) {
      qa_qw_info info;
      qa_qw_info_init(&info);
      qa_quakeworld_info_parse(message->str_value ? message->str_value : "", &info);
      scoreboard_info(remote, message->slot, &info, &translated);
    } else if (strcmp(kind, "set-info") == 0) {
      userinfo_entry *existing = userinfo_of(remote, message->slot, false);
      /* Native info strings are at most 512 bytes; bound keys and values so a flood stays linear. */
      const char *set_key = message->key ? message->key : "";
      bool known = existing && qa_qw_info_get(&existing->info, set_key) != NULL;
      if (strlen(set_key) > 63 || strlen(message->str_value ? message->str_value : "") > 511
          || (!known && (existing ? existing->info.count : 0) >= 64))
        qa_raise("Error", 0, 0, "QuakeWorld set-info exceeds its limits");
      qa_qw_info info;
      qa_qw_info_init(&info);
      if (existing)
        for (size_t j = 0; j < existing->info.count; j++)
          qa_qw_info_set(&info, existing->info.entries[j].key, existing->info.entries[j].value);
      qa_qw_info_set(&info, message->key ? message->key : "", message->str_value ? message->str_value : "");
      scoreboard_info(remote, message->slot, &info, &translated);
    } else if (strcmp(kind, "print") == 0) {
      qa_netquake_message print = {0};
      print.kind = "print";
      print.text = copy_text(message->text ? message->text : "");
      translated_push(&translated, print);
    } else if (strcmp(kind, "intermission") == 0) {
      if (remote->has_intermission) qa_quakeworld_message_destroy(&remote->intermission);
      message_copy(message, &remote->intermission);
      remote->has_intermission = true;
      qa_netquake_message set_angle = {0};
      set_angle.kind = "set-angle";
      set_angle.angles = message->angles;
      translated_push(&translated, set_angle);
      qa_netquake_message intermission = {0};
      intermission.kind = "intermission";
      translated_push(&translated, intermission);
    } else if (strcmp(kind, "cd-track") == 0) {
      qa_netquake_message music = {0};
      music.kind = "cd-track";
      music.track = message->track;
      music.loop_track = message->track;
      translated_push(&translated, music);
    } else if (strcmp(kind, "sound") == 0 || strcmp(kind, "static-sound") == 0) {
      if (message->index < 1 || message->index > (double)remote->sound_count)
        qa_raise("Error", 0, 0, "Invalid QW sound index");
      if (remote->available_sounds[(size_t)message->index]) {
        qa_netquake_message sound = {0};
        sound.kind = kind;
        sound.index = message->index;
        sound.entity = message->entity;
        sound.channel = message->channel;
        sound.origin = message->origin;
        sound.volume = message->volume;
        sound.attenuation = message->attenuation;
        translated_push(&translated, sound);
      }
    } else if (strcmp(kind, "set-view") == 0) {
      qa_netquake_message set_view = {0};
      set_view.kind = "set-view";
      set_view.entity = message->entity;
      translated_push(&translated, set_view);
    } else if (strcmp(kind, "frags") == 0) {
      qa_netquake_message frags = {0};
      frags.kind = "frags";
      frags.slot = message->slot;
      frags.value = message->value;
      translated_push(&translated, frags);
    } else if (strcmp(kind, "baseline") == 0 || strcmp(kind, "static") == 0) {
      qa_netquake_message state = {0};
      state.kind = kind;
      state.state = message->state;
      translated_push(&translated, state);
    } else if (strcmp(kind, "set-angle") == 0 || strcmp(kind, "light-style") == 0
        || strcmp(kind, "stop-sound") == 0 || strcmp(kind, "damage") == 0
        || strcmp(kind, "temporary-entity") == 0 || strcmp(kind, "pause") == 0
        || strcmp(kind, "center-print") == 0 || strcmp(kind, "finale") == 0) {
      qa_netquake_message copy = {0};
      copy.kind = kind;
      copy.text = message->text ? copy_text(message->text) : NULL;
      /* light-style carries its pattern in value (str_value) on both wires. */
      copy.str_value = message->str_value ? copy_text(message->str_value) : NULL;
      copy.source = message->source;
      copy.index = message->index;
      copy.value = message->value;
      copy.entity = message->entity;
      copy.angles = message->angles;
      copy.origin = message->origin;
      copy.direction = message->source;
      copy.channel = message->channel;
      copy.volume = message->volume;
      copy.attenuation = message->attenuation;
      copy.armor = message->armor;
      copy.blood = message->blood;
      copy.count = message->count;
      copy.effect = message->effect;
      copy.paused = message->paused;
      translated_push(&translated, copy);
    }
  }
  if (frame && remote->has_own_player) {
    translated_list with_frame = {0};
    qa_netquake_message time_message = {0};
    time_message.kind = "time";
    time_message.seconds = now / 1000;
    translated_push(&with_frame, time_message);
    for (size_t i = 0; i < translated.count; i++) translated_push(&with_frame, translated.items[i]);
    free(translated.items);
    translated = with_frame;
    for (size_t i = 0; i < remote->entity_count; i++) {
      qa_netquake_message entity = {0};
      entity.kind = "entity";
      entity.state = remote->entities[i];
      entity.state.step = true;
      translated_push(&translated, entity);
    }
    for (size_t i = 0; i < nail_count; i++) {
      qa_netquake_message entity = {0};
      entity.kind = "entity";
      entity.state = nails[i];
      translated_push(&translated, entity);
    }
    for (size_t i = 0; i < player_count; i++) {
      qa_netquake_message entity = {0};
      entity.kind = "entity";
      entity.state = player_entity(players[i]);
      translated_push(&translated, entity);
    }
    qa_netquake_message client_data = {0};
    client_data.kind = "client-data";
    client_data.data.view_height = (qa_js_to_int32(remote->own_player.flags) & 1024) != 0 ? 8
        : (qa_js_to_int32(remote->own_player.flags) & 512) != 0 ? -16 : 22;
    client_data.data.punch_angles = (qa_vec3){remote->kick, 0, 0};
    client_data.data.velocity = remote->own_player.velocity;
    client_data.data.items = stat_of(remote, 15);
    client_data.data.weapon_frame = remote->own_player.weapon_frame;
    client_data.data.armor = stat_of(remote, 4);
    client_data.data.weapon_model = stat_of(remote, 2);
    client_data.data.health = stat_of(remote, 0);
    client_data.data.ammo = stat_of(remote, 3);
    client_data.data.shells = stat_of(remote, 6);
    client_data.data.nails = stat_of(remote, 7);
    client_data.data.rockets = stat_of(remote, 8);
    client_data.data.cells = stat_of(remote, 9);
    client_data.data.active_weapon = stat_of(remote, 10);
    translated_push(&translated, client_data);
    remote->kick = 0;
  }
  qa_app_q1_remote_receive(remote->shared, translated.items, translated.count, now,
      (qa_app_q1_remote_assert_fn)assert_current, assert_ctx);
  qa_cleanup_pop(&translated_guard);
  translated_free(&translated);
  if (assert_current) assert_current(assert_ctx);
  qa_app_qw_remote_prepare_skins(remote, assert_current, assert_ctx);
  if (assert_current) assert_current(assert_ctx);
  if (frame) link_solids(remote, players, player_count, nails, nail_count);
  bool has_own = false;
  for (size_t i = 0; i < player_count; i++)
    if (players[i].number == remote->data.player_slot) has_own = true;
  if (has_own) receive_prediction(remote, now);
  free(players);
  free(nails);
}
qa_qw_user_command qa_app_qw_remote_command(qa_app_qw_remote *remote,
    const qa_actor_command *input) {
  if (input->command.kind != QA_USER_COMMAND_QW) qa_raise("Error", 0, 0, "QW requires QuakeWorld input");
  qa_qw_user_command command = input->command.as.qw;
  if (remote->has_data && remote->data.spectator && remote->has_own_player && !remote->has_intermission) {
    qa_app_qw_camera_frame frame = {0};
    frame.self = remote->own_player;
    for (size_t i = 0; i < 32; i++) {
      frame.has_player[i] = remote->has_camera_players[i];
      frame.players[i] = remote->camera_players[i];
    }
    for (size_t i = 0; i < remote->userinfo_count; i++) {
      int64_t slot = (int64_t)remote->userinfos[i].slot;
      if (slot < 0 || slot >= 32) continue;
      frame.has_user[slot] = true;
      const char *name = qa_qw_info_get(&remote->userinfos[i].info, "name");
      const char *spectator = qa_qw_info_get(&remote->userinfos[i].info, "*spectator");
      frame.users[slot].name = name ? name : "";
      frame.users[slot].spectator = spectator && spectator[0] != '\0';
      frame.users[slot].frags = 0;
    }
    const qa_app_q1_client_row *rows = NULL;
    size_t row_count = qa_app_q1_remote_scoreboard(remote->shared, &rows);
    for (size_t i = 0; i < row_count; i++) {
      int64_t slot = (int64_t)rows[i].slot;
      if (slot >= 0 && slot < 32) frame.users[slot].frags = rows[i].frags;
    }
    double now = remote->options.now_milliseconds
        ? remote->options.now_milliseconds(remote->options.shared.ctx) : 0;
    frame.seconds = now / 1000;
    qa_app_qw_camera_view previous;
    bool has_previous = qa_app_qw_spectator_camera_view(remote->camera, &previous);
    command = qa_app_qw_spectator_camera_command(remote->camera, command, &frame);
    qa_app_qw_camera_view camera;
    bool has_camera = qa_app_qw_spectator_camera_view(remote->camera, &camera);
    if (!has_camera && has_previous) {
      camera = previous;
      has_camera = true;
    }
    if (has_camera) {
      remote->own_player.origin = camera.origin;
      if (camera.chase) remote->own_player.weapon_frame = camera.target.weapon_frame;
      receive_prediction(remote, now);
    }
  }
  qa_actor_command translated = *input;
  translated.command.kind = QA_USER_COMMAND_Q1;
  memset(&translated.command.as.q1, 0, sizeof translated.command.as.q1);
  translated.command.as.q1.view_angles = command.angles;
  translated.command.as.q1.forward_move = command.forward_move;
  translated.command.as.q1.side_move = command.side_move;
  translated.command.as.q1.up_move = command.up_move;
  translated.command.as.q1.buttons = command.buttons;
  translated.command.as.q1.impulse = command.impulse;
  if (input->arsenal) {
    qa_arsenal_intent intent = *input->arsenal;
    translated.arsenal = &intent;
    qa_q1_user_command selected = qa_app_q1_remote_command(remote->shared, &translated);
    command.impulse = selected.impulse;
  } else {
    translated.arsenal = NULL;
    qa_q1_user_command selected = qa_app_q1_remote_command(remote->shared, &translated);
    command.impulse = selected.impulse;
  }
  return command;
}
bool qa_app_qw_remote_is_player(qa_app_qw_remote *remote, qa_actor_id actor) {
  return qa_app_q1_remote_is_player(remote->shared, actor);
}
const qa_world_text *qa_app_qw_remote_world_text(qa_app_qw_remote *remote, size_t *out_count) {
  return qa_app_q1_remote_world_text(remote->shared, out_count);
}
qa_app_sim_player_ui qa_app_qw_remote_player_ui(qa_app_qw_remote *remote, qa_actor_id actor) {
  return qa_app_q1_remote_player_ui(remote->shared, actor);
}
qa_app_sim_player_view qa_app_qw_remote_player_view(qa_app_qw_remote *remote, qa_actor_id actor) {
  qa_app_sim_player_view view = qa_app_q1_remote_player_view(remote->shared, actor);
  if (remote->has_intermission) {
    view.origin = remote->intermission.origin;
    view.angles = remote->intermission.angles;
    view.view_height = 0;
    view.has_kick_angles = true;
    view.kick_angles = (qa_vec3){0, 0, 0};
    view.has_pitch_drift = true;
    view.pitch_drift_grounded = false;
    view.pitch_drift_ideal_pitch = 0;
    view.pitch_drift_disabled = true;
    return view;
  }
  qa_app_qw_camera_view camera;
  bool has_camera = qa_app_qw_spectator_camera_view(remote->camera, &camera);
  if (has_camera && remote->has_data && remote->data.spectator) {
    view.origin = camera.origin;
    view.angles = camera.angles;
    view.view_height = (qa_js_to_int32(camera.target.flags) & 512) != 0 && camera.chase ? -16 : 22;
    view.has_kick_angles = true;
    view.kick_angles = (qa_vec3){0, 0, 0};
    view.has_pitch_drift = true;
    view.pitch_drift_grounded = false;
    view.pitch_drift_ideal_pitch = 0;
    view.pitch_drift_disabled = true;
    return view;
  }
  bool usable = remote->has_predicted && remote->predicted.status
      && strcmp(remote->predicted.status, "history-exhausted") != 0
      && remote->predicted.player.state.kind == QA_MOVEMENT_STATE_QW;
  if (usable) view.origin = remote->predicted.player.state.as.qw.origin;
  view.has_pitch_drift = true;
  view.pitch_drift_grounded = usable
      && remote->predicted.player.state.as.qw.ground.kind != QA_TRACE_HIT_NONE;
  view.pitch_drift_ideal_pitch = 0;
  view.pitch_drift_disabled = view.pitch_drift_disabled
      || (remote->has_data && remote->data.spectator);
  if (remote->has_own_player && (qa_js_to_int32(remote->own_player.flags) & 512) != 0)
    view.angles.z = 80;
  return view;
}
void qa_app_qw_remote_player_command(qa_app_qw_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count) {
  qa_app_q1_remote_player_command(remote->shared, actor, name, args, count);
}
const void *qa_app_qw_remote_character_views(qa_app_qw_remote *remote, size_t *out_count) {
  return qa_app_q1_remote_character_views(remote->shared, out_count);
}
const qa_app_simulation_presentation *qa_app_qw_remote_presentations(qa_app_qw_remote *remote,
    size_t *out_count) {
  size_t base_count = 0;
  const qa_app_simulation_presentation *base =
      qa_app_q1_remote_presentations(remote->shared, &base_count);
  if (remote->presentation_cap < base_count) {
    remote->presentations = realloc(remote->presentations, base_count * sizeof *remote->presentations);
    if (!remote->presentations) qa_range_error("out of memory");
    remote->presentation_cap = base_count;
  }
  size_t count = 0;
  for (size_t i = 0; i < base_count; i++) {
    qa_app_simulation_presentation item = base[i];
    double slot = 0;
    bool has_slot = qa_app_q1_remote_player_slot(remote->shared, item.actor, &slot);
    if (!remote->skin_loading && has_slot && item.path
        && strcmp(item.path, "progs/player.mdl") == 0) {
      for (size_t j = 0; j < remote->selected_count; j++)
        if (remote->selected_skins[j].slot == slot) {
          item.has_indexed_skin = true;
          item.indexed_skin = remote->selected_skins[j].skin;
        }
    }
    remote->presentations[count++] = item;
  }
  if (remote->has_intermission) {
    size_t kept = 0;
    for (size_t i = 0; i < count; i++)
      if (!remote->presentations[i].view_weapon) remote->presentations[kept++] = remote->presentations[i];
    *out_count = kept;
    return remote->presentations;
  }
  qa_app_q1_player player;
  if (!qa_app_q1_remote_player(remote->shared, &player)) {
    *out_count = count;
    return remote->presentations;
  }
  qa_app_sim_player_view view = qa_app_qw_remote_player_view(remote, player.actor);
  qa_app_qw_camera_view camera;
  bool has_camera = qa_app_qw_spectator_camera_view(remote->camera, &camera);
  for (size_t i = 0; i < count; i++) {
    qa_app_simulation_presentation *item = &remote->presentations[i];
    if (item->view_weapon) {
      item->origin = (qa_vec3){view.origin.x, view.origin.y, view.origin.z + view.view_height};
      item->angles = view.angles;
      if (has_camera && camera.chase) {
        item->frame = camera.target.weapon_frame;
        item->old_frame = camera.target.weapon_frame;
      }
      item->visible = item->visible
          && (!(remote->has_data && remote->data.spectator) || (has_camera && camera.chase));
    } else if (has_camera && camera.chase) {
      double slot = 0;
      if (qa_app_q1_remote_player_slot(remote->shared, item->actor, &slot)
          && slot == camera.target.number) item->visible = false;
    }
  }
  *out_count = count;
  return remote->presentations;
}
void qa_app_qw_remote_register_resource(qa_app_qw_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource) {
  qa_app_q1_remote_register_resource(remote->shared, content, path, resource);
}
const qa_simulation_output *qa_app_qw_remote_sample_presentation(qa_app_qw_remote *remote,
    double now) {
  const qa_simulation_output *output = qa_app_q1_remote_sample_presentation(remote->shared, now);
  if (!output) return NULL;
  qa_app_q1_player player;
  if (!qa_app_q1_remote_player(remote->shared, &player)) return output;
  qa_app_sim_player_view view = qa_app_qw_remote_player_view(remote, player.actor);
  /* TS returns a new output ({ ...output, snapshot: { ...output.snapshot, bodies } }); the
   * shared presentation's own output keeps its bodies. */
  free(remote->sample_bodies);
  remote->sample_bodies = xmalloc(output->snapshot.body_count * sizeof *remote->sample_bodies);
  for (size_t i = 0; i < output->snapshot.body_count; i++) {
    remote->sample_bodies[i] = output->snapshot.bodies[i];
    if (qa_same_actor(remote->sample_bodies[i].actor, player.actor)) {
      remote->sample_bodies[i].body.origin = view.origin;
      remote->sample_bodies[i].body.angles = view.angles;
    }
  }
  remote->sample_output = *output;
  remote->sample_output.snapshot.bodies = remote->sample_bodies;
  qa_simulation_output sampled = remote->sample_output;
  sampled.events = NULL;
  sampled.event_count = 0;
  remote->options.shared.publish(remote->options.shared.ctx, &sampled);
  return &remote->sample_output;
}
const qa_app_sim_presentation_event *qa_app_qw_remote_drain_events(qa_app_qw_remote *remote,
    size_t *out_count) {
  return qa_app_q1_remote_drain_events(remote->shared, out_count);
}
void qa_app_qw_remote_prediction_sent(qa_app_qw_remote *remote, double sequence,
    const qa_qw_user_command *command, double now) {
  if (!remote->predictor) return;
  qa_app_quakeworld_prediction_sent(remote->predictor, sequence, *command, now);
  predicted_clear(remote);
  remote->predicted = qa_app_quakeworld_prediction_replay(remote->predictor);
  remote->has_predicted = true;
}
void qa_app_qw_remote_prediction_acknowledged(qa_app_qw_remote *remote,
    double sequence, double now) {
  remote->acknowledged_sequence = sequence;
  if (remote->predictor) qa_app_quakeworld_prediction_acknowledged(remote->predictor, sequence, now);
}
void qa_app_qw_remote_disconnected(qa_app_qw_remote *remote, const char *reason) {
  qa_app_q1_remote_disconnected(remote->shared, reason);
}
void qa_app_qw_remote_print(qa_app_qw_remote *remote, const char *text) {
  remote->options.shared.print(remote->options.shared.ctx, text);
}
static void host_server_data(void *ctx, const void *data) {
  qa_app_qw_remote_server_data(ctx, data);
}
static double host_game_state(void *ctx, const void *data, const char *const *models,
    size_t model_count, const char *const *sounds, size_t sound_count) {
  return qa_app_qw_remote_game_state(ctx, data, models, model_count, sounds, sound_count, NULL, NULL);
}
static void host_receive(void *ctx, const void *const *messages, size_t count, double now) {
  qa_app_qw_remote *remote = ctx;
  const qa_quakeworld_message **list = (const qa_quakeworld_message **)messages;
  qa_quakeworld_message *flat = calloc(count ? count : 1, sizeof *flat);
  if (!flat) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) message_copy(list[i], &flat[i]);
  qa_app_qw_remote_receive(remote, flat, count, now, NULL, NULL);
  for (size_t i = 0; i < count; i++) qa_quakeworld_message_destroy(&flat[i]);
  free(flat);
}
static qa_qw_user_command host_command(void *ctx, const void *actor_command) {
  return qa_app_qw_remote_command(ctx, actor_command);
}
static bool host_teleport(void *ctx, qa_vec3 *out) {
  return qa_app_qw_remote_take_spectator_teleport(ctx, out);
}
static void host_prediction_sent(void *ctx, double sequence,
    const qa_qw_user_command *command, double now) {
  qa_app_qw_remote_prediction_sent(ctx, sequence, command, now);
}
static void host_prediction_ack(void *ctx, double sequence, double now) {
  qa_app_qw_remote_prediction_acknowledged(ctx, sequence, now);
}
static void host_disconnected(void *ctx, const char *reason) {
  qa_app_qw_remote_disconnected(ctx, reason);
}
static void host_print(void *ctx, const char *text) { qa_app_qw_remote_print(ctx, text); }
static const char *const *host_skin_names(void *ctx, size_t *out_count) {
  return qa_app_qw_remote_skin_names(ctx, out_count);
}
static void host_skins_loading(void *ctx, bool value) { qa_app_qw_remote_skins_loading(ctx, value); }
static void host_skins_prepare(void *ctx) { qa_app_qw_remote_skins_prepare(ctx); }
qa_app_qw_client_host qa_app_qw_remote_as_client_host(qa_app_qw_remote *remote) {
  qa_app_qw_client_host host = {0};
  host.ctx = remote;
  host.has_downloads = remote->options.has_downloads;
  host.downloads = remote->options.downloads;
  host.has_skins = true;
  host.skins.ctx = remote;
  host.skins.names = host_skin_names;
  host.skins.loading = host_skins_loading;
  host.skins.prepare = host_skins_prepare;
  host.prediction_sent = host_prediction_sent;
  host.prediction_acknowledged = host_prediction_ack;
  host.server_data = host_server_data;
  host.game_state = host_game_state;
  host.receive = host_receive;
  host.command = host_command;
  host.take_spectator_teleport = host_teleport;
  host.disconnected = host_disconnected;
  host.print = host_print;
  return host;
}
