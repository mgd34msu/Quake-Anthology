/* Port of src/app/bootstrap/q3-client/qvm.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/qvm.h"
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/guest_collision.h"
#include "qa/app/bootstrap/q3_client/status.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/cgame_body.h"
#include "qa/compat/qvm/client_audio_syscalls.h"
#include "qa/compat/qvm/client_browser_syscalls.h"
#include "qa/compat/qvm/client_cinematic_syscalls.h"
#include "qa/compat/qvm/client_collision_syscalls.h"
#include "qa/compat/qvm/client_mark_syscalls.h"
#include "qa/compat/qvm/client_render_syscalls.h"
#include "qa/compat/qvm/client_script_syscalls.h"
#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/client_state_syscalls.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/compatibility.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/legacy_client_abi.h"
#include "qa/compat/qvm/primary_presentation_profile.h"
#include "qa/compat/qvm/render_record.h"
#include "qa/compat/qvm/ui_key_syscalls.h"
#include "qa/content/q3/presentation/cgame_body_profile.h"
#include "qa/content/q3/presentation/world_mark_projector.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/platform/files/writable.h"
#include "qa/ui/common/legacy/script/preprocessor.h"

static noreturn void js_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
static noreturn void js_error(const char *format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static char *xstrdup(const char *text) {
  char *copy = strdup(text != NULL ? text : "");
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---- QvmPresentationArtifacts ---- */
struct qa_app_qvm_presentation_artifacts {
  size_t references;
  bool has[2]; /* ui, cgame */
  qa_resolved_qvm_artifact artifacts[2];
  bool owned[2]; /* loaded here: image and module strings are owned */
};
static qa_app_qvm_presentation_artifacts *artifacts_new(void) {
  qa_app_qvm_presentation_artifacts *a = xcalloc(1, sizeof *a);
  a->references = 1;
  return a;
}
const qa_resolved_qvm_artifact *qa_app_qvm_presentation_artifacts_ui(const qa_app_qvm_presentation_artifacts *artifacts) {
  return artifacts->has[0] ? &artifacts->artifacts[0] : NULL;
}
const qa_resolved_qvm_artifact *qa_app_qvm_presentation_artifacts_cgame(const qa_app_qvm_presentation_artifacts *artifacts) {
  return artifacts->has[1] ? &artifacts->artifacts[1] : NULL;
}
qa_app_qvm_presentation_artifacts *qa_app_qvm_presentation_artifacts_retain(qa_app_qvm_presentation_artifacts *artifacts) {
  if (artifacts != NULL) artifacts->references++;
  return artifacts;
}
void qa_app_qvm_presentation_artifacts_release(qa_app_qvm_presentation_artifacts *artifacts) {
  if (artifacts == NULL || --artifacts->references != 0) return;
  for (int i = 0; i < 2; i++) {
    if (!artifacts->has[i] || !artifacts->owned[i]) continue;
    qa_resolved_qvm_artifact *a = &artifacts->artifacts[i];
    free((char *)a->module.id);
    free((char *)a->module.artifact_path);
    free((char *)a->module.digest);
    free((char *)a->module.revision);
    qa_resolved_qvm_artifact_destroy(a);
  }
  free(artifacts);
}

/* ---- qvmClientCommands ---- */
static double buffer_execute_now(void *ctx, const char *text, const qa_command_context *source) {
  return qa_command_buffer_execute_now(ctx, text, source, -1);
}
static void buffer_insert(void *ctx, const char *text, const qa_command_context *source) {
  qa_command_buffer_insert(ctx, text, source, -1);
}
static void buffer_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_command_buffer_append(ctx, text, source, -1);
}
qa_app_qvm_command_target qa_app_qvm_command_buffer_target(qa_command_buffer *buffer) {
  qa_app_qvm_command_target target = {buffer, buffer_execute_now, buffer_insert, buffer_append};
  return target;
}
struct qa_app_qvm_client_commands {
  qa_app_qvm_command_target commands;
  qa_command_context caller; /* deep copy of the TS context */
  char name[16];
  qa_command_context source;
};
qa_app_qvm_client_commands *qa_app_qvm_client_commands_create(qa_app_qvm_command_target commands,
    const qa_command_context *context, const char *role) {
  qa_app_qvm_client_commands *c = xcalloc(1, sizeof *c);
  c->commands = commands;
  c->caller = qa_command_context_copy(context);
  snprintf(c->name, sizeof c->name, "q3-%s", role);
  memset(&c->source, 0, sizeof c->source);
  c->source.session = context->session;
  c->source.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  c->source.origin.name = c->name;
  c->source.origin.caller = &c->caller.origin;
  return c;
}
void qa_app_qvm_client_commands_destroy(qa_app_qvm_client_commands *commands) {
  if (commands == NULL) return;
  qa_command_context_release(&commands->caller);
  free(commands);
}
const qa_command_context *qa_app_qvm_client_commands_source(const qa_app_qvm_client_commands *commands) {
  return &commands->source;
}
void qa_app_qvm_client_commands_execute_now(qa_app_qvm_client_commands *commands, const char *text) {
  (void)commands->commands.execute_now(commands->commands.ctx, text, &commands->source);
}
void qa_app_qvm_client_commands_insert(qa_app_qvm_client_commands *commands, const char *text) {
  commands->commands.insert(commands->commands.ctx, text, &commands->source);
}
void qa_app_qvm_client_commands_append(qa_app_qvm_client_commands *commands, const char *text) {
  commands->commands.append(commands->commands.ctx, text, &commands->source);
}

/* ---- ApplicationQvmClient ---- */
typedef struct held_invocation {
  int64_t frame, end, gun;
  qa_app_qvm_held_weapon held;
} held_invocation;
/* TS SceneEntity & { actor }: the actor lives beside the entity. */
typedef struct held_parent { qa_scene_entity entity; qa_actor_id actor; } held_parent;
typedef struct observer { qa_app_application_qvm_client *client; size_t index; } observer;
struct qa_app_application_qvm_client {
  qa_app_application_qvm_client_options options;
  qa_app_qvm_client_commands *ui_commands, *cgame_commands;
  qa_qvm_files *cgame_files, *ui_files;
  bool has_writable; qa_user_file_store writable;
  double generation;
  bool shared_models; qa_source_clip_models *native_models;
  qa_app_shared_qvm_client_clip_models *own_models;
  qa_ui_script_global_defines *globals;
  qa_qvm_client_scripts *cgame_scripts, *ui_scripts;
  bool has_marks; qa_q3_pres_world_mark_projector marks;
  char **arguments; size_t argument_count;
  bool retired, ready;
  qa_app_qvm_presentation_artifacts *artifacts;
  bool has_cgame; qa_qvm_cgame cgame;
  qa_qvm_body_submissions *body_submissions;
  qa_qvm_body_submission *body_declarations; size_t body_declaration_count;
  bool has_ui; qa_qvm_ui ui;
  bool has_equipment; qa_app_q3_equipment_presentation equipment;
  bool has_equipment_profile, equipment_profile_owned;
  qa_q3_equipment_qvm_presentation_profile equipment_profile;
  size_t *status_observers; size_t status_observer_count; /* CleanupStack of unbind ids */
  observer *observer_contexts; /* one per status region/function entry */
  bool has_held_observer; size_t held_observer;
  int64_t held_enter_operand;
  held_invocation *held_invocations; size_t held_invocation_count, held_invocation_capacity;
  qa_app_qvm_held_weapon *held_weapons; size_t held_weapon_count, held_weapon_capacity;
  bool has_view_observer; size_t view_observer;
  bool equipment_view_visible;
  bool has_equipment_selector; size_t equipment_selector;
  bool equipment_hud_requested;
  qa_app_cgame_status_view *status;
  qa_q3_wire_snapshot snapshot; bool has_snapshot; /* client-state scratch */
  qa_qvm_entity_state_fields *snapshot_entities; size_t snapshot_entity_capacity;
};

static void assert_current(qa_app_application_qvm_client *client) {
  const qa_q3_pres_presentation_session *session = client->options.session;
  session->assert_current(session->ctx);
  if (client->retired || client->options.connection.get_generation(client->options.connection.ctx) != client->generation)
    js_error("QVM presentation belongs to a retired gamestate");
}
static void assert_current_ctx(void *ctx) { assert_current(ctx); }

static void session_print(qa_app_application_qvm_client *client, const char *text) {
  const qa_q3_pres_presentation_session *session = client->options.session;
  session->print(session->ctx, text);
}
static void print_ctx(void *ctx, const char *text) { session_print(ctx, text); }

