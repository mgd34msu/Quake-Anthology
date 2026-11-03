/* Port of src/app/bootstrap/presentation.ts (seatViewport, the camera
 * helpers and WorldSeatPresentation) and seat-factory.ts's
 * createSeatPresentation (qa_app_world_seat_presentation_create).
 *
 * C accommodations: async prepare/prepareImageRefresh are synchronous and
 * raise through qa_raise as the TS promises reject. Collaborators whose C
 * ports take hosts (SourceFinale, Q1MessageLocalization, Q1ServicePresentation,
 * the native Q2 HUD, rerelease story drawing) get hosts built here over the
 * ApplicationAssets the TS passes them.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRESENTATION_H
#define QA_APP_BOOTSTRAP_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/app/bootstrap/simulation/types.h"
/* Aborts (RangeError) on an invalid layout. */
qa_rect qa_app_seat_viewport(double index, double count, double width, double height);
qa_scene_camera qa_app_camera_with_kick(qa_scene_camera camera, qa_vec3 kick);
qa_scene_camera qa_app_camera_with_client_offset(qa_scene_camera camera, const qa_app_sim_player_view *player);
qa_scene_camera qa_app_camera_with_character_death(qa_scene_camera camera, const qa_app_sim_player_view *player);
#include "qa/app/bootstrap/component_bodies.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/contracts/presentation.h"
#include "qa/render/commands/frame.h"
#include "qa/render/scene/submissions.h"
/* TS ComponentEffectFrame: (camera, source, q1Fog?) => ApplicationEffectFrame.
 * q1_fog NULL is TS undefined; *out is caller-zeroed and caller-destroyed
 * (qa_app_application_effect_frame_destroy). */
typedef struct qa_app_component_effect_frame {
  void *ctx;
  void (*frame)(void *ctx, qa_scene_camera camera, qa_source_scene_order *source, const qa_scene_fog *q1_fog,
                qa_app_application_effect_frame *out);
} qa_app_component_effect_frame;
/* TS bindComponentEffects overlay { owner, bodies?(), draw(frames, camera) }.
 * bodies NULL is the TS absent method; its rows are borrowed until the next call. */
typedef struct qa_app_component_overlay {
  qa_presentation_owner owner;
  void *ctx;
  const qa_app_component_body *(*bodies)(void *ctx, size_t *out_count);
  void (*draw)(void *ctx, qa_scene_frame_builder *frames, qa_scene_camera camera);
} qa_app_component_overlay;
/* The WorldSeatPresentation members the component consumers read (TS
 * ViewingSeat = Pick<WorldSeatPresentation, "camera" | "bindComponentEffects" |
 * "viewport" | "splitScreen"> & local.player, plus ui.centerPrint, which
 * ApplicationSelectedQ3Presentation reads). ctx is the seat's identity: two
 * views are the same TS object when their ctx is equal.
 * bind_component_effects returns the binding the TS unbind closure releases. */
typedef struct qa_app_viewing_seat {
  void *ctx;
  qa_actor_id (*actor)(void *ctx);
  qa_seat_id (*seat)(void *ctx);
  qa_client_id (*client)(void *ctx);
  qa_scene_camera (*camera)(void *ctx);
  qa_rect (*viewport)(void *ctx);
  bool (*split_screen)(void *ctx);
  int64_t (*bind_component_effects)(void *ctx, qa_app_component_effect_frame frame,
                                    const qa_app_component_overlay *overlay);
  void (*unbind_component_effects)(void *ctx, int64_t binding);
  void (*center_print)(void *ctx, const char *text, double time_milliseconds, double duration_milliseconds);
} qa_app_viewing_seat;
/* ---- WorldSeatPresentation ---------------------------------------------- */
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/component_drawings.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/presentation_scene.h"
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/app/bootstrap/q3_client/qvm.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/app/bootstrap/ui.h"
#include "qa/content/q3/foundation/assets.h"
#include "qa/contracts/session.h"
#include "qa/text/atlas.h"
#include "qa/text/draw2d.h"
#include "qa/ui/hud/q2_native.h"

/* The ApplicationQ3Client members WorldSeatPresentation reads (TS
 * ApplicationQ3Client, q3-client.ts). The C ApplicationQ3Client
 * (q3_client.h) does not expose them yet; the application fills this view
 * over its Q3 seat client. Every callback receives ctx. */
typedef struct qa_app_seat_q3_effects {
  const qa_q3_pres_scene_admission *const *q3_admissions; size_t q3_admission_count;
  const qa_scene_operation *operations; size_t operation_count;
  const qa_surface_dynamic_light *lights; size_t light_count;
  const qa_q3_dynamic_light *q3_lights; size_t q3_light_count;
} qa_app_seat_q3_effects;
/* The closures and options WorldSeatPresentation hands q3Client.frame. The
 * effect frame and camera hooks call back into the presentation (ctx). */
