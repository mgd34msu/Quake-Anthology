/* Port of src/text/draw2d.ts. GPL-2.0-or-later. */
#include "qa/text/draw2d.h"

#include <math.h>
#include "qa/foundation/js.h"

static const qa_vec4 WHITE = {1, 1, 1, 1};
static const qa_text_texture_rect FULL_UV = {0, 0, 1, 1};

/* Math.fround staging (docs/PORTING.md). */
static double f32(double value) { return (double)(float)value; }

/* JS ToInt32 for Math.imul. */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double magnitude = fmod(fabs(trunc(value)), 4294967296.0);
  uint32_t bits = (uint32_t)magnitude;
  if (value < 0) bits = ~bits + 1u;
  return (int32_t)bits;
}
static int32_t imul(double left, double right) {
  return (int32_t)((uint32_t)to_int32(left) * (uint32_t)to_int32(right));
}

/* render/commands/frame.ts clipPicture, inlined until the render
 * module is ported; arithmetic is identical to the TS. Returns false
 * for TS null. */
struct clip_result {
  qa_rect rect;
  qa_texture_rect uv;
};
static bool clip_picture(qa_rect rect, qa_texture_rect uv, qa_rect clip,
                         struct clip_result *out) {
  if (rect.width == 0 || rect.height == 0) return false;
  double left = qa_js_max(qa_js_min(rect.x, rect.x + rect.width), clip.x);
  double right = qa_js_min(qa_js_max(rect.x, rect.x + rect.width), clip.x + clip.width);
  double top = qa_js_max(qa_js_min(rect.y, rect.y + rect.height), clip.y);
  double bottom = qa_js_min(qa_js_max(rect.y, rect.y + rect.height), clip.y + clip.height);
  if (left >= right || top >= bottom) return false;
  out->rect.x = left;
  out->rect.y = top;
  out->rect.width = right - left;
  out->rect.height = bottom - top;
  out->uv.s1 = uv.s1 + (uv.s2 - uv.s1) * (left - rect.x) / rect.width;
  out->uv.s2 = uv.s1 + (uv.s2 - uv.s1) * (right - rect.x) / rect.width;
  out->uv.t1 = uv.t1 + (uv.t2 - uv.t1) * (top - rect.y) / rect.height;
  out->uv.t2 = uv.t1 + (uv.t2 - uv.t1) * (bottom - rect.y) / rect.height;
  return true;
}

void qa_text_command_sink_init(qa_text_command_sink *sink, qa_seat_id seat,
                               qa_rect target,
                               qa_render_command_emit_fn emit, void *emit_ctx,
                               qa_material_text_draw_fn material,
                               void *material_ctx) {
  sink->seat = seat;
  sink->target = target;
  sink->emit = emit;
  sink->emit_ctx = emit_ctx;
  sink->material = material;
  sink->material_ctx = material_ctx;
  sink->color = WHITE;
  sink->target_ref = NULL;
}
void qa_text_command_sink_share_target(qa_text_command_sink *sink, const qa_rect *target) {
  sink->target_ref = target;
}
static qa_rect command_sink_target(const qa_text_command_sink *sink) {
  return sink->target_ref != NULL ? *sink->target_ref : sink->target;
}
qa_rect qa_text_draw_sink_target(const qa_text_draw_sink *sink) {
  return sink->target_ref != NULL ? *sink->target_ref : sink->target;
}

void qa_text_command_sink_set_color(qa_text_command_sink *sink,
                                    const qa_vec4 *color) {
  sink->color = color != NULL ? *color : WHITE;
  qa_render_command command = {0};
  command.kind = QA_RENDER_COMMAND_SET_COLOR;
  command.color = sink->color;
  sink->emit(sink->emit_ctx, &command);
}

void qa_text_command_sink_stretch_pixels(qa_text_command_sink *sink,
                                         qa_rect rect,
                                         qa_text_texture_rect uv,
                                         const qa_picture_asset *picture) {
  qa_rect target = command_sink_target(sink);
  qa_rect destination = rect;
  destination.x = rect.x + target.x;
  destination.y = rect.y + target.y;
  if (picture->kind == QA_PICTURE_MATERIAL) {
    qa_material_text_draw draw = {sink->seat, destination, uv, sink->color,
                                  picture->material};
    sink->material(sink->material_ctx, &draw);
  } else {
    qa_texture_rect source = {uv.s, uv.t, uv.s2, uv.t2};
    struct clip_result clipped;
    if (clip_picture(destination, source, target, &clipped)) {
      qa_render_command command = {0};
      command.kind = QA_RENDER_COMMAND_STRETCH_PIC;
      command.rect = clipped.rect;
      command.uv = clipped.uv;
      command.image = picture->image.image;
      sink->emit(sink->emit_ctx, &command);
    }
  }
}

static void sink_set_color(void *ctx, const qa_vec4 *color) {
  qa_text_command_sink_set_color((qa_text_command_sink *)ctx, color);
}
static void sink_stretch_pixels(void *ctx, qa_rect rect,
                                qa_text_texture_rect uv,
                                const qa_picture_asset *picture) {
  qa_text_command_sink_stretch_pixels((qa_text_command_sink *)ctx, rect, uv,
                                      picture);
}

qa_text_draw_sink qa_text_command_sink_sink(qa_text_command_sink *sink) {
  qa_text_draw_sink view;
  view.ctx = sink;
  view.seat = sink->seat;
  view.target = sink->target;
  view.target_ref = sink->target_ref;
  view.set_color = sink_set_color;
  view.stretch_pixels = sink_stretch_pixels;
  return view;
}

