/* Port of src/app/bootstrap/q3-client.ts (ApplicationQ3Client): a seat
 * owns cgame state (the TS Q3ClientPresentation or the original QVM through
 * ApplicationQvmClient); source authority, resource mounts and audio output
 * remain shared. Async flows are synchronous (docs/PORTING.md).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/component_bodies.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/mod_presentation.h"
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include "qa/app/bootstrap/q3_client/equipment.h"
#include "qa/app/bootstrap/q3_client/qvm.h"
#include "qa/app/bootstrap/q3_client/qvm_scalars.h"
#include "qa/app/bootstrap/q3_client/scene.h"
#include "qa/app/bootstrap/q3_client/source.h"
#include "qa/app/bootstrap/render_settings.h"
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/compat/qvm/client_state.h"
#include "qa/content/q3/presentation/client.h"
#include "qa/content/q3/presentation/movement_host.h"
#include "qa/content/q3/presentation/player_state.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/content/q3/presentation/snapshots.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/session.h"
#include "qa/contracts/ui.h"
#include "qa/core/cvars/index.h"
#include "qa/input/client_commands.h"
#include "qa/network/q3/browser_view.h"
#include "qa/network/q3/state/player.h"
#include "qa/world/collision/index.h"

/* TS ApplicationQ3ClientSource (extends SnapshotSource). Every function
 * shares ctx (commands and snapshots carry their own). Optional members
 * NULL are TS undefined. network/remote_q3.c fills it for a remote seat. */
typedef struct qa_app_q3_client_source {
  void *ctx;
  /* getGameState(): borrowed strings valid until the next call. */
  size_t (*game_state)(void *ctx, const char *const **strings);
  /* getServerCommand(sequence): false is TS null; borrowed argv. */
  bool (*server_command)(void *ctx, double sequence, const char *const **strings, size_t *out_count);
  qa_actor_id (*actor_at)(void *ctx, double number);
  /* sourceMode: true is "demo", false "live". */
  bool (*demo)(void *ctx);
  qa_q3_pres_command_source commands;
  qa_q3_pres_snapshot_source snapshots;
  double (*client_number)(void *ctx);
  double (*time)(void *ctx);
  double (*server_message_sequence)(void *ctx);      /* optional */
  double (*last_executed_server_command)(void *ctx); /* optional */
  /* systemInfo?(): borrowed text valid until the next call. */
  const char *(*system_info)(void *ctx);
  /* soundActor?(number): false is TS null. */
  bool (*sound_actor)(void *ctx, double number, qa_actor_id *out);
  /* snapshotPing?(number): false is TS null. */
  bool (*snapshot_ping)(void *ctx, double number, double *out);
} qa_app_q3_client_source;

/* TS Pick<Q3ClientPresentationOptions, "character" | "event" | "predictItem"
 * | "viewWeapon" | "playerWeapon">: the content client option signatures. */
typedef struct qa_app_q3_client_hooks {
  void *ctx;
  void (*character)(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_client_source_fn render, void *render_ctx);
  void (*event)(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position, qa_q3_pres_client_source_fn render,
                void *render_ctx);
  void (*predict_item)(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_client_source_fn predict,
                       void *predict_ctx);
  void (*view_weapon)(void *ctx, qa_q3_base_source_player_state *state, qa_q3_pres_client_source_fn render,
                      void *render_ctx);
  void (*player_weapon)(void *ctx, const qa_q3_pres_ref_model_entity *parent, qa_q3_base_source_player_state *state,
                        qa_q3_pres_client_entity *entity, qa_q3_base_team team, qa_q3_pres_client_source_fn render,
                        void *render_ctx);
} qa_app_q3_client_hooks;

typedef enum qa_app_q3_client_kind {
  QA_APP_Q3_CLIENT_LOCAL,  /* TS kind "local" or omitted */
  QA_APP_Q3_CLIENT_REMOTE, /* "remote" */
  QA_APP_Q3_CLIENT_QVM     /* "qvm" */
} qa_app_q3_client_kind;

/* TS ApplicationQ3ClientOptions. One ctx is shared by every function
 * member; optional members NULL are TS undefined. Borrowed pointers must
 * outlive the client. The kind selects which trailing group is read. */