static void held_weapon_free(qa_app_qvm_held_weapon *held) {
  free(held->parent);
  free(held->passes);
  held->parent = NULL;
  held->passes = NULL;
  held->pass_count = 0;
}
static void clear_held_weapons(qa_app_application_qvm_client *client) {
  for (size_t i = 0; i < client->held_weapon_count; i++) held_weapon_free(&client->held_weapons[i]);
  client->held_weapon_count = 0;
}

/* ---- host() syscall services ---- */
typedef struct host_frame {
  qa_app_application_qvm_client *client;
  const qa_qvm_host_call *call;
} host_frame;

static double common_milliseconds(void *ctx) {
  host_frame *frame = ctx;
  return frame->client->options.now(frame->client->options.ctx);
}
static const char *const *common_arguments(void *ctx, size_t *out_count) {
  host_frame *frame = ctx;
  *out_count = frame->client->argument_count;
  return (const char *const *)frame->client->arguments;
}
static void common_print(void *ctx, const char *text) { session_print(((host_frame *)ctx)->client, text); }
static qa_app_qvm_client_commands *role_commands(host_frame *frame) {
  return is(frame->call->role, "cgame") ? frame->client->cgame_commands : frame->client->ui_commands;
}
static void commands_execute_now(void *ctx, const char *text) {
  qa_app_qvm_client_commands_execute_now(role_commands(ctx), text);
}
static void commands_insert(void *ctx, const char *text) { qa_app_qvm_client_commands_insert(role_commands(ctx), text); }
static void commands_append(void *ctx, const char *text) { qa_app_qvm_client_commands_append(role_commands(ctx), text); }
static void commands_register(void *ctx, const char *name) {
  const qa_q3_pres_presentation_session *session = ((host_frame *)ctx)->client->options.session;
  session->register_cgame_command(session->ctx, name);
}
static void commands_remove(void *ctx, const char *name) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  client->options.remove_command(client->options.ctx, name);
}
static void commands_reliable(void *ctx, const char *text) {
  const qa_q3_pres_presentation_session *session = ((host_frame *)ctx)->client->options.session;
  session->add_reliable_command(session->ctx, text);
}

static qa_qvm_entity_state_fields entity_fields(const qa_q3_entity_state_record *s) {
  qa_qvm_entity_state_fields f;
  f.number = s->number; f.e_type = s->e_type; f.e_flags = s->e_flags;
  f.pos.type = s->pos.type; f.pos.time = s->pos.time; f.pos.duration = s->pos.duration;
  f.pos.base = s->pos.base; f.pos.delta = s->pos.delta;
  f.apos.type = s->apos.type; f.apos.time = s->apos.time; f.apos.duration = s->apos.duration;
  f.apos.base = s->apos.base; f.apos.delta = s->apos.delta;
  f.time = s->time; f.time2 = s->time2;
  f.origin = s->origin; f.origin2 = s->origin2; f.angles = s->angles; f.angles2 = s->angles2;
  f.other_entity_num = s->other_entity_num; f.other_entity_num2 = s->other_entity_num2;
  f.ground_entity_num = s->ground_entity_num; f.constant_light = s->constant_light;
  f.loop_sound = s->loop_sound; f.modelindex = s->modelindex; f.modelindex2 = s->modelindex2;
  f.client_num = s->client_num; f.frame = s->frame; f.solid = s->solid; f.event = s->event;
  f.event_parm = s->event_parm; f.powerups = s->powerups; f.weapon = s->weapon;
  f.legs_anim = s->legs_anim; f.torso_anim = s->torso_anim; f.generic1 = s->generic1;
  return f;
}
static qa_qvmx_player_state_fields player_fields(const qa_q3_player_state_record *p) {
  qa_qvmx_player_state_fields f;
  f.command_time = p->command_time; f.pm_type = p->pm_type; f.bob_cycle = p->bob_cycle;
  f.pm_flags = p->pm_flags; f.pm_time = p->pm_time; f.origin = p->origin; f.velocity = p->velocity;
  f.weapon_time = p->weapon_time; f.gravity = p->gravity; f.speed = p->speed; f.delta_angles = p->delta_angles;
  f.ground_entity_num = p->ground_entity_num; f.legs_timer = p->legs_timer; f.legs_anim = p->legs_anim;
  f.torso_timer = p->torso_timer; f.torso_anim = p->torso_anim; f.movement_dir = p->movement_dir;
  f.grapple_point = p->grapple_point; f.e_flags = p->e_flags; f.event_sequence = p->event_sequence;
  for (int i = 0; i < 2; i++) { f.events[i] = p->events[i]; f.event_parms[i] = p->event_parms[i]; }
  f.external_event = p->external_event; f.external_event_parm = p->external_event_parm;
  f.external_event_time = p->external_event_time; f.client_num = p->client_num; f.weapon = p->weapon;
  f.weapon_state = p->weapon_state; f.viewangles = p->viewangles; f.viewheight = p->viewheight;
  f.damage_event = p->damage_event; f.damage_yaw = p->damage_yaw; f.damage_pitch = p->damage_pitch;
  f.damage_count = p->damage_count;
  for (int i = 0; i < 16; i++) {
    f.stats[i] = p->stats[i]; f.persistant[i] = p->persistant[i];
    f.powerups[i] = p->powerups[i]; f.ammo[i] = p->ammo[i];
  }
  f.generic1 = p->generic1; f.loop_sound = p->loop_sound; f.jumppad_ent = p->jumppad_ent; f.ping = p->ping;
  f.pmove_framecount = p->pmove_framecount; f.jumppad_frame = p->jumppad_frame;
  f.entity_event_sequence = p->entity_event_sequence;
  return f;
}
static qa_qvmx_snapshot_current state_current(void *ctx) {
  const qa_q3_pres_presentation_session *session = ((host_frame *)ctx)->client->options.session;
  qa_q3_pres_snapshot_current current = session->snapshots.current(session->snapshots.ctx);
  qa_qvmx_snapshot_current out = {current.number, current.server_time};
  return out;
}
static void release_snapshot(qa_app_application_qvm_client *client) {
  if (!client->has_snapshot) return;
  free(client->snapshot.entities);
  client->snapshot.entities = NULL;
  client->has_snapshot = false;
}
static bool state_read(void *ctx, double number, qa_qvmx_snapshot *out) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  const qa_q3_pres_presentation_session *session = client->options.session;
  release_snapshot(client);
  if (!session->snapshots.read(session->snapshots.ctx, number, &client->snapshot)) return false;
  client->has_snapshot = true;
  const qa_q3_wire_snapshot *s = &client->snapshot;
  if (s->entity_count > client->snapshot_entity_capacity) {
    qa_qvm_entity_state_fields *next = realloc(client->snapshot_entities, s->entity_count * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    client->snapshot_entities = next;
    client->snapshot_entity_capacity = s->entity_count;
  }
  for (size_t i = 0; i < s->entity_count; i++) client->snapshot_entities[i] = entity_fields(&s->entities[i]);
  out->message_number = (double)s->message_number;
  out->server_time = (double)s->server_time;
  out->delta_number = (double)s->delta_number;
  out->flags = (double)s->flags;
  out->server_command_number = (double)s->server_command_number;
  out->parse_entities_number = (double)s->parse_entities_number;
  out->area_mask = s->area_mask;
  out->area_mask_length = s->area_mask_len;
  out->player_state = player_fields(&s->player_state);
  out->entities = client->snapshot_entities;
  out->entity_count = s->entity_count;
  return true;
}
static bool state_user_command(void *ctx, double number, qa_qvmx_wire_user_command *out) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  qa_q3_client_commands_view commands = client->options.connection.commands;
  qa_qvmx_wire_user_command read;
  if (!commands.read(commands.ctx, number, &read)) return false;
  qa_q3_wire_user_command wire;
  wire.server_time = qa_js_to_int32(read.server_time);
  for (int i = 0; i < 3; i++) wire.angles[i] = qa_js_to_int32(read.angles[i]);
  wire.forwardmove = qa_js_to_int32(read.forwardmove);
  wire.rightmove = qa_js_to_int32(read.rightmove);
  wire.upmove = qa_js_to_int32(read.upmove);
  wire.buttons = qa_js_to_int32(read.buttons);
  wire.weapon = qa_js_to_int32(read.weapon);
  if (!client->has_equipment) {
    *out = read;
    return true;
  }
  /* TS q3EquipmentCommand: { ...command, weapon: primaryWeapon, buttons: buttons & ~1 }. */
  *out = read;
  qa_q3_wire_user_command selected = qa_app_q3_equipment_command(wire, &client->equipment);
  out->weapon = client->equipment.primary_weapon;
  out->buttons = selected.buttons;
  return true;
}
static bool state_snapshot_ping(void *ctx, double number, double *out) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  return client->options.connection.snapshot_ping(client->options.connection.ctx, number, out);
}
static void set_arguments(qa_app_application_qvm_client *client, char **argv, size_t count) {
  qa_qvm_legacy_client_command_free(client->arguments, client->argument_count);
  client->arguments = argv;
  client->argument_count = count;
}
static bool state_get_server_command(void *ctx, double number, const char *const **out_tokens, size_t *out_count) {
  host_frame *frame = ctx;
  qa_app_application_qvm_client *client = frame->client;
  const char *const *argv = NULL;
  size_t argc = 0;
  bool present = client->options.connection.get_server_command(client->options.connection.ctx, number, &argv, &argc);
  assert_current(client);
  if (!present) return false;
  size_t translated_count = 0;
  char **translated = qa_qvm_legacy_client_command(argv, argc,
      frame->call->has_abi_profile ? frame->call->abi_profile : "q3-modern", &translated_count);
  if (translated == NULL) {
    char message[1200];
    snprintf(message, sizeof message, "Dropped server command \"cs %s\": no legacy client mapping\n",
             argc > 1 ? argv[1] : "");
    session_print(client, message);
    set_arguments(client, NULL, 0);
    return false;
  }
  set_arguments(client, translated, translated_count);
  *out_tokens = argv;
  *out_count = argc;
  return true;
}
static void state_set_user_command_value(void *ctx, double weapon, double sensitivity) {
  const qa_q3_pres_presentation_session *session = ((host_frame *)ctx)->client->options.session;
  session->set_user_command_value(session->ctx, weapon, sensitivity);
}

