/* Port of src/content/q3/presentation/client.ts. GPL-2.0-or-later.
 * Composition of source cgame runtimes over one unified client/seat.
 * TS async creation is synchronous; create returns NULL only on OOM.
 * TS getter-backed plain fields on landed hosts are refreshed whenever the
 * VM cvars can have changed (see refresh_live in client.c). TS world is taken
 * as its mark geometry: qa has no worldMarkProjector over WorldScene. */
#ifndef QA_CONTENT_Q3_PRESENTATION_CLIENT_H
#define QA_CONTENT_Q3_PRESENTATION_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/mixer.h"
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/audio.h"
#include "qa/content/q3/presentation/config.h"
#include "qa/content/q3/presentation/console.h"
#include "qa/content/q3/presentation/frame.h"
#include "qa/content/q3/presentation/info.h"
#include "qa/content/q3/presentation/mark_projector.h"
#include "qa/content/q3/presentation/media.h"
#include "qa/content/q3/presentation/mission_hud.h"
#include "qa/content/q3/presentation/movement_host.h"
#include "qa/content/q3/presentation/player_state.h"
#include "qa/content/q3/presentation/scoreboard.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/common.h"
#include "qa/contracts/math.h"
#include "qa/text/draw2d.h"
#include "qa/text/q3_font.h"
typedef enum qa_q3_pres_session_mode { QA_Q3_PRES_SESSION_LIVE, QA_Q3_PRES_SESSION_DEMO } qa_q3_pres_session_mode;
/* TS Q3PresentationSession. get_game_state/get_server_command write borrowed
 * token spans; get_server_command false is TS null. status_visible NULL is the
 * TS absent optional. Primary-only members may be NULL on a scene session. */