typedef struct qa_app_application_q3_client_options {
  qa_app_q3_client_kind kind;
  void *ctx;
  /* Common. */
  const qa_app_system_cinematic_host *system_cinematics;
  /* remapShader?(...): returns after the remap (TS Promise<void>). */
  void (*remap_shader)(void *ctx, const char *original, const char *replacement, const char *offset, bool initializing,
                       bool (*current)(void *current_ctx), void *current_ctx);
  bool (*save_font_data)(void *ctx);
  qa_app_q3_renderer_settings (*render_settings)(void *ctx);
  qa_cvar_registry *cvars;      /* NULL = undefined */
  qa_cvar_registry *time_cvars; /* NULL = undefined */
  qa_q3_pres_weapon_hud_reading (*weapon_hud)(void *ctx);
  void (*assert_current)(void *ctx);
  qa_app_application_assets *assets;
  qa_app_qvm_display_renderer renderer; /* Pick<NativeRenderer, "backend" | "driver" | "glConfig"> */
  qa_shared_scene_queries *queries;
  qa_app_local_input *local;
  qa_app_application_audio *audio;
  const qa_cvar_snapshot *settings; size_t setting_count; bool has_settings;
  bool (*split_screen)(void *ctx); /* TS splitScreen?: a getter in TS callers */
  /* serverSettings?(): malloc'd snapshots (caller of the fn frees with
   * qa_cvar_read_free each + free). */
  qa_cvar_snapshot *(*server_settings)(void *ctx, size_t *out_count);
  qa_rect (*viewport)(void *ctx);
  /* The viewport closure's own context (QvmVideoReopenOptions replaces the
   * viewport closure alone); NULL calls viewport with ctx. viewport_release
   * (NULL = borrowed) frees viewport_ctx when the client is destroyed. */
  void *viewport_ctx;
  void (*viewport_release)(void *viewport_ctx);
  double (*now)(void *ctx);
  /* commands: { reliable, console, print }. */
  void (*reliable)(void *ctx, const char *text);
  void (*console)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  /* commandRegistration: the ClientCommandRegistration owner. */
  qa_client_command_bindings *command_bindings;
  qa_client_command_owner *command_registration;
  const qa_app_q3_client_hooks *hooks; /* NULL = undefined */
  /* "local" and "remote". */
  qa_q3_pres_movement_host movement;
  /* "local". */
  const qa_app_q3_source_presentation_state *initial;
  bool (*link_bounds)(void *ctx, double number, qa_bounds *out);   /* false = null */
  bool (*source_actor)(void *ctx, double number, qa_actor_id *out); /* false = null */
  qa_q3_base_user_command (*prediction_command)(void *ctx, qa_actor_command command, double source_time_ms);
  /* "remote" and "qvm". */
  const qa_app_q3_client_source *source;
  /* "remote". */
  const qa_q3_player_state_record *initial_player;
  /* "qvm". */
  bool local_server;
  qa_q3_client_state connection;
  bool (*equipment_weapon)(void *ctx, qa_app_q3_equipment_presentation *out); /* false = null */
  qa_q3_browser_view *browser;
  qa_app_key_profile *keys;
  qa_app_qvm_cvar_services guest_cvars;
  qa_app_qvm_client_input guest_input;
  qa_app_qvm_command_target command_buffer;
  qa_app_qvm_client_state_info (*client_state)(void *ctx);
} qa_app_application_q3_client_options;

/* TS ApplicationQ3LocalRound. */
typedef struct qa_app_q3_local_round {
  qa_actor_id actor;
  const qa_app_q3_source_presentation_state *initial;
  qa_q3_pres_movement_host movement;
  void *ctx;
  bool (*link_bounds)(void *ctx, double number, qa_bounds *out);
  bool (*source_actor)(void *ctx, double number, qa_actor_id *out);
  qa_q3_base_user_command (*prediction_command)(void *ctx, qa_actor_command command, double source_time_ms);
  qa_cvar_snapshot *(*server_settings)(void *ctx, size_t *out_count);
} qa_app_q3_local_round;

typedef struct qa_app_application_q3_client qa_app_application_q3_client;

