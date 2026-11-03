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
#include "qa/foundation/js.h"
#include "qa/core/numeric.h"
/* TS `throw new Error(message)`. */
#define js_error(...) qa_raise("Error", 0, 0, __VA_ARGS__)
static void release_string(void *arg) { char **text = arg; free(*text); *text = NULL; }
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
  /* q3GuestPlayerUi output buffers; playerUi results borrow them until the next call. */
  qa_app_guest_player_ui ui;
  double game_state_message, game_state_commands;
  qa_app_presentation_prediction_adapter *prediction;
  char **source_records; size_t source_record_count;
  char **server_command; size_t server_command_count;
  char *system_info; /* the source's systemInfo() result until the next call */
  struct source_ctx { qa_app_q3_remote *remote; } source_ctx;
  struct entities_ctx { qa_app_q3_remote *remote; } entities_ctx;
};
static qa_actor_id actor_at(qa_app_q3_remote *remote, double number) {
  if (number != trunc(number) || number < 0 || number >= 1024)
    js_error("Invalid Q3 source entity");
  for (size_t i = 0; i < remote->actor_count; i++)
    if (remote->actors[i].number == number) return remote->actors[i].actor;
  qa_actor_id actor = remote->options.identity_actor(remote->options.ctx, number,
      remote->options.next_generation(remote->options.ctx, number));
  if (remote->actor_count == remote->actor_cap) {
    size_t cap = remote->actor_cap ? remote->actor_cap * 2 : 64;
    void *grown = realloc(remote->actors, cap * sizeof *remote->actors);
    if (!grown) qa_range_error("out of memory");
    remote->actors = grown; remote->actor_cap = cap;
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
typedef struct entities_ctx entities_ctx;
/* TS `{ actorAt: number => this.actorAt(number) }`: an invalid number throws. */
static bool entities_actor_at(void *ctx, double number, qa_actor_id *out) {
  entities_ctx *entities = ctx;
  *out = actor_at(entities->remote, number);
  return true;
}
static bool entities_number_of(void *ctx, qa_actor_id actor, double *out) {
  entities_ctx *entities = ctx;
  return qa_app_q3_remote_number_of(entities->remote, actor, out);
}
static qa_app_prediction_source_entities source_entities(qa_app_q3_remote *remote) {
  remote->entities_ctx.remote = remote;
  qa_app_prediction_source_entities entities = {&remote->entities_ctx, entities_actor_at, entities_number_of};
  return entities;
}
static void release_retail(void *arg) { qa_q3_pres_retail_snapshot_destroy(arg); }
/* TS predictionSnapshot(): the base carries the recipe providers, playerUi ammo,
 * bounds and view offset; readPredictionSourceState replaces the source-owned
 * fields from the retail player state. The result owns its ammo array
 * (qa_app_movement_prediction_snapshot_destroy). */
static qa_app_movement_prediction_snapshot prediction_snapshot(qa_app_q3_remote *remote) {
  qa_app_q3_player player;
  if (!remote->has_current || !qa_app_q3_remote_player(remote, &player)) js_error("Q3 prediction needs a snapshot");
  const qa_q3_player_state_record *ps = &remote->current.player_state;
  qa_q3_adapted_player_state adapted;
  qa_q3_to_player_state(ps, &adapted);
  const qa_q3_player_state *canonical = &adapted.state;
  const qa_executable_recipe *recipe = recipe_of(remote);
  qa_app_prediction_source_entities entities = source_entities(remote);
  qa_app_movement_prediction_snapshot base;
  memset(&base, 0, sizeof base);
  base.sequence = remote->connection
      ? (double)qa_q3_wire_command_history_current_number(
            qa_q3_client_connection_commands(remote->connection)) : 0;
  base.command_time_milliseconds = canonical->command_time_milliseconds;
  base.state.kind = QA_MOVEMENT_STATE_Q3;
  qa_q3_movement_state *state = &base.state.as.q3;
  state->command_time_milliseconds = canonical->command_time_milliseconds;
  state->movement_type = canonical->movement_type;
  state->bob_cycle = canonical->bob_cycle;
  state->movement_flags = canonical->movement_flags;
  state->movement_time_milliseconds = canonical->movement_time_milliseconds;
  state->origin = canonical->origin;
  state->velocity = canonical->velocity;
  state->gravity = canonical->gravity;
  state->speed = canonical->speed;
  state->delta_angle_words = canonical->delta_angle_words;
  state->movement_direction = canonical->movement_direction;
  state->grapple_point = canonical->grapple_point;
  state->flags = canonical->flags;
  state->view_angles = canonical->view_angles;
  state->view_height = canonical->view_height;
  state->ground = qa_app_prediction_source_hit((double)ps->ground_entity_num, entities);
  state->predictable_event_sequence = (double)ps->event_sequence;
  qa_actor_id jump_pad;
  if (ps->jumppad_ent != 0) { jump_pad = actor_at(remote, (double)ps->jumppad_ent); state->jump_pad = &jump_pad; }
  state->movement_frame = (double)ps->pmove_framecount;
  state->jump_pad_frame = (double)ps->jumppad_frame;
  qa_app_sim_player_ui ui = qa_app_q3_remote_player_ui(remote, player.actor);
  const qa_q3_weapon_item *weapon = qa_q3_get_weapon_item((double)ps->weapon);
  base.arsenal.provider = recipe->inventory.provider;
  base.arsenal.active_weapon = weapon ? weapon->item : NULL;
  base.arsenal.ammo = ui.inventory;
  base.arsenal.ammo_count = ui.inventory_count;
  base.arsenal.state.kind = "q3";
  base.arsenal.state.source_weapon = (double)ps->weapon;
  base.arsenal.state.state = (double)ps->weapon_state;
  base.arsenal.state.time_milliseconds = (double)ps->weapon_time;
  base.animation.provider = recipe->character.definition.provider;
  base.animation.state.kind = "q3";
  base.animation.state.legs = (double)ps->legs_anim;
  base.animation.state.torso = (double)ps->torso_anim;
  base.animation.state.legs_timer_milliseconds = (double)ps->legs_timer;
  base.animation.state.torso_timer_milliseconds = (double)ps->torso_timer;
  base.environment.health = (double)ps->stats[0];
  base.environment.gravity_multiplier = 1;
  base.bounds = (qa_bounds){{-15, -15, -24}, {15, 15, 32}};
  base.view_angles = canonical->view_angles;
  base.view_height = canonical->view_height;
  base.view_offset = (qa_vec3){0, 0, canonical->view_height};
  qa_q3_pres_retail_snapshot retail;
  memset(&retail, 0, sizeof retail);
  if (!qa_q3_pres_retail_snapshot_from_wire(&retail, &remote->current))
    qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_retail, &retail);
  qa_app_movement_prediction_snapshot result =
      qa_app_read_prediction_source_state(base, &retail.player_state, entities);
  qa_cleanup_pop(&guard);
  qa_q3_pres_retail_snapshot_destroy(&retail);
  return result;
}
static void release_prediction(void *arg) { qa_app_movement_prediction_snapshot_destroy(arg); }
static void capture_prediction(qa_app_q3_remote *remote) {
  qa_app_movement_prediction_snapshot capture = prediction_snapshot(remote);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_prediction, &capture);
  qa_app_presentation_prediction_adapter_capture(remote->prediction, capture);
  qa_cleanup_pop(&guard);
  qa_app_movement_prediction_snapshot_destroy(&capture);
}
static void publish_remote(qa_app_q3_remote *remote, double time) {
  /* TS reads this.player (actorAt) before testing world.ready. */
  qa_app_q3_player player;
  if (!remote->has_current || !qa_app_q3_remote_player(remote, &player)) return;
  if (!qa_app_remote_world_loaded(remote->world)) return;
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
    body->body.origin = entity->pos.base;
    body->body.angles = entity->apos.base;
    body->body.velocity = entity->pos.delta;
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
  free(remote->ui.powerups);
  free(remote->ui.inventory);
  free(remote->ui.items);
  for (size_t i = 0; i < remote->source_record_count; i++) free(remote->source_records[i]);
  free(remote->source_records);
  for (size_t i = 0; i < remote->server_command_count; i++) free(remote->server_command[i]);
  free(remote->server_command);
  free(remote->system_info);
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
  if (!remote->connection) js_error("Q3 seat has no decoded connection");
  double number = (double)qa_q3_client_connection_client_number(remote->connection);
  qa_app_q3_player player = {0};
  player.actor = actor_at(remote, number);
  player.client = remote->options.client;
  player.source_entity = number;
  return player;
}
const qa_q3_player_state_record *qa_app_q3_remote_initial_player(qa_app_q3_remote *remote) {
  if (!remote->has_current) js_error("Q3 cgame needs its first decoded player state");
  return &remote->current.player_state;
}
const qa_app_q3_client_source *qa_app_q3_remote_cgame_source(qa_app_q3_remote *remote) {
  if (!remote->has_source) js_error("Q3 cgame has no connection source");
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
typedef struct source_ctx source_ctx;
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
/* The ApplicationQ3ClientSource getters over the attached connection. */
static bool source_demo(void *ctx) { return is_demo(((source_ctx *)ctx)->remote); }
static double source_time(void *ctx) { return (double)qa_q3_client_clock_time(((source_ctx *)ctx)->remote->clock); }
static double source_client_number(void *ctx) {
  return (double)qa_q3_client_connection_client_number(((source_ctx *)ctx)->remote->connection);
}
static double source_server_message_sequence(void *ctx) { return ((source_ctx *)ctx)->remote->game_state_message; }
static double source_last_executed_server_command(void *ctx) { return ((source_ctx *)ctx)->remote->game_state_commands; }
/* systemInfo: () => connection.gameState.get(1) ?? "" */
static const char *source_system_info(void *ctx) {
  qa_app_q3_remote *remote = ((source_ctx *)ctx)->remote;
  free(remote->system_info);
  remote->system_info = NULL;
  char *value = NULL;
  if (!qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(remote->connection), 1, &value))
    qa_range_error("out of memory");
  if (value == NULL) {
    value = malloc(1);
    if (value == NULL) qa_range_error("out of memory");
    value[0] = '\0';
  }
  remote->system_info = value;
  return value;
}
/* snapshotPing: number => connection.snapshotPing(number) (false is null). */
static bool source_snapshot_ping(void *ctx, double number, double *out) {
  int64_t ping = 0;
  if (!qa_q3_client_connection_snapshot_ping(((source_ctx *)ctx)->remote->connection, (int64_t)number, &ping)) return false;
  *out = (double)ping;
  return true;
}
/* commands: { currentNumber, read: number => ({ ...value, angles: { x, y, z } }) }. */
static double source_command_number(void *ctx) {
  return (double)qa_q3_wire_command_history_current_number(qa_q3_client_connection_commands(((source_ctx *)ctx)->remote->connection));
}
static bool source_command_read(void *ctx, double number, qa_q3_base_user_command *out) {
  qa_q3_wire_user_command value;
  if (!qa_q3_wire_command_history_read(qa_q3_client_connection_commands(((source_ctx *)ctx)->remote->connection),
                                       (int64_t)number, &value))
    return false;
  out->server_time = value.server_time;
  out->angles = (qa_vec3){value.angles[0], value.angles[1], value.angles[2]};
  out->buttons = value.buttons;
  out->weapon = value.weapon;
  out->forwardmove = value.forwardmove;
  out->rightmove = value.rightmove;
  out->upmove = value.upmove;
  return true;
}
void qa_app_q3_remote_attach(qa_app_q3_remote *remote, qa_q3_client_connection *connection) {
  remote->connection = connection;
  qa_q3_pres_history_snapshot_source_init(&remote->history_source,
      qa_q3_client_connection_history(connection), connection,
      history_parse_entities, history_print);
  remote->history_init = true;
  remote->source_ctx.remote = remote;
  remote->source.ctx = &remote->source_ctx;
  remote->source.game_state = source_game_state;
  remote->source.server_command = source_server_command;
  remote->source.actor_at = source_actor_at;
  remote->source.demo = source_demo;
  remote->source.commands.ctx = &remote->source_ctx;
  remote->source.commands.current_number = source_command_number;
  remote->source.commands.read = source_command_read;
  remote->source.snapshots = qa_q3_pres_history_snapshot_source_as_source(&remote->history_source);
  remote->source.client_number = source_client_number;
  remote->source.time = source_time;
  remote->source.server_message_sequence = source_server_message_sequence;
  remote->source.last_executed_server_command = source_last_executed_server_command;
  remote->source.system_info = source_system_info;
  remote->source.sound_actor = NULL; /* the remote source has no soundActor */
  remote->source.snapshot_ping = source_snapshot_ping;
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
  double pure_value = pure ? qa_js_string_to_number(pure) : 0;
  free(pure);
  if (!is_demo(remote) && pure_value != 0 && !remote->options.initialize)
    js_error("This server requires pure verification, which is not supported yet.");
  char *game = qa_q3_info_value(info, "fs_game");
  if (!game) qa_range_error("out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_string, &game);
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make("q3-baseq3", game, &selection))
    qa_range_error("out of memory");
  qa_cleanup_pop(&guard);
  qa_remote_content_selection_free(&selection);
  free(game);
}
static char *game_state_get(qa_app_q3_remote *remote, int64_t index) {
  char *out = NULL;
  if (!qa_q3_client_game_state_storage_get(
          qa_q3_client_connection_game_state(remote->connection), index, &out))
    qa_range_error("out of memory");
  return out;
}
/* Owned scratch of one gamestate. The server-provided info string reaches
 * q3InfoValue (CommonError on an oversize infostring), and assertCurrent,
 * downloads.prepare, loadContent and initialize are host code that raise; a
 * catching caller loses nothing. */
