/* Port of src/app/bootstrap/q3-client.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/q3_client/services.h"
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include "qa/app/bootstrap/q3_client/view.h"
#include "qa/app/bootstrap/q3_client/visibility.h"
#include "qa/app/bootstrap/weapon_view.h"
#include "qa/content/q3/presentation/config.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/world_mark_projector.h"
#include "qa/core/cvars/mirror.h"
#include "qa/core/info_string.h"
#include "qa/core/math.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/input/mouse_buttons.h"
#include "qa/materials/q3_lighting.h"
#include "qa/movement/q3/constants.h"
#include "qa/render/commands/frame.h"
#include "qa/render/commands/material2d.h"
#include "qa/render/q3_hardware.h"
#include "qa/render/scene/models/light_sampler.h"
#include "qa/render/scene/portal.h"
#include "qa/render/scene/submissions.h"
#include "qa/render/scene/view.h"
#include "qa/render/scene/visibility.h"
#include "qa/render/scene/world.h"
#include "qa/world/collision/q3/settings.h"

static noreturn void js_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
static noreturn void js_error(const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size == 0 ? 1 : size);
  if (next == NULL) qa_range_error("Out of memory");
  return next;
}
static char *xstrdup(const char *text) {
  char *copy = strdup(text != NULL ? text : "");
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static char *lower(const char *text) {
  char *out = qa_js_to_lower_case(text != NULL ? text : "");
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* Map<number, ActorId> keyed by actor slot. */
typedef struct slot_map { qa_actor_id *actors; size_t count, capacity; } slot_map;
static void slot_set(slot_map *map, qa_actor_id actor) {
  for (size_t i = 0; i < map->count; i++)
    if (map->actors[i].slot == actor.slot) { map->actors[i] = actor; return; }
  if (map->count == map->capacity) {
    map->capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    map->actors = xrealloc(map->actors, map->capacity * sizeof *map->actors);
  }
  map->actors[map->count++] = actor;
}
/* map.get(actor.slot)?.equals(actor) */
static bool slot_holds(const slot_map *map, qa_actor_id actor) {
  for (size_t i = 0; i < map->count; i++)
    if (map->actors[i].slot == actor.slot) return qa_same_actor(map->actors[i], actor);
  return false;
}
typedef struct pose { qa_actor_id actor; qa_vec3 origin, angles; } pose;
typedef enum submission_kind { SUBMISSION_SCENE, SUBMISSION_TEXT, SUBMISSION_COMMAND } submission_kind;
typedef struct submission {
  submission_kind kind;
  qa_q3_pres_presented_scene *scene; /* owned */
  qa_material_text_draw draw;
  qa_render_command command;
} submission;
typedef struct owned_block { void (*free_fn)(void *); void *block; } owned_block;
typedef enum backend_kind { BACKEND_NONE, BACKEND_TYPESCRIPT, BACKEND_QVM } backend_kind;

struct qa_app_application_q3_client {
  qa_app_application_q3_client_options options;
  qa_app_q3_client_source source;
  bool has_round; qa_app_q3_local_round round;
  qa_app_application_q3_source *local_source;
  qa_q3_base_product product;
  bool supplemental_view_weapon, view_weapon_visible;
  slot_map selected_held_actors;
  bool has_weapon_selection; double weapon_selection;
  qa_cvar_registry *cvar_owner; bool owns_cvars;
  char **command_names; size_t command_name_count;
  backend_kind backend;
  qa_q3_pres_client_presentation *game;
  qa_app_application_qvm_client *qvm;
  submission *submissions; size_t submission_count, submission_capacity;
  qa_event_ring audio_operations; qa_event_cursor audio_cursor;
  qa_app_application_q3_assets *media;
  qa_app_application_q3_scene_renderer *scene_renderer;
  qa_scene_frame_builder frames;
  owned_block *frame_store; size_t frame_store_count, frame_store_capacity;
  qa_rect viewport;
  qa_model_light_sampler light_sampler;
  qa_scene_camera latest_camera;
  double frame_number;
  bool closed, initializing;
  qa_shared_cvar_mirror *time_mirror;
  qa_cvar_mirror_registry mirror_owner, mirror_target;
  char *applied_time_system_info, *applied_system_info;
  double key_catcher;
  qa_app_q3_command_selection selection;
  qa_app_application_q3_cinematics *cinematics;
  double event_count;
  qa_app_application_q3_services *services;
  char **shared_cvar_names; size_t shared_cvar_name_count;
  pose *body_poses; size_t body_pose_count, body_pose_capacity;
  slot_map pose_actors, hidden_bodies, selected_bodies;
  qa_app_prepared_primary_body *primary_bodies; size_t primary_body_count, primary_body_capacity;
  bool status_visible;
  qa_collision_map_settings collision_settings;
  qa_q3_pres_fog_selection *fog_selections; size_t fog_selection_count;
  qa_app_qvm_presentation_artifacts *artifacts;
  qa_app_qvm_application_scalars scalars;
  qa_q3_pres_presentation_session session;
  qa_q3_pres_world_mark_projector marks; bool has_marks;
  qa_app_qvm_body_capture body_capture;
  qa_app_qvm_body_overrides body_overrides;
  qa_app_ui_cinematic_asset prepared_cinematic;
  qa_ui_cinematic_asset prepared_menu_cinematic;
  /* qa_app_q3_snapshot_selection for the local transport. */
  qa_app_q3_snapshot_selection *selection_state;
  qa_app_q3_snapshot_source_entity *selection_rows;
  bool has_frames, has_light_sampler;
  void **body_store; size_t body_store_count; /* custom skin arrays of primary_bodies */
  double server_message_sequence;
};

/* ---- option shortcuts ---- */
static void assert_current_option(qa_app_application_q3_client *client) {
  if (client->options.assert_current != NULL) client->options.assert_current(client->options.ctx);
}
static void print_option(qa_app_application_q3_client *client, const char *text) {
  client->options.print(client->options.ctx, text);
}
static void print_ctx(void *ctx, const char *text) { print_option(ctx, text); }
/* options.viewport() with the closure's own context. */
static qa_rect option_viewport(const qa_app_application_q3_client_options *o) {
  return o->viewport(o->viewport_ctx != NULL ? o->viewport_ctx : o->ctx);
}
static qa_seat_id seat_of(const qa_app_application_q3_client *client) {
  return qa_session_seat_id(client->options.local->player.seat);
}
static qa_client_id client_of(const qa_app_application_q3_client *client) {
  return qa_session_client_id(qa_session_seat_client(client->options.local->player.seat));
}

static void frame_store_add(qa_app_application_q3_client *client, void (*free_fn)(void *), void *block) {
  if (client->frame_store_count == client->frame_store_capacity) {
    client->frame_store_capacity = client->frame_store_capacity == 0 ? 16 : client->frame_store_capacity * 2;
    client->frame_store = xrealloc(client->frame_store, client->frame_store_capacity * sizeof *client->frame_store);
  }
  client->frame_store[client->frame_store_count++] = (owned_block){free_fn, block};
}
static void frame_store_clear(qa_app_application_q3_client *client) {
  for (size_t i = client->frame_store_count; i > 0; i--) client->frame_store[i - 1].free_fn(client->frame_store[i - 1].block);
  client->frame_store_count = 0;
}
static const qa_image_resource_operation *registry_drain(void *ctx, size_t *out_count) {
  qa_app_application_q3_client *client = ctx;
  /* Drained operations stay with the frame until the next frame (the builder borrows them). */
  qa_image_resource_operation *drained =
      qa_scene_image_registry_drain_operations(qa_app_application_assets_images(client->options.assets), out_count);
  if (drained != NULL) frame_store_add(client, free, drained);
  return drained;
}
static void registry_require(void *ctx, qa_renderer_image image) {
  qa_scene_image_registry_require(qa_app_application_assets_images(((qa_app_application_q3_client *)ctx)->options.assets), image);
}
static void clear_submissions(qa_app_application_q3_client *client) {
  for (size_t i = 0; i < client->submission_count; i++)
    if (client->submissions[i].scene != NULL) {
      qa_q3_pres_presented_scene_destroy(client->submissions[i].scene);
      free(client->submissions[i].scene);
    }
  client->submission_count = 0;
}
static submission *push_submission(qa_app_application_q3_client *client) {
  if (client->submission_count == client->submission_capacity) {
    client->submission_capacity = client->submission_capacity == 0 ? 16 : client->submission_capacity * 2;
    client->submissions = xrealloc(client->submissions, client->submission_capacity * sizeof *client->submissions);
  }
  submission *s = &client->submissions[client->submission_count++];
  memset(s, 0, sizeof *s);
  return s;
}

/* ---- source ---- */
static qa_app_application_q3_source *local_source_or_throw(qa_app_application_q3_client *client) {
  if (client->local_source == NULL) js_error("Missing native local source");
  return client->local_source;
}
static const qa_app_q3_local_round *local_round(const qa_app_application_q3_client *client) {
  if (!client->has_round) js_error("Q3 client has no native local round");
  return &client->round;
}
static qa_actor_id source_actor_at(const qa_app_application_q3_client *client, double number) {
  return client->source.actor_at(client->source.ctx, number);
}
static double source_time(const qa_app_application_q3_client *client) { return client->source.time(client->source.ctx); }

/* The local ApplicationQ3Source as an ApplicationQ3ClientSource. */
static size_t local_game_state(void *ctx, const char *const **strings) {
  size_t count = 0;
  *strings = qa_app_application_q3_source_game_state(((qa_app_application_q3_client *)ctx)->local_source, &count);
  return count;
}
static bool local_server_command(void *ctx, double sequence, const char *const **strings, size_t *out_count) {
  return qa_app_application_q3_source_get_server_command(((qa_app_application_q3_client *)ctx)->local_source, sequence,
                                                         strings, out_count);
}
static qa_actor_id local_actor_at(void *ctx, double number) {
  return qa_app_application_q3_source_actor_at(((qa_app_application_q3_client *)ctx)->local_source, number);
}
static bool local_demo(void *ctx) {
  return is(qa_app_application_q3_source_mode(((qa_app_application_q3_client *)ctx)->local_source), "demo");
}
static double local_client_number(void *ctx) {
  return qa_app_application_q3_source_client_number(((qa_app_application_q3_client *)ctx)->local_source);
}
static double local_time(void *ctx) { return qa_app_application_q3_source_time(((qa_app_application_q3_client *)ctx)->local_source); }
static bool local_sound_actor(void *ctx, double number, qa_actor_id *out) {
  return qa_app_application_q3_source_sound_actor(((qa_app_application_q3_client *)ctx)->local_source, number, out);
}
static qa_q3_pres_snapshot_current local_current(void *ctx) {
  qa_q3_pres_snapshot_current current;
  qa_app_application_q3_source_current(((qa_app_application_q3_client *)ctx)->local_source, &current.number,
                                       &current.server_time);
  return current;
}
static bool local_read(void *ctx, double number, qa_q3_wire_snapshot *out) {
  const qa_q3_wire_snapshot *stored = NULL;
  if (!qa_app_application_q3_source_read(((qa_app_application_q3_client *)ctx)->local_source, number, &stored)) return false;
  if (!qa_q3_wire_snapshot_copy(out, stored)) qa_range_error("Out of memory");
  return true;
}
/* TS selectApplicationQ3Snapshot(player, source, queries, linkBounds, leaves, print). */
static bool round_link_bounds(void *ctx, double number, qa_bounds *out) {
  const qa_app_q3_local_round *round = local_round(ctx);
  return round->link_bounds(round->ctx, number, out);
}
static void local_select(void *ctx, const qa_q3_player_state_record *player, const qa_app_q3_source_presentation_state *state,
                         qa_q3_visible_entities *out) {
  qa_app_application_q3_client *client = ctx;
  qa_app_q3_snapshot_selection_destroy(client->selection_state);
  client->selection_state = NULL;
  free(client->selection_rows);
  client->selection_rows = xcalloc(state->entity_count, sizeof *client->selection_rows);
  for (size_t i = 0; i < state->entity_count; i++) {
    client->selection_rows[i].state = &state->entities[i].state;
    client->selection_rows[i].linked = state->entities[i].linked;
    client->selection_rows[i].server_flags = state->entities[i].server_flags;
    client->selection_rows[i].single_client = state->entities[i].single_client;
  }
  client->selection_state = qa_app_select_application_q3_snapshot(player, client->selection_rows, state->entity_count,
      client->options.queries, round_link_bounds, client,
      qa_app_world_leaf_count(qa_app_application_assets_world(client->options.assets)), print_ctx, client);
  if (client->selection_state == NULL) qa_range_error("Out of memory");
  *out = *qa_app_q3_snapshot_selection_visible(client->selection_state);
}
static bool local_source_actor(void *ctx, double number, qa_actor_id *out) {
  const qa_app_q3_local_round *round = local_round(ctx);
  return round->source_actor(round->ctx, number, out);
}
static qa_q3_base_user_command local_prediction(void *ctx, qa_actor_command command, double time) {
  const qa_app_q3_local_round *round = local_round(ctx);
  if (round->prediction_command != NULL) return round->prediction_command(round->ctx, command, time);
  if (command.command.kind != QA_USER_COMMAND_Q3) js_error("Foreign movement requires prediction command binding");
  const qa_q3_user_command *input = &command.command.as.q3;
  qa_q3_base_user_command out;
  memset(&out, 0, sizeof out);
  out.server_time = input->server_time_milliseconds;
  out.angles.x = input->angle_words.values[0];
  out.angles.y = input->angle_words.values[1];
  out.angles.z = input->angle_words.values[2];
  out.buttons = input->buttons;
  out.weapon = input->weapon;
  out.forwardmove = input->forward_move;
  out.rightmove = input->right_move;
  out.upmove = input->up_move;
  return out;
}
static void local_level_shot(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  qa_seat_console_close(client->options.local->console);
  client->options.console(client->options.ctx, "wait; wait; wait; wait; screenshot levelshot\n");
}