static qa_qvm_client_clip_models collision_models(void *ctx) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  return client->native_models != NULL ? qa_qvm_source_client_clip_models(client->native_models)
                                       : qa_app_shared_qvm_client_clip_models_as_client(client->own_models);
}
static void collision_load_map(void *ctx, const char *name) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  if (!is(name, client->options.map)) js_error("Cgame requested a different collision map: %s", name);
  assert_current(client);
}

static double cinematic_play(void *ctx, const char *path, qa_rect rect, double bits) {
  return qa_app_application_q3_cinematics_play_guest(ctx, path, rect, bits);
}
static double cinematic_run(void *ctx, double handle) { return qa_app_application_q3_cinematics_run_guest(ctx, handle); }
static double cinematic_stop(void *ctx, double handle) { return qa_app_application_q3_cinematics_stop_guest(ctx, handle); }
static void cinematic_draw(void *ctx, double handle, const qa_draw2d *draw) {
  qa_app_application_q3_cinematics_draw_guest(ctx, handle, (qa_draw2d *)draw);
}
static void cinematic_extents(void *ctx, double handle, qa_rect rect) {
  qa_app_application_q3_cinematics_set_extents(ctx, handle, rect);
}

/* Q3BrowserView as the compat browser services. Returned strings the
 * service owns are released after the dispatcher reads them. */
typedef struct browser_frame {
  qa_q3_browser_view *view;
  char *strings[2]; size_t string_count;
  char *retained;
} browser_frame;
typedef struct browser_write { qa_qvm_browser_write write; } browser_write;
static void browser_write_text(void *ctx, const char *text) {
  browser_write *w = ctx;
  w->write.call(w->write.ctx, text);
}
static void browser_hold(browser_frame *frame, char *text) {
  free(frame->retained);
  frame->retained = text;
}
static double b_server_count(void *ctx, double source) {
  return (double)qa_q3_browser_view_get_server_count(((browser_frame *)ctx)->view, (int64_t)source);
}
static void b_address(void *ctx, double source, double index, double size, qa_qvm_browser_write write) {
  browser_write w = {write};
  free(qa_q3_browser_view_get_server_address_string(((browser_frame *)ctx)->view, (int64_t)source, (int64_t)index,
                                                     (int64_t)size, browser_write_text, &w));
}
static void b_info(void *ctx, double source, double index, double size, qa_qvm_browser_write write) {
  browser_write w = {write};
  free(qa_q3_browser_view_get_server_info(((browser_frame *)ctx)->view, (int64_t)source, (int64_t)index,
                                          (int64_t)size, browser_write_text, &w));
}
static double b_ping(void *ctx, double source, double index) {
  return (double)qa_q3_browser_view_get_server_ping(((browser_frame *)ctx)->view, (int64_t)source, (int64_t)index);
}
static void b_mark(void *ctx, double source, double index, double visible) {
  qa_q3_browser_view_mark_server_visible_value(((browser_frame *)ctx)->view, (int64_t)source, (int64_t)index,
                                               (int64_t)visible);
}
static double b_visibility(void *ctx, double source, double index) {
  return (double)qa_q3_browser_view_server_visibility_value(((browser_frame *)ctx)->view, (int64_t)source,
                                                            (int64_t)index);
}
static void b_reset(void *ctx, double source) { qa_q3_browser_view_reset_pings(((browser_frame *)ctx)->view, (int64_t)source); }
static double b_compare(void *ctx, double source, double key, double direction, double first, double second) {
  return (double)qa_q3_browser_view_compare_servers(((browser_frame *)ctx)->view, (int64_t)source, (int64_t)key,
                                                    (int64_t)direction, (int64_t)first, (int64_t)second);
}
static double b_queue(void *ctx) { return (double)qa_q3_browser_view_get_ping_queue_count(((browser_frame *)ctx)->view); }
static void b_clear(void *ctx, double index) { qa_q3_browser_view_clear_ping(((browser_frame *)ctx)->view, (int64_t)index); }
static qa_qvmx_browser_ping b_get_ping(void *ctx, double index, double size, qa_qvm_browser_write write) {
  browser_frame *frame = ctx;
  browser_write w = {write};
  char *address = NULL;
  int64_t time = 0;
  (void)qa_q3_browser_view_get_ping(frame->view, (int64_t)index, (int64_t)size, browser_write_text, &w, &address, &time);
  browser_hold(frame, address);
  qa_qvmx_browser_ping ping = {address, (double)time};
  return ping;
}
static const char *b_source_ping_info(void *ctx, double index, double size, qa_qvm_browser_write write) {
  browser_frame *frame = ctx;
  browser_write w = {write};
  char *info = qa_q3_browser_view_source_ping_info(frame->view, (int64_t)index, (int64_t)size, browser_write_text, &w);
  browser_hold(frame, info);
  return info;
}
static bool b_update(void *ctx, double source) {
  return qa_q3_browser_view_update_visible_pings(((browser_frame *)ctx)->view, (int64_t)source);
}
static void b_load(void *ctx) { qa_q3_browser_view_load_cached_servers(((browser_frame *)ctx)->view); }
static void b_save(void *ctx) { qa_q3_browser_view_save_servers_to_cache(((browser_frame *)ctx)->view); }
typedef struct browser_string { browser_frame *frame; qa_qvm_browser_string_source source; } browser_string;
static const char *browser_string_read(void *ctx) {
  browser_string *s = ctx;
  char *text = s->source.call(s->source.ctx);
  if (s->frame->string_count < 2) s->frame->strings[s->frame->string_count++] = text;
  return text;
}
static void browser_release_strings(browser_frame *frame) {
  for (size_t i = 0; i < frame->string_count; i++) free(frame->strings[i]);
  frame->string_count = 0;
}
static double b_add(void *ctx, double source, qa_qvm_browser_string_source name, qa_qvm_browser_string_source address) {
  browser_frame *frame = ctx;
  browser_string n = {frame, name}, a = {frame, address};
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))browser_release_strings, frame);
  int64_t result = qa_q3_browser_view_add_server(frame->view, (int64_t)source, browser_string_read, &n,
                                                 browser_string_read, &a);
  qa_cleanup_pop(&node);
  browser_release_strings(frame);
  return (double)result;
}
static void b_remove(void *ctx, double source, qa_qvm_browser_string_source address) {
  browser_frame *frame = ctx;
  browser_string a = {frame, address};
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))browser_release_strings, frame);
  qa_q3_browser_view_remove_server(frame->view, (int64_t)source, browser_string_read, &a);
  qa_cleanup_pop(&node);
  browser_release_strings(frame);
}
static const char *b_status(void *ctx, const char *address, bool has_size, double size, qa_qvm_browser_write write) {
  browser_frame *frame = ctx;
  browser_write w = {write};
  char *status = qa_q3_browser_view_server_status(frame->view, address, has_size, (int64_t)size, browser_write_text, &w);
  browser_hold(frame, status);
  return status;
}
static void browser_frame_release(void *ctx) {
  browser_frame *frame = ctx;
  browser_release_strings(frame);
  free(frame->retained);
  frame->retained = NULL;
}
static qa_qvmx_browser_view browser_services(browser_frame *frame) {
  qa_qvmx_browser_view view;
  memset(&view, 0, sizeof view);
  view.ctx = frame;
  view.get_server_count = b_server_count;
  view.get_server_address_string = b_address;
  view.get_server_info = b_info;
  view.get_server_ping = b_ping;
  view.mark_server_visible_value = b_mark;
  view.server_visibility_value = b_visibility;
  view.reset_pings = b_reset;
  view.compare_servers = b_compare;
  view.get_ping_queue_count = b_queue;
  view.clear_ping = b_clear;
  view.get_ping = b_get_ping;
  view.source_ping_info = b_source_ping_info;
  view.update_visible_pings = b_update;
  view.load_cached_servers = b_load;
  view.save_servers_to_cache = b_save;
  view.add_server = b_add;
  view.remove_server = b_remove;
  view.server_status = b_status;
  return view;
}