typedef struct qa_q3_pres_presentation_session {
  qa_q3_base_product product;
  double client_number, last_executed_server_command;
  qa_q3_pres_session_mode mode;
  qa_q3_pres_command_source commands;
  qa_q3_pres_snapshot_source snapshots;
  qa_cvar_registry *cvars;
  void *ctx;
  double (*server_message_sequence)(void *ctx);
  bool (*status_visible)(void *ctx);
  const char *const *(*get_game_state)(void *ctx, size_t *out_count);
  bool (*get_server_command)(void *ctx, double sequence, const char *const **out, size_t *out_count);
  bool (*snapshot_ping)(void *ctx, double number, double *out);
  void (*add_reliable_command)(void *ctx, const char *text);
  void (*append_console_command)(void *ctx, const char *text);
  void (*register_cgame_command)(void *ctx, const char *name);
  void (*set_user_command_value)(void *ctx, double weapon, double sensitivity);
  void (*assert_current)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_q3_pres_presentation_session;
/* TS Q3ClientSound. origin NULL is TS null. */
typedef struct qa_q3_pres_client_sound {
  qa_q3_pres_presentation_sound_bank *bank;
  void *ctx;
  void (*start_sound)(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *pcm);
  void (*start_source_sound)(void *ctx, qa_pcm_sound *pcm, qa_start_sound_options options);
  void (*start_local_sound)(void *ctx, qa_pcm_sound *pcm, double channel);
  void (*add_loop_sound)(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *pcm, bool real);
  void (*update_sound_position)(void *ctx, double entity, qa_vec3 position);
  void (*stop_looping_sound)(void *ctx, double entity);
  void (*clear_looping_sounds)(void *ctx, bool kill_all);
  void (*set_listener)(void *ctx, double client, qa_vec3 origin, qa_axis axis);
  bool (*start_background_track)(void *ctx, const char *intro, const char *loop);
} qa_q3_pres_client_sound;
typedef void (*qa_q3_pres_client_source_fn)(void *ctx);
/* Option fields common to TS Q3ClientPresentationOptions and the Pick that
 * Q3ScenePresentationOptions takes. body_hidden NULL is the TS absent optional. */
typedef struct qa_q3_pres_presentation_common {
  qa_q3_pres_presentation_session session;
  qa_q3_pres_sound_asset_reader assets;
  qa_q3_pres_renderer_resources resources;
  qa_q3_pres_scene_recorder *scene;
  qa_q3_pres_mark_geometry mark_geometry; /* TS worldMarkProjector(world) input */
  qa_q3_pres_collision_world collision;
  qa_q3_pres_client_sound sound;
  qa_draw2d draw;
  qa_q3_ui_asset_registry *font_registry;
  double target_width, target_height;
  bool ragepro; /* TS hardware === "ragepro" */
  void *ctx;
  double (*milliseconds)(void *ctx);
  double (*server_time)(void *ctx); /* TS clock.serverTime; NULL when the producer has no transport clock */
  double (*frame_number)(void *ctx);
  bool (*body_hidden)(void *ctx, double entity);
  qa_q3_pres_lighting_sample (*light_for_point)(void *ctx, qa_vec3 point);
  double (*memory_remaining)(void *ctx);
  /* Non-NULL: the TS target Rect shared with its owner, read for the view
   * settings' video size in place of target_width/target_height. */
  const qa_rect *target;
} qa_q3_pres_presentation_common;
/* TS Q3ClientPresentationOptions. menus_missionpack selects the TS menus kind;
 * the menu_* fields are read only for it. Optional hooks NULL are absent. */
typedef struct qa_q3_pres_client_presentation_options {
  qa_q3_pres_presentation_common common;
  qa_command_context command_context;
  qa_q3_pres_movement_host movement;
  bool source_debug;
  bool menus_missionpack;
  qa_q3_pres_engine_ui_cinematics menu_cinematics;
  qa_ui_runtime_audio menu_audio;
  qa_command_buffer *menu_commands; /* the buffer session.append_console_command feeds */
  void (*set_key_catcher)(void *ctx, double mask);
  void (*weapon_selection)(void *ctx, double weapon);
  bool (*body_submission)(void *ctx, double entity, qa_qvm_body_part part, const qa_q3_pres_ref_model_entity *source, bool base);
  void (*body_pose)(void *ctx, qa_q3_pres_client_entity *entity);
  qa_q3_pres_weapon_hud_reading (*weapon_hud)(void *ctx);
  void (*update_loading_screen)(void *ctx, qa_q3_pres_client_source_fn draw, void *draw_ctx);
  void (*character)(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_client_source_fn source, void *source_ctx);
  void (*event)(void *ctx, qa_q3_pres_client_entity *entity, qa_vec3 position, qa_q3_pres_client_source_fn source, void *source_ctx);
  void (*predict_item)(void *ctx, qa_q3_pres_client_entity *entity, qa_q3_pres_client_source_fn source, void *source_ctx);
  void (*view_weapon)(void *ctx, qa_q3_base_source_player_state *state, qa_q3_pres_client_source_fn source, void *source_ctx);
  void (*player_weapon)(void *ctx, const qa_q3_pres_ref_model_entity *parent, qa_q3_base_source_player_state *state,
                        qa_q3_pres_client_entity *entity, qa_q3_base_team team, qa_q3_pres_client_source_fn source, void *source_ctx);
} qa_q3_pres_client_presentation_options;
/* TS Q3ScenePresentationOptions. Supplemental source scenes share cgame
 * effects, but never own input, UI or the destination view. */
typedef struct qa_q3_pres_scene_presentation_options {
  qa_q3_pres_presentation_common common;
  void (*center_print)(void *ctx, const char *text, double time_ms, double duration_ms);
} qa_q3_pres_scene_presentation_options;
typedef struct qa_q3_pres_client_presentation qa_q3_pres_client_presentation;
/* TS createQ3ClientPresentation / createQ3ScenePresentation. NULL only on OOM;
 * TS throws abort with the TS text. */
qa_q3_pres_client_presentation *qa_q3_pres_create_client_presentation(qa_q3_pres_client_presentation_options options);
qa_q3_pres_client_presentation *qa_q3_pres_create_scene_presentation(qa_q3_pres_scene_presentation_options options);
void qa_q3_pres_client_presentation_destroy(qa_q3_pres_client_presentation *presentation);
/* TS result members. Primary-only accessors return NULL on a scene presentation;
 * menus also returns NULL on baseq3. */
qa_q3_pres_client_game_state *qa_q3_pres_client_presentation_state(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_game_static_state *qa_q3_pres_client_presentation_static_state(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_configuration *qa_q3_pres_client_presentation_configuration(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_media *qa_q3_pres_client_presentation_media(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_info_store *qa_q3_pres_client_presentation_clients(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_prediction_runtime *qa_q3_pres_client_presentation_prediction(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_view_runtime *qa_q3_pres_client_presentation_view(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_snapshot_runtime *qa_q3_pres_client_presentation_snapshots(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_packet_entity_presenter *qa_q3_pres_client_presentation_packet(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_event_runtime *qa_q3_pres_client_presentation_events(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_effects *qa_q3_pres_client_presentation_effects(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_local_entity_system *qa_q3_pres_client_presentation_local_entities(qa_q3_pres_client_presentation *presentation);
qa_particle_system *qa_q3_pres_client_presentation_particles(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_impact_mark_system *qa_q3_pres_client_presentation_marks(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_weapon_runtime *qa_q3_pres_client_presentation_weapons(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_server_command_runtime *qa_q3_pres_client_presentation_server_commands(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_console_runtime *qa_q3_pres_client_presentation_console(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_frame_audio *qa_q3_pres_client_presentation_frame_audio(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_loading_screen *qa_q3_pres_client_presentation_loading(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_hud *qa_q3_pres_client_presentation_hud(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_draw_status *qa_q3_pres_client_presentation_status(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_draw_tools *qa_q3_pres_client_presentation_tools(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_mission_hud *qa_q3_pres_client_presentation_menus(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_presentation_frame_runtime *qa_q3_pres_client_presentation_frames(qa_q3_pres_client_presentation *presentation);
/* TS configString closure over the current strings. */
const char *qa_q3_pres_client_presentation_config_string(qa_q3_pres_client_presentation *presentation, double index);
void qa_q3_pres_client_presentation_close(qa_q3_pres_client_presentation *presentation);
void qa_q3_pres_client_presentation_key_event(qa_q3_pres_client_presentation *presentation, double key, bool down);
void qa_q3_pres_client_presentation_mouse_event(qa_q3_pres_client_presentation *presentation, double x, double y);
void qa_q3_pres_client_presentation_event_handling(qa_q3_pres_client_presentation *presentation, double type);
#endif