/* ---- cvars ---- */
static qa_cvar_snapshot *server_settings(qa_app_application_q3_client *client, size_t *out_count) {
  *out_count = 0;
  if (!client->has_round) return client->options.server_settings == NULL ? NULL
      : client->options.server_settings(client->options.ctx, out_count);
  return client->round.server_settings == NULL ? NULL : client->round.server_settings(client->round.ctx, out_count);
}
static void free_settings(qa_cvar_snapshot *settings, size_t count) {
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&settings[i]);
  free(settings);
}
static void cvar_set_forced(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot ignored;
  if (qa_cvar_registry_set(cvars, name, value, true, &ignored)) qa_cvar_read_free(&ignored);
}
static bool shared_cvar(const qa_app_application_q3_client *client, const char *name) {
  char *key = lower(name);
  bool found = false;
  for (size_t i = 0; i < client->shared_cvar_name_count && !found; i++) found = strcmp(client->shared_cvar_names[i], key) == 0;
  free(key);
  return found;
}
static void apply_shared_server_settings(qa_app_application_q3_client *client) {
  size_t count = 0;
  qa_cvar_snapshot *settings = server_settings(client, &count);
  for (size_t i = 0; i < count; i++)
    if (shared_cvar(client, settings[i].name)) cvar_set_forced(client->cvar_owner, settings[i].name, settings[i].value);
  free_settings(settings, count);
}
static int cheats_allowed(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  size_t count = 0;
  qa_cvar_snapshot *settings = server_settings(client, &count);
  int result = -2;
  for (size_t i = 0; i < count && result == -2; i++) {
    char *name = lower(settings[i].name);
    if (strcmp(name, "sv_cheats") == 0) result = settings[i].integer_value != 0 ? 1 : 0;
    free(name);
  }
  free_settings(settings, count);
  if (result != -2) return result;
  if (client->source.system_info == NULL) return -1;
  char *value = qa_info_value_for_key(client->source.system_info(client->source.ctx), "sv_cheats");
  if (value == NULL) qa_range_error("Out of memory");
  result = strcmp(value, "1") == 0 ? 1 : 0;
  free(value);
  return result;
}
typedef struct skip_state { qa_app_application_q3_client *client; const char *info; } skip_state;
static bool skip_time_cvar(void *ctx, const char *name) {
  skip_state *state = ctx;
  qa_app_application_q3_client *client = state->client;
  char *key = lower(name);
  bool skip = false;
  if (client->options.time_cvars != NULL) {
    size_t count = 0;
    const char *const *names = qa_app_frame_time_cvar_names(qa_cvar_registry_dialect(client->options.time_cvars), &count);
    for (size_t i = 0; i < count && !skip; i++) skip = strcmp(names[i], key) == 0;
  }
  if (!skip && strcmp(key, "timescale") == 0)
    skip = client->applied_time_system_info != NULL && strcmp(state->info, client->applied_time_system_info) == 0;
  free(key);
  return skip;
}
void qa_app_application_q3_client_refresh_system_info(qa_app_application_q3_client *client) {
  if (client->closed) js_error("Q3 presentation is closed");
  assert_current_option(client);
  const char *info = client->source.system_info == NULL ? NULL : client->source.system_info(client->source.ctx);
  /* A server's systeminfo is applied when it changes, not rewritten over the player's settings every frame. */
  if (info != NULL && (client->initializing || client->applied_system_info == NULL || strcmp(info, client->applied_system_info) != 0)) {
    char *copy = xstrdup(info);
    skip_state state = {client, copy};
    qa_cleanup node;
    qa_cleanup_push(&node, free, copy);
    qa_app_apply_server_system_info(client->cvar_owner, copy, skip_time_cvar, &state);
    qa_cleanup_pop(&node);
    free(client->applied_time_system_info);
    client->applied_time_system_info = xstrdup(copy);
    free(client->applied_system_info);
    client->applied_system_info = copy;
  }
  if (client->time_mirror != NULL) qa_shared_cvar_mirror_refresh(client->time_mirror);
  else if (client->options.time_cvars != NULL) qa_app_refresh_frame_time_cvars(client->options.time_cvars, client->cvar_owner);
}
static void mirror_assert(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  if (client->closed) js_error("Q3 presentation is closed");
  assert_current_option(client);
}
static void bind_frame_time(qa_app_application_q3_client *client) {
  if (client->time_mirror != NULL) qa_shared_cvar_mirror_destroy(client->time_mirror);
  client->time_mirror = NULL;
  qa_cvar_registry *owner = client->options.time_cvars;
  if (owner == NULL || owner == client->cvar_owner) return;
  qa_app_application_q3_client_refresh_system_info(client);
  size_t time_count = 0;
  const char *const *time_names = qa_app_frame_time_cvar_names(qa_cvar_registry_dialect(owner), &time_count);
  const char **names = xcalloc(time_count + 3, sizeof *names);
  size_t count = 0;
  for (size_t i = 0; i < time_count + 3; i++) {
    const char *name = i < time_count ? time_names[i] : qa_collision_map_cvar_definitions[i - time_count].name;
    qa_cvar_read read;
    if (!qa_cvar_registry_find(owner, name, &read)) continue;
    qa_cvar_read_free(&read);
    names[count++] = name;
  }
  client->mirror_owner = qa_cvar_registry_mirror_view(owner);
  client->mirror_target = qa_cvar_registry_mirror_view(client->cvar_owner);
  qa_cleanup node;
  qa_cleanup_push(&node, free, names);
  client->time_mirror = qa_shared_cvar_mirror_create(&client->mirror_owner, &client->mirror_target, names, count,
                                                     mirror_assert, client);
  qa_cleanup_pop(&node);
  free(names);
  if (client->time_mirror == NULL) qa_range_error("Out of memory");
}

/* ---- simple accessors ---- */
const qa_app_application_q3_client_options *qa_app_application_q3_client_options_of(const qa_app_application_q3_client *client) {
  return &client->options;
}
void qa_app_application_q3_client_reliable(qa_app_application_q3_client *client, const char *text) {
  client->options.reliable(client->options.ctx, text);
}
const qa_app_q3_client_source *qa_app_application_q3_client_source(qa_app_application_q3_client *client) {
  return &client->source;
}
qa_app_application_q3_assets *qa_app_application_q3_client_media(qa_app_application_q3_client *client) { return client->media; }
qa_cvar_registry *qa_app_application_q3_client_cvars(const qa_app_application_q3_client *client) { return client->cvar_owner; }
qa_cvar_registry *qa_app_application_q3_client_time_cvars(const qa_app_application_q3_client *client) {
  return client->options.time_cvars;
}
void qa_app_application_q3_client_adopt_cvars(qa_app_application_q3_client *client, qa_cvar_registry *cvars) {
  if (qa_cvar_registry_dialect(cvars) != qa_cvar_registry_dialect(client->cvar_owner)
      || qa_cvar_registry_context(cvars).session.token != qa_cvar_registry_context(client->cvar_owner).session.token)
    js_error("Q3 client cvar owner changed identity");
  if (client->owns_cvars) {
    /* The registry this client created is replaced; nothing else holds it. */
    if (client->time_mirror != NULL) qa_shared_cvar_mirror_destroy(client->time_mirror);
    client->time_mirror = NULL;
    qa_cvar_registry_destroy(client->cvar_owner);
    client->owns_cvars = false;
  }
  client->cvar_owner = cvars;
  bind_frame_time(client);
}
bool qa_app_application_q3_client_has_command_name(const qa_app_application_q3_client *client, const char *name) {
  for (size_t i = 0; i < client->command_name_count; i++)
    if (strcmp(client->command_names[i], name) == 0) return true;
  return false;
}
static void command_name_add(qa_app_application_q3_client *client, const char *name) {
  if (qa_app_application_q3_client_has_command_name(client, name)) return;
  client->command_names = xrealloc(client->command_names, (client->command_name_count + 1) * sizeof *client->command_names);
  client->command_names[client->command_name_count++] = xstrdup(name);
}
static void command_name_delete(qa_app_application_q3_client *client, const char *name) {
  for (size_t i = 0; i < client->command_name_count; i++)
    if (strcmp(client->command_names[i], name) == 0) {
      free(client->command_names[i]);
      memmove(&client->command_names[i], &client->command_names[i + 1],
              (client->command_name_count - i - 1) * sizeof *client->command_names);
      client->command_name_count--;
      return;
    }
}
const qa_app_prepared_primary_body *qa_app_application_q3_client_shared_bodies(const qa_app_application_q3_client *client,
                                                                              size_t *out_count) {
  *out_count = client->primary_body_count;
  return client->primary_bodies;
}
static qa_app_application_q3_services *require_services(qa_app_application_q3_client *client) {
  if (client->closed || client->services == NULL) js_error("Q3 presentation services are closed or uninitialized");
  return client->services;
}
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_client_resource_diagnostics(qa_app_application_q3_client *client) {
  return qa_app_application_q3_services_resources(require_services(client));
}
static void require_backend(const qa_app_application_q3_client *client) {
  if (client->closed || client->backend == BACKEND_NONE) js_error("Q3 cgame seat is closed or uninitialized");
}
static qa_q3_pres_client_presentation *require_game(const qa_app_application_q3_client *client) {
  require_backend(client);
  if (client->backend != BACKEND_TYPESCRIPT) js_error("This seat runs native guest cgame");
  return client->game;
}
qa_q3_pres_client_presentation *qa_app_application_q3_client_cgame(qa_app_application_q3_client *client) {
  return require_game(client);
}

/* ---- round restart ---- */
void qa_app_application_q3_client_assert_can_restart_round(qa_app_application_q3_client *client) {
  assert_current_option(client);
  if (client->closed || client->local_source == NULL || !client->has_round || client->backend != BACKEND_TYPESCRIPT)
    js_error("Q3 fast restart requires an active native local cgame");
  qa_app_application_q3_source_assert_can_restart_round(client->local_source);
}
void qa_app_application_q3_client_begin_round_restart(qa_app_application_q3_client *client, double bit,
                                                      const qa_app_q3_source_presentation_state *source) {
  qa_app_application_q3_client_assert_can_restart_round(client);
  qa_app_application_q3_source_begin_round_restart(local_source_or_throw(client), bit, source);
  clear_submissions(client);
  qa_event_ring_reset(&client->audio_operations);
}
void qa_app_application_q3_client_rebind_round(qa_app_application_q3_client *client, const qa_app_q3_local_round *binding) {
  assert_current_option(client);
  if (client->closed || client->local_source == NULL || !client->has_round || client->backend != BACKEND_TYPESCRIPT)
    js_error("Q3 fast restart requires an active native local cgame");
  qa_app_application_q3_source_validate_round_actor(client->local_source, binding->actor, binding->initial);
  if (!is(binding->movement.command_timing, client->round.movement.command_timing)
      && binding->movement.command_timing != client->round.movement.command_timing)
    js_error("Q3 restart changed movement timing");
  client->round = *binding;
  qa_app_application_q3_source_rebind_round(client->local_source, binding->actor, binding->initial);
  client->body_pose_count = 0;
  client->pose_actors.count = 0;
  client->has_weapon_selection = false;
}

/* ---- light ---- */
static qa_lighting_sample light(qa_app_application_q3_client *client, qa_vec3 point) {
  qa_lighting_sample sample;
  qa_lighting_scales scales = {1, 1};
  if (client->light_sampler.has_grid && qa_light_for_point(&client->light_sampler.grid, point, scales, &sample)) return sample;
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.camera = client->latest_camera;
  input.time.kind = QA_SOURCE_TIME_MILLISECONDS;
  input.time.value = source_time(client);
  input.target_is_seat = true;
  input.target_seat = seat_of(client);
  qa_model_light_sample sampled = qa_model_light_sampler_sample(&client->light_sampler, point, &input, true);
  sample.ambient_light = (qa_vec3){sampled.color.x * 255, sampled.color.y * 255, sampled.color.z * 255};
  sample.directed_light = (qa_vec3){0, 0, 0};
  sample.light_dir = (qa_vec3){0, 0, 1};
  return sample;
}
static qa_lighting_sample light_ctx(void *ctx, qa_vec3 point) { return light(ctx, point); }

