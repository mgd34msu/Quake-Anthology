/* Port of src/app/bootstrap/q3-client/qvm.ts. The guest modules
 * are the landed qa_qvm_ui / qa_qvm_cgame; the per-syscall landed
 * service chain (files/scripts/render/audio/state/collision/
 * marks/cinematic/browser/keys) is composed by the caller behind
 * options.dispatch, while this file ports the dispatch order,
 * held-weapon interception, lifecycle guards, equipment/HUD
 * bookkeeping and input routing. Equipment/body invocation
 * observers install through the host (landed bind_invocation
 * rides the concrete module). Async flows are synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/app/bootstrap/q3_client/equipment.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/cgame.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/compat/qvm/ui.h"
#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
typedef struct qa_app_application_qvm_client qa_app_application_qvm_client;
/* TS QvmHeldWeapon pass. */
typedef struct qa_app_qvm_held_weapon_pass {
  const char *shader; /* NULL = TS null */
  qa_vec4 color;
  double shader_time;
} qa_app_qvm_held_weapon_pass;
/* TS QvmHeldWeapon. parent_actor is the TS parent.actor. */
typedef struct qa_app_qvm_held_weapon {
  qa_content_id content;
  qa_actor_id parent_actor;
  qa_scene_entity *parent; /* owned copy */
  qa_app_qvm_held_weapon_pass *passes;
  size_t pass_count;
} qa_app_qvm_held_weapon;
/* TS QvmPresentationArtifacts. */
typedef struct qa_app_qvm_presentation_artifacts {
  qa_qvmy_artifact ui;
  qa_qvmy_artifact cgame;
} qa_app_qvm_presentation_artifacts;
/* TS session surface read here (Q3PresentationSession mirror). */
typedef struct qa_app_qvm_session {
  void *ctx;
  void (*assert_current)(void *ctx);
  void (*print)(void *ctx, const char *text);
  bool (*status_visible)(void *ctx); /* NULL fn = TS undefined */
  double (*server_message_sequence)(void *ctx);
  double (*last_executed_server_command)(void *ctx);
  double (*client_number)(void *ctx);
} qa_app_qvm_session;
/* TS connection surface read here (Q3ClientState mirror). */
typedef struct qa_app_qvm_connection {
  void *ctx;
  double (*generation)(void *ctx);
  double (*server_message_sequence)(void *ctx);
} qa_app_qvm_connection;
/* Host: landed syscall chain + equipment/body observers + module
 * load/profile reads (compat owners). */
typedef struct qa_app_qvm_client_host {
  void *ctx;
  /* host() chain after status: false = unhandled (try scalar). */
  bool (*dispatch)(void *ctx, const qa_qvm_host_call *call, qa_qvm_host_result *out);
  /* bodySubmissions?.suppress(call); NULL fn = no submissions. */
  bool (*body_suppress)(void *ctx, const qa_qvm_host_call *call);
  /* module(role) artifact resolution when options.artifacts absent:
   * fills the artifact for vm/<role>.qvm (aborts on TS errors). */
  qa_qvmy_artifact (*load_artifact)(void *ctx, const char *role);
  /* Equipment/body profile installation; returns whether an
   * equipment profile exists, and installs observers that call
   * the notify callbacks on the client. NULL = no profiles. */
  bool (*install_profiles)(void *ctx, qa_app_application_qvm_client *client, const qa_qvmy_artifact *cgame);
  void (*release_profiles)(void *ctx);
  bool (*body_captures_meshes)(void *ctx);
  void (*body_enable)(void *ctx, bool enabled);
  void (*body_close)(void *ctx);
  /* CgameStatusView syscall dispatcher (status.ts host): the
   * landed cvar syscall over the status services. */
  bool (*status_syscall)(void *ctx, const char *role, uint8_t *words, size_t words_length,
                         qa_qvm_memory *memory, qa_app_qvm_cvar_services cvars, double *out_result);
  /* readQvmRefEntity for the held-weapon gun record: fills shader
   * name (NULL ok), color, shaderTime. */
  void (*read_gun_pass)(void *ctx, const qa_qvm_host_call *call, int32_t pointer,
                        const char **out_shader, qa_vec4 *out_color, double *out_shader_time);
} qa_app_qvm_client_host;
/* TS ApplicationQvmClientOptions. Optional fns NULL = TS undefined. */
typedef struct qa_app_application_qvm_client_options {
  bool has_artifacts;
  qa_app_qvm_presentation_artifacts artifacts;
  qa_seat_id seat;
  qa_command_context command_context;
  qa_app_qvm_session session;
  qa_app_qvm_connection connection;
  qa_command_buffer *commands;
  qa_app_qvm_cvar_services cvars;
  qa_app_application_q3_assets *media;
  const char *map;
  double (*now)(void *ctx); void *now_ctx;
  double (*key_catcher)(void *ctx); void *key_catcher_ctx;
  bool (*equipment_weapon)(void *ctx, qa_app_q3_equipment_presentation *out);
  void *equipment_weapon_ctx;
  bool (*view_weapon_visible)(void *ctx); void *view_weapon_visible_ctx; /* NULL fn = undefined */
  void (*remove_command)(void *ctx, const char *name); void *remove_command_ctx;
  /* scalar(call, owner). */
  bool (*scalar)(void *ctx, const qa_qvm_host_call *call, qa_app_application_qvm_client *owner,
                 qa_qvm_host_result *out);
  void *scalar_ctx;
  /* bodyCapture/bodyOverrides active probes. */
  bool (*body_capture_active)(void *ctx); void *body_ctx;
  bool (*body_overrides_active)(void *ctx);
  qa_app_qvm_client_host host;
} qa_app_application_qvm_client_options;
/* TS qvmClientCommands result. */
typedef struct qa_app_qvm_client_commands {
  qa_command_buffer *commands;
  qa_command_context source;
} qa_app_qvm_client_commands;
qa_app_qvm_client_commands qa_app_qvm_client_commands_make(qa_command_buffer *commands,
                                                           qa_command_context context, const char *role);