typedef struct {
  char *info, *text, *map, *map_path;
  char *models[256]; size_t model_count;
  char *sounds[256]; size_t sound_count;
} gamestate_scratch;
static void gamestate_scratch_release(void *arg) {
  gamestate_scratch *sc = arg;
  free(sc->info); free(sc->text); free(sc->map); free(sc->map_path);
  for (size_t i = 0; i < sc->model_count; i++) free(sc->models[i]);
  for (size_t i = 0; i < sc->sound_count; i++) free(sc->sounds[i]);
  memset(sc, 0, sizeof *sc);
}
/* names(first, count): nonempty configstrings in index order. */
static void gamestate_names(qa_app_q3_remote *remote, int64_t first, char **names, size_t *count) {
  for (int64_t i = 0; i < 256; i++) {
    char *value = game_state_get(remote, first + i);
    if (value && value[0] != '\0') names[(*count)++] = value;
    else free(value);
  }
}
static bool valid_map_name(const char *map) {
  if (!map[0] || strstr(map, "..")) return false;
  for (const char *p = map; *p; p++) {
    char c = *p;
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || c == '_' || c == '/' || c == '-')) return false;
  }
  return true;
}
static void gamestate_body(qa_app_q3_remote *remote, gamestate_scratch *sc, const qa_q3_gamestate *state,
    qa_app_q3_remote_assert_fn assert_current, void *assert_ctx) {
  sc->info = game_state_get(remote, 0);
  if (!sc->info) { sc->info = malloc(1); if (!sc->info) qa_range_error("out of memory"); sc->info[0] = 0; }
  sc->text = qa_q3_info_value(sc->info, "protocol");
  if (!sc->text) qa_range_error("out of memory");
  if (strcmp(sc->text, "68") != 0) js_error("Q3 remote requires protocol 68");
  free(sc->text);
  sc->text = qa_q3_info_value(sc->info, "g_gametype");
  if (!sc->text) qa_range_error("out of memory");
  double game_type = qa_js_string_to_number(sc->text);
  if (game_type != trunc(game_type) || game_type < 0 || game_type > 4)
    js_error("Q3 remote requires a baseq3 game type");
  sc->map = qa_q3_info_value(sc->info, "mapname");
  if (!sc->map) qa_range_error("out of memory");
  if (!valid_map_name(sc->map)) js_error("Invalid Q3 remote map name");
  remote->game_state_message =
      (double)qa_q3_client_connection_server_message_sequence(remote->connection);
  remote->game_state_commands = (double)state->command_sequence;
  bool downloading = is_demo(remote) ? false
      : remote->options.has_downloads && remote->options.downloads.prepare
          ? remote->options.downloads.prepare(remote->options.downloads.ctx, remote->connection)
          : false;
  assert_current(assert_ctx);
  remote->loading_downloads = downloading;
  if (downloading) return;
  size_t path_size = strlen(sc->map) + sizeof "maps/.bsp";
  sc->map_path = xmalloc(path_size);
  snprintf(sc->map_path, path_size, "maps/%s.bsp", sc->map);
  gamestate_names(remote, 32, sc->models, &sc->model_count);
  gamestate_names(remote, 288, sc->sounds, &sc->sound_count);
  void *content = remote->options.load_content(remote->options.ctx, sc->map_path,
      (const char *const *)sc->models, sc->model_count,
      (const char *const *)sc->sounds, sc->sound_count, remote->connection);
  assert_current(assert_ctx);
  qa_app_remote_world_set_loaded(remote->world, content);
  if (remote->options.initialize)
    remote->options.initialize(remote->options.ctx, remote->connection);
  assert_current(assert_ctx);
}
void qa_app_q3_remote_gamestate(qa_app_q3_remote *remote, const qa_q3_gamestate *state,
    double generation, qa_app_q3_remote_assert_fn assert_current, void *assert_ctx) {
  (void)generation; /* TS `_generation`: unused by the remote presentation */
  assert_current(assert_ctx);
  if (!remote->connection) js_error("Q3 gamestate has no connection");
  gamestate_scratch *sc = calloc(1, sizeof *sc);
  if (!sc) qa_range_error("out of memory");
  qa_cleanup scratch_guard, release_guard;
  qa_cleanup_push(&scratch_guard, free, sc);
  qa_cleanup_push(&release_guard, gamestate_scratch_release, sc);
  gamestate_body(remote, sc, state, assert_current, assert_ctx);
  qa_cleanup_pop(&release_guard);
  qa_cleanup_pop(&scratch_guard);
  gamestate_scratch_release(sc);
  free(sc);
}
double qa_app_q3_remote_download_size(qa_app_q3_remote *remote, double size) {
  if (is_demo(remote)) js_error("Q3 demo cannot request package downloads");
  if (!remote->options.has_downloads)
    js_error("Q3 package downloads have no writable content owner");
  return remote->options.downloads.publish_size(remote->options.downloads.ctx, size);
}
void qa_app_q3_remote_download(qa_app_q3_remote *remote, const qa_q3_download_block *block) {
  if (is_demo(remote)) js_error("Q3 demo cannot request package downloads");
  if (!remote->options.has_downloads)
    js_error("Q3 package downloads have no writable content owner");
  remote->options.downloads.receive(remote->options.downloads.ctx, block);
}
void qa_app_q3_remote_snapshot(qa_app_q3_remote *remote,
    const qa_q3_wire_snapshot *snapshot, double ping) {
  (void)ping;
  if (remote->has_current) qa_q3_wire_snapshot_destroy(&remote->current);
  remote->has_current = false;
  if (!qa_q3_wire_snapshot_copy(&remote->current, snapshot)) qa_range_error("out of memory");
  remote->has_current = true;
  qa_q3_client_clock_publish(remote->clock, &remote->current);
  if (remote->prediction) capture_prediction(remote);
  publish_remote(remote, (double)snapshot->server_time);
}
void qa_app_q3_remote_map_restart(qa_app_q3_remote *remote) {
  if (remote->prediction && remote->has_current) capture_prediction(remote);
}
static const qa_q3_wire_snapshot *require_player(qa_app_q3_remote *remote, qa_actor_id actor) {
  if (!remote->has_current
      || !qa_same_actor(actor_at(remote, (double)remote->current.player_state.client_num), actor))
    js_error("No decoded Q3 player state");
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
/* Capacities for q3GuestPlayerUi's caller-owned outputs: every source powerup,
 * a weapon and an ammo row per catalog weapon, one item per catalog weapon. */
enum { REMOTE_UI_POWERUPS = 32, REMOTE_UI_INVENTORY = 2 * QA_Q3_STANDARD_GUEST_WEAPON_COUNT,
       REMOTE_UI_ITEMS = QA_Q3_STANDARD_GUEST_WEAPON_COUNT };
qa_app_sim_player_ui qa_app_q3_remote_player_ui(qa_app_q3_remote *remote, qa_actor_id actor) {
  const qa_q3_wire_snapshot *snapshot = require_player(remote, actor);
  const qa_executable_recipe *recipe = recipe_of(remote);
  if (recipe->weapons.count == 0) js_error("Q3 remote has no native weapon provider");
  if (!remote->ui.powerups) {
    remote->ui.powerups = xmalloc(REMOTE_UI_POWERUPS * sizeof *remote->ui.powerups);
    remote->ui.powerup_capacity = REMOTE_UI_POWERUPS;
  }
  if (!remote->ui.inventory) {
    remote->ui.inventory = xmalloc(REMOTE_UI_INVENTORY * sizeof *remote->ui.inventory);
    remote->ui.inventory_capacity = REMOTE_UI_INVENTORY;
  }
  if (!remote->ui.items) {
    remote->ui.items = xmalloc(REMOTE_UI_ITEMS * sizeof *remote->ui.items);
    remote->ui.item_capacity = REMOTE_UI_ITEMS;
  }
  qa_q3_adapted_player_state adapted;
  qa_q3_to_player_state(&snapshot->player_state, &adapted);
  /* this.current?.serverTime ?? ps.commandTime: requirePlayer guarantees current. */
  remote->ui = qa_app_q3_guest_player_ui(adapted.state, recipe->weapons.values[0],
      (double)snapshot->server_time, NULL, 0, product_name(snapshot->player_state.product),
      NULL, 0, false, NULL, false, remote->ui);
  const qa_app_guest_player_ui *guest = &remote->ui;
  qa_app_sim_player_ui ui = {0};
  ui.health = guest->health;
  ui.armor = guest->armor;
  ui.powerups = guest->powerups;
  ui.powerup_count = guest->powerup_count;
  ui.weapon_status = guest->has_weapon_status ? &guest->weapon_status : NULL;
  ui.arsenal_warning = guest->arsenal_warning;
  ui.active_weapon = guest->active_weapon;
  ui.has_ammo = guest->has_ammo;
  ui.ammo_item = guest->ammo_item;
  ui.ammo_count = guest->ammo_count;
  ui.inventory = guest->inventory;
  ui.inventory_count = guest->inventory_count;
  ui.items = (const qa_app_sim_player_ui_item *)guest->items;
  ui.item_count = guest->item_count;
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
static bool has_command_delimiter(const char *value) {
  for (const char *p = value; *p; p++)
    if (*p == '"' || *p == '\n' || *p == '\r' || *p == ';') return true;
  return false;
}
void qa_app_q3_remote_player_command(qa_app_q3_remote *remote, qa_actor_id actor,
    const char *name, const char *const *args, size_t count) {
  require_player(remote, actor);
  bool invalid = has_command_delimiter(name);
  for (size_t i = 0; i < count && !invalid; i++) invalid = has_command_delimiter(args[i]);
  if (invalid) js_error("Invalid Q3 command delimiter");
  size_t total = strlen(name) + 1;
  for (size_t i = 0; i < count; i++) total += strlen(args[i]) + 3;
  char *line = xmalloc(total);
  size_t used = (size_t)snprintf(line, total, "%s", name);
  for (size_t i = 0; i < count; i++)
    used += (size_t)snprintf(line + used, total - used, " \"%s\"", args[i]);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_string, &line);
  remote->options.send_command(remote->options.ctx, line);
  qa_cleanup_pop(&guard);
  free(line);
}
qa_q3_wire_user_command qa_app_q3_remote_command(qa_app_q3_remote *remote,
    const qa_actor_command *command) {
  const qa_q3_wire_snapshot *snapshot = require_player(remote, command->actor);
  if (command->command.kind != QA_USER_COMMAND_Q3)
    js_error("Native Q3 remote requires Q3 commands");
  double time = (double)qa_q3_client_clock_time(remote->clock);
  qa_q3_base_user_command presented = qa_app_presentation_source_command(
      *command, time, command->command.as.q3.weapon);
  qa_q3_base_user_command source = qa_app_sim_relative_q3_source_command(
      command->source, "q3", presented, snapshot->player_state.delta_angles, command->angle_space);
  qa_q3_wire_user_command result = {0};
  /* TS keeps these as numbers and the delta writer emits their low bits; the int32
   * wire fields take ToInt32 (the old implicit conversions were undefined for NaN). */
  result.server_time = qa_js_to_int32(time);
  result.buttons = qa_js_to_int32(source.buttons);
  result.weapon = qa_js_to_int32(source.weapon);
  result.forwardmove = qa_js_to_int32(source.forwardmove);
  result.rightmove = qa_js_to_int32(source.rightmove);
  result.upmove = qa_js_to_int32(source.upmove);
  /* JS `x & 65535` (ToInt32 first). */
  result.angles[0] = qa_int32(source.angles.x) & 65535;
  result.angles[1] = qa_int32(source.angles.y) & 65535;
  result.angles[2] = qa_int32(source.angles.z) & 65535;
  return result;
}
const qa_simulation_output *qa_app_q3_remote_sample_presentation(qa_app_q3_remote *remote,
    double now) {
  int64_t time = 0;
  if (is_demo(remote)) {
    if (!remote->has_current) return NULL;
    time = qa_q3_client_clock_time(remote->clock);
  } else {
    double presented = trunc(remote->options.presentation_time
        ? remote->options.presentation_time(remote->options.ctx) : now);
    qa_q3_clock_options options = {0};
    options.time_nudge = remote->options.time_nudge ? remote->options.time_nudge(remote->options.ctx) : 0;
    options.timescale = remote->options.timescale ? remote->options.timescale(remote->options.ctx) : 1;
    /* Q3ClientClock.advance's sourceClock(realTime) rejects what int64 cannot carry. */
    if (!(presented >= -2147483648.0 && presented <= 2147483647.0))
      qa_range_error("Q3 source clock arithmetic exceeds signed int32");
    if (!qa_q3_client_clock_advance(remote->clock, (int64_t)presented, &options, &time))
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
  if (!qa_app_q3_remote_player(remote, &player)) js_error("Q3 prediction has no player");
  const qa_executable_recipe *recipe = recipe_of(remote);
  qa_app_prediction_source_entities entities = source_entities(remote);
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
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_prediction, &options.initial);
  options.entities = entities;
  options.host_ctx = remote;
  options.relative_source_command = relative_hook;
  remote->prediction = qa_app_create_presentation_movement_host(options);
  qa_cleanup_pop(&guard);
  qa_app_movement_prediction_snapshot_destroy(&options.initial);
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
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_string, &line);
  qa_app_q3_remote_print(remote, line);
  qa_cleanup_pop(&guard);
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