/* TS ApplicationQ3Client.create(options, artifacts?): artifacts NULL is TS
 * undefined (retained otherwise). Raises the TS errors after closing the
 * partial client. */
qa_app_application_q3_client *qa_app_application_q3_client_create(const qa_app_application_q3_client_options *options,
    qa_app_qvm_presentation_artifacts *artifacts);
/* Frees a closed client (closes it first). */
void qa_app_application_q3_client_destroy(qa_app_application_q3_client *client);
const qa_app_application_q3_client_options *qa_app_application_q3_client_options_of(const qa_app_application_q3_client *client);
/* TS options.commands.reliable(text): startTeamArenaSkirmish and
 * requestRestoredScores send through it. */
void qa_app_application_q3_client_reliable(qa_app_application_q3_client *client, const char *text);
const qa_app_q3_client_source *qa_app_application_q3_client_source(qa_app_application_q3_client *client);
qa_app_application_q3_assets *qa_app_application_q3_client_media(qa_app_application_q3_client *client);
qa_cvar_registry *qa_app_application_q3_client_cvars(const qa_app_application_q3_client *client);
qa_cvar_registry *qa_app_application_q3_client_time_cvars(const qa_app_application_q3_client *client);
void qa_app_application_q3_client_adopt_cvars(qa_app_application_q3_client *client, qa_cvar_registry *cvars);
/* TS commandNames: true when a cgame command of that exact name is held. */
bool qa_app_application_q3_client_has_command_name(const qa_app_application_q3_client *client, const char *name);
const qa_app_prepared_primary_body *qa_app_application_q3_client_shared_bodies(const qa_app_application_q3_client *client,
                                                                              size_t *out_count);
void qa_app_application_q3_client_assert_can_restart_round(qa_app_application_q3_client *client);
/* bit: TS 0 | 4. */
void qa_app_application_q3_client_begin_round_restart(qa_app_application_q3_client *client, double bit,
                                                      const qa_app_q3_source_presentation_state *source);
void qa_app_application_q3_client_rebind_round(qa_app_application_q3_client *client, const qa_app_q3_local_round *binding);
void qa_app_application_q3_client_refresh_system_info(qa_app_application_q3_client *client);
/* TS captureVideoReopen(): the returned closure state; reopen with
 * qa_app_q3_video_reopen_open after the client shut down (and before it is
 * destroyed: a cvar registry the captured client created moves to the
 * reopened client); free with qa_app_q3_video_reopen_destroy. */
typedef struct qa_app_q3_video_reopen qa_app_q3_video_reopen;
/* TS QvmVideoReopenOptions = Pick<..., "renderer" | "viewport" | "commandRegistration">.
 * ctx is the viewport closure's context (it becomes viewport_ctx); release
 * (NULL = borrowed) becomes viewport_release, owned by the reopened client
 * from the call on. */
typedef struct qa_app_qvm_video_reopen_options {
  qa_app_qvm_display_renderer renderer;
  void *ctx;
  qa_rect (*viewport)(void *ctx);
  void (*release)(void *ctx);
  qa_client_command_bindings *command_bindings;
  qa_client_command_owner *command_registration;
} qa_app_qvm_video_reopen_options;
qa_app_q3_video_reopen *qa_app_application_q3_client_capture_video_reopen(qa_app_application_q3_client *client);
qa_app_application_q3_client *qa_app_q3_video_reopen_open(qa_app_q3_video_reopen *reopen,
                                                          const qa_app_qvm_video_reopen_options *overrides);
void qa_app_q3_video_reopen_destroy(qa_app_q3_video_reopen *reopen);
/* TS resourceDiagnostics(): the services' renderer resources, whose
 * registered models and skins are the TS { models, skins }. */
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_client_resource_diagnostics(qa_app_application_q3_client *client);
/* TS cgame (typescript backend only). */
qa_q3_pres_client_presentation *qa_app_application_q3_client_cgame(qa_app_application_q3_client *client);
/* TS consumeWeaponSelection(): false is TS null. */
bool qa_app_application_q3_client_consume_weapon_selection(qa_app_application_q3_client *client, double *out);
qa_app_q3_command_selection qa_app_application_q3_client_user_command_selection(qa_app_application_q3_client *client);
double qa_app_application_q3_client_presented_events(const qa_app_application_q3_client *client);
const qa_app_qvm_held_weapon *qa_app_application_q3_client_shared_held_weapons(qa_app_application_q3_client *client,
                                                                             size_t *out_count);