typedef struct qa_app_seat_q3_frame_hooks {
  void *ctx;
  /* (camera, source) => effect frame plus the scene's supplemental operations;
   * valid until the presentation's next prepare. */
  qa_app_seat_q3_effects (*effects)(void *ctx, qa_scene_camera camera, qa_source_scene_order *source);
  qa_scene_camera (*camera)(void *ctx, qa_scene_camera camera);
  /* options: { noWorldModel, ...q1Services.view(actor), q1Fog? } */
  bool no_world_model;
  const qa_q2_sky_view *source_sky; /* NULL is TS absent */
  bool has_q1_fog; qa_scene_fog q1_fog;
  /* clientViewOffsetDelta (false is TS undefined) */
  bool has_client_view_offset_delta; qa_vec3 client_view_offset_delta;
} qa_app_seat_q3_frame_hooks;
typedef struct qa_app_seat_q3_client {
  void *ctx;
  const char *kind;                 /* options.kind: "typescript" | "qvm" */
  qa_seat_id seat; qa_actor_id actor; /* options.local.player */
  qa_cvar_registry *(*cvars)(void *ctx);
  qa_scene_camera (*camera)(void *ctx);
  void (*prepare)(void *ctx, double frame, qa_rect viewport, const qa_app_simulation_presentation *presentations,
                  size_t presentation_count, bool draw_status, const qa_app_component_body *bodies, size_t body_count,
                  bool weapon_visible, bool native_view);
  /* sharedEquipmentViewVisible: false is TS undefined. */
  bool (*shared_equipment_view_visible)(void *ctx, bool *out);
  const qa_app_qvm_held_weapon *(*shared_held_weapons)(void *ctx, size_t *out_count);
  const qa_app_prepared_primary_body *(*shared_bodies)(void *ctx, size_t *out_count);
  /* bodyPose(actor): false is TS null. */
  bool (*body_pose)(void *ctx, qa_actor_id actor, qa_vec3 *origin, qa_vec3 *angles);
  /* frame(...): false is TS undefined; commands borrowed until the next frame. */
  bool (*frame)(void *ctx, const qa_app_seat_q3_frame_hooks *hooks, const qa_render_command **commands, size_t *command_count);
  qa_scene_camera (*supplemental_weapon_camera)(void *ctx, qa_scene_camera camera);
  void (*weapon_hud_view)(void *ctx, bool *visible, bool *aggregate_warning);
  void (*close)(void *ctx);
} qa_app_seat_q3_client;

/* TS WorldSeatPresentation constructor arguments (seat-factory.ts
 * SeatPresentationSpec). NULL callbacks and pointers take the TS defaults. */
typedef struct qa_app_world_seat_presentation_options {
  qa_app_local_input *local;
  qa_app_application_assets *assets;
  qa_app_native_renderer *renderer;
  const qa_app_sim_presentation_access *simulation; /* playerView, worldText, modClientPresentationSources */
  double seat_count;
  qa_text_font_selection font;
  const qa_q3_character_assets *characters; /* NULL is TS null */
  qa_app_seat_ui *ui;
  qa_app_application_effects *effects;
  const qa_app_seat_q3_client *q3_client;   /* NULL is TS null; copied */
  qa_app_rerelease_presentation *rerelease; /* NULL is TS null */
  double (*world_text_cull_factor)(void *ctx); void *world_text_cull_factor_ctx; /* NULL is TS null */
  double (*field_of_view)(void *ctx); void *field_of_view_ctx;                   /* default 90 */
  const qa_app_sim_debug_shape_access *debug_shapes;                             /* NULL is TS null */
  double (*console_scale)(void *ctx); void *console_scale_ctx;                   /* default 0 */
  bool (*view_size)(void *ctx, qa_app_q1_view_settings *out); void *view_size_ctx; /* default null */
  bool (*planar_shadows)(void *ctx); void *planar_shadows_ctx;                   /* default false */
  qa_scene_camera (*camera_override)(void *ctx, qa_scene_camera camera); void *camera_override_ctx;
  void (*graph_overlay)(void *ctx, const qa_draw2d *draw, qa_rect view, qa_app_component_drawings *components);
  void *graph_overlay_ctx;                                                       /* NULL is TS null */
  /* nativeQ2?: { frame(), ownsEffects } */
  bool has_native_q2;
  qa_native_q2_hud_frame (*native_q2_frame)(void *ctx); void *native_q2_ctx;
  bool native_q2_owns_effects;
} qa_app_world_seat_presentation_options;