/* ---- bodies ---- */
static void body_store_add(qa_app_application_q3_client *client, void *block) {
  client->body_store = xrealloc(client->body_store, (client->body_store_count + 1) * sizeof *client->body_store);
  client->body_store[client->body_store_count++] = block;
}
static void body_store_clear(qa_app_application_q3_client *client) {
  for (size_t i = 0; i < client->body_store_count; i++) free(client->body_store[i]);
  client->body_store_count = 0;
}
static bool body_hidden(qa_app_application_q3_client *client, double number) {
  if (client->hidden_bodies.count == 0) return false;
  return slot_holds(&client->hidden_bodies, source_actor_at(client, number));
}
static bool body_selected(qa_app_application_q3_client *client, double number) {
  if (client->selected_bodies.count == 0 || body_hidden(client, number)) return false;
  return slot_holds(&client->selected_bodies, source_actor_at(client, number));
}
static bool submit_body(qa_app_application_q3_client *client, double number, qa_qvm_body_part part,
                        const qa_q3_pres_ref_model_entity *source, bool base) {
  if (!body_selected(client, number)) return false;
  qa_q3_pres_ref_entity ref = qa_q3_pres_copy_ref_entity(&(qa_q3_pres_ref_entity){QA_Q3_PRES_REF_MODEL, {.model = *source}});
  if (ref.kind != QA_Q3_PRES_REF_MODEL) js_error("Source body changed reference kind");
  qa_actor_id actor = source_actor_at(client, number);
  qa_q3_pres_presented_model prepared;
  if (!qa_q3_pres_prepare_model(&ref.as.model, &actor, 0, &prepared)) return false;
  qa_content_id content;
  if (!qa_app_application_q3_assets_model_content(client->media, &source->model, &content))
    js_error("Source body lost its registered geometry owner");
  /* TS options: { customShader, customSkin: surfaces, nonNormalizedAxes, shaderTexCoord }. */
  qa_model_custom_skin_entry *skin = NULL;
  if (prepared.custom_skin != NULL) {
    skin = xcalloc(prepared.custom_skin_count, sizeof *skin);
    for (size_t i = 0; i < prepared.custom_skin_count; i++)
      skin[i] = (qa_model_custom_skin_entry){prepared.custom_skin[i].name, prepared.custom_skin[i].shader};
    body_store_add(client, skin);
  }
  if (client->primary_body_count == client->primary_body_capacity) {
    client->primary_body_capacity = client->primary_body_capacity == 0 ? 8 : client->primary_body_capacity * 2;
    client->primary_bodies = xrealloc(client->primary_bodies, client->primary_body_capacity * sizeof *client->primary_bodies);
    for (size_t i = 0; i < client->primary_body_count; i++) client->primary_bodies[i].entity.actor = &client->primary_bodies[i].actor;
  }
  qa_app_prepared_primary_body *body = &client->primary_bodies[client->primary_body_count++];
  memset(body, 0, sizeof *body);
  body->actor = actor;
  body->part = part;
  body->content = content;
  body->entity = prepared.entity;
  body->entity.actor = &body->actor; /* TS entity.actor is the same ActorId */
  body->base = base;
  body->options.custom_shader = prepared.custom_shader;
  body->options.custom_skin = skin;
  body->options.custom_skin_count = skin != NULL ? prepared.custom_skin_count : 0;
  body->options.has_non_normalized_axes = true;
  body->options.non_normalized_axes = prepared.non_normalized_axes;
  body->options.has_shader_tex_coord = true;
  body->options.shader_tex_coord = ref.as.model.shading.shader_tex_coord;
  body->shader_content = qa_app_application_q3_assets_content(client->media);
  body->time = source_time(client);
  return true;
}
static bool capture_active(void *ctx) { return ((qa_app_application_q3_client *)ctx)->selected_bodies.count != 0; }
static bool capture_selected(void *ctx, double entity) { return body_selected(ctx, entity); }
static bool capture_submit(void *ctx, double entity, qa_qvm_body_part part, const qa_q3_pres_ref_model_entity *source,
                           bool base) {
  return submit_body(ctx, entity, part, source, base);
}
static bool overrides_active(void *ctx) { return ((qa_app_application_q3_client *)ctx)->hidden_bodies.count != 0; }
static bool overrides_hidden(void *ctx, double entity) { return body_hidden(ctx, entity); }

/* ---- services ---- */
static qa_app_q3_seat_audio_operation clear_loops(void) {
  qa_app_q3_seat_audio_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_APP_Q3_SEAT_AUDIO_CLEAR_LOOPS;
  operation.kill_all = true;
  return operation;
}
static void flush_audio(qa_app_application_q3_client *client) {
  size_t count = 0;
  qa_app_q3_seat_audio_operation *operations = qa_event_ring_take(&client->audio_operations, &client->audio_cursor, &count);
  if (operations == NULL && count != 0) qa_range_error("Out of memory");
  qa_event_ring_retire(&client->audio_operations);
  qa_app_q3_seat_audio_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.content = qa_app_application_q3_assets_content(client->media);
  frame.seat = seat_of(client);
  frame.operations = operations;
  frame.operation_count = count;
  qa_cleanup node;
  qa_cleanup_push(&node, free, operations);
  qa_app_application_audio_receive_cgame_frame(client->options.audio, &frame);
  qa_cleanup_pop(&node);
  free(operations);
}
static void output_scene(void *ctx, qa_q3_pres_presented_scene *scene) {
  qa_app_application_q3_client *client = ctx;
  qa_q3_pres_presented_scene *owned = xcalloc(1, sizeof *owned);
  *owned = *scene;
  submission *s = push_submission(client);
  s->kind = SUBMISSION_SCENE;
  s->scene = owned;
  if (((int64_t)owned->source.render_flags & QA_Q3_PRES_RDF_NOWORLDMODEL) == 0) client->latest_camera = owned->camera;
}
static void output_command(void *ctx, const qa_render_command *command) {
  submission *s = push_submission(ctx);
  s->kind = SUBMISSION_COMMAND;
  s->command = *command;
}
static void output_text(void *ctx, const qa_material_text_draw *draw) {
  submission *s = push_submission(ctx);
  s->kind = SUBMISSION_TEXT;
  s->draw = *draw;
}
static void output_audio(void *ctx, qa_app_q3_seat_audio_operation operation) {
  qa_app_application_q3_client *client = ctx;
  if (qa_event_ring_admit(&client->audio_operations, &operation) < 0) qa_range_error("Out of memory");
}
static void output_listener(void *ctx, qa_vec3 origin, qa_axis axis) {
  qa_app_application_q3_client *client = ctx;
  client->latest_camera.origin = origin;
  client->latest_camera.axis = axis;
}
static bool services_actor_at(void *ctx, double number, qa_actor_id *out) {
  qa_app_application_q3_client *client = ctx;
  if (client->source.sound_actor == NULL) {
    *out = source_actor_at(client, number);
    return true;
  }
  return client->source.sound_actor(client->source.ctx, number, out);
}
static double clock_now(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  return client->options.now(client->options.ctx);
}
static double clock_frame(void *ctx) { return ((qa_app_application_q3_client *)ctx)->frame_number; }
static double clock_server_time(void *ctx) { return source_time(ctx); }
static bool remap_is_current(void *ctx) { return !((qa_app_application_q3_client *)ctx)->closed; }
static void services_remap(void *ctx, const char *original, const char *replacement, const char *offset) {
  qa_app_application_q3_client *client = ctx;
  assert_current_option(client);
  client->options.remap_shader(client->options.ctx, original, replacement, offset, client->initializing, remap_is_current,
                               client);
  assert_current_option(client);
}
/* ApplicationAssets.world.fogSelections and content.world as the services world. */
static const qa_q3_pres_fog_selection *world_fog_selections(void *ctx, size_t *out_count) {
  qa_app_application_q3_client *client = ctx;
  *out_count = client->fog_selection_count;
  return client->fog_selections;
}
/* ApplicationAudio as the services audio (engine streams and music). */
static void mixer_queue(void *ctx, const qa_audio_stream_target *target, const qa_stream_pcm *pcm) {
  qa_unified_audio_queue_stream(ctx, *target, pcm);
}
static void mixer_stop(void *ctx, const char *id) { qa_unified_audio_stop_stream(ctx, id); }
static void mixer_pause(void *ctx, const char *id, bool paused) { qa_unified_audio_pause_stream(ctx, id, paused); }
static qa_save_value *mixer_capture(void *ctx, const char *id) { return qa_unified_audio_capture_stream_checkpoint(ctx, id); }
static void mixer_restore(void *ctx, const qa_audio_stream_target *target, const qa_save_value *value) {
  qa_unified_audio_restore_stream_checkpoint(ctx, *target, value);
}
static void audio_play_music(void *ctx, qa_content_id content, const char *track) {
  qa_app_application_audio_play_music(ctx, content, track, NULL);
}

/* ---- session ---- */
/* TS serverMessageSequence: source.serverMessageSequence ?? 0, read once when the session is built. */
static double session_server_message_sequence(void *ctx) {
  return ((qa_app_application_q3_client *)ctx)->server_message_sequence;
}
static bool session_status_visible(void *ctx) { return ((qa_app_application_q3_client *)ctx)->status_visible; }
static const char *const *session_game_state(void *ctx, size_t *out_count) {
  const qa_app_q3_client_source *source = &((qa_app_application_q3_client *)ctx)->source;
  const char *const *strings = NULL;
  *out_count = source->game_state(source->ctx, &strings);
  return strings;
}
static bool session_server_command(void *ctx, double sequence, const char *const **out, size_t *out_count) {
  const qa_app_q3_client_source *source = &((qa_app_application_q3_client *)ctx)->source;
  return source->server_command(source->ctx, sequence, out, out_count);
}
static bool session_snapshot_ping(void *ctx, double number, double *out) {
  const qa_app_q3_client_source *source = &((qa_app_application_q3_client *)ctx)->source;
  if (source->snapshot_ping == NULL || !source->snapshot_ping(source->ctx, number, out)) *out = 0;
  return true;
}
static void session_reliable(void *ctx, const char *text) { qa_app_application_q3_client_reliable(ctx, text); }
static void session_console(void *ctx, const char *text) {
  qa_app_application_q3_client *client = ctx;
  client->options.console(client->options.ctx, text);
}
static void session_register(void *ctx, const char *name) {
  qa_app_application_q3_client *client = ctx;
  command_name_add(client, name);
  qa_client_command_owner_register(client->options.command_bindings, client->options.command_registration, name);
}
static void session_selection(void *ctx, double weapon, double sensitivity) {
  qa_app_application_q3_client *client = ctx;
  client->selection.weapon = weapon;
  client->selection.sensitivity = sensitivity;
}
static void session_assert_current(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  assert_current_option(client);
  if (client->closed) js_error("Q3 cgame belongs to a retired world");
}

/* ---- QVM backend options ---- */
static double qvm_now(void *ctx) { return clock_now(ctx); }
static double qvm_key_catcher(void *ctx) { return ((qa_app_application_q3_client *)ctx)->key_catcher; }
static bool qvm_equipment(void *ctx, qa_app_q3_equipment_presentation *out) {
  qa_app_application_q3_client *client = ctx;
  return client->options.equipment_weapon(client->options.ctx, out);
}
static bool qvm_view_weapon_visible(void *ctx) { return ((qa_app_application_q3_client *)ctx)->view_weapon_visible; }
static bool qvm_held_actor(void *ctx, double number, qa_actor_id *out) {
  qa_app_application_q3_client *client = ctx;
  qa_actor_id actor = source_actor_at(client, number);
  if (body_hidden(client, number) || !slot_holds(&client->selected_held_actors, actor)) return false;
  *out = actor;
  return true;
}
static void qvm_remove_command(void *ctx, const char *name) {
  qa_app_application_q3_client *client = ctx;
  command_name_delete(client, name);
  qa_client_command_owner_remove(client->options.command_bindings, client->options.command_registration, name);
}
typedef struct update_screen_state { qa_app_application_qvm_client *owner; const qa_qvm_host_call *call; } update_screen_state;
static void update_screen(void *ctx) {
  update_screen_state *state = ctx;
  qa_app_application_qvm_client_update_screen(state->owner, state->call);
}
static bool qvm_scalar(void *ctx, const qa_qvm_host_call *call, qa_app_application_qvm_client *owner, qa_qvm_host_result *out) {
  qa_app_application_q3_client *client = ctx;
  update_screen_state state = {owner, call};
  return qa_app_qvm_application_scalars_dispatch(&client->scalars, call, update_screen, &state, out);
}
static void scalar_viewport(void *ctx, double *out_width, double *out_height) {
  qa_app_application_q3_client *client = ctx;
  qa_rect viewport = option_viewport(&client->options);
  *out_width = viewport.width;
  *out_height = viewport.height;
}
static bool scalar_overstrike_get(void *ctx) {
  return ((qa_app_application_q3_client *)ctx)->options.local->console->field.overstrike;
}
static void scalar_overstrike_set(void *ctx, bool value) {
  ((qa_app_application_q3_client *)ctx)->options.local->console->field.overstrike = value;
}
static double scalar_key_catcher_get(void *ctx) { return ((qa_app_application_q3_client *)ctx)->key_catcher; }
static void scalar_key_catcher_set(void *ctx, double value) { ((qa_app_application_q3_client *)ctx)->key_catcher = value; }
static qa_app_qvm_client_state_info scalar_client_state(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  return client->options.client_state(client->options.ctx);
}