/* TS sharedEquipmentViewVisible: false return is TS null. */
bool qa_app_application_q3_client_shared_equipment_view_visible(qa_app_application_q3_client *client, bool *out);
void qa_app_application_q3_client_weapon_hud_view(qa_app_application_q3_client *client, bool *visible,
                                                  bool *aggregate_warning);
qa_scene_camera qa_app_application_q3_client_camera(const qa_app_application_q3_client *client);
/* TS bodyPose(actor): false is TS null. */
bool qa_app_application_q3_client_body_pose(const qa_app_application_q3_client *client, qa_actor_id actor,
                                            qa_vec3 *origin_out, qa_vec3 *angles_out);
qa_scene_camera qa_app_application_q3_client_supplemental_weapon_camera(qa_app_application_q3_client *client,
                                                                        qa_scene_camera camera);
void qa_app_application_q3_client_receive_events(qa_app_application_q3_client *client,
    const qa_app_sim_presentation_event *events, size_t count);
void qa_app_application_q3_client_receive(qa_app_application_q3_client *client, const qa_app_q3_source_presentation_state *state,
    const qa_app_sim_presentation_event *events, size_t event_count, const qa_actor_command *commands, size_t command_count);
/* TS prepare(frameNumber, viewport = options.viewport(), presentations = [],
 * statusVisible = true, bodies = [], viewWeaponVisible = true,
 * cameraControlled = false). viewport NULL takes the default. */
void qa_app_application_q3_client_prepare(qa_app_application_q3_client *client, double frame_number, const qa_rect *viewport,
    const qa_app_simulation_presentation *presentations, size_t presentation_count, bool status_visible,
    const qa_app_component_body *bodies, size_t body_count, bool view_weapon_visible, bool camera_controlled);
bool qa_app_application_q3_client_command(qa_app_application_q3_client *client, const char *const *argv, size_t argc);
bool qa_app_application_q3_client_handles_command(qa_app_application_q3_client *client, const char *name);
void qa_app_application_q3_client_key_event(qa_app_application_q3_client *client, double key, bool down);
void qa_app_application_q3_client_mouse_event(qa_app_application_q3_client *client, double x, double y);
/* Raises RangeError outside the integral modes 0..3. */
void qa_app_application_q3_client_event_handling(qa_app_application_q3_client *client, double type);
/* TS input(event): routes a SeatInputEvent of this seat. */
void qa_app_application_q3_client_input(qa_app_application_q3_client *client, const qa_seat_input_event *event);
bool qa_app_application_q3_client_captures_input(const qa_app_application_q3_client *client);
/* TS frame(additionalEffects?, transformCamera?, environment = {},
 * viewOffset?). The frame's operations borrow the client until the next
 * frame or close. */
typedef struct qa_app_q3_frame_environment {
  bool has_q1_fog; qa_scene_fog q1_fog;
  const qa_q2_sky_view *source_sky; /* NULL is absent */
  bool no_world_model;
} qa_app_q3_frame_environment;
typedef struct qa_app_q3_frame_hooks {
  void *ctx;
  /* additionalEffects?(camera, source): false is TS undefined. */
  bool (*additional_effects)(void *ctx, qa_scene_camera camera, qa_source_scene_order *source,
                             qa_app_application_effect_frame *out);
  /* transformCamera?(camera). */
  qa_scene_camera (*transform_camera)(void *ctx, qa_scene_camera camera);
} qa_app_q3_frame_hooks;
qa_render_frame qa_app_application_q3_client_frame(qa_app_application_q3_client *client, const qa_app_q3_frame_hooks *hooks,
    const qa_app_q3_frame_environment *environment, const qa_vec3 *view_offset);
void qa_app_application_q3_client_shutdown(qa_app_application_q3_client *client);
void qa_app_application_q3_client_close(qa_app_application_q3_client *client);
#endif