void qa_app_qvm_client_commands_execute_now(qa_app_qvm_client_commands commands, const char *text);
void qa_app_qvm_client_commands_insert(qa_app_qvm_client_commands commands, const char *text);
void qa_app_qvm_client_commands_append(qa_app_qvm_client_commands commands, const char *text);
/* TS ApplicationQvmClient.create (synchronous). */
qa_app_application_qvm_client *qa_app_application_qvm_client_create(qa_app_application_qvm_client_options options);
const qa_app_application_qvm_client_options *qa_app_application_qvm_client_options_get(const qa_app_application_qvm_client *client);
const qa_app_qvm_held_weapon *qa_app_application_qvm_client_shared_held_weapons(
    const qa_app_application_qvm_client *client, size_t *out_count);
bool qa_app_application_qvm_client_shared_equipment_hud(const qa_app_application_qvm_client *client);
/* TS sharedEquipmentViewVisible: false return + *out unset is null. */
bool qa_app_application_qvm_client_shared_equipment_view_visible(const qa_app_application_qvm_client *client, bool *out);
/* Equipment observer notifications (host profile callbacks). */
void qa_app_application_qvm_client_note_equipment_hud(qa_app_application_qvm_client *client);
void qa_app_application_qvm_client_note_equipment_view_visible(qa_app_application_qvm_client *client);
bool qa_app_application_qvm_client_equipment_present(const qa_app_application_qvm_client *client);
void qa_app_application_qvm_client_push_held_weapon(qa_app_application_qvm_client *client, qa_app_qvm_held_weapon held);
/* Held-weapon invocation scope (profile host): begin records the
 * frame range + gun pointer the host() interception matches. */
void qa_app_application_qvm_client_begin_held_invocation(qa_app_application_qvm_client *client,
    int64_t frame, int64_t end, int64_t gun, qa_app_qvm_held_weapon held);
void qa_app_application_qvm_client_end_held_invocation(qa_app_application_qvm_client *client);
qa_app_qvm_presentation_artifacts qa_app_application_qvm_client_presentation_artifacts(qa_app_application_qvm_client *client);
void qa_app_application_qvm_client_update_screen(qa_app_application_qvm_client *client, const qa_qvm_host_call *call);
void qa_app_application_qvm_client_draw(qa_app_application_qvm_client *client, double time, bool demo_playback);
void qa_app_application_qvm_client_refresh_status(qa_app_application_qvm_client *client);
bool qa_app_application_qvm_client_command(qa_app_application_qvm_client *client, const char *const *argv, size_t argc);
void qa_app_application_qvm_client_key_event(qa_app_application_qvm_client *client, double key, bool down);
void qa_app_application_qvm_client_mouse_event(qa_app_application_qvm_client *client, double x, double y);
bool qa_app_application_qvm_client_captures_input(const qa_app_application_qvm_client *client);
void qa_app_application_qvm_client_event_handling(qa_app_application_qvm_client *client, qa_q3_cgame_event_handling mode);
void qa_app_application_qvm_client_shutdown(qa_app_application_qvm_client *client);
void qa_app_application_qvm_client_close(qa_app_application_qvm_client *client);
void qa_app_application_qvm_client_destroy(qa_app_application_qvm_client *client);
#endif
