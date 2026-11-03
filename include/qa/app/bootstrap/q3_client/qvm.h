/* Port of src/app/bootstrap/q3-client/qvm.ts. The guest modules are the
 * compat QvmUi / QvmCgame; host() runs the TS syscall chain (status, common,
 * files, scripts, render, audio, client state, collision, marks,
 * cinematics, browser, UI keys, scalar) over the real services. Async flows
 * are synchronous (docs/PORTING.md). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_QVM_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/app/bootstrap/q3_client/equipment.h"
#include "qa/app/bootstrap/q3_client/services.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/cgame.h"
#include "qa/compat/qvm/client_state.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/compat/qvm/ui.h"
#include "qa/content/q3/presentation/client.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/ui.h"
#include "qa/core/commands/index.h"
#include "qa/network/q3/browser_view.h"
#include "qa/world/collision/index.h"
typedef struct qa_app_application_qvm_client qa_app_application_qvm_client;
/* TS QvmHeldWeapon pass. shader NULL is TS null; it borrows the resource
 * owner's shader name. */
typedef struct qa_app_qvm_held_weapon_pass {
  const char *shader;
  qa_vec4 color;
  double shader_time;
} qa_app_qvm_held_weapon_pass;
/* TS QvmHeldWeapon. parent is the TS SceneEntity & { actor }: an owned
 * entity whose actor points at parent_actor's storage inside the same
 * allocation (free with qa_app_offset_q3_view_entity_free's plain free). */
typedef struct qa_app_qvm_held_weapon {
  qa_content_id content;
  qa_actor_id parent_actor;
  qa_scene_entity *parent;
  qa_app_qvm_held_weapon_pass *passes;
  size_t pass_count;
} qa_app_qvm_held_weapon;
/* TS QvmPresentationArtifacts: the resolved ui and cgame artifacts, shared
 * by reference between a client and the reopen closure that retains them.
 * Retain/release count the holders; the last release destroys them. */
typedef struct qa_app_qvm_presentation_artifacts qa_app_qvm_presentation_artifacts;
const qa_resolved_qvm_artifact *qa_app_qvm_presentation_artifacts_ui(const qa_app_qvm_presentation_artifacts *artifacts);
const qa_resolved_qvm_artifact *qa_app_qvm_presentation_artifacts_cgame(const qa_app_qvm_presentation_artifacts *artifacts);
qa_app_qvm_presentation_artifacts *qa_app_qvm_presentation_artifacts_retain(qa_app_qvm_presentation_artifacts *artifacts);
void qa_app_qvm_presentation_artifacts_release(qa_app_qvm_presentation_artifacts *artifacts);
/* TS Pick<CommandBuffer, "executeNow" | "insert" | "append">. */
typedef struct qa_app_qvm_command_target {
  void *ctx;
  double (*execute_now)(void *ctx, const char *text, const qa_command_context *source);
  void (*insert)(void *ctx, const char *text, const qa_command_context *source);
  void (*append)(void *ctx, const char *text, const qa_command_context *source);
} qa_app_qvm_command_target;
/* A CommandBuffer as the Pick (ctx = the buffer). */
qa_app_qvm_command_target qa_app_qvm_command_buffer_target(qa_command_buffer *buffer);
/* TS qvmClientCommands(commands, context, role): the source context is
 * { session, origin: { kind: "script", name: `q3-${role}`, caller:
 * context.origin } }. The record owns a deep copy of context. */
typedef struct qa_app_qvm_client_commands qa_app_qvm_client_commands;
qa_app_qvm_client_commands *qa_app_qvm_client_commands_create(qa_app_qvm_command_target commands,
    const qa_command_context *context, const char *role);
void qa_app_qvm_client_commands_destroy(qa_app_qvm_client_commands *commands);
const qa_command_context *qa_app_qvm_client_commands_source(const qa_app_qvm_client_commands *commands);
void qa_app_qvm_client_commands_execute_now(qa_app_qvm_client_commands *commands, const char *text);
void qa_app_qvm_client_commands_insert(qa_app_qvm_client_commands *commands, const char *text);
void qa_app_qvm_client_commands_append(qa_app_qvm_client_commands *commands, const char *text);
/* TS bodyCapture / bodyOverrides. source is the resolved RefModelEntity. */
typedef struct qa_app_qvm_body_capture {
  void *ctx;
  bool (*active)(void *ctx);
  bool (*selected)(void *ctx, double entity);
  bool (*submit)(void *ctx, double entity, qa_qvm_body_part part, const qa_q3_pres_ref_model_entity *source, bool base);
} qa_app_qvm_body_capture;
typedef struct qa_app_qvm_body_overrides {
  void *ctx;
  bool (*active)(void *ctx);
  bool (*hidden)(void *ctx, double entity);
} qa_app_qvm_body_overrides;
/* TS ApplicationQvmClientOptions. All function members share ctx; optional
 * members NULL are TS undefined. session, services, media, assets, queries,
 * browser and keys are borrowed and must outlive the client; artifacts is
 * retained (TS optional; NULL loads vm/<role>.qvm from the media mounts). */
typedef struct qa_app_application_qvm_client_options {
  qa_app_qvm_presentation_artifacts *artifacts;
  qa_seat_id seat;
  qa_command_context command_context;
  qa_app_application_q3_services *services;
  qa_app_application_q3_assets *media;
  qa_app_application_assets *assets; /* TS media.assets */
  const qa_q3_pres_presentation_session *session;
  qa_q3_client_state connection;
  qa_shared_scene_queries *queries;
  qa_app_qvm_command_target commands;
  qa_app_qvm_cvar_services cvars;
  qa_q3_browser_view *browser;
  qa_app_key_profile *keys;
  const char *map;
  const qa_app_qvm_body_capture *body_capture;     /* NULL = undefined */
  const qa_app_qvm_body_overrides *body_overrides; /* NULL = undefined */
  void *ctx;
  double (*now)(void *ctx);
  double (*key_catcher)(void *ctx);
  /* equipmentWeapon?(): false is TS null. */
  bool (*equipment_weapon)(void *ctx, qa_app_q3_equipment_presentation *out);
  bool (*view_weapon_visible)(void *ctx);
  /* heldWeaponActor?(entity): false is TS null. */
  bool (*held_weapon_actor)(void *ctx, double entity, qa_actor_id *out);
  void (*remove_command)(void *ctx, const char *name);
  /* scalar(call, owner): false is TS null. */
  bool (*scalar)(void *ctx, const qa_qvm_host_call *call, qa_app_application_qvm_client *owner, qa_qvm_host_result *out);
} qa_app_application_qvm_client_options;
/* TS ApplicationQvmClient.create. Raises the TS errors after closing the
 * partial client (the C object is then destroyed). */
qa_app_application_qvm_client *qa_app_application_qvm_client_create(const qa_app_application_qvm_client_options *options);
void qa_app_application_qvm_client_destroy(qa_app_application_qvm_client *client);
const qa_app_qvm_held_weapon *qa_app_application_qvm_client_shared_held_weapons(
    const qa_app_application_qvm_client *client, size_t *out_count);
bool qa_app_application_qvm_client_shared_equipment_hud(const qa_app_application_qvm_client *client);
/* TS sharedEquipmentViewVisible: false return is TS null. */
bool qa_app_application_qvm_client_shared_equipment_view_visible(const qa_app_application_qvm_client *client, bool *out);
/* TS presentationArtifacts(): a retained reference (release it). */
qa_app_qvm_presentation_artifacts *qa_app_application_qvm_client_presentation_artifacts(qa_app_application_qvm_client *client);
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
#endif
