/* Port of src/content/q3/presentation/frame.ts. GPL-2.0-or-later.
 * CG_DrawActiveFrame ordering, owned by one viewing seat.
 * TS async steps are synchronous; false returns are OOM only. */
#ifndef QA_CONTENT_Q3_PRESENTATION_FRAME_H
#define QA_CONTENT_Q3_PRESENTATION_FRAME_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/config.h"
#include "qa/content/q3/presentation/draw_status.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/content/q3/presentation/frame_audio.h"
#include "qa/content/q3/presentation/hud.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/marks.h"
#include "qa/content/q3/presentation/media.h"
#include "qa/content/q3/presentation/prediction.h"
#include "qa/content/q3/presentation/scene.h"
#include "qa/content/q3/presentation/server_commands.h"
#include "qa/content/q3/presentation/snapshots.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/content/q3/presentation/view.h"
#include "qa/content/q3/presentation/weapons.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/render/scene/particles/q3_system.h"
typedef enum qa_q3_pres_frame_stereo {
  QA_Q3_PRES_STEREO_CENTER, QA_Q3_PRES_STEREO_LEFT, QA_Q3_PRES_STEREO_RIGHT
} qa_q3_pres_frame_stereo;
const char *qa_q3_pres_frame_stereo_name(qa_q3_pres_frame_stereo stereo);
/* TS Q3PresentationFrame. */
typedef struct qa_q3_pres_presentation_frame {
  double server_time;
  qa_q3_pres_frame_stereo stereo;
  bool demo_playback;
  double engine_frame_number;
} qa_q3_pres_presentation_frame;
typedef void (*qa_q3_pres_frame_source_fn)(void *ctx);
/* TS Q3PresentationFrameHost. loading_frame false only on OOM. */
typedef struct qa_q3_pres_presentation_frame_host {
  qa_q3_pres_client_configuration *configuration;
  qa_q3_pres_client_media *media;
  qa_q3_pres_snapshot_runtime *snapshots;
  qa_q3_pres_prediction_runtime *prediction;
  qa_q3_pres_view_runtime *view;
  qa_q3_pres_packet_entity_presenter *packet;
  qa_q3_pres_impact_mark_system *marks;
  qa_particle_system *particles;
  qa_q3_pres_local_entity_system *local_entities;
  qa_q3_pres_client_weapon_runtime *weapons;
  qa_q3_pres_client_frame_audio *frame_audio;
  qa_q3_pres_client_server_command_runtime *server_commands;
  qa_q3_pres_client_hud *hud;
  qa_q3_pres_client_draw_status *status;
  qa_q3_pres_client_draw_tools *tools;
  qa_q3_pres_scene_recorder *scene;
  qa_q3_pres_client_weapon_media_registry *weapon_registry;
  bool ragepro; /* TS hardware === "ragepro" */
  void *ctx;
  qa_q3_pres_packet_entity_options (*packet_options)(void *ctx);
  void (*present_view_weapon)(void *ctx, qa_q3_base_source_player_state *state,
                              qa_q3_pres_frame_source_fn source, void *source_ctx);
  void (*enter_frame)(void *ctx, qa_q3_pres_presentation_frame frame);
  void (*set_user_command_value)(void *ctx, double weapon, double sensitivity);
  void (*clear_looping_sounds)(void *ctx, bool kill_all);
  void (*set_listener)(void *ctx, double client, qa_vec3 origin, qa_axis axis);
  bool (*loading_frame)(void *ctx);
  void (*set_timescale)(void *ctx, double value);
  void (*print)(void *ctx, const char *text);
} qa_q3_pres_presentation_frame_host;
/* TS Q3PresentationSceneHost (scope "scene"): the frame-host fields
 * it picks; unused primary pointers stay NULL. */
typedef struct qa_q3_pres_presentation_scene_host {
  qa_q3_pres_presentation_frame_host frame;
} qa_q3_pres_presentation_scene_host;
typedef struct qa_q3_pres_presentation_frame_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_presentation_frame_host host;
  bool scene_scope;
  bool drawing, scene_drawing, closed;
} qa_q3_pres_presentation_frame_runtime;
/* Owner-state mismatch aborts with the TS Error text. */
void qa_q3_pres_presentation_frame_runtime_init(qa_q3_pres_presentation_frame_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_presentation_frame_host host);
void qa_q3_pres_presentation_frame_runtime_init_scene(qa_q3_pres_presentation_frame_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_presentation_scene_host host);
void qa_q3_pres_presentation_frame_runtime_close(qa_q3_pres_presentation_frame_runtime *runtime);
/* False only on OOM. Scope/state errors abort as the TS throws. */
bool qa_q3_pres_presentation_frame_runtime_draw_active_frame(qa_q3_pres_presentation_frame_runtime *runtime,
                                                             qa_q3_pres_presentation_frame input);
bool qa_q3_pres_presentation_frame_runtime_draw_scene_frame(qa_q3_pres_presentation_frame_runtime *runtime,
    qa_q3_pres_presentation_frame input, qa_scene_camera camera);
#endif