/* ---- TS presentation options ---- */
static bool media_read(void *ctx, const char *path, uint8_t **out, size_t *out_length) {
  *out = qa_app_application_q3_assets_read(((qa_app_application_q3_client *)ctx)->media, path, out_length);
  return *out != NULL || *out_length == 0;
}
static bool media_has(void *ctx, const char *path) { return qa_app_application_q3_assets_has(((qa_app_application_q3_client *)ctx)->media, path); }
static bool media_list(void *ctx, const char *prefix, char ***out, size_t *out_count) {
  *out = qa_app_application_q3_assets_list(((qa_app_application_q3_client *)ctx)->media, prefix, out_count);
  return *out != NULL || *out_count == 0;
}
static double media_length(void *ctx, const char *path) {
  return qa_app_application_q3_assets_read_file_length(((qa_app_application_q3_client *)ctx)->media, path);
}
static bool media_read_sync(void *ctx, const char *path, uint8_t **out, size_t *out_length) {
  const uint8_t *bytes = qa_app_application_q3_assets_read_sync(((qa_app_application_q3_client *)ctx)->media, path, out_length);
  *out = malloc(*out_length == 0 ? 1 : *out_length);
  if (*out == NULL) return false;
  if (*out_length > 0) memcpy(*out, bytes, *out_length);
  return true;
}
static double memory_remaining(void *ctx) {
  (void)ctx; /* TS Math.min(0x7fffffff, freemem()) reads the host */
  long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
  double free_bytes = pages < 0 || size < 0 ? 0 : (double)pages * (double)size;
  return qa_js_min(2147483647.0, free_bytes);
}
static bool pres_body_hidden(void *ctx, double entity) { return body_hidden(ctx, entity); }
static void update_loading_screen(void *ctx, qa_q3_pres_client_source_fn draw, void *draw_ctx) {
  (void)ctx; /* TS updateLoadingScreen: paint => paint() */
  draw(draw_ctx);
}
static void weapon_selection(void *ctx, double weapon) {
  qa_app_application_q3_client *client = ctx;
  client->has_weapon_selection = true;
  client->weapon_selection = weapon;
}
static bool pres_body_submission(void *ctx, double entity, qa_qvm_body_part part, const qa_q3_pres_ref_model_entity *source,
                                 bool base) {
  return submit_body(ctx, entity, part, source, base);
}
static void pres_body_pose(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_app_application_q3_client *client = ctx;
  if (client->pose_actors.count == 0) return;
  qa_actor_id actor = source_actor_at(client, entity->current_state.number);
  if (!slot_holds(&client->pose_actors, actor)) return;
  for (size_t i = 0; i < client->body_pose_count; i++)
    if (qa_same_actor(client->body_poses[i].actor, actor)) {
      client->body_poses[i].origin = entity->lerp_origin;
      client->body_poses[i].angles = entity->lerp_angles;
      return;
    }
  if (client->body_pose_count == client->body_pose_capacity) {
    client->body_pose_capacity = client->body_pose_capacity == 0 ? 8 : client->body_pose_capacity * 2;
    client->body_poses = xrealloc(client->body_poses, client->body_pose_capacity * sizeof *client->body_poses);
  }
  client->body_poses[client->body_pose_count++] = (pose){actor, entity->lerp_origin, entity->lerp_angles};
}
static void pres_character(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_client_source_fn render, void *render_ctx) {
  qa_app_application_q3_client *client = ctx;
  if (body_hidden(client, entity->current_state.number)) render(render_ctx);
  else if (client->options.hooks != NULL) client->options.hooks->character(client->options.hooks->ctx, entity, render, render_ctx);
  else {
    /* Supplemental presentations own foreign-character bodies (human seats). Clients with no supplemental body
     * (native Q3 bots) are drawn by cgame from their own CS_PLAYERS model. */
    qa_actor_id actor = source_actor_at(client, entity->current_state.number);
    if (!slot_holds(&client->pose_actors, actor)) render(render_ctx);
  }
}
static void pres_event(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position, qa_q3_pres_client_source_fn render,
                       void *render_ctx) {
  qa_app_application_q3_client *client = ctx;
  client->event_count++;
  if (client->options.hooks == NULL) render(render_ctx);
  else client->options.hooks->event(client->options.hooks->ctx, entity, position, render, render_ctx);
}
static void pres_predict_item(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_client_source_fn predict,
                              void *predict_ctx) {
  qa_app_application_q3_client *client = ctx;
  if (client->options.hooks == NULL) predict(predict_ctx);
  else client->options.hooks->predict_item(client->options.hooks->ctx, entity, predict, predict_ctx);
}
static bool q3_weapon_recipe(const qa_app_application_q3_client *client) {
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_app_application_assets_content(client->options.assets));
  for (size_t i = 0; i < recipe->weapons.count; i++)
    if (strncmp(recipe->weapons.values[i].provider, "q3:", 3) == 0) return true;
  return false;
}
static void pres_view_weapon(void *ctx, qa_q3_base_source_player_state *state, qa_q3_pres_client_source_fn render,
                             void *render_ctx) {
  qa_app_application_q3_client *client = ctx;
  if (!client->view_weapon_visible || client->supplemental_view_weapon) return;
  if (client->options.hooks != NULL) client->options.hooks->view_weapon(client->options.hooks->ctx, state, render, render_ctx);
  else if (q3_weapon_recipe(client)) render(render_ctx);
}
static void pres_player_weapon(void *ctx, const qa_q3_pres_ref_model_entity *parent, qa_q3_base_source_player_state *state,
                               qa_q3_pres_client_entity *entity, qa_q3_base_team team, qa_q3_pres_client_source_fn render,
                               void *render_ctx) {
  qa_app_application_q3_client *client = ctx;
  if (client->options.hooks != NULL)
    client->options.hooks->player_weapon(client->options.hooks->ctx, parent, state, entity, team, render, render_ctx);
  else if (q3_weapon_recipe(client)) render(render_ctx);
}
static qa_q3_pres_weapon_hud_reading pres_weapon_hud(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  return client->options.weapon_hud(client->options.ctx);
}
static void movement_move(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command,
                          qa_q3_pres_movement_options options, qa_bounds *out) {
  const qa_q3_pres_movement_host *movement = &local_round(ctx)->movement;
  *out = movement->move_player(movement->ctx, state, command, options);
}
static qa_bounds round_move_player(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command,
                                   qa_q3_pres_movement_options options) {
  qa_bounds out;
  movement_move(ctx, state, command, options, &out);
  return out;
}
static void round_update_view_angles(void *ctx, qa_q3_pres_source_player_state *state, qa_q3_pres_user_command command) {
  const qa_q3_pres_movement_host *movement = &local_round(ctx)->movement;
  movement->update_view_angles(movement->ctx, state, command);
}
static void menu_key_catcher(void *ctx, double mask) { ((qa_app_application_q3_client *)ctx)->key_catcher = mask; }
static void menu_play_local(void *ctx, qa_ui_runtime_sound_arg sound) {
  qa_app_application_q3_client *client = ctx;
  qa_q3_pres_client_sound sounds = qa_app_application_q3_services_sound(client->services);
  qa_pcm_sound *pcm = sound.kind == QA_UI_SOUND_ARG_HANDLE
      ? qa_q3_pres_presentation_sound_bank_sound_at_index(qa_app_application_q3_assets_bank(client->media), sound.handle)
      : (qa_pcm_sound *)sound.sound;
  sounds.start_local_sound(sounds.ctx, pcm, 6);
}
static void menu_start_background(void *ctx, const char *path) {
  qa_app_application_q3_client *client = ctx;
  qa_q3_pres_client_sound sounds = qa_app_application_q3_services_sound(client->services);
  (void)sounds.start_background_track(sounds.ctx, path != NULL ? path : "", path != NULL ? path : "");
}
static void menu_stop_background(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  qa_q3_pres_client_sound sounds = qa_app_application_q3_services_sound(client->services);
  (void)sounds.start_background_track(sounds.ctx, "", "");
}
/* ApplicationQ3Cinematics as EngineUiCinematics (TS passes the owner itself). */
static bool menu_cinematic_play(void *ctx, qa_ui_cinematic_asset asset, qa_rect rect, qa_ui_cinematic_instance *out) {
  qa_app_application_q3_client *client = ctx;
  qa_app_ui_cinematic_asset own = {asset.path};
  double handle = 0;
  if (!qa_app_application_q3_cinematics_play(client->cinematics, own, rect, &handle)) return false;
  out->asset = asset;
  out->handle_index = handle;
  return true;
}
static void menu_cinematic_run(void *ctx, double handle, double time) {
  (void)time; /* TS run(handle) */
  qa_app_application_q3_cinematics_run(((qa_app_application_q3_client *)ctx)->cinematics, handle);
}
static void menu_cinematic_draw(void *ctx, double handle, qa_rect rect, const qa_draw2d *draw) {
  qa_app_application_q3_cinematics_draw(((qa_app_application_q3_client *)ctx)->cinematics, handle, rect, (qa_draw2d *)draw);
}
static void menu_cinematic_stop(void *ctx, double handle) {
  qa_app_application_q3_cinematics_stop(((qa_app_application_q3_client *)ctx)->cinematics, handle);
}
static bool menu_cinematic_prepare(void *ctx, const char *path, const qa_ui_cinematic_asset **out) {
  qa_app_application_q3_client *client = ctx;
  if (!qa_app_application_q3_cinematics_prepare(client->cinematics, path, &client->prepared_cinematic)) return false;
  client->prepared_menu_cinematic.path = client->prepared_cinematic.path;
  *out = &client->prepared_menu_cinematic;
  return true;
}
static void menu_cinematic_stop_slot(void *ctx, double index) {
  qa_app_application_q3_cinematics_stop(((qa_app_application_q3_client *)ctx)->cinematics, index);
}
static qa_q3_hardware renderer_hardware(const qa_app_application_q3_client *client) {
  const qa_app_gl_driver *driver = client->options.renderer.driver;
  return qa_q3_hardware_classify(driver != NULL && driver->renderer != NULL ? driver->renderer : "");
}

