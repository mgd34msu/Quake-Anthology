/* Port of src/app/bootstrap/q3-client/overlay.ts.
 * ApplicationAssets (../assets.ts, sibling slice) world material
 * context/registrations and the scene renderer's operation build
 * are host fn+ctx; submission routing, seat/world guards, picture
 * clipping and the clipped icon camera are ported here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_OVERLAY_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_OVERLAY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/presentation/scene.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/materials/evaluate.h"
#include "qa/render/commands/frame.h"
#include "qa/text/draw2d.h"
/* TS Q3OverlaySubmission. kind: "command"|"text"|"scene". */
typedef struct qa_app_q3_overlay_submission {
  const char *kind;
  qa_render_command command;             /* command (set-color|stretch-pic) */
  qa_material_text_draw draw;            /* text */
  const qa_q3_pres_presented_scene *scene; /* scene (borrowed) */
} qa_app_q3_overlay_submission;
/* Host mirrors for assets.world.materialContext and
 * ApplicationQ3SceneRenderer.operations + finishSceneOperations. */
typedef struct qa_app_q3_overlay_host {
  void *ctx;
  /* assets.world.materialContext({camera,target,time}). */
  qa_material_draw_context (*material_context)(void *ctx, qa_scene_camera camera,
                                               qa_seat_id seat, double time_milliseconds);
  /* renderer.operations(scene, input, firstEntity, options) finished:
   * returns borrowed operations for the clipped camera; lights are
   * already sliced to 32 by the caller (TS .slice(0, 32)). */
  void (*scene_operations)(void *ctx, const qa_q3_pres_presented_scene *scene,
                           qa_scene_camera camera, qa_seat_id seat, double time_milliseconds,
                           const qa_render_operation **out_operations, size_t *out_count);
} qa_app_q3_overlay_host;
/* TS drawQ3Overlay options. */
typedef struct qa_app_q3_overlay_options {
  const qa_app_q3_overlay_submission *submissions;
  size_t submission_count;
  qa_app_q3_overlay_host host;
  qa_scene_frame_builder *frames;
  qa_scene_camera camera;
  qa_rect viewport;
  qa_seat_id seat;
  double time;
} qa_app_q3_overlay_options;
void qa_app_draw_q3_overlay(qa_app_q3_overlay_options options);
#endif