double qa_draw2d_width(const qa_draw2d *draw) { return qa_text_draw_sink_target(&draw->commands).width; }
double qa_draw2d_height(const qa_draw2d *draw) {
  return qa_text_draw_sink_target(&draw->commands).height;
}

double qa_draw2d_scale_x(const qa_draw2d *draw) {
  switch (draw->space) {
    case QA_COORD_PIXELS: return 1;
    case QA_COORD_STRETCH_640: return f32(f32(qa_draw2d_width(draw)) / 640);
    case QA_COORD_BASE_UI_640: return f32(f32(qa_draw2d_height(draw)) * f32(1.0 / 480));
    case QA_COORD_TEAM_UI_640: return f32(f32(qa_draw2d_width(draw)) * f32(1.0 / 640));
  }
  return 1;
}

double qa_draw2d_scale_y(const qa_draw2d *draw) {
  switch (draw->space) {
    case QA_COORD_PIXELS: return 1;
    case QA_COORD_STRETCH_640: return f32(f32(qa_draw2d_height(draw)) / 480);
    case QA_COORD_BASE_UI_640: return qa_draw2d_scale_x(draw);
    case QA_COORD_TEAM_UI_640: return f32(f32(qa_draw2d_height(draw)) * f32(1.0 / 480));
  }
  return 1;
}

double qa_draw2d_bias_x(const qa_draw2d *draw) {
  double width = qa_draw2d_width(draw);
  double height = qa_draw2d_height(draw);
  if (draw->space == QA_COORD_BASE_UI_640 &&
      imul(width, 480) > imul(height, 640)) {
    return f32(0.5 * f32(f32(width) - f32(f32(height) * f32(640.0 / 480))));
  }
  return 0;
}

void qa_draw2d_set_color(const qa_draw2d *draw, const qa_vec4 *color) {
  draw->commands.set_color(draw->commands.ctx, color);
}

qa_rect qa_draw2d_adjust(const qa_draw2d *draw, qa_rect rect) {
  double scale_x = qa_draw2d_scale_x(draw);
  double scale_y = qa_draw2d_scale_y(draw);
  double x = f32(f32(rect.x) * scale_x);
  qa_rect out;
  out.x = draw->space == QA_COORD_TEAM_UI_640
              ? x
              : f32(x + qa_draw2d_bias_x(draw));
  out.y = f32(f32(rect.y) * scale_y);
  out.width = f32(f32(rect.width) * scale_x);
  out.height = f32(f32(rect.height) * scale_y);
  return out;
}

void qa_draw2d_stretch_pixels(const qa_draw2d *draw, qa_rect rect,
                              qa_text_texture_rect uv,
                              const qa_picture_asset *picture) {
  draw->commands.stretch_pixels(draw->commands.ctx, rect, uv, picture);
}

void qa_draw2d_stretch_pixels_lazy(const qa_draw2d *draw, qa_rect rect,
                                   qa_text_texture_rect uv,
                                   qa_picture_producer_fn produce, void *ctx) {
  qa_picture_asset picture = produce(ctx);
  qa_draw2d_stretch_pixels(draw, rect, uv, &picture);
}

void qa_draw2d_stretch_pic(const qa_draw2d *draw, qa_rect rect,
                           qa_text_texture_rect uv,
                           const qa_picture_asset *picture) {
  qa_draw2d_stretch_pixels(draw, qa_draw2d_adjust(draw, rect), uv, picture);
}

void qa_draw2d_stretch_pic_lazy(const qa_draw2d *draw, qa_rect rect,
                                qa_text_texture_rect uv,
                                qa_picture_producer_fn produce, void *ctx) {
  qa_picture_asset picture = produce(ctx);
  qa_draw2d_stretch_pic(draw, rect, uv, &picture);
}

void qa_draw2d_draw_pic(const qa_draw2d *draw, qa_rect rect,
                        const qa_picture_asset *picture) {
  qa_draw2d_stretch_pic(draw, rect, FULL_UV, picture);
}

static void draw_handle_pic(const qa_draw2d *draw, qa_rect rect,
                            const qa_picture_asset *picture) {
  qa_rect flipped = rect;
  flipped.width = fabs(rect.width);
  flipped.height = fabs(rect.height);
  qa_text_texture_rect uv;
  uv.s = rect.width < 0 ? 1 : 0;
  uv.s2 = rect.width < 0 ? 0 : 1;
  uv.t = rect.height < 0 ? 1 : 0;
  uv.t2 = rect.height < 0 ? 0 : 1;
  qa_draw2d_stretch_pic(draw, flipped, uv, picture);
}

void qa_draw2d_draw_handle_pic(const qa_draw2d *draw, qa_rect rect,
                               const qa_picture_asset *picture) {
  draw_handle_pic(draw, rect, picture);
}

void qa_draw2d_draw_handle_pic_lazy(const qa_draw2d *draw, qa_rect rect,
                                    qa_picture_producer_fn produce,
                                    void *ctx) {
  qa_picture_asset picture = produce(ctx);
  draw_handle_pic(draw, rect, &picture);
}

void qa_draw2d_fill_rect(const qa_draw2d *draw, qa_rect rect, qa_vec4 color,
                         const qa_picture_asset *picture) {
  qa_text_texture_rect zero = {0, 0, 0, 0};
  qa_draw2d_set_color(draw, &color);
  qa_draw2d_stretch_pic(draw, rect, zero, picture);
  qa_draw2d_set_color(draw, NULL);
}