static void initialize(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  const qa_app_application_q3_client_options *o = &client->options;
  qa_seat_id seat = seat_of(client);
  qa_collision_map_settings_init(&client->collision_settings, o->time_cvars != NULL ? o->time_cvars : client->cvar_owner);
  qa_collision_map_settings_register_map(&client->collision_settings);
  qa_world_scene *world = qa_app_application_assets_world(o->assets);
  client->fog_selection_count = world->fog_selection_count;
  client->fog_selections = xcalloc(world->fog_selection_count, sizeof *client->fog_selections);
  for (size_t i = 0; i < world->fog_selection_count; i++) {
    client->fog_selections[i].index = world->fog_selections[i].index;
    client->fog_selections[i].bounds = world->fog_selections[i].volume.bounds;
  }
  qa_app_application_world map = qa_app_loaded_content_world(qa_app_application_assets_content(o->assets));
  qa_app_application_q3_service_options services;
  memset(&services, 0, sizeof services);
  services.collision_settings = &client->collision_settings;
  services.media = client->media;
  services.world.ctx = client;
  services.world.fog_selections = world_fog_selections;
  services.world.map_kind = map.kind;
  services.world.map_geometry = is(map.kind, "q3-bsp") ? map.value : NULL;
  services.audio.ctx = o->audio;
  services.audio.engine.ctx = qa_app_application_audio_engine(o->audio);
  services.audio.engine.queue_stream = mixer_queue;
  services.audio.engine.stop_stream = mixer_stop;
  services.audio.engine.pause_stream = mixer_pause;
  services.audio.engine.capture_stream_checkpoint = mixer_capture;
  services.audio.engine.restore_stream_checkpoint = mixer_restore;
  services.audio.play_music = audio_play_music;
  services.seat = seat;
  services.viewport = &client->viewport; /* TS viewportValue: shared, updated by prepare */
  services.queries = qa_shared_as_scene_queries(o->queries);
  services.shared_queries = o->queries;
  if (o->remap_shader != NULL) {
    services.remap_shader = services_remap;
    services.remap_ctx = client;
  }
  services.system_cinematics = o->system_cinematics;
  services.actor_at = services_actor_at;
  services.actor_ctx = client;
  services.now = clock_now;
  services.now_ctx = client;
  services.frame_number = clock_frame;
  services.frame_ctx = client;
  services.output_ctx = client;
  services.output_scene = output_scene;
  services.output_command = output_command;
  services.output_text = output_text;
  services.output_audio = output_audio;
  services.output_listener = output_listener;
  client->services = qa_app_create_application_q3_services(services);
  if (client->services == NULL) qa_range_error("Out of memory");
  client->cinematics = qa_app_application_q3_services_cinematics(client->services);
  /* TS ApplicationQ3SceneRenderer(media, { picture: requireServices().resources.picture }). */
  client->scene_renderer = qa_app_application_q3_scene_renderer_create(client->media,
      qa_app_application_q3_services_resources(client->services));
  qa_q3_pres_presentation_session *session = &client->session;
  memset(session, 0, sizeof *session);
  session->product = client->product;
  session->client_number = client->source.client_number(client->source.ctx);
  session->last_executed_server_command = client->source.last_executed_server_command == NULL ? 0
      : client->source.last_executed_server_command(client->source.ctx);
  session->mode = QA_Q3_PRES_SESSION_LIVE;
  session->commands = client->source.commands;
  session->snapshots = client->source.snapshots;
  session->cvars = client->cvar_owner;
  session->ctx = client;
  client->server_message_sequence = client->source.server_message_sequence == NULL ? 0
      : client->source.server_message_sequence(client->source.ctx);
  session->server_message_sequence = session_server_message_sequence;
  session->status_visible = session_status_visible;
  session->get_game_state = session_game_state;
  session->get_server_command = session_server_command;
  session->snapshot_ping = session_snapshot_ping;
  session->add_reliable_command = session_reliable;
  session->append_console_command = session_console;
  session->register_cgame_command = session_register;
  session->set_user_command_value = session_selection;
  session->assert_current = session_assert_current;
  session->print = print_ctx;
  if (o->kind == QA_APP_Q3_CLIENT_QVM) {
    qa_app_qvm_application_scalar_options scalar;
    memset(&scalar, 0, sizeof scalar);
    scalar.backend_stencil_bits = o->renderer.backend_stencil_bits;
    scalar.driver = o->renderer.driver;
    scalar.gl_config = o->renderer.gl_config;
    scalar.viewport_ctx = client;
    scalar.viewport = scalar_viewport;
    scalar.overstrike_get = scalar_overstrike_get;
    scalar.overstrike_set = scalar_overstrike_set;
    scalar.local_ctx = client;
    scalar.input = o->guest_input;
    scalar.media = client->media;
    scalar.resources = qa_app_application_q3_services_resources(client->services);
    scalar.now = clock_now;
    scalar.now_ctx = client;
    scalar.key_catcher_get = scalar_key_catcher_get;
    scalar.key_catcher_set = scalar_key_catcher_set;
    scalar.key_catcher_ctx = client;
    scalar.client_state = scalar_client_state;
    scalar.client_state_ctx = client;
    scalar.light_for_point = light_ctx;
    scalar.light_ctx = client;
    scalar.assert_current = session_assert_current;
    scalar.assert_ctx = client;
    qa_app_qvm_application_scalars_init(&client->scalars, scalar);
    client->body_capture = (qa_app_qvm_body_capture){client, capture_active, capture_selected, capture_submit};
    client->body_overrides = (qa_app_qvm_body_overrides){client, overrides_active, overrides_hidden};
    qa_app_application_qvm_client_options qvm;
    memset(&qvm, 0, sizeof qvm);
    qvm.artifacts = client->artifacts;
    qvm.seat = seat;
    qvm.command_context.session = client->options.local->player.actor.session;
    qvm.command_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
    qvm.command_context.origin.seat = seat;
    qvm.command_context.origin.client = client_of(client);
    qvm.services = client->services;
    qvm.media = client->media;
    qvm.assets = o->assets;
    qvm.session = session;
    qvm.connection = o->connection;
    qvm.queries = o->queries;
    qvm.commands = o->command_buffer;
    qvm.cvars = o->guest_cvars;
    qvm.browser = o->browser;
    qvm.keys = o->keys;
    qvm.map = qa_app_loaded_content_recipe(qa_app_application_assets_content(o->assets))->map.geometry.requested_path;
    qvm.body_capture = &client->body_capture;
    qvm.body_overrides = &client->body_overrides;
    qvm.ctx = client;
    qvm.now = qvm_now;
    qvm.key_catcher = qvm_key_catcher;
    if (o->equipment_weapon != NULL) qvm.equipment_weapon = qvm_equipment;
    qvm.view_weapon_visible = qvm_view_weapon_visible;
    qvm.held_weapon_actor = qvm_held_actor;
    qvm.remove_command = qvm_remove_command;
    qvm.scalar = qvm_scalar;
    client->qvm = qa_app_application_qvm_client_create(&qvm);
    client->backend = BACKEND_QVM;
    clear_submissions(client);
    return;
  }
  qa_q3_pres_client_presentation_options p;
  memset(&p, 0, sizeof p);
  qa_q3_pres_presentation_common *common = &p.common;
  common->session = *session;
  common->assets.base.ctx = client;
  common->assets.base.read = media_read;
  common->assets.base.has = media_has;
  common->assets.base.list = media_list;
  common->assets.read_file_length = media_length;
  common->assets.read_sync = media_read_sync;
  common->resources = qa_q3_pres_q3_renderer_resources_as_resources(qa_app_application_q3_services_resources(client->services));
  common->scene = qa_app_application_q3_services_scene(client->services);
  qa_q3_pres_world_mark_projector_init(&client->marks, world);
  client->has_marks = true;
  common->mark_geometry = client->marks.projector.geometry;
  common->collision = qa_app_application_q3_services_collision(client->services);
  common->sound = qa_app_application_q3_services_sound(client->services);
  common->draw = *qa_app_application_q3_services_draw(client->services);
  common->font_registry = qa_app_application_q3_assets_font_registry(client->media);
  common->target_width = client->viewport.width;
  common->target_height = client->viewport.height;
  common->target = &client->viewport; /* TS target: this.viewportValue */
  common->ragepro = is(renderer_hardware(client), "ragepro");
  common->ctx = client;
  common->milliseconds = clock_now;
  common->server_time = clock_server_time;
  common->frame_number = clock_frame;
  common->body_hidden = pres_body_hidden;
  common->light_for_point = light_ctx;
  common->memory_remaining = memory_remaining;
  p.command_context.session = client->options.local->player.actor.session;
  p.command_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  p.command_context.origin.seat = seat;
  p.command_context.origin.client = client_of(client);
  if (client->has_round) {
    p.movement.command_timing = client->round.movement.command_timing;
    p.movement.ctx = client;
    p.movement.move_player = round_move_player;
    p.movement.update_view_angles = round_update_view_angles;
  } else p.movement = o->movement;
  p.menus_missionpack = client->product != QA_Q3_BASE_PRODUCT_BASEQ3;
  if (p.menus_missionpack) {
    p.menu_cinematics.base.ctx = client;
    p.menu_cinematics.base.play = menu_cinematic_play;
    p.menu_cinematics.base.run = menu_cinematic_run;
    p.menu_cinematics.base.draw = menu_cinematic_draw;
    p.menu_cinematics.base.stop = menu_cinematic_stop;
    p.menu_cinematics.owner_ctx = client;
    p.menu_cinematics.prepare = menu_cinematic_prepare;
    p.menu_cinematics.stop_slot = menu_cinematic_stop_slot;
    p.menu_audio.ctx = client;
    p.menu_audio.play_local = menu_play_local;
    p.menu_audio.start_background = menu_start_background;
    p.menu_audio.stop_background = menu_stop_background;
    p.set_key_catcher = menu_key_catcher;
  }
  if (o->weapon_hud != NULL) p.weapon_hud = pres_weapon_hud;
  p.update_loading_screen = update_loading_screen;
  p.body_submission = pres_body_submission;
  p.weapon_selection = weapon_selection;
  p.body_pose = pres_body_pose;
  p.character = pres_character;
  p.event = pres_event;
  p.predict_item = pres_predict_item;
  p.view_weapon = pres_view_weapon;
  p.player_weapon = pres_player_weapon;
  client->game = qa_q3_pres_create_client_presentation(p);
  if (client->game == NULL) qa_range_error("Out of memory");
  client->backend = BACKEND_TYPESCRIPT;
  clear_submissions(client);
}

/* ---- create ---- */
static bool no_font_data(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  return client->options.save_font_data(client->options.ctx);
}
static void destroy_storage(qa_app_application_q3_client *client);
static void close_ctx(void *ctx) { qa_app_application_q3_client_close(ctx); }
qa_app_application_q3_client *qa_app_application_q3_client_create(const qa_app_application_q3_client_options *options,
                                                                  qa_app_qvm_presentation_artifacts *artifacts) {
  if (options->assert_current != NULL) options->assert_current(options->ctx);
  qa_app_application_q3_client *client = xcalloc(1, sizeof *client);
  client->options = *options;
  client->initializing = true;
  client->view_weapon_visible = true;
  client->status_visible = true;
  client->selection.weapon = 2;
  client->selection.sensitivity = 1;
  client->artifacts = qa_app_qvm_presentation_artifacts_retain(artifacts);
  if (!qa_event_ring_init(&client->audio_operations, 4096, sizeof(qa_app_q3_seat_audio_operation))
      || !qa_event_ring_open_cursor(&client->audio_operations, &client->audio_cursor, true))
    qa_range_error("Out of memory");
  /* ApplicationQ3Assets.create(assets, recipe content, print, saveFontData, mode). */
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_app_application_assets_content(options->assets));
  client->media = qa_app_application_q3_assets_create_from_application(options->assets, recipe->engine_behavior.content,
      print_ctx, client, no_font_data, client, options->kind == QA_APP_Q3_CLIENT_QVM ? "guest-async" : "source-sync", NULL);
  if (client->media == NULL) qa_range_error("Out of memory");
  const qa_app_application_q3_client_options *o = &client->options;
  bool local = o->kind == QA_APP_Q3_CLIENT_LOCAL;
  if (local) {
    client->has_round = true;
    memset(&client->round, 0, sizeof client->round);
    client->round.actor = o->local->player.actor;
    client->round.initial = o->initial;
    client->round.movement = o->movement;
    client->round.ctx = o->ctx;
    client->round.link_bounds = o->link_bounds;
    client->round.source_actor = o->source_actor;
    client->round.prediction_command = o->prediction_command;
    client->round.server_settings = o->server_settings;
  }
  client->product = local ? o->initial->product : QA_Q3_BASE_PRODUCT_BASEQ3;
  if (local) {
    qa_app_q3_source_callbacks callbacks = {client, local_select, local_source_actor, local_prediction, local_level_shot};
    client->local_source = qa_app_application_q3_source_create(o->local->player.actor, o->initial, callbacks);
    memset(&client->source, 0, sizeof client->source);
    client->source.ctx = client;
    client->source.game_state = local_game_state;
    client->source.server_command = local_server_command;
    client->source.actor_at = local_actor_at;
    client->source.demo = local_demo;
    client->source.commands = qa_q3_pres_client_command_history_source(qa_app_application_q3_source_commands(client->local_source));
    client->source.snapshots.ctx = client;
    client->source.snapshots.current = local_current;
    client->source.snapshots.read = local_read;
    client->source.client_number = local_client_number;
    client->source.time = local_time;
    client->source.sound_actor = local_sound_actor;
  } else {
    if (o->source == NULL) js_error("Local Q3 client has no snapshot source");
    client->source = *o->source;
  }
  client->viewport = option_viewport(o);
  const qa_q3_player_state_record *remote_player = o->kind == QA_APP_Q3_CLIENT_REMOTE ? o->initial_player : NULL;
  const qa_q3_base_player_state *local_player = NULL;
  if (local)
    for (size_t i = 0; i < o->initial->client_count && local_player == NULL; i++)
      if (qa_same_actor(o->initial->clients[i].actor, o->local->player.actor)) local_player = &o->initial->clients[i].state;
  if ((local && local_player == NULL) || (o->kind == QA_APP_Q3_CLIENT_REMOTE && remote_player == NULL))
    js_error("Q3 cgame seat lacks a source player");
  qa_vec3 origin = {0, 0, 0}, angles = {0, 0, 0};
  if (local_player != NULL) {
    origin = qa_q3_base_player_state_record_origin(local_player);
    origin.z += local_player->viewheight;
    angles = local_player->viewangles;
  } else if (remote_player != NULL) {
    origin = remote_player->origin;
    origin.z += remote_player->viewheight;
    angles = remote_player->viewangles;
  }
  memset(&client->latest_camera, 0, sizeof client->latest_camera);
  client->latest_camera.origin = origin;
  client->latest_camera.axis = qa_angles_to_axis(angles);
  client->latest_camera.viewport = client->viewport;
  client->latest_camera.projection = qa_scene_perspective_projection(90, 73.739795, 16384, 4);
  if (o->cvars != NULL) client->cvar_owner = o->cvars;
  else {
    qa_cvar_registry_options registry;
    memset(&registry, 0, sizeof registry);
    registry.dialect = QA_COMMAND_DIALECT_Q3;
    registry.context.session = o->local->player.actor.session;
    registry.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
    registry.context.origin.seat = seat_of(client);
    registry.context.origin.client = client_of(client);
    registry.print = print_ctx;
    registry.print_ctx = client;
    registry.cheats_allowed = cheats_allowed;
    registry.cheats_allowed_ctx = client;
    client->cvar_owner = qa_cvar_registry_create(&registry);
    if (client->cvar_owner == NULL) qa_range_error("Out of memory");
    client->owns_cvars = true;
  }
  for (size_t i = 0; o->has_settings && i < o->setting_count; i++)
    cvar_set_forced(client->cvar_owner, o->settings[i].name, o->settings[i].value);
  size_t table_count = 0;
  const qa_q3_pres_cvar_definition *table = qa_q3_pres_cvar_table(client->product, &table_count);
  client->shared_cvar_names = xcalloc(table_count, sizeof *client->shared_cvar_names);
  for (size_t i = 0; i < table_count; i++) client->shared_cvar_names[client->shared_cvar_name_count++] = lower(table[i].name);
  if (o->server_settings != NULL) {
    size_t count = 0;
    qa_cvar_snapshot *settings = o->server_settings(o->ctx, &count);
    for (size_t i = 0; i < count; i++)
      if (shared_cvar(client, settings[i].name)) cvar_set_forced(client->cvar_owner, settings[i].name, settings[i].value);
    free_settings(settings, count);
  }
  qa_app_application_q3_client_refresh_system_info(client);
  cvar_set_forced(client->cvar_owner, "sv_running",
                  o->kind == QA_APP_Q3_CLIENT_REMOTE || (o->kind == QA_APP_Q3_CLIENT_QVM && !o->local_server) ? "0" : "1");
  qa_render_frame_image_registry registry = {client, qa_app_application_assets_images(o->assets)->owner, registry_drain,
                                             registry_require};
  if (!qa_scene_frame_builder_init(&client->frames, registry)) qa_range_error("Out of memory");
  client->has_frames = true;
  if (!qa_model_light_sampler_init(&client->light_sampler, qa_app_application_assets_world(o->assets)))
    qa_range_error("Out of memory");
  client->has_light_sampler = true;
  qa_caught_error error;
  if (qa_try(initialize, client, &error)) {
    assert_current_option(client);
    client->initializing = false;
    bind_frame_time(client);
    return client;
  }
  qa_caught_error cleanup;
  if (!qa_try(close_ctx, client, &cleanup)) qa_raise("AggregateError", 0, 2, "%s", error.message);
  destroy_storage(client);
  qa_rethrow(&error);
}