static void key_write_ui(void *ctx, int64_t unique, const char *directory, const uint8_t *source, size_t length) {
  qa_app_application_qvm_client *client = ((host_frame *)ctx)->client;
  qa_app_key_profile_write_ui(client->options.keys, (double)unique, directory, source, length);
}
static const char *key_game_directory(void *ctx) {
  return qa_app_key_profile_game_directory(((host_frame *)ctx)->client->options.keys);
}
static void key_assert_current(void *ctx) { assert_current(((host_frame *)ctx)->client); }

static int32_t read_guest_int32(qa_qvm_memory *memory, int64_t pointer) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory, pointer, 4, 0, &view);
  return qa_qvm_writable_view_get_int32(&view, 0, true);
}

static qa_qvm_host_result host_call(void *ctx, const qa_qvm_host_call *call) {
  qa_app_application_qvm_client *client = ctx;
  assert_current(client);
  if (is(call->kind, "engine") && is(call->role, "cgame") && call->code == QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE) {
    int64_t pointer = qa_qvm_host_call_word(call, 1);
    if (client->held_invocation_count > 0) {
      held_invocation *invocation = &client->held_invocations[client->held_invocation_count - 1];
      if (pointer >= invocation->frame && pointer + QA_QVM_REF_ENTITY_BYTES <= invocation->end) {
        if (pointer == invocation->gun) {
          qa_qvm_writable_view view;
          qa_qvm_memory_view(call->guest, pointer, QA_QVM_REF_ENTITY_BYTES, 0, &view);
          qa_q3_pres_source_ref_entity_record ref;
          qa_qvm_read_ref_entity(&view, &ref);
          const qa_q3_pres_scene_shader *shader = ref.custom_shader.is_handle
              ? qa_q3_pres_q3_renderer_resources_shader_for_handle(
                    qa_app_application_q3_services_resources(client->options.services), ref.custom_shader.handle)
              : ref.custom_shader.value.shader;
          qa_app_qvm_held_weapon *held = &invocation->held;
          qa_app_qvm_held_weapon_pass *passes = realloc(held->passes, (held->pass_count + 1) * sizeof *passes);
          if (passes == NULL) qa_range_error("Out of memory");
          held->passes = passes;
          held->passes[held->pass_count].shader = shader != NULL ? shader->name : NULL;
          held->passes[held->pass_count].color = ref.shader_rgba;
          held->passes[held->pass_count].shader_time = ref.shader_time;
          held->pass_count++;
        }
        return 0;
      }
    }
  }
  if (client->body_submissions != NULL && qa_qvm_body_submissions_suppress(client->body_submissions, call)) return 0;
  if (!is(call->role, "cgame") && !is(call->role, "ui")) qa_qvm_reject_syscall(call);
  qa_qvm_host_result result = 0;
  if (is(call->role, "cgame")) {
    double status = 0;
    if (qa_app_cgame_status_view_syscall(client->status, (uint8_t *)call->base.words, call->base.words_length,
                                         call->guest, &status))
      return qa_qvm_host_word(status);
  }
  const qa_app_application_qvm_client_options *o = &client->options;
  host_frame frame = {client, call};
  qa_qvm_cvar_table status_cvars = qa_app_cgame_status_view_cvars(client->status);
  qa_qvm_common_services common;
  memset(&common, 0, sizeof common);
  common.role = call->role;
  common.cvar_table = is(call->role, "cgame") ? &status_cvars : &o->cvars;
  common.ctx = &frame;
  common.print = common_print;
  common.milliseconds = common_milliseconds;
  common.arguments = common_arguments;
  common.commands.ctx = &frame;
  common.commands.append = commands_append;
  if (is(call->role, "cgame")) {
    common.commands.register_command = commands_register;
    common.commands.remove_command = commands_remove;
    common.commands.reliable = commands_reliable;
  } else {
    common.commands.execute_now = commands_execute_now;
    common.commands.insert = commands_insert;
  }
  if (qa_qvm_common_syscall(call, common, &result)) return result;
  if (qa_qvm_file_syscall(call, is(call->role, "cgame") ? client->cgame_files : client->ui_files, &result)) return result;
  if (qa_qvm_client_script_syscall(call, is(call->role, "cgame") ? client->cgame_scripts : client->ui_scripts, &result))
    return result;
  if (qa_qvm_client_render_syscall(call, qa_app_application_q3_services_resources(o->services),
                                   qa_app_application_q3_services_draw(o->services), &result))
    return result;
  qa_qvm_client_audio_services audio = {call->role, qa_app_application_q3_services_sound(o->services), &frame, common_print};
  if (qa_qvm_client_audio_syscall(call, audio, &result)) return result;
  qa_qvm_client_state_services state;
  memset(&state, 0, sizeof state);
  state.connection = o->connection;
  state.snapshots.ctx = &frame;
  state.snapshots.current = state_current;
  state.snapshots.read = state_read;
  state.ctx = &frame;
  state.snapshot_ping = state_snapshot_ping;
  state.get_server_command = state_get_server_command;
  state.set_user_command_value = state_set_user_command_value;
  state.user_command = state_user_command;
  if (qa_qvm_client_state_syscall(call, state, &result)) return result;
  qa_qvm_client_collision_services collision = {&frame, collision_models, collision_load_map};
  if (qa_qvm_client_collision_syscall(call, collision, &result)) return result;
  if (qa_qvm_client_mark_syscall(call, qa_qvm_bsp_mark_projector(&client->marks.projector), &result)) return result;
  qa_qvm_client_cinematic_services cinematics;
  cinematics.cinematics.ctx = qa_app_application_q3_services_cinematics(o->services);
  cinematics.cinematics.play_guest = cinematic_play;
  cinematics.cinematics.run_guest = cinematic_run;
  cinematics.cinematics.stop_guest = cinematic_stop;
  cinematics.cinematics.draw_guest = cinematic_draw;
  cinematics.cinematics.set_extents = cinematic_extents;
  cinematics.draw = qa_app_application_q3_services_draw(o->services);
  cinematics.ctx = &frame;
  cinematics.developer_print = common_print;
  if (qa_qvm_client_cinematic_syscall(call, cinematics, &result)) return result;
  browser_frame browser = {o->browser, {NULL, NULL}, 0, NULL};
  qa_cleanup browser_node;
  qa_cleanup_push(&browser_node, browser_frame_release, &browser);
  bool browsed = qa_qvm_client_browser_syscall(call, browser_services(&browser), &result);
  qa_cleanup_pop(&browser_node);
  browser_frame_release(&browser);
  if (browsed) return result;
  qa_qvm_ui_key_services keys = {qa_app_key_profile_state(o->keys), &frame, key_write_ui, key_game_directory,
                                 key_assert_current};
  if (qa_qvm_ui_key_syscall(call, &keys, &result)) return result;
  if (o->scalar(o->ctx, call, client, &result)) return result;
  qa_qvm_reject_syscall(call);
}

