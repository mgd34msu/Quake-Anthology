/* Port of src/ui/common/draw.ts. GPL-2.0-or-later. */
#include "qa/ui/common/draw.h"

#include <string.h>

#include "qa/render/commands/frame.h"
#include "qa/text/layout.h"
#include "qa/ui/common/layout.h"

static const qa_vec4 WHITE = { 1, 1, 1, 1 };

void qa_ui_draw_sink_init(qa_ui_draw_sink *sink, qa_seat_id seat,
                          qa_rect target,
                          const qa_ui_render_services *services) {
  sink->seat = seat;
  sink->target = target;
  sink->clip = target;
  sink->color = WHITE;
  sink->services = services;
}

void qa_ui_draw_sink_set_color(qa_ui_draw_sink *sink, const qa_vec4 *color) {
  sink->color = color != NULL ? *color : WHITE;
}

void qa_ui_draw_sink_stretch_pixels(qa_ui_draw_sink *sink, qa_rect rect,
                                    qa_text_texture_rect uv,
                                    qa_picture_asset picture) {
  qa_texture_rect source = { uv.s, uv.t, uv.s2, uv.t2 };
  qa_clipped_picture clipped;
  if (!qa_clip_picture(rect, source, sink->clip, &clipped)) return;
  const qa_ui_render_services *services = sink->services;
  if (picture.kind == QA_PICTURE_MATERIAL) {
    qa_material_text_draw draw;
    memset(&draw, 0, sizeof draw);
    draw.seat = sink->seat;
    draw.rect = clipped.rect;
    draw.uv.s = clipped.uv.s1;
    draw.uv.t = clipped.uv.t1;
    draw.uv.s2 = clipped.uv.s2;
    draw.uv.t2 = clipped.uv.t2;
    draw.color = sink->color;
    draw.picture = picture.material;
    services->material(services->ctx, &draw);
  } else {
    qa_render_command command;
    memset(&command, 0, sizeof command);
    command.kind = QA_RENDER_COMMAND_SET_COLOR;
    command.color = sink->color;
    services->emit(services->ctx, &command);
    memset(&command, 0, sizeof command);
    command.kind = QA_RENDER_COMMAND_STRETCH_PIC;
    command.image = picture.image.image;
    command.rect = clipped.rect;
    command.uv = clipped.uv;
    services->emit(services->ctx, &command);
  }
}

static void sink_set_color(void *ctx, const qa_vec4 *color) {
  qa_ui_draw_sink_set_color(ctx, color);
}
static void sink_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv,
                         const qa_picture_asset *picture) {
  qa_ui_draw_sink_stretch_pixels(ctx, rect, uv, *picture);
}

qa_text_draw_sink qa_ui_draw_sink_sink(qa_ui_draw_sink *sink) {
  qa_text_draw_sink view;
  view.ctx = sink;
  view.seat = sink->seat;
  view.target = sink->target;
  view.set_color = sink_set_color;
  view.stretch_pixels = sink_stretch;
  return view;
}

void qa_ui_render_commands(qa_ui_draw_context context,
                           const qa_ui_draw_command *commands,
                           size_t command_count,
                           const qa_ui_render_services *services) {
  qa_ui_draw_sink sink;
  qa_ui_draw_sink_init(&sink, context.binding.seat,
                       context.binding.viewport, services);
  qa_draw2d draw = { qa_ui_draw_sink_sink(&sink), QA_COORD_PIXELS };
  for (size_t i = 0; i < command_count; i++) {
    const qa_ui_draw_command *command = &commands[i];
    switch (command->kind) {
      case QA_UI_DRAW_CLIP:
        sink.clip = !command->has_clip_rect ? context.binding.viewport
          : qa_ui_intersect(context.binding.viewport, command->rect);
        break;
      case QA_UI_DRAW_TEXT: {
        qa_text_layout layout;
        qa_ui_draw_context ctx = context;
        qa_ui_draw_command cmd = *command;
        if (qa_ui_text_renderer_draw(services->text, &ctx, &cmd, &draw,
                                     &layout))
          qa_text_layout_destroy(&layout);
        break;
      }
      case QA_UI_DRAW_FILL: {
        qa_text_texture_rect uv = { 0, 0, 0, 0 };
        qa_ui_draw_sink_set_color(&sink, &command->color);
        qa_ui_draw_sink_stretch_pixels(&sink, command->rect, uv,
                                       services->white);
        break;
      }
      case QA_UI_DRAW_IMAGE: {
        qa_text_texture_rect uv = { command->tex_coords[0].x,
          command->tex_coords[0].y, command->tex_coords[1].x,
          command->tex_coords[1].y };
        qa_ui_draw_sink_set_color(&sink, &command->color);
        qa_ui_draw_sink_stretch_pixels(
          &sink, command->rect, uv,
          services->picture(services->ctx, command->resource));
        break;
      }
    }
  }
  qa_render_command reset;
  memset(&reset, 0, sizeof reset);
  reset.kind = QA_RENDER_COMMAND_SET_COLOR;
  reset.color = WHITE;
  services->emit(services->ctx, &reset);
}