/* ---- captureVideoReopen ---- */
struct qa_app_q3_video_reopen {
  qa_app_application_q3_client *client; /* the captured owner (closed before reopening) */
  qa_app_application_q3_client_options retained;
  qa_app_qvm_presentation_artifacts *artifacts;
  bool opened;
};
qa_app_q3_video_reopen *qa_app_application_q3_client_capture_video_reopen(qa_app_application_q3_client *client) {
  require_backend(client);
  if (client->options.kind != QA_APP_Q3_CLIENT_QVM || client->backend != BACKEND_QVM)
    js_error("Video guest reopen requires QVM presentation");
  qa_app_q3_video_reopen *reopen = xcalloc(1, sizeof *reopen);
  reopen->artifacts = qa_app_application_qvm_client_presentation_artifacts(client->qvm);
  reopen->client = client;
  /* { settings, serverSettings, timeCvars, ...retained } = options; cvars and timeCvars are re-added. */
  reopen->retained = client->options;
  reopen->retained.settings = NULL;
  reopen->retained.setting_count = 0;
  reopen->retained.has_settings = false;
  reopen->retained.server_settings = NULL;
  reopen->retained.cvars = client->cvar_owner;
  reopen->retained.viewport_ctx = NULL;
  reopen->retained.viewport_release = NULL;
  reopen->retained.time_cvars = client->options.time_cvars;
  return reopen;
}
qa_app_application_q3_client *qa_app_q3_video_reopen_open(qa_app_q3_video_reopen *reopen,
                                                          const qa_app_qvm_video_reopen_options *overrides) {
  if (!reopen->client->closed || reopen->opened) {
    /* The reopened client would own the viewport closure; none is created. */
    if (overrides->release != NULL) overrides->release(overrides->ctx);
    if (!reopen->client->closed) js_error("Previous guest presentation must shut down before reopening");
    js_error("Video guest reopen was already attempted");
  }
  reopen->opened = true;
  qa_app_application_q3_client_options options = reopen->retained;
  options.renderer = overrides->renderer;
  options.viewport = overrides->viewport;
  options.viewport_ctx = overrides->ctx;
  options.viewport_release = overrides->release;
  options.command_bindings = overrides->command_bindings;
  options.command_registration = overrides->command_registration;
  /* The captured cvars stay alive with the reopened client (TS GC): a
   * registry the closed client created becomes the reopened client's. */
  qa_app_application_q3_client *previous = reopen->client;
  bool transfer = previous->owns_cvars && previous->cvar_owner == options.cvars;
  qa_app_application_q3_client *client = qa_app_application_q3_client_create(&options, reopen->artifacts);
  if (transfer) {
    previous->owns_cvars = false;
    client->owns_cvars = true;
  }
  return client;
}
void qa_app_q3_video_reopen_destroy(qa_app_q3_video_reopen *reopen) {
  if (reopen == NULL) return;
  qa_app_qvm_presentation_artifacts_release(reopen->artifacts);
  free(reopen);
}

/* ---- seat views ---- */
bool qa_app_application_q3_client_consume_weapon_selection(qa_app_application_q3_client *client, double *out) {
  bool selected = client->has_weapon_selection;
  if (selected) *out = client->weapon_selection;
  client->has_weapon_selection = false;
  return selected;
}
qa_app_q3_command_selection qa_app_application_q3_client_user_command_selection(qa_app_application_q3_client *client) {
  qa_app_q3_equipment_presentation equipment;
  if (client->options.kind == QA_APP_Q3_CLIENT_QVM && client->options.equipment_weapon != NULL
      && client->options.equipment_weapon(client->options.ctx, &equipment)) {
    qa_app_q3_command_selection selection = {equipment.primary_weapon, client->selection.sensitivity};
    return selection;
  }
  return client->selection;
}
double qa_app_application_q3_client_presented_events(const qa_app_application_q3_client *client) { return client->event_count; }
const qa_app_qvm_held_weapon *qa_app_application_q3_client_shared_held_weapons(qa_app_application_q3_client *client,
                                                                             size_t *out_count) {
  require_backend(client);
  *out_count = 0;
  return client->backend == BACKEND_QVM ? qa_app_application_qvm_client_shared_held_weapons(client->qvm, out_count) : NULL;
}
bool qa_app_application_q3_client_shared_equipment_view_visible(qa_app_application_q3_client *client, bool *out) {
  require_backend(client);
  return client->backend == BACKEND_QVM && qa_app_application_qvm_client_shared_equipment_view_visible(client->qvm, out);
}
static double cvar_integer(qa_cvar_registry *cvars, const char *name, double fallback) {
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_get(cvars, name, &snapshot)) return fallback;
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
void qa_app_application_q3_client_weapon_hud_view(qa_app_application_q3_client *client, bool *visible,
                                                  bool *aggregate_warning) {
  require_backend(client);
  if (client->backend == BACKEND_QVM) {
    *visible = qa_app_application_qvm_client_shared_equipment_hud(client->qvm);
    *aggregate_warning = false;
    return;
  }
  qa_q3_pres_client_game_state *state = qa_q3_pres_client_presentation_state(require_game(client));
  const qa_q3_base_source_player_state *ps = state->snap == NULL ? NULL : &state->snap->player_state;
  *visible = ps != NULL && !state->level_shot && !state->show_scores && qa_q3_base_player_state_record_health(ps) > 0
      && ps->pm_type != QA_Q3_PM_INTERMISSION
      && qa_q3_base_player_state_slots_get(&ps->persistant, QA_Q3_BASE_PERS_TEAM) != QA_Q3_BASE_TEAM_SPECTATOR
      && cvar_integer(client->cvar_owner, "cg_draw2D", 1) != 0 && cvar_integer(client->cvar_owner, "cg_drawStatus", 1) != 0;
  *aggregate_warning = cvar_integer(client->cvar_owner, "cg_drawAmmoWarning", 1) != 0;
}
qa_scene_camera qa_app_application_q3_client_camera(const qa_app_application_q3_client *client) { return client->latest_camera; }
bool qa_app_application_q3_client_body_pose(const qa_app_application_q3_client *client, qa_actor_id actor,
                                            qa_vec3 *origin_out, qa_vec3 *angles_out) {
  for (size_t i = 0; i < client->body_pose_count; i++)
    if (qa_same_actor(client->body_poses[i].actor, actor)) {
      *origin_out = client->body_poses[i].origin;
      *angles_out = client->body_poses[i].angles;
      return true;
    }
  return false;
}
qa_scene_camera qa_app_application_q3_client_supplemental_weapon_camera(qa_app_application_q3_client *client,
                                                                        qa_scene_camera camera) {
  require_backend(client);
  if (client->backend == BACKEND_QVM) return camera;
  qa_rect *occupied = xcalloc(client->submission_count, sizeof *occupied);
  size_t count = 0;
  for (size_t i = 0; i < client->submission_count; i++) {
    const submission *s = &client->submissions[i];
    if (s->kind == SUBMISSION_SCENE && ((int64_t)s->scene->source.render_flags & QA_Q3_PRES_RDF_NOWORLDMODEL) != 0)
      occupied[count++] = s->scene->viewport;
  }
  bool split = client->options.split_screen != NULL && client->options.split_screen(client->options.ctx);
  qa_scene_camera result = qa_app_weapon_view_camera(qa_app_q3_weapon_camera(camera, split), occupied, count);
  free(occupied);
  return result;
}
void qa_app_application_q3_client_receive_events(qa_app_application_q3_client *client,
    const qa_app_sim_presentation_event *events, size_t count) {
  assert_current_option(client);
  (void)require_game(client);
  if (client->local_source == NULL) js_error("Remote Q3 cgame receives events through its network connection");
  qa_app_application_q3_source_receive_events(client->local_source, events, count);
}
void qa_app_application_q3_client_receive(qa_app_application_q3_client *client, const qa_app_q3_source_presentation_state *state,
    const qa_app_sim_presentation_event *events, size_t event_count, const qa_actor_command *commands, size_t command_count) {
  (void)require_game(client);
  if (client->local_source == NULL) js_error("Remote Q3 cgame receives snapshots through its network connection");
  qa_app_application_q3_source_receive(client->local_source, state, events, event_count, commands, command_count);
}

/* ---- prepare ---- */
static void draw_backend(void *ctx) {
  qa_app_application_q3_client *client = ctx;
  bool demo = client->source.demo != NULL && client->source.demo(client->source.ctx);
  if (client->backend == BACKEND_TYPESCRIPT) {
    qa_q3_pres_presentation_frame frame = {source_time(client), QA_Q3_PRES_STEREO_CENTER, demo, client->frame_number};
    if (!qa_q3_pres_presentation_frame_runtime_draw_active_frame(qa_q3_pres_client_presentation_frames(client->game), frame))
      qa_range_error("Out of memory");
  } else qa_app_application_qvm_client_draw(client->qvm, source_time(client), demo);
}
static void refresh_status(void *ctx) { qa_app_application_qvm_client_refresh_status(((qa_app_application_q3_client *)ctx)->qvm); }
void qa_app_application_q3_client_prepare(qa_app_application_q3_client *client, double frame_number, const qa_rect *viewport,
    const qa_app_simulation_presentation *presentations, size_t presentation_count, bool status_visible,
    const qa_app_component_body *bodies, size_t body_count, bool view_weapon_visible, bool camera_controlled) {
  qa_rect next_viewport = viewport != NULL ? *viewport : option_viewport(&client->options);
  qa_actor_id local = client->options.local->player.actor;
  client->view_weapon_visible = view_weapon_visible;
  client->hidden_bodies.count = 0;
  if (camera_controlled) slot_set(&client->hidden_bodies, local);
  client->selected_bodies.count = 0;
  client->primary_body_count = 0;
  body_store_clear(client);
  for (size_t i = 0; i < body_count; i++) slot_set(&client->selected_bodies, bodies[i].actor);
  client->selected_held_actors.count = 0;
  client->body_pose_count = 0;
  client->pose_actors.count = 0;
  for (size_t i = 0; i < presentation_count; i++) {
    const qa_app_simulation_presentation *model = &presentations[i];
    bool source_client = is(model->render_owner, "source-client");
    if (!source_client && model->view_weapon && ((model->path != NULL && model->path[0] != '\0') || model->held_weapon != NULL))
      slot_set(&client->selected_held_actors, model->actor);
    if (model->replaces_body) slot_set(&client->hidden_bodies, model->actor);
    if (!source_client && model->visible && !model->view_weapon) slot_set(&client->pose_actors, model->actor);
  }
  qa_app_application_q3_client_refresh_system_info(client);
  require_backend(client);
  client->frame_number = frame_number;
  client->viewport = next_viewport;
  clear_submissions(client);
  client->supplemental_view_weapon = false;
  for (size_t i = 0; i < presentation_count && !client->supplemental_view_weapon; i++) {
    const qa_app_simulation_presentation *source = &presentations[i];
    client->supplemental_view_weapon = !is(source->render_owner, "source-client") && source->visible && source->view_weapon
        && qa_same_actor(source->actor, local);
  }
  apply_shared_server_settings(client);
  bool previous_status = client->status_visible;
  client->status_visible = status_visible;
  qa_caught_error failure;
  bool drawn = qa_try(draw_backend, client, &failure);
  client->status_visible = previous_status;
  if (client->backend == BACKEND_QVM) {
    qa_caught_error status;
    if (!qa_try(refresh_status, client, &status)) {
      if (!drawn) qa_raise("AggregateError", 0, 2, "Cgame frame and status restoration failed");
      qa_rethrow(&status);
    }
  }
  if (!drawn) qa_rethrow(&failure);
  const qa_q3_pres_scene_content **scenes = xcalloc(client->submission_count, sizeof *scenes);
  size_t scene_count = 0;
  for (size_t i = 0; i < client->submission_count; i++)
    if (client->submissions[i].kind == SUBMISSION_SCENE) scenes[scene_count++] = &client->submissions[i].scene->content;
  qa_cleanup node;
  qa_cleanup_push(&node, free, scenes);
  qa_app_application_q3_scene_renderer_preload(client->scene_renderer, scenes, scene_count);
  qa_cleanup_pop(&node);
  free(scenes);
  flush_audio(client);
}