/* ---- module(role) ---- */
static qa_qvm_module_options module_options(qa_app_application_qvm_client *client, int index) {
  const char *role = index == 0 ? "ui" : "cgame";
  const qa_app_application_qvm_client_options *o = &client->options;
  const qa_resolved_qvm_artifact *retained = o->artifacts == NULL ? NULL
      : index == 0 ? qa_app_qvm_presentation_artifacts_ui(o->artifacts) : qa_app_qvm_presentation_artifacts_cgame(o->artifacts);
  if (retained != NULL) {
    if (!is(retained->role, role)) js_error("Retained presentation artifact has the wrong role");
  } else {
    char path[32];
    snprintf(path, sizeof path, "vm/%s.qvm", role);
    qa_content_mounted *mounts = qa_app_application_q3_assets_provider(o->media)->mounts;
    qa_content_opened_resource opened;
    bool found = qa_content_mounted_open(mounts, path, NULL, NULL, &opened);
    assert_current(client);
    if (!found) js_error("Missing native module: %s", path);
    qa_cleanup bytes_node;
    qa_cleanup_push(&bytes_node, free, opened.bytes);
    qa_qvm_abi_profile abi_profile = NULL;
    qa_qvm_module_path_digest module = {path, opened.reference->digest};
    if (!qa_qvm_read_compatibility(mounts, module, role, &abi_profile)) qa_range_error("Out of memory");
    assert_current(client);
    const qa_resource_provenance *provenance = &opened.reference->provenance;
    const qa_mount_identity *identity = provenance->kind == QA_CONTENT_MOUNT_ARCHIVE
        ? &provenance->archive_mount->identity : &provenance->loose_mount->identity;
    char revision[512], id[32];
    snprintf(revision, sizeof revision, "%s:%lld", identity->id, (long long)identity->generation);
    snprintf(id, sizeof id, "q3:%s", role);
    qa_qvm_resolve_artifact_options resolve;
    memset(&resolve, 0, sizeof resolve);
    resolve.module.id = xstrdup(id);
    resolve.module.artifact_path = xstrdup(path);
    resolve.module.digest = xstrdup(opened.reference->digest);
    resolve.module.revision = xstrdup(revision);
    resolve.role = role;
    resolve.bytes = opened.bytes;
    resolve.length = opened.byte_length;
    resolve.has_abi_profile = true;
    resolve.abi_profile = abi_profile;
    qa_resolved_qvm_artifact *slot = &client->artifacts->artifacts[index];
    if (!qa_qvm_resolve_artifact(resolve, slot)) qa_range_error("Out of memory");
    qa_cleanup_pop(&bytes_node);
    free(opened.bytes);
    client->artifacts->has[index] = true;
    client->artifacts->owned[index] = true;
    if (slot->is_typescript || (slot->known != NULL && !is(slot->known->product, "baseq3")))
      js_error("Unsupported native baseq3 module: %s", path);
  }
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = &client->artifacts->artifacts[index];
  options.host = host_call;
  options.host_ctx = client;
  /* TS qvmSemanticsFromCvar(cvars, role): vm_<role> 1 is interpreted. */
  qa_cvar_snapshot semantics;
  bool registered = o->cvars.register_cvar(o->cvars.ctx, index == 0 ? "vm_ui" : "vm_cgame", "2", true, 1, &semantics);
  bool interpreted = registered && semantics.integer_value == 1;
  if (registered) qa_cvar_read_free(&semantics);
  options.semantics = interpreted ? "interpreted" : "compiled";
  return options;
}