typedef struct qa_app_world_seat_presentation qa_app_world_seat_presentation;
/* TS createSeatPresentation(spec) / new WorldSeatPresentation(...). The
 * collaborators are borrowed; ui and q3_client are closed by close(). NULL
 * only on allocation failure. */
qa_app_world_seat_presentation *qa_app_world_seat_presentation_create(const qa_app_world_seat_presentation_options *options);
/* Frees a closed presentation (closes it first when still open). */
void qa_app_world_seat_presentation_destroy(qa_app_world_seat_presentation *presentation);

qa_app_local_input *qa_app_world_seat_presentation_local(const qa_app_world_seat_presentation *presentation);
qa_app_application_assets *qa_app_world_seat_presentation_assets(const qa_app_world_seat_presentation *presentation);
qa_app_seat_ui *qa_app_world_seat_presentation_ui(const qa_app_world_seat_presentation *presentation);
/* TS q3Client getter: NULL is TS null. */
const qa_app_seat_q3_client *qa_app_world_seat_presentation_q3_client(const qa_app_world_seat_presentation *presentation);
/* TS replaceQ3Client: returns the previous client (false is TS null). Raises
 * "Replacement presentation belongs to another local player". */
bool qa_app_world_seat_presentation_replace_q3_client(qa_app_world_seat_presentation *presentation,
    const qa_app_seat_q3_client *candidate, qa_app_seat_q3_client *previous);
qa_rect qa_app_world_seat_presentation_viewport(const qa_app_world_seat_presentation *presentation);
bool qa_app_world_seat_presentation_split_screen(const qa_app_world_seat_presentation *presentation);
/* RangeError "Invalid local seat layout". */
void qa_app_world_seat_presentation_publish_layout(qa_app_world_seat_presentation *presentation, double index, double count);
/* TS state getter; the ui rows are borrowed until the next state read. */
qa_seat_client_state qa_app_world_seat_presentation_state(qa_app_world_seat_presentation *presentation);
qa_scene_camera qa_app_world_seat_presentation_camera(qa_app_world_seat_presentation *presentation);
void qa_app_world_seat_presentation_receive(qa_app_world_seat_presentation *presentation,
    const qa_simulation_event *events, size_t count);
void qa_app_world_seat_presentation_source_events(qa_app_world_seat_presentation *presentation,
    const qa_app_sim_presentation_event *events, size_t count);
void qa_app_world_seat_presentation_prepare(qa_app_world_seat_presentation *presentation, const qa_world_snapshot *snapshot,
    const qa_app_simulation_presentation *presentations, size_t presentation_count,
    const qa_q3_character_view *characters, size_t character_count);
/* TS bindComponentEffects: the returned id is the TS unbind closure. */
int64_t qa_app_world_seat_presentation_bind_component_effects(qa_app_world_seat_presentation *presentation,
    qa_app_component_effect_frame frame, const qa_app_component_overlay *overlay);
void qa_app_world_seat_presentation_unbind_component_effects(qa_app_world_seat_presentation *presentation, int64_t binding);
/* TS frame(snapshot): the frame borrows presentation storage until the next frame. */
qa_render_frame qa_app_world_seat_presentation_frame(qa_app_world_seat_presentation *presentation, const qa_world_snapshot *snapshot);
/* Raises "Seat presentation uses another renderer". */
void qa_app_world_seat_presentation_render(qa_app_world_seat_presentation *presentation, qa_render_frame frame,
                                           qa_renderer_backend backend);
/* TS prepareImageRefresh(images): commit or discard consumes the handle. */
typedef struct qa_app_world_seat_image_refresh qa_app_world_seat_image_refresh;
qa_app_world_seat_image_refresh *qa_app_world_seat_presentation_prepare_image_refresh(
    qa_app_world_seat_presentation *presentation, qa_app_prepared_images *images);
void qa_app_world_seat_image_refresh_commit(qa_app_world_seat_image_refresh *refresh);
void qa_app_world_seat_image_refresh_discard(qa_app_world_seat_image_refresh *refresh);
/* TS close(): raises "Failed to close seat presentation" (AggregateError)
 * after closing every part when any part failed. */
void qa_app_world_seat_presentation_close(qa_app_world_seat_presentation *presentation);
/* The SeatPresentation (contracts/session.ts) this presentation implements;
 * state is read now. */
qa_seat_presentation qa_app_world_seat_presentation_seat(qa_app_world_seat_presentation *presentation);
/* The ViewingSeat the component consumers take (ctx is the presentation). */
qa_app_viewing_seat qa_app_world_seat_presentation_viewing_seat(qa_app_world_seat_presentation *presentation);

#endif
