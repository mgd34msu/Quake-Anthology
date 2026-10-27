/*
 * Port of src/text/draw2d.ts: 2D picture/text draw sinks and the
 * Q3 cg_drawtools/ui_atoms coordinate transforms.
 * C accommodations:
 *  - TS TextureRect (s/t/s2/t2) is qa_text_texture_rect, distinct from
 *    contracts/render.h qa_texture_rect (s1/t1/s2/t2); TextCommandSink
 *    converts between them exactly as the TS does.
 *  - CompiledMaterial (materials/compile.ts, unported) and
 *    RegisteredFont (text/q3-font.ts, unported) appear only as opaque
 *    pointers in these interface shapes; no behavior here reads them.
 *  - Interfaces become function-pointer tables plus void *ctx. The
 *    Promise returns of FontFileReader/FontAssetServices are
 *    synchronous; RetainedFontFile results use a has-value bool.
 *  - PictureAsset strings/texts are borrowed unless a function says
 *    otherwise. TS `PictureAsset | (() => PictureAsset)` parameters get
 *    a plain form and a `_lazy` form taking a producer fn + ctx.
 *  - SeatId equality uses qa_same_seat (contracts/identity.h).
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_DRAW2D_H
#define QA_TEXT_DRAW2D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"

typedef struct qa_text_texture_rect {
  double s, t, s2, t2;
} qa_text_texture_rect;

typedef struct qa_image_picture {
  const char *name; /* borrowed */
  qa_renderer_image image;
} qa_image_picture;

typedef struct qa_material_picture {
  const char *name; /* borrowed */
  double order;
  const void *compiled; /* TS CompiledMaterial; module unported. */
} qa_material_picture;

/* TS PictureAsset = ImagePicture | MaterialPicture. */
typedef enum qa_picture_kind {
  QA_PICTURE_IMAGE,
  QA_PICTURE_MATERIAL
} qa_picture_kind;
typedef struct qa_picture_asset {
  qa_picture_kind kind;
  qa_image_picture image;       /* QA_PICTURE_IMAGE */
  qa_material_picture material; /* QA_PICTURE_MATERIAL */
} qa_picture_asset;

/* TS RetainedFontFile: bytes are retained by the producing reader
 * until its freeFile is called with this value. */
typedef struct qa_retained_font_file {
  qa_byte_view bytes;
  double length;
} qa_retained_font_file;

/* TS FontFileReader. read_file_retained false is TS undefined. */
typedef struct qa_font_file_reader {
  void *ctx;
  double (*read_file_length)(void *ctx, const char *path);
  bool (*read_file_retained)(void *ctx, const char *path,
                             qa_retained_font_file *out);
  void (*free_file)(void *ctx, qa_retained_font_file file);
} qa_font_file_reader;

typedef void (*qa_font_print_fn)(void *ctx, const char *text);

/* TS FontAssetServices. mode is "no-mip" | "mip" (exact TS strings).
 * register_font returns a borrowed TS RegisteredFont (opaque until
 * text/q3-font.ts is ported), NULL for TS null. path NULL is TS null.
 * register_picture false is OOM only; on true *out borrows any strings
 * the service retains, as in the TS. */
typedef struct qa_font_asset_services {
  void *ctx;
  bool (*register_picture)(void *ctx, const char *path, const char *mode,
                           qa_picture_asset *out);
  const void *(*register_font)(void *ctx, const char *path, double size,
                               qa_font_print_fn print, void *print_ctx);
} qa_font_asset_services;

/* TS TextDrawSink. set_color with NULL color is TS null (white). */
typedef struct qa_text_draw_sink {
  void *ctx;
  qa_seat_id seat;
  qa_rect target;
  void (*set_color)(void *ctx, const qa_vec4 *color);
  void (*stretch_pixels)(void *ctx, qa_rect rect, qa_text_texture_rect uv,
                         const qa_picture_asset *picture);
} qa_text_draw_sink;

