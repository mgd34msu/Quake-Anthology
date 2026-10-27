/*
 * Port of src/ui/common/draw.ts: UiDrawSink + renderUiCommands.
 * UiDrawCommand coordinates are absolute drawable pixels; all glyphs
 * and images are seat-clipped.
 * C accommodations: UiRenderServices closures become fn ptr + ctx;
 * emit follows the landed qa_render_command_emit_fn (the TS
 * Exclude<RenderCommand, swap-buffers> narrowing is documentation).
 * The UiTextRenderer draw layout out-param is discarded here, as the
 * TS ignores draw()'s return.
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_COMMON_DRAW_H
#define QA_UI_COMMON_DRAW_H

#include <stddef.h>

#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/text/draw2d.h"
#include "qa/text/ui.h"

/** TS UiRenderServices. picture returns a borrowed PictureAsset copy. */
typedef struct qa_ui_render_services {
  void *ctx;
  qa_ui_text_renderer *text;
  qa_picture_asset white;
  qa_picture_asset (*picture)(void *ctx, qa_resource_id resource);
  void (*emit)(void *ctx, const qa_render_command *command);
  void (*material)(void *ctx, const qa_material_text_draw *draw);
} qa_ui_render_services;

/** TS UiDrawSink (the TS class; clip is mutable draw state). */
typedef struct qa_ui_draw_sink {
  qa_seat_id seat;
  qa_rect target;
  qa_rect clip;
  qa_vec4 color;
  const qa_ui_render_services *services;
} qa_ui_draw_sink;

void qa_ui_draw_sink_init(qa_ui_draw_sink *sink, qa_seat_id seat,
                          qa_rect target,
                          const qa_ui_render_services *services);
/** TS setColor(color | null): NULL resets to white. */
void qa_ui_draw_sink_set_color(qa_ui_draw_sink *sink, const qa_vec4 *color);
/** TS stretchPixels(rect, uv, picture). */
void qa_ui_draw_sink_stretch_pixels(qa_ui_draw_sink *sink, qa_rect rect,
                                    qa_text_texture_rect uv,
                                    qa_picture_asset picture);
/** View as the TextDrawSink the Draw2D consumes. */
qa_text_draw_sink qa_ui_draw_sink_sink(qa_ui_draw_sink *sink);

/** TS renderUiCommands(context, commands, services). */
void qa_ui_render_commands(qa_ui_draw_context context,
                           const qa_ui_draw_command *commands,
                           size_t command_count,
                           const qa_ui_render_services *services);

#endif