/* ---- commands and input ---- */
bool qa_app_application_q3_client_command(qa_app_application_q3_client *client, const char *const *argv, size_t argc) {
  require_backend(client);
  return client->backend == BACKEND_TYPESCRIPT
      ? qa_q3_pres_client_console_runtime_execute(qa_q3_pres_client_presentation_console(client->game), argv, argc)
      : qa_app_application_qvm_client_command(client->qvm, argv, argc);
}
bool qa_app_application_q3_client_handles_command(qa_app_application_q3_client *client, const char *name) {
  require_backend(client);
  return client->backend == BACKEND_TYPESCRIPT
      ? qa_q3_pres_client_console_runtime_handles(qa_q3_pres_client_presentation_console(client->game), name)
      : qa_app_application_q3_client_has_command_name(client, name);
}
void qa_app_application_q3_client_key_event(qa_app_application_q3_client *client, double key, bool down) {
  require_backend(client);
  if (client->backend == BACKEND_TYPESCRIPT) qa_q3_pres_client_presentation_key_event(client->game, key, down);
  else qa_app_application_qvm_client_key_event(client->qvm, key, down);
}
void qa_app_application_q3_client_mouse_event(qa_app_application_q3_client *client, double x, double y) {
  require_backend(client);
  if (client->backend == BACKEND_TYPESCRIPT) qa_q3_pres_client_presentation_mouse_event(client->game, x, y);
  else qa_app_application_qvm_client_mouse_event(client->qvm, x, y);
}
void qa_app_application_q3_client_event_handling(qa_app_application_q3_client *client, double type) {
  if (type != trunc(type) || !isfinite(type) || type < 0 || type > 3) qa_range_error("Invalid Q3 event handling mode");
  require_backend(client);
  if (client->backend == BACKEND_TYPESCRIPT) qa_q3_pres_client_presentation_event_handling(client->game, type);
  else
    qa_app_application_qvm_client_event_handling(client->qvm, type == 0 ? "none" : type == 1 ? "team-menu"
                                                                       : type == 2 ? "scoreboard" : "edit-hud");
}
/* One UTF-8 code point (JS `for (const character of text)` over valid text). */
static uint32_t next_code_point(const unsigned char **cursor) {
  const unsigned char *p = *cursor;
  uint32_t code = p[0];
  size_t length = code < 0x80 ? 1 : (code & 0xe0) == 0xc0 ? 2 : (code & 0xf0) == 0xe0 ? 3 : (code & 0xf8) == 0xf0 ? 4 : 1;
  if (length > 1) {
    code &= 0xff >> (length + 1);
    for (size_t i = 1; i < length; i++) {
      if ((p[i] & 0xc0) != 0x80) { length = 1; code = 0xfffd; break; }
      code = (code << 6) | (p[i] & 0x3f);
    }
  } else if (code >= 0x80) code = 0xfffd;
  *cursor = p + length;
  return code;
}
void qa_app_application_q3_client_input(qa_app_application_q3_client *client, const qa_seat_input_event *event) {
  require_backend(client);
  if (!qa_same_seat(event->seat, seat_of(client))) js_error("Q3 input belongs to another seat");
  if (is(event->kind, "key")) qa_app_application_q3_client_key_event(client, event->code, event->down);
  else if (is(event->kind, "text")) {
    for (const unsigned char *p = (const unsigned char *)(event->text != NULL ? event->text : ""); *p != '\0';) {
      uint32_t code = next_code_point(&p);
      qa_app_application_q3_client_key_event(client, (double)(code | QA_KEY_CHAR_FLAG), true);
    }
  } else if (is(event->kind, "mouse-button"))
    qa_app_application_q3_client_key_event(client, QA_KEY_MOUSE1 + qa_quake_mouse_button(event->button) - 1, event->down);
  else if (is(event->kind, "mouse-motion")) qa_app_application_q3_client_mouse_event(client, event->delta.x, event->delta.y);
  else if (is(event->kind, "mouse-wheel")) {
    for (double count = 0; count < fabs(event->delta.y); count++) {
      double key = event->delta.y > 0 ? QA_KEY_MOUSE_WHEEL_UP : QA_KEY_MOUSE_WHEEL_DOWN;
      qa_app_application_q3_client_key_event(client, key, true);
      qa_app_application_q3_client_key_event(client, key, false);
    }
  } else if (is(event->kind, "controller-button")) {
    if (event->button >= 0 && event->button < 32) qa_app_application_q3_client_key_event(client, QA_KEY_JOY1 + event->button, event->down);
  }
}
bool qa_app_application_q3_client_captures_input(const qa_app_application_q3_client *client) {
  return client->backend == BACKEND_QVM ? qa_app_application_qvm_client_captures_input(client->qvm) : client->key_catcher != 0;
}

/* ---- frame ---- */
static void free_scene_group(void *ctx) {
  qa_scene_group_destroy(ctx);
  free(ctx);
}
static qa_scene_operation model_group_operation(qa_app_application_q3_client *client, const qa_scene_model_group *group) {
  qa_scene_group *wrapped = xcalloc(1, sizeof *wrapped);
  if (is(group->kind, "compiled")) *wrapped = qa_compiled_draw_group(group->material, group->batches, group->batch_count);
  else if (is(group->kind, "source")) {
    qa_source_surface_order order = {group->source_view, group->source_entity, group->surface, group->fog, group->dlight};
    *wrapped = qa_source_draw_group(group->source_material, order, group->batches, group->batch_count);
  } else {
    qa_sequence_phase phase = QA_SEQUENCE_SKY;
    for (int i = QA_SEQUENCE_SKY; i <= QA_SEQUENCE_TRANSLUCENT; i++)
      if (is(qa_sequence_phase_name((qa_sequence_phase)i), group->sequence)) phase = (qa_sequence_phase)i;
    *wrapped = qa_sequence_draw_group(phase, group->batches, group->batch_count);
  }
  frame_store_add(client, free_scene_group, wrapped);
  qa_scene_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.is_group = true;
  operation.group = *wrapped;
  return operation;
}
static void free_scene_operations(void *ctx) {
  qa_app_q3_scene_operations_release(ctx);
  free(ctx);
}
static void free_finished(void *ctx) {
  qa_finished_operations_destroy(ctx);
  free(ctx);
}
static void free_prepared_view(void *ctx) {
  qa_prepared_world_view_destroy(ctx);
  free(ctx);
}
static void free_effect_frame(void *ctx) {
  qa_app_application_effect_frame_destroy(ctx);
  free(ctx);
}
static void free_admission(void *ctx) {
  qa_world_surface_admission_destroy(ctx);
  free(ctx);
}
typedef struct text_batches { qa_draw_batch *batches; size_t count; } text_batches;
static void free_text_batches(void *ctx) {
  text_batches *owned = ctx;
  if (owned->batches != NULL) qa_mat_draw_batches_destroy(owned->batches, owned->count);
  free(owned);
}
static void free_render_text(void *ctx) {
  char **rows = ctx;
  for (size_t i = 0; i < 8; i++) free(rows[i]);
  free(rows);
}
/* TS { ...view, source: createWorldSurfaceAdmission(source) } with the order
 * and admission kept until the next frame. */