/* TS MaterialTextDraw. */
typedef struct qa_material_text_draw {
  qa_seat_id seat;
  qa_rect rect;
  qa_text_texture_rect uv;
  qa_vec4 color;
  qa_material_picture picture;
} qa_material_text_draw;

typedef void (*qa_render_command_emit_fn)(void *ctx,
                                          const qa_render_command *command);
typedef void (*qa_material_text_draw_fn)(void *ctx,
                                         const qa_material_text_draw *draw);

/* TS TextCommandSink implements TextDrawSink. Use
 * qa_text_command_sink_sink() to view it as a qa_text_draw_sink. */
typedef struct qa_text_command_sink {
  qa_seat_id seat;
  qa_rect target;
  qa_render_command_emit_fn emit;
  void *emit_ctx;
  qa_material_text_draw_fn material;
  void *material_ctx;
  qa_vec4 color;
} qa_text_command_sink;

void qa_text_command_sink_init(qa_text_command_sink *sink, qa_seat_id seat,
                               qa_rect target,
                               qa_render_command_emit_fn emit, void *emit_ctx,
                               qa_material_text_draw_fn material,
                               void *material_ctx);
qa_text_draw_sink qa_text_command_sink_sink(qa_text_command_sink *sink);
void qa_text_command_sink_set_color(qa_text_command_sink *sink,
                                    const qa_vec4 *color);
void qa_text_command_sink_stretch_pixels(qa_text_command_sink *sink,
                                         qa_rect rect,
                                         qa_text_texture_rect uv,
                                         const qa_picture_asset *picture);

/* TS CoordinateSpace (exact strings in the comments). */
typedef enum qa_coordinate_space {
  QA_COORD_PIXELS,      /* "pixels" */
  QA_COORD_STRETCH_640, /* "stretch-640" */
  QA_COORD_BASE_UI_640, /* "base-ui-640" */
  QA_COORD_TEAM_UI_640  /* "team-ui-640" */
} qa_coordinate_space;

/* TS Draw2D. commands is stored by value (a sink view). */
typedef struct qa_draw2d {
  qa_text_draw_sink commands;
  qa_coordinate_space space;
} qa_draw2d;

typedef qa_picture_asset (*qa_picture_producer_fn)(void *ctx);

double qa_draw2d_width(const qa_draw2d *draw);
double qa_draw2d_height(const qa_draw2d *draw);
double qa_draw2d_scale_x(const qa_draw2d *draw);
double qa_draw2d_scale_y(const qa_draw2d *draw);
double qa_draw2d_bias_x(const qa_draw2d *draw);
void qa_draw2d_set_color(const qa_draw2d *draw, const qa_vec4 *color);
qa_rect qa_draw2d_adjust(const qa_draw2d *draw, qa_rect rect);
void qa_draw2d_stretch_pic(const qa_draw2d *draw, qa_rect rect,
                           qa_text_texture_rect uv,
                           const qa_picture_asset *picture);
void qa_draw2d_stretch_pic_lazy(const qa_draw2d *draw, qa_rect rect,
                                qa_text_texture_rect uv,
                                qa_picture_producer_fn produce, void *ctx);
void qa_draw2d_stretch_pixels(const qa_draw2d *draw, qa_rect rect,
                              qa_text_texture_rect uv,
                              const qa_picture_asset *picture);
void qa_draw2d_stretch_pixels_lazy(const qa_draw2d *draw, qa_rect rect,
                                   qa_text_texture_rect uv,
                                   qa_picture_producer_fn produce, void *ctx);
void qa_draw2d_draw_pic(const qa_draw2d *draw, qa_rect rect,
                        const qa_picture_asset *picture);
void qa_draw2d_draw_handle_pic(const qa_draw2d *draw, qa_rect rect,
                               const qa_picture_asset *picture);
void qa_draw2d_draw_handle_pic_lazy(const qa_draw2d *draw, qa_rect rect,
                                    qa_picture_producer_fn produce,
                                    void *ctx);
void qa_draw2d_fill_rect(const qa_draw2d *draw, qa_rect rect, qa_vec4 color,
                         const qa_picture_asset *picture);

#endif
