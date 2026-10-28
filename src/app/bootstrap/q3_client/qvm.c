/* Port of src/app/bootstrap/q3-client/qvm.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/qvm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/status.h"
#include "qa/app/bootstrap/q3_client/view.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/render_record.h"
#include "qa/foundation/support.h"
typedef struct held_invocation {
  int64_t frame, end, gun;
  qa_app_qvm_held_weapon held;
} held_invocation;
struct qa_app_application_qvm_client {
  qa_app_application_qvm_client_options options;
  qa_app_qvm_client_commands ui_commands, cgame_commands;
  qa_command_origin ui_origin, cgame_origin;
  char ui_origin_name[16], cgame_origin_name[16];
  double generation;
  bool retired, ready;
  bool has_ui_artifact, has_cgame_artifact;
  qa_qvmy_artifact ui_artifact, cgame_artifact;
  qa_qvm_ui ui;
  bool ui_created;
  qa_qvm_cgame cgame;
  bool cgame_created;
  qa_app_cgame_status_view *status;
  bool has_equipment;
  qa_app_q3_equipment_presentation equipment;
  bool has_equipment_profile;
  held_invocation *held_invocations; size_t held_invocation_count, held_invocation_capacity;
  qa_app_qvm_held_weapon *held_weapons; size_t held_weapon_count, held_weapon_capacity;
  bool equipment_view_visible;
  bool equipment_hud_requested;
  bool equipment_selector_bound;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_app_qvm_client_commands qa_app_qvm_client_commands_make(qa_command_buffer *commands,
                                                           qa_command_context context, const char *role) {
  (void)role;
  qa_app_qvm_client_commands out;
  out.commands = commands;
  out.source = context;
  /* The script origin is installed by the client (name storage). */
  return out;
}
void qa_app_qvm_client_commands_execute_now(qa_app_qvm_client_commands commands, const char *text) {
  qa_command_buffer_execute_now(commands.commands, text, &commands.source, -1);
}
void qa_app_qvm_client_commands_insert(qa_app_qvm_client_commands commands, const char *text) {
  qa_command_buffer_insert(commands.commands, text, &commands.source, -1);
}
void qa_app_qvm_client_commands_append(qa_app_qvm_client_commands commands, const char *text) {
  qa_command_buffer_append(commands.commands, text, &commands.source, -1);
}
static void assert_current(qa_app_application_qvm_client *client) {
  client->options.session.assert_current(client->options.session.ctx);
  if (client->retired || client->options.connection.generation(client->options.connection.ctx) != client->generation)
    qa_range_error("QVM presentation belongs to a retired gamestate");
}
static void assert_current_void(void *ctx) { assert_current(ctx); }
static int32_t read_i32(const uint8_t *words, size_t offset) {
  int32_t v;
  memcpy(&v, words + offset, sizeof v);
  return v;
}
static qa_qvm_host_result host_call(void *ctx, const qa_qvm_host_call *call) {
  qa_app_application_qvm_client *client = ctx;
  assert_current(client);
  if (is(call->kind, "engine") && is(call->role, "cgame")
      && call->code == QA_QVM_CGAME_IMPORT_CG_R_ADDREFENTITYTOSCENE) {
    if (client->held_invocation_count > 0) {
      held_invocation *invocation = &client->held_invocations[client->held_invocation_count - 1];
      int64_t pointer = read_i32(call->base.words, 4);
      if (pointer >= invocation->frame && pointer + QA_QVM_REF_ENTITY_BYTES <= invocation->end) {
        if (pointer == invocation->gun) {
          const char *shader = NULL;
          qa_vec4 color = {0, 0, 0, 0};
          double shader_time = 0;
          client->options.host.read_gun_pass(client->options.host.ctx, call, (int32_t)pointer,
                                             &shader, &color, &shader_time);
          qa_app_qvm_held_weapon *held = &invocation->held;
          qa_app_qvm_held_weapon_pass *passes = realloc(held->passes, (held->pass_count + 1) * sizeof *passes);
          if (passes == NULL) qa_range_error("Out of memory");
          held->passes = passes;
          held->passes[held->pass_count++] = (qa_app_qvm_held_weapon_pass){shader, color, shader_time};
        }
        return 0;
      }
    }
  }
  if (client->options.host.body_suppress != NULL
      && client->options.host.body_suppress(client->options.host.ctx, call)) return 0;
  if (!is(call->role, "cgame") && !is(call->role, "ui")) qa_qvm_reject_syscall(call);
  if (is(call->role, "cgame")) {
    double result = 0;
    uint8_t *words = (uint8_t *)call->base.words;
    if (qa_app_cgame_status_view_syscall(client->status, words, call->base.words_length,
                                         call->guest, &result))
      return (qa_qvm_host_result)result;
  }
  qa_qvm_host_result out = 0;
  if (client->options.host.dispatch(client->options.host.ctx, call, &out)) return out;
  if (client->options.scalar(client->options.scalar_ctx, call, client, &out)) return out;
  qa_qvm_reject_syscall(call);
}
static qa_qvm_cgame_lifetime_state lifetime_current(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  qa_qvm_cgame_lifetime_state state = {
    client->options.connection.generation(client->options.connection.ctx),
    client->options.connection.server_message_sequence(client->options.connection.ctx), NULL};
  return state;
}
static void lifetime_begin_loading(void *ctx) { ((qa_app_application_qvm_client *)ctx)->ready = false; }
static void lifetime_prime(void *ctx, double generation) {
  (void)generation;
  ((qa_app_application_qvm_client *)ctx)->ready = true;
}
static bool status_visible_bridge(void *ctx) {
  qa_app_application_qvm_client *client = ctx;
  if (client->options.session.status_visible == NULL) return true;
  return client->options.session.status_visible(client->options.session.ctx) != false;
}
static qa_qvm_module_options module_options(qa_app_application_qvm_client *client, const char *role,
                                            qa_qvmy_artifact *artifact_out) {
  qa_qvmy_artifact artifact;
  bool retained = false;
  if (client->options.has_artifacts) {
    artifact = is(role, "ui") ? client->options.artifacts.ui : client->options.artifacts.cgame;
    retained = true;
  }
  if (retained) {
    if (!is(artifact.role, role)) qa_range_error("Retained presentation artifact has the wrong role");
  } else {
    artifact = client->options.host.load_artifact(client->options.host.ctx, role);
    assert_current(client);
  }
  if (is(role, "ui")) { client->ui_artifact = artifact; client->has_ui_artifact = true; artifact_out = &client->ui_artifact; }
  else { client->cgame_artifact = artifact; client->has_cgame_artifact = true; artifact_out = &client->cgame_artifact; }
  qa_qvm_module_options options;
  memset(&options, 0, sizeof options);
  options.artifact = artifact_out;
  options.host = host_call;
  options.host_ctx = client;
  return options;
}
qa_app_application_qvm_client *qa_app_application_qvm_client_create(qa_app_application_qvm_client_options options) {
  qa_app_application_qvm_client *client = calloc(1, sizeof *client);
  if (client == NULL) qa_range_error("Out of memory");
  client->options = options;
  client->status = qa_app_cgame_status_view_create(options.cvars, status_visible_bridge, client,
      (qa_app_cgame_status_syscall_host){options.host.ctx, options.host.status_syscall});
  for (int i = 0; i < 2; i++) {
    const char *role = i == 0 ? "ui" : "cgame";
    char *name = i == 0 ? client->ui_origin_name : client->cgame_origin_name;
    snprintf(name, 16, "q3-%s", role);
    qa_command_origin *origin = i == 0 ? &client->ui_origin : &client->cgame_origin;
    memset(origin, 0, sizeof *origin);
    origin->kind = QA_COMMAND_ORIGIN_SCRIPT;
    origin->name = name;
    origin->caller = &options.command_context.origin;
    qa_command_context source = options.command_context;
    source.origin = *origin;
    qa_app_qvm_client_commands made = qa_app_qvm_client_commands_make(options.commands, source, role);
    if (i == 0) client->ui_commands = made; else client->cgame_commands = made;
  }
  client->generation = options.connection.generation(options.connection.ctx);
  qa_qvmy_artifact *ui_artifact = NULL;
  qa_qvm_module_options ui_options = module_options(client, "ui", ui_artifact);
  (void)ui_artifact;
  qa_qvm_ui_create(&client->ui, options.seat, &ui_options, assert_current_void, client);
  client->ui_created = true;
  qa_qvm_ui_init_export(&client->ui, true);
  qa_qvmy_artifact *cgame_artifact = NULL;
  qa_qvm_module_options cgame_options = module_options(client, "cgame", cgame_artifact);
  (void)cgame_artifact;
  qa_qvm_cgame_lifetime lifetime = {client, assert_current_void, lifetime_current, lifetime_begin_loading, lifetime_prime};
  qa_qvm_cgame_init_module(&client->cgame, options.seat, &cgame_options, lifetime);
  client->cgame_created = true;
  if (options.host.install_profiles != NULL)
    client->has_equipment_profile = options.host.install_profiles(
        options.host.ctx, client, &client->cgame_artifact);
  qa_qvm_cgame_init(&client->cgame, options.session.server_message_sequence(options.session.ctx),
                    options.session.last_executed_server_command(options.session.ctx),
                    options.session.client_number(options.session.ctx));
  assert_current(client);
  return client;
}
const qa_app_application_qvm_client_options *qa_app_application_qvm_client_options_get(const qa_app_application_qvm_client *client) {
  return &client->options;
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
void qa_app_application_qvm_client_note_equipment_hud(qa_app_application_qvm_client *client) {
  client->equipment_hud_requested = true;
}
void qa_app_application_qvm_client_note_equipment_view_visible(qa_app_application_qvm_client *client) {
  client->equipment_view_visible = true;
}
bool qa_app_application_qvm_client_equipment_present(const qa_app_application_qvm_client *client) {
  return client->has_equipment;
}
static void held_weapon_free(qa_app_qvm_held_weapon *held) {
  qa_app_offset_q3_view_entity_free(held->parent);
  free(held->passes);
}
void qa_app_application_qvm_client_push_held_weapon(qa_app_application_qvm_client *client, qa_app_qvm_held_weapon held) {
  if (client->held_weapon_count == client->held_weapon_capacity) {
    size_t cap = client->held_weapon_capacity == 0 ? 4 : client->held_weapon_capacity * 2;
    qa_app_qvm_held_weapon *next = realloc(client->held_weapons, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    client->held_weapons = next; client->held_weapon_capacity = cap;
  }
  client->held_weapons[client->held_weapon_count++] = held;
}
void qa_app_application_qvm_client_begin_held_invocation(qa_app_application_qvm_client *client,
    int64_t frame, int64_t end, int64_t gun, qa_app_qvm_held_weapon held) {
  if (client->held_invocation_count == client->held_invocation_capacity) {
    size_t cap = client->held_invocation_capacity == 0 ? 4 : client->held_invocation_capacity * 2;
    held_invocation *next = realloc(client->held_invocations, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    client->held_invocations = next; client->held_invocation_capacity = cap;
  }
  client->held_invocations[client->held_invocation_count++] = (held_invocation){frame, end, gun, held};
}
void qa_app_application_qvm_client_end_held_invocation(qa_app_application_qvm_client *client) {
  if (client->held_invocation_count == 0)
    qa_range_error("Original weapon attachment scopes unwound out of order");
  held_invocation invocation = client->held_invocations[--client->held_invocation_count];
  if (invocation.held.pass_count != 0) qa_app_application_qvm_client_push_held_weapon(client, invocation.held);
  else held_weapon_free(&invocation.held);
}
qa_app_qvm_presentation_artifacts qa_app_application_qvm_client_presentation_artifacts(qa_app_application_qvm_client *client) {
  if (!client->has_ui_artifact || !client->has_cgame_artifact)
    qa_range_error("Presentation modules are not initialized");
  qa_app_qvm_presentation_artifacts out = {client->ui_artifact, client->cgame_artifact};
  return out;
}
void qa_app_application_qvm_client_update_screen(qa_app_application_qvm_client *client, const qa_qvm_host_call *call) {
  assert_current(client);
  if (is(call->role, "ui")) {
    /* TS call.invokeAsync(UI_DRAW_CONNECT_SCREEN): the guest call
     * itself drives the export through the module invocation in
     * the landed runtime; the connect-screen draw is the visible
     * effect available here. */
    qa_qvm_ui_draw_connect_screen(&client->ui, true);
  } else if (client->ui_created) {
    qa_qvm_ui_draw_connect_screen(&client->ui, true);
  }
}
void qa_app_application_qvm_client_draw(qa_app_application_qvm_client *client, double time, bool demo_playback) {
  assert_current(client);
  if (!client->ready || !client->cgame_created) qa_range_error("QVM cgame has not initialized");
  bool hide_bodies = client->options.body_overrides_active != NULL
    && client->options.body_overrides_active(client->options.body_ctx);
  bool capture_bodies = client->options.body_capture_active != NULL
    && client->options.body_capture_active(client->options.body_ctx);
  bool has_bodies = client->options.host.body_enable != NULL;
  if ((hide_bodies || capture_bodies) && !has_bodies)
    qa_range_error("This cgame needs an artifact-matched cgame-presentation.json declaration for body replacements");
  if (capture_bodies && (client->options.host.body_captures_meshes == NULL
      || !client->options.host.body_captures_meshes(client->options.host.ctx)))
    qa_range_error("This cgame needs artifact-matched player mesh call sites for component body materials");
  if (has_bodies) client->options.host.body_enable(client->options.host.ctx, hide_bodies || capture_bodies);
  if (client->options.view_weapon_visible != NULL
      && !client->options.view_weapon_visible(client->options.view_weapon_visible_ctx)
      && !client->has_equipment_profile)
    qa_range_error("Component camera control requires a qualified original view-weapon boundary");
  client->has_equipment = client->options.equipment_weapon != NULL
    && client->options.equipment_weapon(client->options.equipment_weapon_ctx, &client->equipment);
  if (client->has_equipment && !client->has_equipment_profile)
    qa_range_error("Selected weapon presentation requires a qualified original cgame HUD boundary");
  if (client->has_equipment && client->has_equipment_profile) client->equipment_selector_bound = true;
  else if (!client->has_equipment) client->equipment_selector_bound = false;
  client->equipment_hud_requested = false;
  client->equipment_view_visible = false;
  for (size_t i = 0; i < client->held_weapon_count; i++) held_weapon_free(&client->held_weapons[i]);
  client->held_weapon_count = 0;
  qa_qvm_cgame_draw_active_frame(&client->cgame, time, "center", demo_playback);
  if (((int64_t)client->options.key_catcher(client->options.key_catcher_ctx) & 2) != 0 && client->ui_created)
    qa_qvm_ui_refresh(&client->ui, (int32_t)trunc(client->options.now(client->options.now_ctx)));
}
void qa_app_application_qvm_client_refresh_status(qa_app_application_qvm_client *client) {
  if (!client->retired) {
    assert_current(client);
    qa_app_cgame_status_view_refresh(client->status);
  }
}
bool qa_app_application_qvm_client_command(qa_app_application_qvm_client *client, const char *const *argv, size_t argc) {
  assert_current(client);
  if (client->cgame_created && qa_qvm_cgame_console_command(&client->cgame, argv, argc)) return true;
  if (!client->ui_created) return false;
  return qa_qvm_ui_console_command(&client->ui, (int32_t)trunc(client->options.now(client->options.now_ctx)), argv, argc);
}
void qa_app_application_qvm_client_key_event(qa_app_application_qvm_client *client, double key, bool down) {
  assert_current(client);
  if (((int64_t)client->options.key_catcher(client->options.key_catcher_ctx) & 2) != 0) {
    if (client->ui_created) qa_qvm_ui_key_event(&client->ui, (int32_t)key, down);
  } else if (client->cgame_created && qa_qvm_cgame_get_supports_input_events(&client->cgame)) {
    qa_qvm_cgame_key_event(&client->cgame, key, down);
  }
}
void qa_app_application_qvm_client_mouse_event(qa_app_application_qvm_client *client, double x, double y) {
  assert_current(client);
  if (((int64_t)client->options.key_catcher(client->options.key_catcher_ctx) & 2) != 0) {
    if (client->ui_created) qa_qvm_ui_mouse_event(&client->ui, (int32_t)x, (int32_t)y);
  } else if (client->cgame_created && qa_qvm_cgame_get_supports_input_events(&client->cgame)) {
    qa_qvm_cgame_mouse_event(&client->cgame, x, y);
  }
}
bool qa_app_application_qvm_client_captures_input(const qa_app_application_qvm_client *client) {
  double catcher = client->options.key_catcher(client->options.key_catcher_ctx);
  if (client->cgame_created && qa_qvm_cgame_get_supports_input_events(&client->cgame)) return catcher != 0;
  return ((int64_t)catcher & 2) != 0;
}
void qa_app_application_qvm_client_event_handling(qa_app_application_qvm_client *client, qa_q3_cgame_event_handling mode) {
  assert_current(client);
  if (is(mode, "none") && client->cgame_created && !qa_qvm_cgame_get_supports_input_events(&client->cgame)) return;
  if (client->cgame_created) qa_qvm_cgame_event_handling(&client->cgame, mode);
}
void qa_app_application_qvm_client_shutdown(qa_app_application_qvm_client *client) {
  if (client->retired) return;
  if (client->cgame_created) qa_qvm_cgame_shutdown(&client->cgame);
  if (client->ui_created) qa_qvm_ui_shutdown(&client->ui);
  qa_app_application_qvm_client_close(client);
}
void qa_app_application_qvm_client_close(qa_app_application_qvm_client *client) {
  if (client->retired) return;
  client->retired = true;
  client->ready = false;
  if (client->options.host.body_close != NULL) client->options.host.body_close(client->options.host.ctx);
  client->equipment_selector_bound = false;
  if (client->options.host.release_profiles != NULL) client->options.host.release_profiles(client->options.host.ctx);
  for (size_t i = 0; i < client->held_weapon_count; i++) held_weapon_free(&client->held_weapons[i]);
  client->held_weapon_count = 0;
  client->equipment_view_visible = false;
  client->has_equipment = false;
  if (client->cgame_created) qa_qvm_cgame_retire(&client->cgame);
  if (client->ui_created) qa_qvm_ui_retire(&client->ui);
}
void qa_app_application_qvm_client_destroy(qa_app_application_qvm_client *client) {
  if (client == NULL) return;
  qa_app_application_qvm_client_close(client);
  if (client->cgame_created) qa_qvm_cgame_destroy(&client->cgame);
  if (client->ui_created) qa_qvm_ui_destroy(&client->ui);
  qa_app_cgame_status_view_destroy(client->status);
  for (size_t i = 0; i < client->held_invocation_count; i++) held_weapon_free(&client->held_invocations[i].held);
  free(client->held_invocations);
  free(client->held_weapons);
  free(client);
}