static qa_source_scene_order *frame_order(qa_app_application_q3_client *client) {
  qa_source_scene_order *order = xcalloc(1, sizeof *order);
  frame_store_add(client, free, order);
  qa_source_scene_order_init(order, qa_app_application_assets_material_registrations(client->options.assets));
  return order;
}
static qa_world_surface_admission *frame_admission(qa_app_application_q3_client *client, qa_source_scene_order *order) {
  qa_world_surface_admission *admission = xcalloc(1, sizeof *admission);
  qa_create_world_surface_admission(order, admission);
  frame_store_add(client, free_admission, admission);
  return admission;
}
static const qa_scene_operation *scene_operations(qa_app_application_q3_client *client, const qa_q3_pres_presented_scene *scene,
    const qa_world_view_input *input, double first_entity, qa_app_q3_scene_render_options options,
    const qa_scene_operation *additions, size_t addition_count, size_t *out_count) {
  qa_app_q3_scene_operations *operations = xcalloc(1, sizeof *operations);
  frame_store_add(client, free_scene_operations, operations);
  qa_app_application_q3_scene_renderer_operations(client->scene_renderer, &scene->content, input, first_entity, options,
                                                  additions, addition_count, operations);
  *out_count = operations->count;
  return operations->items;
}
typedef struct publish_state {
  qa_app_application_q3_client *client;
  const qa_q3_pres_presented_scene *scene;
  const qa_app_q3_frame_hooks *hooks;
  const qa_vec3 *view_offset;
  qa_app_q3_scene_render_options options;
} publish_state;
static void publish(publish_state *state, const qa_world_view_input *view) {
  qa_app_application_q3_client *client = state->client;
  qa_world_scene *world = qa_app_application_assets_world(client->options.assets);
  qa_source_scene_order *source = frame_order(client);
  double first_entity = qa_reserve_source_entity_range(source, (double)state->scene->content.admission.entity_count);
  qa_app_application_effect_frame *effects = NULL;
  if (state->hooks != NULL && state->hooks->additional_effects != NULL) {
    effects = xcalloc(1, sizeof *effects);
    frame_store_add(client, free_effect_frame, effects);
    if (!state->hooks->additional_effects(state->hooks->ctx, view->camera, source, effects)) effects = NULL;
  }
  qa_world_view_input combined = *view;
  combined.source = frame_admission(client, source);
  const qa_scene_operation *additions = NULL;
  size_t addition_count = 0;
  if (effects != NULL) {
    combined.lights = effects->lights;
    combined.light_count = effects->light_count;
    combined.has_lights = true;
    size_t count = view->visibility.q3_light_count + effects->q3_light_count;
    if (count > 32) count = 32;
    qa_q3_dynamic_light *lights = xcalloc(count, sizeof *lights);
    frame_store_add(client, free, lights);
    size_t n = 0;
    for (size_t i = 0; i < view->visibility.q3_light_count && n < count; i++) lights[n++] = view->visibility.q3_lights[i];
    for (size_t i = 0; i < effects->q3_light_count && n < count; i++) lights[n++] = effects->q3_lights[i];
    combined.visibility.q3_lights = lights;
    combined.visibility.q3_light_count = n;
    qa_scene_operation *list = xcalloc(effects->operations.count, sizeof *list);
    frame_store_add(client, free, list);
    for (size_t i = 0; i < effects->operations.count; i++) {
      const qa_app_effect_operation *operation = &effects->operations.items[i];
      list[i] = operation->is_model_group ? model_group_operation(client, operation->model_group) : operation->scene;
    }
    additions = list;
    addition_count = effects->operations.count;
  }
  qa_world_operations ignored;
  qa_world_scene_prepare_world_operations(world, &combined, NULL, &ignored);
  qa_app_q3_scene_render_options options = state->options;
  options.no_world_model = view->no_world_model;
  options.view_offset = state->view_offset;
  size_t operation_count = 0;
  combined.operations = scene_operations(client, state->scene, &combined, first_entity, options, additions, addition_count,
                                         &operation_count);
  combined.operation_count = operation_count;
  qa_prepared_world_view *prepared = xcalloc(1, sizeof *prepared);
  qa_world_scene_prepare_view(world, &combined, prepared);
  frame_store_add(client, free_prepared_view, prepared);
  qa_render_prepared_world_view out = {prepared->image_operations, prepared->image_operation_count, prepared->view};
  qa_scene_frame_builder_world(&client->frames, out);
}
/* TS portal(scene, input): the first visible mirror/portal surface's child view. */
static bool portal(qa_app_application_q3_client *client, const qa_q3_pres_presented_scene *scene, const qa_world_view_input *input,
                   qa_world_view_input *out) {
  if (scene->content.portal_count == 0 || input->camera.clip_is_portal) return false;
  qa_world_scene *world = qa_app_application_assets_world(client->options.assets);
  qa_visible_world visible;
  if (!qa_visible_world_new(world->map, input->camera, &input->visibility, &world->visibility_cache, &visible))
    qa_range_error("Out of memory");
  qa_portal_entity *portals = xcalloc(scene->content.portal_count, sizeof *portals);
  for (size_t i = 0; i < scene->content.portal_count; i++) {
    const qa_q3_pres_ref_portal_entity *source = &scene->content.portals[i].source->entity.as.portal;
    portals[i] = (qa_portal_entity){source->origin, source->old_origin, source->axis, source->frame, source->old_frame,
                                    source->skin_num};
  }
  bool found = false;
  for (size_t i = 0; i < visible.surface_count && !found; i++) {
    size_t index = (size_t)visible.surfaces[i];
    if (index >= world->surface_count) continue;
    const qa_world_surface *surface = &world->surfaces[index];
    if (!surface->is_q3 || !surface->has_shader || !surface->has_plane
        || qa_registered_scene_material_finished(surface->shader)->sort != 1)
      continue;
    qa_portal_camera child;
    if (!qa_portal_camera_new(surface->plane, portals, scene->content.portal_count, input->camera, source_time(client), NULL,
                              &child))
      continue;
    if (qa_portal_surface_offscreen(surface->geometry, input->camera,
                                    qa_registered_scene_material_material(surface->shader)->portal_range, child.mirror))
      continue;
    /* The parent snapshot's area mask describes the parent eye, not the portal destination; let BSP PVS from the
     * portal origin decide. */
    *out = *input;
    out->camera = child.camera;
    out->visibility.has_pvs_origin = true;
    out->visibility.pvs_origin = child.pvs_origin;
    out->visibility.has_visible_areas = false;
    out->visibility.visible_areas = NULL;
    out->visibility.visible_area_count = 0;
    found = true;
  }
  free(portals);
  qa_visible_world_destroy(&visible);
  return found;
}
static const qa_vec4 clear_black = {0, 0, 0, 1}, clear_grey = {0.3, 0.3, 0.3, 1};
qa_render_frame qa_app_application_q3_client_frame(qa_app_application_q3_client *client, const qa_app_q3_frame_hooks *hooks,
    const qa_app_q3_frame_environment *environment, const qa_vec3 *view_offset) {
  require_backend(client);
  frame_store_clear(client);
  qa_scene_frame_builder_begin_default(&client->frames);
  qa_seat_id seat = seat_of(client);
  qa_world_scene *world = qa_app_application_assets_world(client->options.assets);
  qa_source_time time = {QA_SOURCE_TIME_MILLISECONDS, source_time(client)};
  bool split = client->options.split_screen != NULL && client->options.split_screen(client->options.ctx);
  for (size_t i = 0; i < client->submission_count; i++) {
    const submission *s = &client->submissions[i];
    if (s->kind == SUBMISSION_COMMAND) {
      qa_scene_frame_builder_command(&client->frames, s->command);
      continue;
    }
    if (s->kind == SUBMISSION_TEXT) {
      qa_world_view_input input;
      memset(&input, 0, sizeof input);
      input.camera = client->latest_camera;
      input.time = time;
      input.target_is_seat = true;
      input.target_seat = seat;
      qa_world_material_context context;
      qa_world_scene_material_context(world, &input, NULL, NULL, &context);
      qa_draw_batch *batches = NULL;
      size_t batch_count = 0;
      if (qa_prepare_material_text(s->draw, client->viewport, context.context, &batches, &batch_count) == 0)
        qa_range_error("Out of memory");
      text_batches *owned_batches = xcalloc(1, sizeof *owned_batches);
      *owned_batches = (text_batches){batches, batch_count};
      frame_store_add(client, free_text_batches, owned_batches);
      qa_render_operation *operation = xcalloc(1, sizeof *operation);
      frame_store_add(client, free, operation);
      operation->kind = QA_RENDER_OP_DRAW;
      operation->batches = batches;
      operation->batch_count = batch_count;
      qa_render_view view;
      memset(&view, 0, sizeof view);
      view.state.viewport = client->viewport;
      view.target_is_seat = true;
      view.target_seat = seat;
      view.time = time;
      view.operations = operation;
      view.operation_count = 1;
      qa_scene_frame_builder_view(&client->frames, view);
      continue;
    }
    const qa_q3_pres_presented_scene *scene = s->scene;
    bool no_world = ((int64_t)scene->source.render_flags & QA_Q3_PRES_RDF_NOWORLDMODEL) != 0;
    qa_scene_camera camera = no_world || hooks == NULL || hooks->transform_camera == NULL
        ? scene->camera : hooks->transform_camera(hooks->ctx, scene->camera);
    qa_app_q3_renderer_settings rendering = client->options.render_settings != NULL
        ? client->options.render_settings(client->options.ctx) : qa_app_q3_renderer_settings_from(NULL);
    /* r_shadows is the cgame cvar cg_shadows in id's renderer. */
    double shadow_mode = no_world ? 0 : cvar_integer(client->cvar_owner, "cg_shadows", 0);
    qa_world_view_input input;
    memset(&input, 0, sizeof input);
    input.camera = camera;
    input.time = time;
    input.target_is_seat = true;
    input.target_seat = seat;
    input.has_curve_error = true;
    input.curve_error = rendering.curve_error;
    if (shadow_mode == 2 || shadow_mode == 3) input.model_shadows = shadow_mode;
    size_t light_count = scene->content.light_count < 32 ? scene->content.light_count : 32;
    qa_q3_dynamic_light *lights = xcalloc(light_count, sizeof *lights);
    frame_store_add(client, free, lights);
    for (size_t l = 0; l < light_count; l++) {
      const qa_scene_light *light_source = &scene->content.lights[l];
      lights[l] = (qa_q3_dynamic_light){light_source->origin, light_source->radius, light_source->color, light_source->additive};
    }
    input.visibility.q3_lights = lights;
    input.visibility.q3_light_count = light_count;
    char **rows = xcalloc(8, sizeof *rows);
    frame_store_add(client, free_render_text, rows);
    for (size_t r = 0; r < 8; r++) {
      const qa_q3_pres_render_row *row = &scene->source.text.rows[r];
      size_t length = row->length < 32 ? row->length : 32;
      rows[r] = xcalloc(length + 1, 1);
      memcpy(rows[r], row->bytes, length);
    }
    input.render_text = (const char *const *)rows;
    input.render_text_count = 8;
    size_t area_total = scene->source.area_mask_length * 8, area_count = 0;
    double *areas = xcalloc(area_total, sizeof *areas);
    frame_store_add(client, free, areas);
    for (size_t area = 0; area < area_total; area++)
      if ((scene->source.area_mask[area >> 3] & (1 << (area & 7))) == 0) areas[area_count++] = (double)area;
    input.visibility.has_visible_areas = true;
    input.visibility.visible_areas = areas;
    input.visibility.visible_area_count = area_count;
    qa_render_view_clear *clear = xcalloc(1, sizeof *clear);
    frame_store_add(client, free, clear);
    clear->depth = 1;
    clear->stencil = false;
    clear->color = no_world ? NULL
        : environment != NULL && environment->no_world_model ? &clear_grey : &clear_black;
    input.clear = clear;
    qa_app_q3_scene_render_options options;
    memset(&options, 0, sizeof options);
    options.rail = &rendering.rail;
    options.split_screen = split;
    options.supplemental_view_weapon = client->supplemental_view_weapon;
    if (no_world) {
      qa_source_scene_order *source = frame_order(client);
      qa_world_view_input selected = input;
      selected.source = frame_admission(client, source);
      double first_entity = qa_reserve_source_entity_range(source, (double)scene->content.admission.entity_count);
      options.no_world_model = true;
      size_t operation_count = 0;
      const qa_scene_operation *operations = scene_operations(client, scene, &selected, first_entity, options, NULL, 0,
                                                              &operation_count);
      qa_finished_operations *finished = xcalloc(1, sizeof *finished);
      frame_store_add(client, free_finished, finished);
      if (!qa_finish_scene_operations(operations, operation_count, finished)) qa_range_error("Out of memory");
      qa_render_view view;
      memset(&view, 0, sizeof view);
      view.state.viewport = scene->viewport;
      view.state.clear = input.clear;
      view.target_is_seat = true;
      view.target_seat = seat;
      view.time = time;
      view.operations = finished->operations;
      view.operation_count = finished->count;
      qa_scene_frame_builder_view(&client->frames, view);
      continue;
    }
    publish_state state = {client, scene, hooks, view_offset, options};
    qa_world_view_input world_input = input;
    if (environment != NULL) {
      world_input.has_q1_fog = environment->has_q1_fog;
      world_input.q1_fog = environment->q1_fog;
      world_input.source_sky = environment->source_sky;
      world_input.no_world_model = environment->no_world_model;
    }
    qa_world_view_input child;
    bool has_child = !world_input.no_world_model && portal(client, scene, &world_input, &child);
    if (has_child) publish(&state, &child);
    /* The portal child is drawn first; the parent must clear depth only or its color clear erases the destination
     * the surface should reveal. */
    if (has_child) {
      qa_render_view_clear *depth_only = xcalloc(1, sizeof *depth_only);
      frame_store_add(client, free, depth_only);
      depth_only->depth = 1;
      world_input.clear = depth_only;
    }
    publish(&state, &world_input);
  }
  return qa_scene_frame_builder_finish(&client->frames, false);
}

/* ---- shutdown / close ---- */
void qa_app_application_q3_client_shutdown(qa_app_application_q3_client *client) {
  if (client->backend == BACKEND_QVM && !client->closed) qa_app_application_qvm_client_shutdown(client->qvm);
  qa_app_application_q3_client_close(client);
}
void qa_app_application_q3_client_close(qa_app_application_q3_client *client) {
  if (client->closed) return;
  qa_client_command_owner_close(client->options.command_bindings, client->options.command_registration);
  if (client->time_mirror != NULL) qa_shared_cvar_mirror_destroy(client->time_mirror);
  client->time_mirror = NULL;
  if (client->backend == BACKEND_TYPESCRIPT) qa_q3_pres_client_presentation_close(client->game);
  else if (client->backend == BACKEND_QVM) qa_app_application_qvm_client_close(client->qvm);
  qa_app_q3_seat_audio_operation clear = clear_loops();
  if (qa_event_ring_admit(&client->audio_operations, &clear) < 0) qa_range_error("Out of memory");
  flush_audio(client);
  client->closed = true;
  if (client->cinematics != NULL) qa_app_application_q3_cinematics_close(client->cinematics);
  qa_app_application_q3_assets_close(client->media);
  client->body_pose_count = 0;
  client->pose_actors.count = 0;
  client->selected_bodies.count = 0;
  client->primary_body_count = 0;
  body_store_clear(client);
  if (client->scene_renderer != NULL) qa_app_application_q3_scene_renderer_close(client->scene_renderer);
  clear_submissions(client);
}
static void destroy_storage(qa_app_application_q3_client *client) {
  frame_store_clear(client);
  free(client->frame_store);
  clear_submissions(client);
  free(client->submissions);
  if (client->qvm != NULL) qa_app_application_qvm_client_destroy(client->qvm);
  if (client->game != NULL) qa_q3_pres_client_presentation_destroy(client->game);
  if (client->has_marks) qa_q3_pres_world_mark_projector_destroy(&client->marks);
  if (client->scene_renderer != NULL) qa_app_application_q3_scene_renderer_destroy(client->scene_renderer);
  if (client->services != NULL) qa_app_application_q3_services_destroy(client->services);
  if (client->media != NULL) qa_app_application_q3_assets_destroy(client->media);
  if (client->has_frames) qa_scene_frame_builder_destroy(&client->frames);
  if (client->has_light_sampler) qa_model_light_sampler_destroy(&client->light_sampler);
  if (client->time_mirror != NULL) qa_shared_cvar_mirror_destroy(client->time_mirror);
  if (client->owns_cvars) qa_cvar_registry_destroy(client->cvar_owner);
  if (client->local_source != NULL) qa_app_application_q3_source_destroy(client->local_source);
  qa_app_q3_snapshot_selection_destroy(client->selection_state);
  free(client->selection_rows);
  qa_event_ring_destroy(&client->audio_operations);
  for (size_t i = 0; i < client->command_name_count; i++) free(client->command_names[i]);
  free(client->command_names);
  for (size_t i = 0; i < client->shared_cvar_name_count; i++) free(client->shared_cvar_names[i]);
  free(client->shared_cvar_names);
  free(client->body_poses);
  free(client->primary_bodies);
  body_store_clear(client);
  free(client->body_store);
  free(client->selected_held_actors.actors);
  free(client->pose_actors.actors);
  free(client->hidden_bodies.actors);
  free(client->selected_bodies.actors);
  free(client->fog_selections);
  free(client->applied_system_info);
  free(client->applied_time_system_info);
  qa_app_qvm_presentation_artifacts_release(client->artifacts);
  if (client->options.viewport_release != NULL) client->options.viewport_release(client->options.viewport_ctx);
  free(client);
}
void qa_app_application_q3_client_destroy(qa_app_application_q3_client *client) {
  if (client == NULL) return;
  qa_app_application_q3_client_close(client);
  destroy_storage(client);
}
