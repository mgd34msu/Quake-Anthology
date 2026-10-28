/* Port of src/app/bootstrap/network/remote-q3.ts.
 * GPL-2.0-or-later. */
#include "qa/movement/q3/weapon.h"
#include "qa/app/bootstrap/network/remote_q3.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client.h"
#include "qa/app/bootstrap/simulation/players.h"
#include "qa/app/bootstrap/simulation/prediction/presentation.h"
#include "qa/app/bootstrap/simulation/q3/guest_player.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/catalog/index.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/world.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/adapters.h"
#include "qa/network/q3/parse_entities.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/game_state.h"
static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("out of memory");
  return p;
}
struct qa_app_q3_remote {
  qa_app_q3_remote_options options;
  qa_app_remote_world_content *world;
  qa_q3_client_clock *clock;
  qa_q3_client_connection *connection;
  qa_q3_pres_history_snapshot_source history_source; bool history_init;
  qa_app_q3_client_source source; bool has_source;
  struct { double number; qa_actor_id actor; } *actors; size_t actor_count, actor_cap;
  bool has_current; qa_q3_wire_snapshot current;
  bool loading_downloads;
  qa_simulation_output published; bool has_published;
  qa_body_snapshot *published_bodies;
  qa_actor_observation *published_actors;
  qa_inventory_snapshot published_inventory[1];
  qa_actor_configuration published_configuration[1];
  qa_scene_snapshot_world published_world;
  qa_byte_view published_area_bits;
  qa_inventory_entry *ui_inventory; size_t ui_inventory_cap;
  double game_state_message, game_state_commands;
  qa_app_presentation_prediction_adapter *prediction;
  char **source_records; size_t source_record_count;
  char **server_command; size_t server_command_count;
};
static qa_actor_id actor_at(qa_app_q3_remote *remote, double number) {
  if (number != trunc(number) || number < 0 || number >= 1024)
    qa_range_error("Invalid Q3 source entity");
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].number == number) return remote->actors[i].actor;
  qa_actor_id actor = remote->options.identity_actor(remote->options.ctx, number,
      remote->options.next_generation(remote->options.ctx, number));
  if (remote->actor_count == remote->actor_cap) {
    remote->actor_cap = remote->actor_cap ? remote->actor_cap * 2 : 64;
    remote->actors = realloc(remote->actors, remote->actor_cap * sizeof *remote->actors);
    if (!remote->actors) qa_range_error("out of memory");
  }
  remote->actors[remote->actor_count].number = number;
  remote->actors[remote->actor_count].actor = actor;
  remote->actor_count++;
  return actor;
}
static const qa_executable_recipe *recipe_of(qa_app_q3_remote *remote) {
  return remote->options.content_recipe(remote->options.ctx,
      qa_app_remote_world_loaded(remote->world));
}
static bool is_demo(qa_app_q3_remote *remote) {
  if (!remote->connection) return false;
  const qa_q3_client_mode *mode = qa_q3_client_connection_get_mode(remote->connection);
  return mode && mode->kind == QA_Q3_CLIENT_MODE_DEMO;
}
typedef struct entities_ctx { qa_app_q3_remote *remote; } entities_ctx;
static bool entities_actor_at(void *ctx, double number, qa_actor_id *out) {
  entities_ctx *entities = ctx;
  if (number == trunc(number) && number >= 0 && number < 1024) {
    *out = actor_at(entities->remote, number);
    return true;
  }
  return false;
}
static bool entities_number_of(void *ctx, qa_actor_id actor, double *out) {
  entities_ctx *entities = ctx;
  return qa_app_q3_remote_number_of(entities->remote, actor, out);
}
static qa_app_prediction_source_entities source_entities(qa_app_q3_remote *remote,
    entities_ctx *storage) {
  storage->remote = remote;
  qa_app_prediction_source_entities entities = {storage, entities_actor_at, entities_number_of};
  return entities;
}
static qa_app_movement_prediction_snapshot prediction_snapshot(qa_app_q3_remote *remote) {
  if (!remote->has_current)
    qa_range_error("Q3 prediction needs a snapshot");
  qa_app_q3_player player;
  if (!qa_app_q3_remote_player(remote, &player)) qa_range_error("Q3 prediction needs a snapshot");
  const qa_q3_player_state_record *ps = &remote->current.player_state;
  qa_q3_adapted_player_state adapted;
  qa_q3_to_player_state(ps, &adapted);
  qa_q3_player_state canonical = adapted.state;
  const qa_executable_recipe *recipe = recipe_of(remote);
  entities_ctx storage;
  qa_app_prediction_source_entities entities = source_entities(remote, &storage);
  qa_app_movement_prediction_snapshot base = {0};
  base.sequence = remote->connection
      ? (double)qa_q3_wire_command_history_current_number(
            qa_q3_client_connection_commands(remote->connection)) : 0;
  base.command_time_milliseconds = canonical.command_time_milliseconds;
  base.state.kind = QA_MOVEMENT_STATE_Q3;
  base.state.as.q3.command_time_milliseconds = canonical.command_time_milliseconds;
  base.state.as.q3.movement_type = canonical.movement_type;
  base.state.as.q3.bob_cycle = canonical.bob_cycle;
  base.state.as.q3.movement_flags = canonical.movement_flags;
  base.state.as.q3.movement_time_milliseconds = canonical.movement_time_milliseconds;
  base.state.as.q3.origin = canonical.origin;
  base.state.as.q3.velocity = canonical.velocity;
  base.state.as.q3.gravity = canonical.gravity;
  base.state.as.q3.speed = canonical.speed;
  base.state.as.q3.delta_angle_words = canonical.delta_angle_words;
  base.state.as.q3.movement_direction = canonical.movement_direction;
  base.state.as.q3.grapple_point = canonical.grapple_point;
  base.state.as.q3.flags = canonical.flags;
  base.state.as.q3.view_angles = canonical.view_angles;
  base.state.as.q3.view_height = canonical.view_height;
  qa_q3_pres_retail_snapshot retail = {0};
  if (!qa_q3_pres_retail_snapshot_from_wire(&retail, &remote->current))
    qa_range_error("out of memory");
  qa_app_movement_prediction_snapshot result =
      qa_app_read_prediction_source_state(base, &retail.player_state, entities);
  qa_q3_pres_retail_snapshot_destroy(&retail);
  /* Re-apply TS base fields readPredictionSourceState does not own. */
  result.view_angles = canonical.view_angles;
  result.view_height = canonical.view_height;
  result.view_offset = (qa_vec3){0, 0, canonical.view_height};
  result.bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  result.environment.health = canonical.stats.values ? canonical.stats.values[0] : 0;
  result.environment.gravity_multiplier = 1;
  qa_app_sim_player_ui ui = qa_app_q3_remote_player_ui(remote, player.actor);
  result.arsenal.provider = recipe->inventory.provider;
  const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item((double)ps->weapon);
  result.arsenal.active_weapon = weapon ? weapon->item : NULL;
  result.arsenal.ammo = ui.inventory;
  result.arsenal.ammo_count = ui.inventory_count;
  result.arsenal.state.kind = "q3";
  result.arsenal.state.source_weapon = (double)ps->weapon;
  result.arsenal.state.state = (double)ps->weapon_state;
  result.arsenal.state.time_milliseconds = (double)ps->weapon_time;
  result.animation.provider = recipe->character.definition.provider;
  result.animation.state.kind = "q3";
  result.animation.state.legs = (double)ps->legs_anim;
  result.animation.state.torso = (double)ps->torso_anim;
  result.animation.state.legs_timer_milliseconds = (double)ps->legs_timer;
  result.animation.state.torso_timer_milliseconds = (double)ps->torso_timer;
  return result;
}
static void publish_remote(qa_app_q3_remote *remote, double time) {
  if (!remote->has_current || !qa_app_remote_world_loaded(remote->world)) return;
  qa_app_q3_player player;
  if (!qa_app_q3_remote_player(remote, &player)) return;
  const qa_executable_recipe *recipe = recipe_of(remote);
  const qa_q3_wire_snapshot *snapshot = &remote->current;
  const qa_q3_player_state_record *ps = &snapshot->player_state;
  static const qa_bounds bounds = {{-15, -15, -24}, {15, 15, 32}};
  free(remote->published_bodies);
  free(remote->published_actors);
  remote->published_bodies = xmalloc((snapshot->entity_count + 1) * sizeof *remote->published_bodies);
  remote->published_actors = xmalloc((snapshot->entity_count + 1) * sizeof *remote->published_actors);
  size_t count = 0;
  for (size_t i = 0; i < snapshot->entity_count; i++) {
    const qa_q3_entity_state_record *entity = &snapshot->entities[i];
    if (entity->number == ps->client_num) continue;
    qa_body_snapshot *body = &remote->published_bodies[count];
    body->actor = actor_at(remote, (double)entity->number);
    body->body.origin = entity->origin;
    body->body.angles = entity->angles;
    body->body.velocity = (qa_vec3){0, 0, 0};
    body->body.bounds = bounds;
    body->body.ground = NULL;
    remote->published_actors[count].id = body->actor;
    remote->published_actors[count].owner = recipe->map.entities.provider;
    remote->published_actors[count].definition = "q3:remote-entity";
    count++;
  }
  qa_body_snapshot *own = &remote->published_bodies[count];
  own->actor = player.actor;
  own->body.origin = ps->origin;
  own->body.angles = ps->viewangles;
  own->body.velocity = ps->velocity;
  own->body.bounds = bounds;
  own->body.ground = NULL;
  remote->published_actors[count].id = player.actor;
  remote->published_actors[count].owner = recipe->map.entities.provider;
  remote->published_actors[count].definition = "q3:remote-entity";
  count++;
  qa_app_sim_player_ui ui = qa_app_q3_remote_player_ui(remote, player.actor);
  remote->published_inventory[0].actor = player.actor;
  remote->published_inventory[0].entries = ui.inventory;
  remote->published_inventory[0].entry_count = ui.inventory_count;
  remote->published_configuration[0].actor = player.actor;
  remote->published_configuration[0].movement = recipe->movement;
  remote->published_configuration[0].character = recipe->character;
  remote->published_configuration[0].weapons = recipe->weapons;
  remote->published_configuration[0].inventory = recipe->inventory;
  memset(&remote->published, 0, sizeof remote->published);
  remote->published.snapshot.session = remote->options.session;
  remote->published.snapshot.frame.frame = (double)snapshot->message_number;
  remote->published.snapshot.frame.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, time};
  remote->published.snapshot.frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, 0};
  remote->published.snapshot.frame.phase = QA_FRAME_PHASE_FRAME_EXIT;
  remote->published.snapshot.actors = remote->published_actors;
  remote->published.snapshot.actor_count = count;
  remote->published.snapshot.bodies = remote->published_bodies;
  remote->published.snapshot.body_count = count;
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
  remote->published_area_bits.bytes = (uint8_t *)snapshot->area_mask;
  remote->published_area_bits.length = snapshot->area_mask_len;
  remote->published.snapshot.scene.area_bits = &remote->published_area_bits;
  remote->has_published = true;
  remote->options.publish(remote->options.ctx, &remote->published);
}
qa_app_q3_remote *qa_app_q3_remote_create(qa_app_q3_remote_options options) {
  qa_app_q3_remote *remote = calloc(1, sizeof *remote);
  if (!remote) return NULL;
  remote->options = options;
  remote->world = qa_app_remote_world_content_create(options.world_host, options.content);
  if (!remote->world) { free(remote); return NULL; }
  if (!qa_q3_client_clock_init(&remote->clock)) {
    qa_app_remote_world_content_destroy(remote->world);
    free(remote);
    return NULL;
  }
  return remote;
}
void qa_app_q3_remote_destroy(qa_app_q3_remote *remote) {
  if (!remote) return;
  qa_app_remote_world_content_destroy(remote->world);
  qa_q3_client_clock_destroy(remote->clock);
  if (remote->has_current) qa_q3_wire_snapshot_destroy(&remote->current);
  if (remote->prediction) qa_app_presentation_prediction_adapter_destroy(remote->prediction);
  free(remote->actors);
  free(remote->published_bodies);
  free(remote->published_actors);
  free(remote->ui_inventory);
  for (size_t i = 0; i < remote->source_record_count; i++) free(remote->source_records[i]);
  free(remote->source_records);
  for (size_t i = 0; i < remote->server_command_count; i++) free(remote->server_command[i]);
  free(remote->server_command);
  free(remote);
}
qa_client_id qa_app_q3_remote_client(const qa_app_q3_remote *remote) {
  return remote->options.client;
}
const char *qa_app_q3_remote_userinfo(qa_app_q3_remote *remote) {
  return remote->options.userinfo(remote->options.ctx);
}
qa_q3_client_clock *qa_app_q3_remote_clock(qa_app_q3_remote *remote) { return remote->clock; }
bool qa_app_q3_remote_downloading(const qa_app_q3_remote *remote) {
  return remote->loading_downloads;
}
void *qa_app_q3_remote_scene(qa_app_q3_remote *remote) {
  return qa_app_remote_world_scene(remote->world);
}
void qa_app_q3_remote_bind_collision_settings(qa_app_q3_remote *remote, const void *settings) {
  qa_app_remote_world_bind_collision_settings(remote->world, (void *)settings);
}
const qa_simulation_output *qa_app_q3_remote_output(const qa_app_q3_remote *remote) {
  return remote->has_published ? &remote->published : NULL;
}
bool qa_app_q3_remote_player(const qa_app_q3_remote *remote, qa_app_q3_player *out) {
  if (!remote->has_current) return false;
  qa_app_q3_remote *mutable_remote = (qa_app_q3_remote *)remote;
  out->actor = actor_at(mutable_remote,
      (double)remote->current.player_state.client_num);
  out->client = remote->options.client;
  out->source_entity = (double)remote->current.player_state.client_num;
  return true;
}
qa_app_q3_player qa_app_q3_remote_admitted_player(qa_app_q3_remote *remote) {
  if (!remote->connection) qa_range_error("Q3 seat has no decoded connection");
  double number = (double)qa_q3_client_connection_client_number(remote->connection);
  qa_app_q3_player player = {0};
  player.actor = actor_at(remote, number);
  player.client = remote->options.client;
  player.source_entity = number;
  return player;
}
const qa_q3_player_state_record *qa_app_q3_remote_initial_player(qa_app_q3_remote *remote) {
  if (!remote->has_current) qa_range_error("Q3 cgame needs its first decoded player state");
  return &remote->current.player_state;
}
const qa_app_q3_client_source *qa_app_q3_remote_cgame_source(qa_app_q3_remote *remote) {
  if (!remote->has_source) qa_range_error("Q3 cgame has no connection source");
  return &remote->source;
}
static void free_strings(char ***strings, size_t *count) {
  for (size_t i = 0; i < *count; i++) free((*strings)[i]);
  free(*strings);
  *strings = NULL;
  *count = 0;
}
const char *const *qa_app_q3_remote_source_records(qa_app_q3_remote *remote, size_t *out_count) {
  free_strings(&remote->source_records, &remote->source_record_count);
  *out_count = 0;
  if (!remote->connection) return NULL;
  char **strings = NULL;
  size_t count = 0;
  if (!qa_q3_client_game_state_storage_copy_strings(
          qa_q3_client_connection_game_state(remote->connection), &strings, &count))
    qa_range_error("out of memory");
  remote->source_records = strings;
  remote->source_record_count = count;
  *out_count = count;
  return (const char *const *)remote->source_records;
}
qa_actor_id qa_app_q3_remote_actor_at(qa_app_q3_remote *remote, double number) {
  return actor_at(remote, number);
}
bool qa_app_q3_remote_number_of(qa_app_q3_remote *remote, qa_actor_id actor, double *out) {
  for (size_t i = 0; i < remote->actor_count; i++)
    if (qa_same_actor(remote->actors[i].actor, actor)) {
      *out = remote->actors[i].number;
      return true;
    }
  return false;
}
bool qa_app_q3_remote_is_player(qa_app_q3_remote *remote, qa_actor_id actor) {
  double number = 0;
  return qa_app_q3_remote_number_of(remote, actor, &number) && number < 64;
}
typedef struct source_ctx { qa_app_q3_remote *remote; } source_ctx;
static size_t source_game_state(void *ctx, const char *const **strings) {
  source_ctx *source = ctx;
  size_t count = 0;
  *strings = qa_app_q3_remote_source_records(source->remote, &count);
  return count;
}
static bool source_server_command(void *ctx, double sequence,
    const char *const **strings, size_t *out_count) {
  source_ctx *source = ctx;
  qa_app_q3_remote *remote = source->remote;
  free_strings(&remote->server_command, &remote->server_command_count);
  char **argv = NULL;
  size_t argc = 0;
  if (!qa_q3_client_connection_get_server_command(remote->connection,
          (int64_t)sequence, &argv, &argc)) return false;
  remote->server_command = argv;
  remote->server_command_count = argc;
  *strings = (const char *const *)argv;
  *out_count = argc;
  return true;
}
static qa_actor_id source_actor_at(void *ctx, double number) {
  source_ctx *source = ctx;
  return actor_at(source->remote, number);
}
static double history_parse_entities(void *ctx) {
  qa_q3_client_connection *connection = ctx;
  qa_q3_source_parse_entities *entities = qa_q3_client_connection_parse_entities(connection);
  return (double)qa_q3_source_parse_entities_number(entities);
}
static void history_print(void *ctx, const char *message) {
  qa_app_q3_remote *remote = ctx;
  remote->options.print(remote->options.ctx, message);
}
void qa_app_q3_remote_attach(qa_app_q3_remote *remote, qa_q3_client_connection *connection) {
  remote->connection = connection;
  qa_q3_pres_history_snapshot_source_init(&remote->history_source,
      qa_q3_client_connection_history(connection), connection,
      history_parse_entities, history_print);
  remote->history_init = true;
  source_ctx *ctx = xmalloc(sizeof *ctx);
  ctx->remote = remote;
  remote->source.ctx = ctx;
  remote->source.game_state = source_game_state;
  remote->source.server_command = source_server_command;
  remote->source.actor_at = source_actor_at;
  remote->has_source = true;
}
void qa_app_q3_remote_clear_active(qa_app_q3_remote *remote,
    qa_app_q3_remote_assert_fn assert_current, void *assert_ctx) {
  assert_current(assert_ctx);
  if (remote->options.shutdown) remote->options.shutdown(remote->options.ctx);
  assert_current(assert_ctx);
  if (remote->options.has_downloads && remote->options.downloads.close)
    remote->options.downloads.close(remote->options.downloads.ctx);
  remote->loading_downloads = false;
  remote->actor_count = 0;
  if (remote->has_current) {
    qa_q3_wire_snapshot_destroy(&remote->current);
    remote->has_current = false;
  }
  remote->has_published = false;
  if (remote->prediction) {
    qa_app_presentation_prediction_adapter_destroy(remote->prediction);
    remote->prediction = NULL;
  }
  qa_q3_client_clock_clear(remote->clock);
}
void qa_app_q3_remote_system_info(qa_app_q3_remote *remote, const char *info) {
  char *pure = qa_q3_info_value(info, "sv_pure");
  double pure_value = pure ? strtod(pure, NULL) : 0;
  free(pure);
  if (!is_demo(remote) && pure_value != 0 && !remote->options.initialize)
    qa_range_error("This server requires pure verification, which is not supported yet.");
  char *game = qa_q3_info_value(info, "fs_game");
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make("q3-baseq3", game ? game : "", &selection))
    qa_range_error("out of memory");
  qa_remote_content_selection_free(&selection);
  free(game);
}
static char *game_state_get(qa_app_q3_remote *remote, int64_t index) {
  char *out = NULL;
  qa_q3_client_game_state_storage_get(
      qa_q3_client_connection_game_state(remote->connection), index, &out);
  return out;
}
void qa_app_q3_remote_gamestate(qa_app_q3_remote *remote, const qa_q3_gamestate *state,
    double generation, qa_app_q3_remote_assert_fn assert_current, void *assert_ctx) {
  (void)generation;
  assert_current(assert_ctx);
  if (!remote->connection) qa_range_error("Q3 gamestate has no connection");
  char *info = game_state_get(remote, 0);
  char *protocol = qa_q3_info_value(info ? info : "", "protocol");
  if (!protocol || strcmp(protocol, "68") != 0) {
    free(protocol);
    free(info);
    qa_range_error("Q3 remote requires protocol 68");
  }
  free(protocol);
  char *type_text = qa_q3_info_value(info, "g_gametype");
  double game_type = type_text ? strtod(type_text, NULL) : NAN;
  free(type_text);
  if (game_type != trunc(game_type) || game_type < 0 || game_type > 4) {
    free(info);
    qa_range_error("Q3 remote requires a baseq3 game type");
  }
  char *map = qa_q3_info_value(info, "mapname");
  bool valid = map && map[0] != '\0';
  for (const char *p = map; valid && *p; p++) {
    char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '_' || c == '/' || c == '-')) valid = false;
  }
  if (valid && strstr(map, "..")) valid = false;
  if (!valid) {
    free(map);
    free(info);
    qa_range_error("Invalid Q3 remote map name");
  }
  free(info);
  remote->game_state_message =
      (double)qa_q3_client_connection_server_message_sequence(remote->connection);
  remote->game_state_commands = (double)state->command_sequence;
  bool downloading = is_demo(remote) ? false
      : remote->options.has_downloads && remote->options.downloads.prepare
          ? remote->options.downloads.prepare(remote->options.downloads.ctx, remote->connection)
          : false;
  assert_current(assert_ctx);
  remote->loading_downloads = downloading;
  if (downloading) {
    free(map);
    return;
  }
  char **model_names = xmalloc(256 * sizeof *model_names);
  size_t model_count = 0;
  for (int64_t i = 0; i < 256; i++) {
    char *value = game_state_get(remote, 32 + i);
    if (value && value[0] != '\0') model_names[model_count++] = value;
    else free(value);
  }
  char **sound_names = xmalloc(256 * sizeof *sound_names);
  size_t sound_count = 0;
  for (int64_t i = 0; i < 256; i++) {
    char *value = game_state_get(remote, 288 + i);
    if (value && value[0] != '\0') sound_names[sound_count++] = value;
    else free(value);
  }
  char *map_path = xmalloc(strlen(map) + 10);
  snprintf(map_path, strlen(map) + 10, "maps/%s.bsp", map);
  void *content = remote->options.load_content(remote->options.ctx, map_path,
      (const char *const *)model_names, model_count,
      (const char *const *)sound_names, sound_count, remote->connection);
  assert_current(assert_ctx);
  qa_app_remote_world_set_loaded(remote->world, content);
  if (remote->options.initialize)
    remote->options.initialize(remote->options.ctx, remote->connection);
  assert_current(assert_ctx);
  for (size_t i = 0; i < model_count; i++) free(model_names[i]);
  free(model_names);
  for (size_t i = 0; i < sound_count; i++) free(sound_names[i]);
  free(sound_names);
  free(map_path);
  free(map);
}
double qa_app_q3_remote_download_size(qa_app_q3_remote *remote, double size) {
  if (is_demo(remote)) qa_range_error("Q3 demo cannot request package downloads");
  if (!remote->options.has_downloads)
    qa_range_error("Q3 package downloads have no writable content owner");
  return remote->options.downloads.publish_size(remote->options.downloads.ctx, size);
}
void qa_app_q3_remote_download(qa_app_q3_remote *remote, const qa_q3_download_block *block) {
  if (is_demo(remote)) qa_range_error("Q3 demo cannot request package downloads");
  if (!remote->options.has_downloads)
    qa_range_error("Q3 package downloads have no writable content owner");
  remote->options.downloads.receive(remote->options.downloads.ctx, block);
}
void qa_app_q3_remote_snapshot(qa_app_q3_remote *remote,
    const qa_q3_wire_snapshot *snapshot, double ping) {
  (void)ping;
  if (remote->has_current) qa_q3_wire_snapshot_destroy(&remote->current);
  if (!qa_q3_wire_snapshot_copy(&remote->current, snapshot)) qa_range_error("out of memory");
  remote->has_current = true;
  qa_q3_client_clock_publish(remote->clock, &remote->current);
  if (remote->prediction) {
    qa_app_movement_prediction_snapshot capture = prediction_snapshot(remote);
    qa_app_presentation_prediction_adapter_capture(remote->prediction, capture);
  }
  publish_remote(remote, (double)snapshot->server_time);
}
void qa_app_q3_remote_map_restart(qa_app_q3_remote *remote) {
  if (remote->prediction && remote->has_current) {
    qa_app_movement_prediction_snapshot capture = prediction_snapshot(remote);
    qa_app_presentation_prediction_adapter_capture(remote->prediction, capture);
  }
}
static const qa_q3_wire_snapshot *require_player(qa_app_q3_remote *remote, qa_actor_id actor) {
  if (!remote->has_current
      || !qa_same_actor(actor_at(remote, (double)remote->current.player_state.client_num), actor))
    qa_range_error("No decoded Q3 player state");
  return &remote->current;
}
const qa_world_text *qa_app_q3_remote_world_text(qa_app_q3_remote *remote, size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
qa_app_sim_player_view qa_app_q3_remote_player_view(qa_app_q3_remote *remote, qa_actor_id actor) {
  if (!remote->has_current) {
    qa_app_q3_player admitted = qa_app_q3_remote_admitted_player(remote);
    if (qa_same_actor(admitted.actor, actor)) {
      qa_app_sim_player_view view = {0};
      return view;
    }
  }
  const qa_q3_wire_snapshot *snapshot = require_player(remote, actor);
  qa_app_sim_player_view view = {0};
  view.origin = snapshot->player_state.origin;
  view.angles = snapshot->player_state.viewangles;
  view.view_height = (double)snapshot->player_state.viewheight;
  return view;
}
static const char *product_name(qa_q3_product product) {
  return product == QA_Q3_PRODUCT_MISSIONPACK ? "missionpack" : "baseq3";
}
qa_app_sim_player_ui qa_app_q3_remote_player_ui(qa_app_q3_remote *remote, qa_actor_id actor) {
  const qa_q3_wire_snapshot *snapshot = require_player(remote, actor);
  const qa_executable_recipe *recipe = recipe_of(remote);
  if (recipe->weapons.count == 0) qa_range_error("Q3 remote has no native weapon provider");
  qa_q3_adapted_player_state adapted;
  qa_q3_to_player_state(&snapshot->player_state, &adapted);
  qa_app_guest_player_ui scratch = {0};
  qa_app_guest_player_ui guest = qa_app_q3_guest_player_ui(adapted.state, recipe->weapons.values[0],
      snapshot->server_time, NULL, 0, product_name(snapshot->player_state.product),
      NULL, 0, false, NULL, false, scratch);
  if (remote->ui_inventory_cap < guest.inventory_count) {
    remote->ui_inventory = realloc(remote->ui_inventory,
        guest.inventory_count * sizeof *remote->ui_inventory);
    if (!remote->ui_inventory) qa_range_error("out of memory");
    remote->ui_inventory_cap = guest.inventory_count;
  }
  if (guest.inventory_count)
    memcpy(remote->ui_inventory, guest.inventory, guest.inventory_count * sizeof *remote->ui_inventory);
  qa_app_sim_player_ui ui = {0};
  ui.health = guest.health;
  ui.armor = guest.armor;
  ui.powerups = guest.powerups;
  ui.powerup_count = guest.powerup_count;
  ui.weapon_status = guest.has_weapon_status ? &guest.weapon_status : NULL;
  ui.arsenal_warning = guest.arsenal_warning;
  ui.active_weapon = guest.active_weapon;
  ui.has_ammo = guest.has_ammo;
  ui.ammo_item = guest.ammo_item;
  ui.ammo_count = guest.ammo_count;
  ui.inventory = remote->ui_inventory;
  ui.inventory_count = guest.inventory_count;
  ui.items = (const qa_app_sim_player_ui_item *)guest.items;
  ui.item_count = guest.item_count;
  return ui;
}
const void *qa_app_q3_remote_character_views(qa_app_q3_remote *remote, size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
const void *qa_app_q3_remote_presentations(qa_app_q3_remote *remote, size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
void qa_app_q3_remote_register_resource(qa_app_q3_remote *remote, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource) {
  (void)remote; (void)content; (void)path; (void)resource;
}
void qa_app_q3_remote_player_command(qa_app_q3_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count) {
  require_player(remote, actor);
  const char *values[64];
  values[0] = name;
  for (size_t i = 0; i < count && i < 63; i++) values[i + 1] = args[i];
  for (size_t i = 0; i <= count && i < 64; i++)
    for (const char *p = values[i]; *p; p++)
      if (*p == '"' || *p == '\n' || *p == '\r' || *p == ';')
        qa_range_error("Invalid Q3 command delimiter");
  size_t total = strlen(name) + 1;
  for (size_t i = 0; i < count; i++) total += strlen(args[i]) + 3;
  char *line = xmalloc(total);
  size_t used = (size_t)snprintf(line, total, "%s", name);
  for (size_t i = 0; i < count; i++)
    used += (size_t)snprintf(line + used, total - used, " \"%s\"", args[i]);
  remote->options.send_command(remote->options.ctx, line);
  free(line);
}
qa_q3_wire_user_command qa_app_q3_remote_command(qa_app_q3_remote *remote,
    const qa_actor_command *command) {
  const qa_q3_wire_snapshot *snapshot = require_player(remote, command->actor);
  if (command->command.kind != QA_USER_COMMAND_Q3)
    qa_range_error("Native Q3 remote requires Q3 commands");
  double time = (double)qa_q3_client_clock_time(remote->clock);
  qa_q3_base_user_command presented = qa_app_presentation_source_command(
      *command, time, command->command.as.q3.weapon);
  qa_q3_base_user_command source = qa_app_sim_relative_q3_source_command(
      command->source, "q3", presented, snapshot->player_state.delta_angles, command->angle_space);
  qa_q3_wire_user_command result = {0};
  result.server_time = (int64_t)time;
  result.angles[0] = source.angles.x;
  result.angles[1] = source.angles.y;
  result.angles[2] = source.angles.z;
  result.buttons = source.buttons;
  result.weapon = source.weapon;
  result.forwardmove = source.forwardmove;
  result.rightmove = source.rightmove;
  result.upmove = source.upmove;
  result.angles[0] = (double)((int64_t)result.angles[0] & 65535);
  result.angles[1] = (double)((int64_t)result.angles[1] & 65535);
  result.angles[2] = (double)((int64_t)result.angles[2] & 65535);
  return result;
}
const qa_simulation_output *qa_app_q3_remote_sample_presentation(qa_app_q3_remote *remote,
    double now) {
  int64_t time = 0;
  if (is_demo(remote)) {
    if (!remote->has_current) return NULL;
    time = qa_q3_client_clock_time(remote->clock);
  } else {
    qa_q3_clock_options options = {0};
    options.time_nudge = remote->options.time_nudge ? (int64_t)remote->options.time_nudge(remote->options.ctx) : 0;
    options.timescale = remote->options.timescale ? remote->options.timescale(remote->options.ctx) : 1;
    double presented = remote->options.presentation_time
        ? remote->options.presentation_time(remote->options.ctx) : now;
    if (!qa_q3_client_clock_advance(remote->clock, (int64_t)trunc(presented), &options, &time))
      return NULL;
  }
  publish_remote(remote, (double)time);
  return remote->has_published ? &remote->published : NULL;
}
static bool is_brush_hit(void *ctx, qa_trace_hit hit) {
  qa_app_q3_remote *remote = ctx;
  if (hit.kind == QA_TRACE_HIT_WORLD) return true;
  if (hit.kind != QA_TRACE_HIT_ACTOR || !remote->has_current) return false;
  for (size_t i = 0; i < remote->current.entity_count; i++)
    if (remote->current.entities[i].solid == 0xffffff
        && qa_same_actor(actor_at(remote, (double)remote->current.entities[i].number), hit.actor))
      return true;
  return false;
}
static qa_q3_base_user_command relative_hook(void *ctx, qa_command_source source,
    qa_user_command_kind kind, qa_q3_base_user_command presented, qa_vec3 delta_angles) {
  (void)ctx;
  (void)kind;
  return qa_app_sim_relative_q3_source_command(source, "q3", presented, delta_angles, NULL);
}
qa_app_presentation_prediction_adapter *qa_app_q3_remote_movement(
    qa_app_q3_remote *remote, qa_seat_id seat) {
  if (remote->prediction) return remote->prediction;
  qa_app_q3_player player;
  if (!qa_app_q3_remote_player(remote, &player)) qa_range_error("Q3 prediction has no player");
  const qa_executable_recipe *recipe = recipe_of(remote);
  entities_ctx *storage = xmalloc(sizeof *storage);
  qa_app_prediction_source_entities entities = source_entities(remote, storage);
  qa_app_presentation_prediction_options options = {0};
  options.movement.probe.actor = remote->options.owned_actor(remote->options.ctx,
      player.actor, &recipe->map.entities);
  options.movement.seat = seat;
  options.movement.probe.recipe = recipe;
  options.movement.probe.profile = qa_app_sim_movement_profile(*recipe);
  options.movement.probe.standing_bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  options.movement.probe.standing_view_height = 26;
  options.movement.probe.host_ctx = remote;
  options.movement.probe.is_brush = is_brush_hit;
  options.initial = prediction_snapshot(remote);
  options.entities = entities;
  options.host_ctx = remote;
  options.relative_source_command = relative_hook;
  remote->prediction = qa_app_create_presentation_movement_host(options);
  if (!remote->prediction) qa_range_error("out of memory");
  return remote->prediction;
}
const qa_app_sim_presentation_event *qa_app_q3_remote_drain_events(qa_app_q3_remote *remote,
    size_t *out_count) {
  (void)remote;
  *out_count = 0;
  return NULL;
}
void qa_app_q3_remote_print(qa_app_q3_remote *remote, const char *text) {
  remote->options.print(remote->options.ctx, text);
}
void qa_app_q3_remote_disconnected(qa_app_q3_remote *remote, const char *reason) {
  char *line = xmalloc(strlen(reason) + 2);
  snprintf(line, strlen(reason) + 2, "%s\n", reason);
  qa_app_q3_remote_print(remote, line);
  free(line);
  remote->options.disconnected(remote->options.ctx, reason);
}
static bool host_downloading(void *ctx) { return qa_app_q3_remote_downloading(ctx); }
static const char *host_userinfo(void *ctx) { return qa_app_q3_remote_userinfo(ctx); }
static void host_system_info(void *ctx, const char *info) {
  qa_app_q3_remote_system_info(ctx, info);
}
static void host_snapshot(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping) {
  qa_app_q3_remote_snapshot(ctx, snapshot, ping);
}
static void host_map_restart(void *ctx) { qa_app_q3_remote_map_restart(ctx); }
static void host_print(void *ctx, const char *text) { qa_app_q3_remote_print(ctx, text); }
static double host_download_size(void *ctx, double size) {
  return qa_app_q3_remote_download_size(ctx, size);
}
static void host_download(void *ctx, const qa_q3_download_block *block) {
  qa_app_q3_remote_download(ctx, block);
}
static void host_clear_active(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx) {
  qa_app_q3_remote_clear_active(ctx, (qa_app_q3_remote_assert_fn)assert_current, assert_ctx);
}
static void host_gamestate(void *ctx, const qa_q3_gamestate *state, double generation,
    qa_app_q3_assert_fn assert_current, void *assert_ctx) {
  qa_app_q3_remote_gamestate(ctx, state, generation,
      (qa_app_q3_remote_assert_fn)assert_current, assert_ctx);
}
static void host_attach(void *ctx, qa_q3_client_connection *connection) {
  qa_app_q3_remote_attach(ctx, connection);
}
static qa_q3_wire_user_command host_command(void *ctx, const void *actor_command) {
  return qa_app_q3_remote_command(ctx, actor_command);
}
static void host_disconnected(void *ctx, const char *reason) {
  qa_app_q3_remote_disconnected(ctx, reason);
}
qa_app_q3_client_host qa_app_q3_remote_as_client_host(qa_app_q3_remote *remote) {
  qa_app_q3_client_host host = {0};
  host.ctx = remote;
  host.identity.client = remote->options.client;
  host.downloading = host_downloading;
  host.userinfo = host_userinfo;
  host.system_info = host_system_info;
  host.snapshot = host_snapshot;
  host.map_restart = host_map_restart;
  host.print = host_print;
  host.download_size = host_download_size;
  host.download = host_download;
  host.clear_active = host_clear_active;
  host.gamestate = host_gamestate;
  host.attach = host_attach;
  host.command = host_command;
  host.disconnected = host_disconnected;
  return host;
}