/* ---- equipment observers ---- */
static int32_t proceed(qa_qvm_module_function_call *call) {
  return is(call->execution, "asynchronous") ? call->proceed_async(call->call_ctx) : call->proceed(call->call_ctx);
}
static int32_t call_word(const qa_qvm_module_function_call *call, int64_t index) {
  int64_t offset = index * 4;
  if (offset < 0 || offset + 4 > (int64_t)call->words_length) qa_range_error("Offset is outside the bounds of the DataView");
  int32_t value;
  memcpy(&value, call->words + offset, sizeof value);
  return value;
}
typedef struct view_decision { qa_app_application_qvm_client *client; } view_decision;
static bool view_decide(void *ctx, bool original_taken, void *call_ctx, qa_qvm_cancel_fn cancel) {
  (void)call_ctx; (void)cancel; /* TS decide(originalTaken) takes no control */
  qa_app_application_qvm_client *client = ctx;
  if (original_taken == client->equipment_profile.view_taken) client->equipment_view_visible = true;
  return original_taken;
}
static int32_t view_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_app_application_qvm_client *client = ctx;
  const qa_app_application_qvm_client_options *o = &client->options;
  if (o->view_weapon_visible != NULL && !o->view_weapon_visible(o->ctx)) return 0;
  qa_qvm_module_branch_binding binding = {(int64_t)client->equipment_profile.view_decision, client, view_decide};
  call->branches(call->call_ctx, &binding, 1);
  return proceed(call);
}
static bool region_decide(void *ctx, bool original_taken, void *call_ctx, qa_qvm_cancel_fn cancel) {
  (void)call_ctx; (void)cancel; /* TS decide(originalTaken) takes no control */
  observer *o = ctx;
  if (original_taken == o->client->equipment_profile.status_regions[o->index].taken) o->client->equipment_hud_requested = true;
  return original_taken;
}
static const char *region_skip(void *ctx, void *control_ctx) {
  (void)ctx; (void)control_ctx; /* TS run: () => "skip" */
  return "skip";
}
static int32_t region_hook(void *ctx, qa_qvm_module_function_call *call) {
  observer *o = ctx;
  qa_app_application_qvm_client *client = o->client;
  if (client->has_equipment) {
    const qa_q3_equipment_hud_region *entry = &client->equipment_profile.status_regions[o->index];
    qa_qvm_module_branch_binding binding = {(int64_t)entry->decision, o, region_decide};
    call->branches(call->call_ctx, &binding, 1);
    qa_qvm_module_region_binding *regions = xcalloc(entry->ammo_count, sizeof *regions);
    for (size_t i = 0; i < entry->ammo_count; i++) {
      regions[i].entry = (int64_t)entry->ammo[i].entry;
      regions[i].join = (int64_t)entry->ammo[i].join;
      regions[i].ctx = client;
      regions[i].run = region_skip;
    }
    call->regions(call->call_ctx, regions, entry->ammo_count);
    qa_cleanup node;
    qa_cleanup_push(&node, free, regions);
    int32_t result = proceed(call);
    qa_cleanup_pop(&node);
    free(regions);
    return result;
  }
  return proceed(call);
}
static int32_t function_hook(void *ctx, qa_qvm_module_function_call *call) {
  observer *o = ctx;
  if (!o->client->has_equipment) return proceed(call);
  o->client->equipment_hud_requested = true;
  return 0;
}
typedef struct warning_state { qa_qvm_writable_view view; int32_t original; } warning_state;
static void restore_warning(void *ctx) {
  warning_state *state = ctx;
  qa_qvm_writable_view_set_int32(&state->view, 0, state->original, true);
}
static int32_t warning_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_app_application_qvm_client *client = ctx;
  if (!client->has_equipment) return proceed(call);
  const qa_q3_equipment_qvm_presentation_profile *profile = &client->equipment_profile;
  warning_state state;
  qa_qvm_memory_view(call->guest, (int64_t)profile->warning_state, 4, 0, &state.view);
  state.original = qa_qvm_writable_view_get_int32(&state.view, 0, true);
  double value = is(client->equipment.warning, "empty") ? profile->warning_empty
      : is(client->equipment.warning, "low") ? profile->warning_low : profile->warning_none;
  qa_qvm_writable_view_set_int32(&state.view, 0, qa_js_to_int32(value), true);
  qa_cleanup node;
  qa_cleanup_push(&node, restore_warning, &state);
  int32_t result = proceed(call);
  qa_cleanup_pop(&node);
  restore_warning(&state);
  return result;
}
static void finish_held(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  if (client->held_invocation_count == 0) js_error("Original weapon attachment scopes unwound out of order");
  held_invocation *invocation = &client->held_invocations[--client->held_invocation_count];
  held_weapon_free(&invocation->held);
}
static int32_t held_hook(void *ctx, qa_qvm_module_function_call *call) {
  qa_app_application_qvm_client *client = ctx;
  const qa_app_application_qvm_client_options *o = &client->options;
  const qa_q3_equipment_qvm_presentation_profile *held = &client->equipment_profile;
  if (call_word(call, (int64_t)held->held_state_argument) != 0) return proceed(call);
  int32_t number = read_guest_int32(call->guest, (int64_t)call_word(call, (int64_t)held->held_entity_argument)
                                                    + (int64_t)held->held_entity_number_offset);
  qa_actor_id actor;
  if (o->held_weapon_actor == NULL || !o->held_weapon_actor(o->ctx, number, &actor)) return proceed(call);
  qa_qvm_writable_view parent_view;
  qa_qvm_memory_view(call->guest, call_word(call, (int64_t)held->held_parent_argument), QA_QVM_REF_ENTITY_BYTES, 0,
                     &parent_view);
  qa_q3_pres_source_ref_entity_record ref;
  qa_qvm_read_ref_entity(&parent_view, &ref);
  if (ref.kind != QA_Q3_PRES_REF_MODEL) js_error("Original weapon parent is not a model");
  qa_q3_pres_scene_model model = ref.model.is_handle
      ? qa_q3_pres_q3_renderer_resources_model_for_handle(qa_app_application_q3_services_resources(o->services),
                                                          ref.model.handle)
      : ref.model.model;
  if (model.kind != QA_Q3_PRES_SCENE_MODEL_LOADED) js_error("Original weapon parent has no decoded model");
  held_parent *parent = xcalloc(1, sizeof *parent);
  parent->actor = actor;
  qa_scene_entity *entity = &parent->entity;
  entity->actor = &parent->actor;
  entity->resource = model.resource;
  entity->model = model.model;
  entity->transform.origin = ref.origin;
  entity->transform.axis = ref.axis;
  entity->transform.scale = (qa_vec3){1, 1, 1};
  entity->previous_origin = ref.old_origin;
  entity->pose.is_frame = true;
  entity->pose.frame = ref.frame;
  entity->pose.previous_frame = ref.old_frame;
  entity->pose.back_lerp = ref.back_lerp;
  entity->color = (qa_vec4){1, 1, 1, 1};
  entity->skin = ref.skin_num;
  entity->shader_time.kind = QA_SOURCE_TIME_SECONDS;
  entity->shader_time.value = ref.shader_time;
  entity->lighting_origin = ref.lighting_origin;
  entity->shadow_plane = ref.shadow_plane;
  entity->flags_kind = "q3";
  entity->flags_bits = ref.render_flags;
  int64_t end = call->words_offset - 8, frame = end - client->held_enter_operand;
  if (client->held_invocation_count == client->held_invocation_capacity) {
    size_t capacity = client->held_invocation_capacity == 0 ? 4 : client->held_invocation_capacity * 2;
    held_invocation *next = realloc(client->held_invocations, capacity * sizeof *next);
    if (next == NULL) { free(parent); qa_range_error("Out of memory"); }
    client->held_invocations = next;
    client->held_invocation_capacity = capacity;
  }
  held_invocation *invocation = &client->held_invocations[client->held_invocation_count++];
  memset(invocation, 0, sizeof *invocation);
  invocation->frame = frame;
  invocation->end = end;
  invocation->gun = frame + (int64_t)held->held_gun;
  invocation->held.content = qa_app_application_q3_assets_content(o->media);
  invocation->held.parent_actor = actor;
  invocation->held.parent = entity;
  size_t depth = client->held_invocation_count;
  qa_cleanup node;
  qa_cleanup_push(&node, finish_held, client);
  int32_t value = proceed(call);
  qa_cleanup_pop(&node);
  if (client->held_invocation_count != depth) js_error("Original weapon attachment scopes unwound out of order");
  invocation = &client->held_invocations[--client->held_invocation_count];
  if (invocation->held.pass_count != 0) {
    if (client->held_weapon_count == client->held_weapon_capacity) {
      size_t capacity = client->held_weapon_capacity == 0 ? 4 : client->held_weapon_capacity * 2;
      qa_app_qvm_held_weapon *next = realloc(client->held_weapons, capacity * sizeof *next);
      if (next == NULL) { held_weapon_free(&invocation->held); qa_range_error("Out of memory"); }
      client->held_weapons = next;
      client->held_weapon_capacity = capacity;
    }
    client->held_weapons[client->held_weapon_count++] = invocation->held;
  } else held_weapon_free(&invocation->held);
  return value;
}
static int32_t selector_hook(void *ctx, qa_qvm_module_function_call *call) {
  (void)ctx; (void)call; /* TS () => 0 */
  return 0;
}
static qa_guest_callback_reference qvm_reference(const qa_resolved_qvm_artifact *artifact, double instruction) {
  qa_guest_callback_reference reference;
  memset(&reference, 0, sizeof reference);
  reference.kind = QA_GUEST_CALLBACK_QVM;
  reference.module = artifact->module;
  reference.instruction_index = instruction;
  return reference;
}
static void push_status_observer(qa_app_application_qvm_client *client, size_t id) {
  size_t *next = realloc(client->status_observers, (client->status_observer_count + 1) * sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  client->status_observers = next;
  client->status_observers[client->status_observer_count++] = id;
}

/* ---- bodies ---- */
static bool body_hidden(void *ctx, double entity) {
  const qa_app_application_qvm_client_options *o = &((qa_app_application_qvm_client *)ctx)->options;
  return o->body_overrides != NULL && o->body_overrides->hidden(o->body_overrides->ctx, entity);
}
static bool body_selected(void *ctx, double entity) {
  const qa_app_application_qvm_client_options *o = &((qa_app_application_qvm_client *)ctx)->options;
  return o->body_capture != NULL && o->body_capture->selected(o->body_capture->ctx, entity);
}
static bool body_submit(void *ctx, double entity, qa_qvm_body_part part, const qa_q3_pres_source_ref_entity_record *source,
                        bool base) {
  const qa_app_application_qvm_client_options *o = &((qa_app_application_qvm_client *)ctx)->options;
  qa_q3_pres_q3_renderer_resources *resources = qa_app_application_q3_services_resources(o->services);
  qa_q3_pres_ref_model_entity resolved;
  memset(&resolved, 0, sizeof resolved);
  resolved.shading.render_flags = source->render_flags;
  resolved.shading.custom_shader = source->custom_shader.is_handle
      ? qa_q3_pres_q3_renderer_resources_shader_for_handle(resources, source->custom_shader.handle)
      : source->custom_shader.value.shader;
  resolved.shading.shader_rgba = source->shader_rgba;
  resolved.shading.shader_tex_coord = source->shader_tex_coord;
  resolved.shading.shader_time = source->shader_time;
  resolved.model = source->model.is_handle ? qa_q3_pres_q3_renderer_resources_model_for_handle(resources, source->model.handle)
                                           : source->model.model;
  resolved.origin = source->origin;
  resolved.old_origin = source->old_origin;
  resolved.axis = source->axis;
  resolved.non_normalized_axes = source->non_normalized_axes;
  resolved.lighting_origin = source->lighting_origin;
  resolved.shadow_plane = source->shadow_plane;
  resolved.frame = source->frame;
  resolved.old_frame = source->old_frame;
  resolved.back_lerp = source->back_lerp;
  resolved.skin_num = source->skin_num;
  resolved.custom_skin = source->custom_skin.is_handle
      ? qa_q3_pres_q3_renderer_resources_skin_for_handle(resources, source->custom_skin.handle)
      : source->custom_skin.value.skin;
  return o->body_capture != NULL && o->body_capture->submit(o->body_capture->ctx, entity, part, &resolved, base);
}

/* ---- lifetime ---- */
static qa_qvm_cgame_lifetime_state lifetime_current(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  qa_qvm_cgame_lifetime_state state;
  state.generation = client->options.connection.get_generation(client->options.connection.ctx);
  state.server_message_number = client->options.connection.get_server_message_sequence(client->options.connection.ctx);
  state.dropped = NULL;
  return state;
}
static void lifetime_begin_loading(void *ctx) { ((qa_app_application_qvm_client *)ctx)->ready = false; }
static void lifetime_prime(void *ctx, double generation) {
  (void)generation; /* TS prime() ignores the generation */
  ((qa_app_application_qvm_client *)ctx)->ready = true;
}
static bool status_visible(void *ctx) {
  const qa_q3_pres_presentation_session *session = ((qa_app_application_qvm_client *)ctx)->options.session;
  return session->status_visible == NULL || session->status_visible(session->ctx) != false;
}

static void initialize(void *ctx) {
  qa_app_application_qvm_client *owner = ctx;
  const qa_app_application_qvm_client_options *options = &owner->options;
  qa_qvm_module_options ui_options = module_options(owner, 0);
  qa_qvm_ui_create(&owner->ui, options->seat, &ui_options, assert_current_ctx, owner);
  owner->has_ui = true;
  qa_qvm_ui_init_export(&owner->ui, true);
  qa_qvm_module_options cgame_options = module_options(owner, 1);
  qa_qvm_cgame_lifetime lifetime = {owner, assert_current_ctx, lifetime_current, lifetime_begin_loading, lifetime_prime};
  qa_qvm_cgame_init_module(&owner->cgame, options->seat, &cgame_options, lifetime);
  owner->has_cgame = true;
  const qa_resolved_qvm_artifact *artifact = cgame_options.artifact;
  qa_content_mounted *mounts = qa_app_application_q3_assets_provider(options->media)->mounts;
  owner->has_equipment_profile = qa_qvm_read_equipment_presentation(artifact, mounts, &owner->equipment_profile,
                                                                    &owner->equipment_profile_owned);
  if (owner->has_equipment_profile) {
    const qa_q3_equipment_qvm_presentation_profile *profile = &owner->equipment_profile;
    qa_guest_callback_reference reference = qvm_reference(artifact, profile->view_entry);
    owner->view_observer = qa_qvm_module_bind_invocation(&owner->cgame.module, &reference, view_hook, owner);
    owner->has_view_observer = true;
    size_t entries = profile->status_is_regions ? profile->status_region_count : profile->status_function_count;
    owner->observer_contexts = xcalloc(entries, sizeof *owner->observer_contexts);
    for (size_t i = 0; i < entries; i++) {
      owner->observer_contexts[i].client = owner;
      owner->observer_contexts[i].index = i;
      reference = qvm_reference(artifact, profile->status_is_regions ? profile->status_regions[i].entry
                                                                     : profile->status_functions[i]);
      push_status_observer(owner, qa_qvm_module_bind_invocation(&owner->cgame.module, &reference,
          profile->status_is_regions ? region_hook : function_hook, &owner->observer_contexts[i]));
    }
    reference = qvm_reference(artifact, profile->warning_entry);
    push_status_observer(owner, qa_qvm_module_bind_invocation(&owner->cgame.module, &reference, warning_hook, owner));
    if (!artifact->is_typescript) {
      int64_t entry = (int64_t)profile->held_entry;
      const qa_qvm_instruction *enter = entry >= 0 && (size_t)entry < artifact->image.instruction_count
          ? &artifact->image.instructions[entry] : NULL;
      if (enter == NULL || enter->opcode != QA_QVM_OP_ENTER) js_error("Original held-weapon entry is invalid");
      owner->held_enter_operand = enter->operand;
      reference = qvm_reference(artifact, profile->held_entry);
      owner->held_observer = qa_qvm_module_bind_invocation(&owner->cgame.module, &reference, held_hook, owner);
      owner->has_held_observer = true;
    }
  }
  qa_q3_pres_body_profile_result bodies = qa_q3_pres_read_cgame_body_profile(artifact, mounts,
      &owner->body_declarations, &owner->body_declaration_count);
  if (bodies == QA_Q3_PRES_BODY_PROFILE_OOM) qa_range_error("Out of memory");
  assert_current(owner);
  if (bodies == QA_Q3_PRES_BODY_PROFILE_OK) {
    qa_qvm_body_hidden_fn hidden = {owner, body_hidden};
    qa_qvm_body_capture capture = {owner, body_selected, body_submit};
    owner->body_submissions = qa_qvm_body_submissions_create(&owner->cgame.module, &artifact->image,
        !artifact->is_typescript && is(artifact->role, "cgame"), owner->body_declarations,
        owner->body_declaration_count, hidden, &capture);
    if (owner->body_submissions == NULL) qa_range_error("Out of memory");
  }
  const qa_q3_pres_presentation_session *session = options->session;
  qa_qvm_cgame_init(&owner->cgame, session->server_message_sequence(session->ctx), session->last_executed_server_command,
                    session->client_number);
  assert_current(owner);
}

static void destroy_storage(qa_app_application_qvm_client *client) {
  release_snapshot(client);
  free(client->snapshot_entities);
  if (client->has_cgame) qa_qvm_cgame_destroy(&client->cgame);
  if (client->has_ui) qa_qvm_ui_destroy(&client->ui);
  if (client->body_submissions != NULL) qa_qvm_body_submissions_destroy(client->body_submissions);
  if (client->body_declarations != NULL)
    qa_q3_pres_body_profile_free(client->body_declarations, client->body_declaration_count);
  if (client->equipment_profile_owned) qa_qvm_equipment_presentation_free(&client->equipment_profile);
  free(client->observer_contexts);
  free(client->status_observers);
  clear_held_weapons(client);
  free(client->held_weapons);
  for (size_t i = 0; i < client->held_invocation_count; i++) held_weapon_free(&client->held_invocations[i].held);
  free(client->held_invocations);
  qa_qvm_files_destroy(client->cgame_files);
  qa_qvm_files_destroy(client->ui_files);
  qa_qvm_client_scripts_destroy(client->cgame_scripts);
  qa_qvm_client_scripts_destroy(client->ui_scripts);
  if (client->globals != NULL) qa_ui_script_global_defines_destroy(client->globals);
  if (client->has_writable) qa_user_file_store_destroy(&client->writable);
  if (client->own_models != NULL) qa_app_shared_qvm_client_clip_models_destroy(client->own_models);
  if (client->has_marks) qa_q3_pres_world_mark_projector_destroy(&client->marks);
  qa_app_cgame_status_view_destroy(client->status);
  qa_app_qvm_client_commands_destroy(client->ui_commands);
  qa_app_qvm_client_commands_destroy(client->cgame_commands);
  set_arguments(client, NULL, 0);
  qa_app_qvm_presentation_artifacts_release(client->artifacts);
  free(client);
}
void qa_app_application_qvm_client_destroy(qa_app_application_qvm_client *client) {
  if (client == NULL) return;
  qa_app_application_qvm_client_close(client);
  destroy_storage(client);
}

static void close_ctx(void *ctx) { qa_app_application_qvm_client_close(ctx); }
qa_app_application_qvm_client *qa_app_application_qvm_client_create(const qa_app_application_qvm_client_options *options) {
  qa_app_application_qvm_client *owner = xcalloc(1, sizeof *owner);
  owner->options = *options;
  owner->status = qa_app_cgame_status_view_create(options->cvars, status_visible, owner);
  owner->ui_commands = qa_app_qvm_client_commands_create(options->commands, &options->command_context, "ui");
  owner->cgame_commands = qa_app_qvm_client_commands_create(options->commands, &options->command_context, "cgame");
  owner->generation = options->connection.get_generation(options->connection.ctx);
  owner->native_models = qa_shared_native_q3_clip_models(options->queries);
  if (owner->native_models == NULL) {
    owner->own_models = qa_app_shared_qvm_client_clip_models_create(options->queries, options->cvars);
    if (owner->own_models == NULL) qa_range_error("Out of memory");
  }
  qa_q3_pres_world_mark_projector_init(&owner->marks, qa_app_application_assets_world(options->assets));
  owner->has_marks = true;
  const char *user_root = qa_app_application_q3_assets_user_content_root(options->media);
  if (user_root != NULL) {
    if (!qa_user_file_store_init(&owner->writable, user_root)) qa_range_error("Out of memory");
    owner->has_writable = true;
  }
  qa_content_mounted *mounts = qa_app_application_q3_assets_provider(options->media)->mounts;
  qa_qvm_file_services files = {mounts, owner->has_writable ? &owner->writable : NULL, owner, print_ctx,
                                assert_current_ctx};
  owner->cgame_files = qa_qvm_files_create(files);
  owner->ui_files = qa_qvm_files_create(files);
  owner->globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  if (owner->cgame_files == NULL || owner->ui_files == NULL || owner->globals == NULL) qa_range_error("Out of memory");
  qa_qvm_client_script_services scripts = {mounts, owner->globals, owner, assert_current_ctx, print_ctx};
  owner->cgame_scripts = qa_qvm_client_scripts_create(scripts);
  owner->ui_scripts = qa_qvm_client_scripts_create(scripts);
  if (owner->cgame_scripts == NULL || owner->ui_scripts == NULL) qa_range_error("Out of memory");
  owner->artifacts = options->artifacts != NULL ? qa_app_qvm_presentation_artifacts_retain(options->artifacts)
                                                : artifacts_new();
  qa_caught_error error;
  if (qa_try(initialize, owner, &error)) return owner;
  qa_caught_error cleanup;
  bool closed = qa_try(close_ctx, owner, &cleanup);
  destroy_storage(owner);
  if (!closed) qa_raise("AggregateError", 0, 2, "QVM initialization and cleanup failed");
  qa_rethrow(&error);
}

const qa_app_qvm_held_weapon *qa_app_application_qvm_client_shared_held_weapons(
    const qa_app_application_qvm_client *client, size_t *out_count) {
  *out_count = client->held_weapon_count;
  return client->held_weapons;
}
bool qa_app_application_qvm_client_shared_equipment_hud(const qa_app_application_qvm_client *client) {
  return client->equipment_hud_requested;
}
bool qa_app_application_qvm_client_shared_equipment_view_visible(const qa_app_application_qvm_client *client, bool *out) {
  if (!client->has_equipment_profile) return false;
  *out = client->equipment_view_visible;
  return true;
}
qa_app_qvm_presentation_artifacts *qa_app_application_qvm_client_presentation_artifacts(qa_app_application_qvm_client *client) {
  if (!client->artifacts->has[0] || !client->artifacts->has[1]) js_error("Presentation modules are not initialized");
  return qa_app_qvm_presentation_artifacts_retain(client->artifacts);
}
void qa_app_application_qvm_client_update_screen(qa_app_application_qvm_client *client, const qa_qvm_host_call *call) {
  assert_current(client);
  if (is(call->role, "ui")) {
    int32_t words[2] = {QA_QVM_UI_EXPORT_UI_DRAW_CONNECT_SCREEN, 1};
    qa_qvm_arguments arguments = qa_qvm_make_arguments(words, 2);
    (void)call->base.invoke_async(call->base.call_ctx, arguments.words, 10, 0, NULL, NULL);
  } else if (client->has_ui) qa_qvm_ui_draw_connect_screen(&client->ui, true);
}
void qa_app_application_qvm_client_draw(qa_app_application_qvm_client *client, double time, bool demo_playback) {
  assert_current(client);
  const qa_app_application_qvm_client_options *o = &client->options;
  if (!client->ready || !client->has_cgame) js_error("QVM cgame has not initialized");
  bool hide_bodies = o->body_overrides != NULL && o->body_overrides->active(o->body_overrides->ctx);
  bool capture_bodies = o->body_capture != NULL && o->body_capture->active(o->body_capture->ctx);
  if ((hide_bodies || capture_bodies) && client->body_submissions == NULL)
    js_error("This cgame needs an artifact-matched cgame-presentation.json declaration for body replacements");
  if (capture_bodies && (client->body_submissions == NULL
                         || !qa_qvm_body_submissions_get_captures_player_meshes(client->body_submissions)))
    js_error("This cgame needs artifact-matched player mesh call sites for component body materials");
  if (client->body_submissions != NULL) qa_qvm_body_submissions_enable(client->body_submissions, hide_bodies || capture_bodies);
  if (o->view_weapon_visible != NULL && !o->view_weapon_visible(o->ctx) && !client->has_equipment_profile)
    js_error("Component camera control requires a qualified original view-weapon boundary");
  client->has_equipment = o->equipment_weapon != NULL && o->equipment_weapon(o->ctx, &client->equipment);
  if (client->has_equipment && !client->has_equipment_profile)
    js_error("Selected weapon presentation requires a qualified original cgame HUD boundary");
  if (client->has_equipment && client->has_equipment_profile && !client->has_equipment_selector) {
    const qa_execution_profile *profile = qa_qvm_module_get_profile(&client->cgame.module);
    qa_guest_callback_reference reference;
    memset(&reference, 0, sizeof reference);
    reference.kind = QA_GUEST_CALLBACK_QVM;
    reference.module = profile->module;
    reference.instruction_index = client->equipment_profile.hud;
    client->equipment_selector = qa_qvm_module_bind_function(&client->cgame.module, &reference, selector_hook, client);
    client->has_equipment_selector = true;
  } else if (!client->has_equipment) {
    if (client->has_equipment_selector) qa_qvm_module_unbind(&client->cgame.module, client->equipment_selector);
    client->has_equipment_selector = false;
  }
  client->equipment_hud_requested = false;
  client->equipment_view_visible = false;
  clear_held_weapons(client);
  qa_qvm_cgame_draw_active_frame(&client->cgame, time, "center", demo_playback);
  if ((qa_js_to_int32(o->key_catcher(o->ctx)) & 2) != 0 && client->has_ui)
    qa_qvm_ui_refresh(&client->ui, qa_js_to_int32(trunc(o->now(o->ctx))));
}
void qa_app_application_qvm_client_refresh_status(qa_app_application_qvm_client *client) {
  if (!client->retired) {
    assert_current(client);
    qa_app_cgame_status_view_refresh(client->status);
  }
}
bool qa_app_application_qvm_client_command(qa_app_application_qvm_client *client, const char *const *argv, size_t argc) {
  assert_current(client);
  if (client->has_cgame && qa_qvm_cgame_console_command(&client->cgame, argv, argc)) return true;
  if (!client->has_ui) return false;
  return qa_qvm_ui_console_command(&client->ui, qa_js_to_int32(trunc(client->options.now(client->options.ctx))), argv, argc);
}
void qa_app_application_qvm_client_key_event(qa_app_application_qvm_client *client, double key, bool down) {
  assert_current(client);
  if ((qa_js_to_int32(client->options.key_catcher(client->options.ctx)) & 2) != 0) {
    if (client->has_ui) qa_qvm_ui_key_event(&client->ui, qa_js_to_int32(key), down);
  } else if (client->has_cgame && qa_qvm_cgame_get_supports_input_events(&client->cgame))
    qa_qvm_cgame_key_event(&client->cgame, key, down);
}
void qa_app_application_qvm_client_mouse_event(qa_app_application_qvm_client *client, double x, double y) {
  assert_current(client);
  if ((qa_js_to_int32(client->options.key_catcher(client->options.ctx)) & 2) != 0) {
    if (client->has_ui) qa_qvm_ui_mouse_event(&client->ui, qa_js_to_int32(x), qa_js_to_int32(y));
  } else if (client->has_cgame && qa_qvm_cgame_get_supports_input_events(&client->cgame))
    qa_qvm_cgame_mouse_event(&client->cgame, x, y);
}
bool qa_app_application_qvm_client_captures_input(const qa_app_application_qvm_client *client) {
  double catcher = client->options.key_catcher(client->options.ctx);
  if (client->has_cgame && qa_qvm_cgame_get_supports_input_events(&client->cgame)) return catcher != 0;
  return (qa_js_to_int32(catcher) & 2) != 0;
}
void qa_app_application_qvm_client_event_handling(qa_app_application_qvm_client *client, qa_q3_cgame_event_handling mode) {
  assert_current(client);
  if (is(mode, "none") && client->has_cgame && !qa_qvm_cgame_get_supports_input_events(&client->cgame)) return;
  if (client->has_cgame) qa_qvm_cgame_event_handling(&client->cgame, mode);
}
static void shutdown_modules(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  if (client->has_cgame) qa_qvm_cgame_shutdown(&client->cgame);
  if (client->has_ui) qa_qvm_ui_shutdown(&client->ui);
}
void qa_app_application_qvm_client_shutdown(qa_app_application_qvm_client *client) {
  if (client->retired) return;
  qa_caught_error error;
  bool ok = qa_try(shutdown_modules, client, &error);
  qa_app_application_qvm_client_close(client);
  if (!ok) qa_rethrow(&error);
}

static void cleanup_observers(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  if (client->body_submissions != NULL) qa_qvm_body_submissions_close(client->body_submissions);
  if (client->body_submissions != NULL) qa_qvm_body_submissions_destroy(client->body_submissions);
  client->body_submissions = NULL;
  if (client->has_equipment_selector) qa_qvm_module_unbind(&client->cgame.module, client->equipment_selector);
  client->has_equipment_selector = false;
  if (client->has_view_observer) qa_qvm_module_unbind(&client->cgame.module, client->view_observer);
  client->has_view_observer = false;
  if (client->has_held_observer) qa_qvm_module_unbind(&client->cgame.module, client->held_observer);
  client->has_held_observer = false;
  size_t count = client->status_observer_count;
  client->status_observer_count = 0;
  for (size_t i = 0; i < count; i++) qa_qvm_module_unbind(&client->cgame.module, client->status_observers[i]);
  clear_held_weapons(client);
  client->equipment_view_visible = false;
  client->has_equipment = false;
}
static void cleanup_cgame(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  if (client->has_cgame) qa_qvm_cgame_retire(&client->cgame);
}
static void cleanup_ui(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  if (client->has_ui) qa_qvm_ui_retire(&client->ui);
}
static void cleanup_cgame_files(void *ctx) { qa_qvm_files_close_all(((qa_app_application_qvm_client *)ctx)->cgame_files); }
static void cleanup_ui_files(void *ctx) { qa_qvm_files_close_all(((qa_app_application_qvm_client *)ctx)->ui_files); }
static void cleanup_cgame_scripts(void *ctx) {
  qa_qvm_client_scripts_close_all(((qa_app_application_qvm_client *)ctx)->cgame_scripts);
}
static void cleanup_ui_scripts(void *ctx) { qa_qvm_client_scripts_close_all(((qa_app_application_qvm_client *)ctx)->ui_scripts); }
static void cleanup_globals(void *ctx) { qa_ui_script_global_defines_clear(((qa_app_application_qvm_client *)ctx)->globals); }
void qa_app_application_qvm_client_close(qa_app_application_qvm_client *client) {
  if (client->retired) return;
  client->retired = true;
  client->ready = false;
  void (*const cleanups[])(void *) = {cleanup_observers, cleanup_cgame, cleanup_ui, cleanup_cgame_files,
                                      cleanup_ui_files, cleanup_cgame_scripts, cleanup_ui_scripts, cleanup_globals};
  size_t failures = 0;
  qa_caught_error first;
  for (size_t i = 0; i < sizeof cleanups / sizeof cleanups[0]; i++) {
    qa_caught_error error;
    if (!qa_try(cleanups[i], client, &error) && failures++ == 0) first = error;
  }
  if (failures > 0) qa_raise("AggregateError", 0, (long long)failures, "QVM client cleanup failed: %s", first.message);
}
