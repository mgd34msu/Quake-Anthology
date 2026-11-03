/* Port of src/app/bootstrap/q3-client/overlay.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/overlay.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/presentation/refdef.h"
#include "qa/foundation/support.h"
#include "qa/render/commands/material2d.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool clipped_camera(qa_scene_camera camera, qa_rect clip, qa_scene_camera *out) {
  qa_rect area = camera.viewport;
  double x = qa_js_max(area.x, clip.x), y = qa_js_max(area.y, clip.y);
  double width = qa_js_min(area.x + area.width, clip.x + clip.width) - x;
  double height = qa_js_min(area.y + area.height, clip.y + clip.height) - y;
  if (width <= 0 || height <= 0) return false;
  if (x == area.x && y == area.y && width == area.width && height == area.height) { *out = camera; return true; }
  double sx = area.width / width, sy = area.height / height;
  double tx = (2 * (area.x - x) + area.width - width) / width;
  double ty = -(2 * (area.y - y) + area.height - height) / height;
  const double *p = camera.projection.values;
  *out = camera;
  out->viewport = (qa_rect){x, y, width, height};
  out->projection.values[0] = sx * p[0] + tx * p[3];
  out->projection.values[1] = sy * p[1] + ty * p[3];
  out->projection.values[4] = sx * p[4] + tx * p[7];
  out->projection.values[5] = sy * p[5] + ty * p[7];
  out->projection.values[8] = sx * p[8] + tx * p[11];
  out->projection.values[9] = sy * p[9] + ty * p[11];
  out->projection.values[12] = sx * p[12] + tx * p[15];
  out->projection.values[13] = sy * p[13] + ty * p[15];
  return true;
}
static void set_white(qa_scene_frame_builder *frames) {
  qa_render_command command = {0};
  command.kind = QA_RENDER_COMMAND_SET_COLOR;
  command.color = (qa_vec4){1, 1, 1, 1};
  qa_scene_frame_builder_command(frames, command);
}
void qa_app_draw_q3_overlay(qa_app_q3_overlay_options options) {
  qa_scene_frame_builder *frames = options.frames;
  qa_rect viewport = options.viewport;
  set_white(frames);
  for (size_t i = 0; i < options.submission_count; i++) {
    const qa_app_q3_overlay_submission *submission = &options.submissions[i];
    if (is(submission->kind, "command")) {
      qa_render_command command = submission->command;
      if (command.kind == QA_RENDER_COMMAND_SET_COLOR) {
        qa_scene_frame_builder_command(frames, command);
      } else {
        qa_clipped_picture clipped;
        if (qa_clip_picture(command.rect, command.uv, viewport, &clipped)) {
          command.rect = clipped.rect;
          command.uv = clipped.uv;
          qa_scene_frame_builder_command(frames, command);
        }
      }
    } else if (is(submission->kind, "text")) {
      if (!qa_same_seat(submission->draw.seat, options.seat))
        qa_range_error("Source HUD text belongs to another seat");
      qa_material_draw_context context = options.host.material_context(
          options.host.ctx, options.camera, options.seat, options.time);
      qa_draw_batch *batches = NULL;
      size_t batch_count = 0;
      qa_prepare_material_text(submission->draw, viewport, context, &batches, &batch_count);
      qa_render_operation operation = {0};
      operation.kind = QA_RENDER_OP_DRAW;
      operation.batches = batches;
      operation.batch_count = batch_count;
      qa_render_view view;
      memset(&view, 0, sizeof view);
      view.state.viewport = viewport;
      view.target_is_seat = true;
      view.target_seat = options.seat;
      view.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, options.time};
      view.operations = &operation;
      view.operation_count = 1;
      qa_scene_frame_builder_view(frames, view);
      free(batches);
    } else {
      const qa_q3_pres_presented_scene *scene = submission->scene;
      if ((qa_js_to_int32(scene->source.render_flags) & QA_Q3_PRES_RDF_NOWORLDMODEL) == 0
          || !qa_same_seat(scene->seat, options.seat))
        qa_range_error("Source HUD cannot replace a world view or another seat");
      qa_scene_camera camera;
      if (!clipped_camera(scene->camera, viewport, &camera)) continue;
      const qa_render_operation *operations = NULL;
      size_t operation_count = 0;
      options.host.scene_operations(options.host.ctx, scene, camera, options.seat,
                                    options.time, &operations, &operation_count);
      qa_render_view_clear clear = {1, NULL, false};
      qa_render_view view;
      memset(&view, 0, sizeof view);
      view.state.viewport = camera.viewport;
      view.state.clear = &clear;
      view.target_is_seat = true;
      view.target_seat = options.seat;
      view.time = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, options.time};
      view.operations = operations;
      view.operation_count = operation_count;
      qa_scene_frame_builder_view(frames, view);
    }
  }
  set_white(frames);
}
