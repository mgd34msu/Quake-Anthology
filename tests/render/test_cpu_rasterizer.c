/*
 * Port of tests/render/cpu/rasterizer.test.ts and row-parallel.test.ts.
 * toThrow cases run in a forked child that must abort (docs/PORTING.md).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/contracts/identity.h"
#include "qa/core/math.h"
#include "qa/materials/source_state.h"
#include "qa/render/cpu/commands.h"
#include "qa/render/cpu/rasterizer.h"
#include "qa/render/cpu/source.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_ABORTS(...) do { checks++; pid_t pid_ = fork(); if (pid_ == 0) { (void)prctl(PR_SET_DUMPABLE, 0); (void)!freopen("/dev/null", "w", stderr); __VA_ARGS__; _exit(0); } \
  int status_ = 0; waitpid(pid_, &status_, 0); if (!WIFSIGNALED(status_)) { failures++; fprintf(stderr, "FAIL %s:%d: expected abort\n", __FILE__, __LINE__); } } while (0)
#define PIXEL_IS(r_, a, b, c, d) do { uint8_t px_[4]; pixel(r_, px_); checks++; if (px_[0] != (a) || px_[1] != (b) || px_[2] != (c) || px_[3] != (d)) { failures++; fprintf(stderr, "FAIL %s:%d: pixel %d,%d,%d,%d\n", __FILE__, __LINE__, px_[0], px_[1], px_[2], px_[3]); } } while (0)

static qa_vec4 v4(double x, double y, double z, double w) { qa_vec4 v; v.x = x; v.y = y; v.z = z; v.w = w; return v; }

typedef struct fixture { qa_identity_owner identity; qa_renderer_resource_owner owner; qa_software_renderer renderer; qa_renderer_image white; } fixture;
static qa_renderer_image make_image(qa_renderer_resource_owner owner, double ordinal, double w, double h) {
  qa_renderer_image image; memset(&image, 0, sizeof image);
  image.owner = owner; image.ordinal = ordinal; image.source_name = "generated"; image.width = w; image.height = h;
  return image;
}
static void upload_rgba(qa_software_renderer *r, qa_renderer_image image, const uint8_t px[4]) {
  qa_image_level level = {1, 1, {px, 4}};
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_CREATE; op.image = image; op.content.kind = QA_RENDER_IMAGE_RGBA8;
  op.content.levels = &level; op.content.level_count = 1; op.sampling.wrap = "repeat"; op.sampling.filter = "nearest";
  qa_software_renderer_apply_image_resource(r, op);
}
static void fixture_open(fixture *f, double alpha_bits) {
  CHECK(qa_identity_owner_create(&f->identity, "cpu test"));
  memset(&f->owner, 0, sizeof f->owner);
  f->owner.identity = f; f->owner.session = f->identity.session; f->owner.generation = 0;
  CHECK(qa_software_renderer_init(&f->renderer, 8, 8, f->owner, 8, 8, alpha_bits));
  f->white = make_image(f->owner, 0, 1, 1);
  static const uint8_t px[4] = {255, 255, 255, 255};
  upload_rgba(&f->renderer, f->white, px);
}
static void fixture_close(fixture *f) { qa_software_renderer_close(&f->renderer); qa_identity_owner_destroy(&f->identity); }
typedef struct tri { qa_render_vertex v[3]; double i[3]; } tri;
static qa_draw_batch triangle(tri *t, qa_renderer_image image, double z, qa_vec4 color) {
  static const double xs[3] = {-1, 1, 0}, ys[3] = {-1, -1, 1};
  for (int i = 0; i < 3; i++) { t->v[i].position = v4(xs[i], ys[i], z, 1); t->v[i].tex_coord.x = 0.5; t->v[i].tex_coord.y = 0.5; t->v[i].color = color; t->i[i] = i; }
  qa_draw_batch b; memset(&b, 0, sizeof b);
  b.lighting.kind = "vertex"; b.indices.values = t->i; b.indices.count = 3; b.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b.texture.image = image;
  b.state = QA_CPU_OPAQUE_STATE; b.vertices = t->v; b.vertex_count = 3;
  return b;
}
static void pixel(qa_software_renderer *r, uint8_t out[4]) { memcpy(out, qa_software_renderer_pixels(r, NULL) + (4 * 8 + 4) * 4, 4); }
static void clear_view(qa_software_renderer *r, qa_rect viewport, const qa_vec4 *plane, qa_vec4 color, double depth, bool stencil) {
  qa_render_view_state view; memset(&view, 0, sizeof view);
  view.viewport = viewport; view.clip_plane = plane;
  qa_render_view_clear clear; clear.depth = depth; clear.color = &color; clear.stencil = stencil; view.clear = &clear;
  qa_software_renderer_begin_view(r, view);
}
static qa_rect rect(double x, double y, double w, double h) { qa_rect o; o.x = x; o.y = y; o.width = w; o.height = h; return o; }

typedef struct composite_ctx { int sentinel; fixture *f; } composite_ctx;
#define SENTINEL 0x7a11
static void composite_draw(void *ctx) {
  composite_ctx *c = (composite_ctx *)ctx; CHECK(c->sentinel == SENTINEL); qa_software_renderer *r = &c->f->renderer;
  tri t; qa_draw_batch first = triangle(&t, c->f->white, 0, v4(0.5, 0.5, 0.5, 1));
  first.state.blend.source = "dst-color"; first.state.blend.destination = "zero";
  qa_software_renderer_draw(r, first);
  qa_software_renderer_draw(r, triangle(&t, c->f->white, 0.2, v4(0, 1, 0, 1)));
  qa_draw_batch second = triangle(&t, c->f->white, 0, v4(0.2, 0.1, 0, 1));
  second.state.blend.source = "one"; second.state.blend.destination = "one";
  qa_software_renderer_draw(r, second);
  CHECK(qa_software_renderer_read_depth_pixel(r, 4, 3) == 0.5);
}
static void test_opacity_composites(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  clear_view(r, rect(0, 0, 8, 8), NULL, v4(0.2, 0.4, 0.6, 1), 0.9, true);
  qa_software_renderer_set_overdraw_measurement(r, true);
  composite_ctx c = {SENTINEL, &f};
  qa_software_renderer_with_object_opacity(r, 0.5, composite_draw, &c);
  PIXEL_IS(r, 64, 90, 115, 255);
  CHECK(qa_software_renderer_read_depth_pixel(r, 4, 3) == (double)(float)0.9);
  uint8_t stencil[64]; qa_software_renderer_read_stencil_overdraw(r, stencil, sizeof stencil);
  bool zero = true; for (int i = 0; i < 64; i++) if (stencil[i]) zero = false;
  CHECK(zero);
  tri t; qa_software_renderer_draw(r, triangle(&t, f.white, 0.4, v4(0, 0, 1, 1)));
  PIXEL_IS(r, 0, 0, 255, 255);
  fixture_close(&f);
}
/* Dynamic upload scopes. A TS draw closure can throw and unwind out of the scope;
 * C throws abort, so the "late object failure" part is covered as abort-free. */
typedef struct dyn_ctx { int sentinel; int uploads; qa_renderer_image white; qa_draw_batch batch; qa_software_renderer *r; } dyn_ctx;
static qa_renderer_image dyn_resolve(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  dyn_ctx *d = (dyn_ctx *)ctx; CHECK(d->sentinel == SENTINEL);
  d->uploads++;
  static const uint8_t red[4] = {255, 0, 0, 255};
  static const qa_image_level level = {1, 1, {red, 4}};
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_UPDATE; op.image = d->white; op.level = 0; op.update_level = &level;
  apply(apply_ctx, op);
  return d->white;
}
static void dyn_draw(void *ctx) { dyn_ctx *d = (dyn_ctx *)ctx; qa_software_renderer_draw(d->r, d->batch); }
static void test_opacity_dynamic_uploads(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  dyn_ctx d; memset(&d, 0, sizeof d); d.sentinel = SENTINEL; d.white = f.white; d.r = r;
  tri t; d.batch = triangle(&t, f.white, 0, v4(1, 1, 1, 1));
  d.batch.texture.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE; d.batch.texture.source.ctx = &d; d.batch.texture.source.resolve = dyn_resolve;
  qa_cpu_render_target target; qa_cpu_render_target_init(&target, r, NULL);
  qa_render_operation op; memset(&op, 0, sizeof op); op.kind = QA_RENDER_OP_OBJECT_OPACITY; op.opacity = 0; op.batches = &d.batch; op.batch_count = 1;
  qa_render_command command; memset(&command, 0, sizeof command); command.kind = QA_RENDER_COMMAND_VIEW;
  command.view.state.viewport = rect(0, 0, 8, 8); command.view.operations = &op; command.view.operation_count = 1;
  qa_render_frame frame; memset(&frame, 0, sizeof frame); frame.owner = f.owner; frame.commands = &command; frame.command_count = 1;
  qa_cpu_render_target_execute(&target, frame);
  CHECK(d.uploads == 0);
  PIXEL_IS(r, 0, 0, 0, 0);
  qa_software_renderer_with_object_opacity(r, 0.5, dyn_draw, &d);
  CHECK(d.uploads == 1);
  PIXEL_IS(r, 128, 0, 0, 128);
  op.opacity = 1;
  qa_cpu_render_target_execute(&target, frame);
  CHECK(d.uploads == 2);
  PIXEL_IS(r, 255, 0, 0, 255);
  CHECK(qa_software_renderer_read_depth_pixel(r, 4, 3) == 0.5);
  fixture_close(&f);
}
static void noop_draw(void *ctx) { (void)ctx; }
typedef struct line_ctx { fixture *f; bool nested_checked; } line_ctx;
static void line_draw(void *ctx) {
  line_ctx *c = (line_ctx *)ctx; qa_software_renderer *r = &c->f->renderer;
  tri t; qa_draw_batch line = triangle(&t, c->f->white, 0, v4(1, 0, 0, 1));
  for (int i = 0; i < 3; i++) t.v[i].position.y = 0;
  line.primitive_is_lines = true; line.line_width = 1; line.indices.count = 2;
  qa_software_renderer_draw(r, line);
  CHECK_ABORTS(qa_software_renderer_with_object_opacity(r, 0.5, noop_draw, NULL));
  c->nested_checked = true;
}
static void test_opacity_viewport_lines_nesting(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  clear_view(r, rect(4, 0, 4, 8), NULL, v4(0, 0, 1, 1), 1, true);
  line_ctx c = {&f, false};
  qa_software_renderer_with_object_opacity(r, 0.5, line_draw, &c);
  CHECK(c.nested_checked);
  int faded = 0;
  const uint8_t *px = qa_software_renderer_pixels(r, NULL);
  for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
    const uint8_t *o = px + (y * 8 + x) * 4;
    if (x < 4) CHECK(o[0] == 0 && o[1] == 0 && o[2] == 0 && o[3] == 0);
    if (o[0] == 128) faded++;
    CHECK(qa_software_renderer_read_depth_pixel(r, x, 7 - y) == 1);
  }
  CHECK(faded > 0);
  double invalid[4] = {NAN, INFINITY, -0.1, 1.1};
  for (int i = 0; i < 4; i++) CHECK_ABORTS(qa_software_renderer_with_object_opacity(r, invalid[i], noop_draw, NULL));
  fixture_close(&f);
}
typedef struct portal_ctx { fixture *f; } portal_ctx;
static void portal_draw(void *ctx) { portal_ctx *c = (portal_ctx *)ctx; tri t; qa_software_renderer_draw(&c->f->renderer, triangle(&t, c->f->white, 0, v4(1, 0, 0, 0.2))); }
static void test_opacity_portal_alpha(void) {
  double bits[2] = {0, 8};
  for (int b = 0; b < 2; b++) {
    fixture f; fixture_open(&f, bits[b]); qa_software_renderer *r = &f.renderer;
    qa_vec4 plane = v4(1, 0, 0, 0);
    clear_view(r, rect(0, 0, 8, 8), &plane, v4(0, 0, 1, 0.4), 1, true);
    portal_ctx c = {&f};
    qa_software_renderer_with_object_opacity(r, 0.5, portal_draw, &c);
    PIXEL_IS(r, 128, 0, 128, bits[b] == 0 ? 255 : 77);
    const uint8_t *o = qa_software_renderer_pixels(r, NULL) + (4 * 8 + 2) * 4;
    CHECK(o[0] == 0 && o[1] == 0 && o[2] == 255 && o[3] == (bits[b] == 0 ? 255 : 102));
    fixture_close(&f);
  }
}
static void test_triangles_depth(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_software_renderer_draw(r, triangle(&t, f.white, 0, v4(1, 0, 0, 1)));
  qa_software_renderer_draw(r, triangle(&t, f.white, 0.5, v4(0, 1, 0, 1)));
  PIXEL_IS(r, 255, 0, 0, 255);
  CHECK(qa_software_renderer_read_depth_pixel(r, 4, 3) == 0.5);
  fixture_close(&f);
}
static void test_paired_material(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  qa_renderer_image image = make_image(f.owner, 1, 1, 1);
  static const uint8_t px[4] = {128, 64, 32, 255};
  upload_rgba(r, image, px);
  tri t; qa_draw_batch base = triangle(&t, f.white, 0, v4(1, 1, 1, 1));
  qa_multitexture_vertex pair[3];
  for (int i = 0; i < 3; i++) { pair[i].vertex = t.v[i]; pair[i].tex_coord2 = t.v[i].tex_coord; }
  base.texturing_is_pair = true; base.vertices = NULL; base.vertex_count = 0; base.pair_vertices = pair; base.pair_vertex_count = 3;
  base.second_texture.binding.kind = QA_TEXTURE_BINDING_BIND_IMAGE; base.second_texture.binding.image = image; base.second_texture.environment = "modulate";
  qa_software_renderer_draw(r, base);
  PIXEL_IS(r, 128, 64, 32, 255);
  qa_cpu_render_target target; qa_cpu_render_target_init(&target, r, NULL);
  qa_render_command commands[3]; memset(commands, 0, sizeof commands);
  commands[0].kind = QA_RENDER_COMMAND_SET_COLOR; commands[0].color = v4(1, 0, 0, 0.5);
  commands[1].kind = QA_RENDER_COMMAND_STRETCH_PIC; commands[1].image = f.white; commands[1].rect = rect(0, 0, 8, 8);
  commands[1].uv.s1 = 0; commands[1].uv.t1 = 0; commands[1].uv.s2 = 1; commands[1].uv.t2 = 1;
  commands[2].kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_render_frame frame; memset(&frame, 0, sizeof frame); frame.owner = f.owner; frame.commands = commands; frame.command_count = 3;
  qa_cpu_render_target_execute(&target, frame);
  PIXEL_IS(r, 192, 32, 16, 191);
  fixture_close(&f);
}
/* TS evaluates a parsed Q3 script down to this batch color; see the final report. */
static void test_constant_material_pixels(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_software_renderer_draw(r, triangle(&t, f.white, 0, v4(63.0 / 255, 127.0 / 255, 1, 1)));
  PIXEL_IS(r, 63, 127, 255, 255);
  fixture_close(&f);
}
static void test_source_primitives(void) {
  for (double primitives = 1; primitives <= 3; primitives++) {
    fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
    qa_draw_batch batch = triangle(&t, f.white, 0, v4(0, 1, 0, 1));
    qa_source_stage_cell scratch[3]; memset(scratch, 0, sizeof scratch);
    for (int i = 0; i < 3; i++) { scratch[i].color = t.v[i].color; scratch[i].tex_coord = t.v[i].tex_coord; scratch[i].raw_tex_coord = t.v[i].tex_coord; }
    qa_software_renderer_begin_source_iterator(r, true, scratch, 3);
    qa_source_stage_data stage; memset(&stage, 0, sizeof stage);
    stage.kind = "generic-single"; stage.state_bits = QA_SOURCE_STATE_DEFAULT; stage.batch = batch; stage.scratch = scratch; stage.scratch_count = 3;
    qa_prepared_source_draw draw = qa_software_renderer_prepare_source_geometry(r, &stage);
    draw.begin(draw.ctx); draw.prepare_texture(draw.ctx, 0); draw.apply_texture(draw.ctx, 0, batch.texture); draw.finish_textures(draw.ctx); draw.draw(draw.ctx, primitives); draw.cleanup(draw.ctx);
    PIXEL_IS(r, 0, 255, 0, 255);
    fixture_close(&f);
  }
}
/* Source state, pair and lifecycle branches beyond the TS case. */
static void test_source_pair_and_lifecycle(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_renderer_image lightmap = make_image(f.owner, 2, 1, 1);
  static const uint8_t px[4] = {128, 128, 128, 255};
  upload_rgba(r, lightmap, px);
  qa_draw_batch batch = triangle(&t, f.white, 0, v4(1, 0, 0, 1));
  qa_multitexture_vertex pair[3];
  for (int i = 0; i < 3; i++) { pair[i].vertex = t.v[i]; pair[i].tex_coord2 = t.v[i].tex_coord; }
  batch.texturing_is_pair = true; batch.vertices = NULL; batch.vertex_count = 0; batch.pair_vertices = pair; batch.pair_vertex_count = 3;
  batch.second_texture.binding.kind = QA_TEXTURE_BINDING_BIND_IMAGE; batch.second_texture.binding.image = lightmap; batch.second_texture.environment = "modulate";
  qa_source_stage_cell scratch[3]; memset(scratch, 0, sizeof scratch);
  for (int i = 0; i < 3; i++) { scratch[i].color = t.v[i].color; scratch[i].tex_coord = t.v[i].tex_coord; scratch[i].tex_coord2 = t.v[i].tex_coord; scratch[i].raw_tex_coord = t.v[i].tex_coord; scratch[i].raw_tex_coord2 = t.v[i].tex_coord; }
  qa_software_renderer_begin_source_iterator(r, false, scratch, 3);
  qa_source_stage_data stage; memset(&stage, 0, sizeof stage);
  stage.kind = "lightmapped-pair"; stage.state_bits = QA_SOURCE_STATE_DEFAULT; stage.batch = batch; stage.scratch = scratch; stage.scratch_count = 3;
  qa_prepared_source_draw draw = qa_software_renderer_prepare_source_geometry(r, &stage);
  draw.begin(draw.ctx);
  draw.prepare_texture(draw.ctx, 0); draw.apply_texture(draw.ctx, 0, batch.texture);
  draw.prepare_texture(draw.ctx, 1); draw.apply_texture(draw.ctx, 1, batch.second_texture.binding);
  draw.finish_textures(draw.ctx); draw.draw(draw.ctx, 2); draw.cleanup(draw.ctx);
  PIXEL_IS(r, 128, 0, 0, 255);
  /* scratch mismatch and draw-before-finish reject */
  stage.scratch_count = 2;
  CHECK_ABORTS((void)qa_software_renderer_prepare_source_geometry(r, &stage));
  stage.scratch_count = 3;
  qa_prepared_source_draw early = qa_software_renderer_prepare_source_geometry(r, &stage);
  CHECK_ABORTS(early.draw(early.ctx, 2));
  CHECK_ABORTS(early.prepare_texture(early.ctx, 0));
  fixture_close(&f);
}
static void test_prepared_order_and_validation(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_draw_batch batch = triangle(&t, f.white, 0, v4(1, 0, 0, 1));
  qa_prepared_backend_draw d = qa_software_renderer_prepare_geometry(r, batch);
  CHECK_ABORTS(d.draw(d.ctx));
  CHECK_ABORTS(d.apply_texture(d.ctx, 0, batch.texture));
  d.begin(d.ctx);
  CHECK_ABORTS(d.begin(d.ctx));
  CHECK_ABORTS(d.apply_texture(d.ctx, 1, batch.texture));
  CHECK_ABORTS(d.cleanup(d.ctx));
  d.apply_texture(d.ctx, 0, batch.texture); d.draw(d.ctx); d.cleanup(d.ctx);
  PIXEL_IS(r, 255, 0, 0, 255);
  /* the batch is a snapshot: vertices changed after prepare do not draw */
  qa_software_renderer_draw(r, triangle(&t, f.white, -0.5, v4(0, 1, 0, 1)));
  batch = triangle(&t, f.white, -0.9, v4(0, 0, 1, 1));
  qa_prepared_backend_draw s = qa_software_renderer_prepare_geometry(r, batch);
  for (int i = 0; i < 3; i++) t.v[i].color = v4(1, 1, 1, 1);
  s.begin(s.ctx); s.apply_texture(s.ctx, 0, batch.texture); s.draw(s.ctx); s.cleanup(s.ctx);
  PIXEL_IS(r, 0, 0, 255, 255);
  /* validation */
  double bad_index[3] = {0, 1, 9}; qa_draw_batch b = batch; b.indices.values = bad_index;
  CHECK_ABORTS(qa_software_renderer_draw(r, b));
  b = batch; b.indices.count = 2;
  CHECK_ABORTS(qa_software_renderer_draw(r, b));
  tri n; b = triangle(&n, f.white, 0, v4(1, 1, 1, 1)); n.v[1].position.x = NAN;
  CHECK_ABORTS(qa_software_renderer_draw(r, b));
  b = batch; b.primitive_is_lines = true; b.line_width = 0; b.indices.count = 2;
  CHECK_ABORTS(qa_software_renderer_draw(r, b));
  b = batch; b.space = "world";
  CHECK_ABORTS(qa_software_renderer_draw(r, b)); /* no view projection */
  CHECK_ABORTS(qa_software_renderer_select_draw_buffer(r, "back-left", false));
  CHECK_ABORTS(qa_software_renderer_read_depth_pixel(r, 8, 0));
  fixture_close(&f);
  CHECK_ABORTS(qa_software_renderer_finish(&f.renderer));
}
/* World-space batches project through the view's matrix. */
static void test_world_space(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_render_view_state view; memset(&view, 0, sizeof view);
  view.viewport = rect(0, 0, 8, 8); view.has_view_projection = true; view.view_projection = qa_identity_mat4();
  qa_software_renderer_begin_view(r, view);
  qa_draw_batch b = triangle(&t, f.white, 0, v4(0, 1, 0, 1)); b.space = "world";
  qa_software_renderer_draw(r, b);
  PIXEL_IS(r, 0, 255, 0, 255);
  fixture_close(&f);
}
static void test_retained_state_ops(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  qa_render_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_RENDER_OP_DEPTH_RANGE; op.range[0] = 0.25; op.range[1] = 0.75; qa_software_renderer_draw_immediate(r, op);
  /* show-image uses the retained state */
  qa_software_renderer_draw_show_image(r, f.white, rect(0, 0, 8, 8), false);
  PIXEL_IS(r, 255, 255, 255, 255);
  CHECK(fabs(qa_software_renderer_read_depth_pixel(r, 4, 3) - 0.25) < 1e-6);
  fixture_close(&f);
}
static void test_sky_and_shadow_ops(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  qa_vertex_position_tex_coord strip[4];
  double xs[4] = {-1, -1, 1, 1}, ys[4] = {1, -1, 1, -1};
  for (int i = 0; i < 4; i++) { strip[i].position = v4(xs[i], ys[i], 0, 1); strip[i].tex_coord.x = 0; strip[i].tex_coord.y = 0; }
  qa_sky_strip sky; sky.vertices = strip; sky.vertex_count = 4;
  qa_render_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_RENDER_OP_SKY_SIDE; op.image = f.white; op.color = v4(0, 0.5, 1, 1); op.strips = &sky; op.strip_count = 1;
  qa_software_renderer_draw_immediate(r, op);
  PIXEL_IS(r, 0, 128, 255, 255);
  sky.vertex_count = 3;
  CHECK_ABORTS(qa_software_renderer_draw_immediate(r, op));
  sky.vertex_count = 4;
  /* This winding survives the "back" cull pass only, so stencil increments once
   * and the decrement pass culls it; the finish pass then modulates by 0.6. */
  qa_vec4 volume[3] = {v4(-1, -1, -0.5, 1), v4(1, -1, -0.5, 1), v4(0, 1, -0.5, 1)};
  double indices[3] = {0, 1, 2};
  memset(&op, 0, sizeof op);
  op.kind = QA_RENDER_OP_SHADOW_VOLUME; op.positions = volume; op.position_count = 3; op.indices.values = indices; op.indices.count = 3; op.white_image = f.white;
  qa_software_renderer_draw_immediate(r, op);
  PIXEL_IS(r, 0, 128, 255, 255); /* color write masked */
  memset(&op, 0, sizeof op);
  op.kind = QA_RENDER_OP_SHADOW_FINISH; op.white_image = f.white;
  op.finish_positions[0] = v4(-1, 1, -1, 1); op.finish_positions[1] = v4(1, 1, -1, 1); op.finish_positions[2] = v4(1, -1, -1, 1); op.finish_positions[3] = v4(-1, -1, -1, 1);
  qa_software_renderer_draw_immediate(r, op);
  PIXEL_IS(r, 0, 77, 153, 255);
  /* Pixels outside the volume keep a zero stencil and stay undarkened. */
  const uint8_t *corner = qa_software_renderer_pixels(r, NULL);
  CHECK(corner[0] == 0 && corner[1] == 128 && corner[2] == 255);
  fixture_close(&f);
}
static void test_q1_fog(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_mat4 projection = qa_perspective_mat4(90, 1, 1, 128);
  double eye_z = -64, clip_z = projection.values[10] * eye_z + projection.values[14], w = -eye_z;
  qa_software_renderer_draw(r, triangle(&t, f.white, clip_z / w, v4(1, 0, 0, 1)));
  qa_q1_fog fog; fog.density = 1; fog.color.x = 0; fog.color.y = 0; fog.color.z = 1;
  qa_software_renderer_apply_q1_fog(r, projection, fog, 0, "classic-indexed");
  PIXEL_IS(r, 255, 0, 0, 255);
  qa_software_renderer_apply_q1_fog(r, projection, fog, 0, "truecolor");
  PIXEL_IS(r, 94, 0, 161, 255);
  fixture_close(&f);
}
static void test_depth_atlas_shadows(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_renderer_image image = make_image(f.owner, 1, 8, 8);
  float depth_pixels[64]; for (int i = 0; i < 64; i++) depth_pixels[i] = 1;
  qa_depth_image_level depth_level; depth_level.width = 8; depth_level.height = 8; depth_level.pixels = depth_pixels; depth_level.pixel_count = 64;
  qa_image_resource_operation create; memset(&create, 0, sizeof create);
  create.kind = QA_IMAGE_OP_CREATE; create.image = image; create.content.kind = QA_RENDER_IMAGE_DEPTH32F; create.content.depth_levels = &depth_level; create.content.depth_level_count = 1;
  create.sampling.filter = "nearest"; create.sampling.wrap = "clamp";
  qa_software_renderer_apply_image_resource(r, create);
  qa_q2_shadow_atlas atlas; atlas.image = image; atlas.texel_size = 1.0 / 8; atlas.near_plane = 4;
  qa_q2_fragment_light light; memset(&light, 0, sizeof light);
  light.origin.z = 64; light.radius = 128; light.color.x = 1; light.scale = 1; light.shadow.kind = "cone"; light.shadow.matrix = qa_identity_mat4(); light.shadow.atlas_rect = v4(0, 0, 1, 1);
  qa_vec3 points[3], normals[3];
  for (int i = 0; i < 3; i++) { points[i].x = 0.5; points[i].y = 0.5; points[i].z = 0.75; normals[i].x = 0; normals[i].y = 0; normals[i].z = 1; }
  qa_draw_batch batch = triangle(&t, f.white, 0, v4(0, 0, 0, 1));
  batch.lighting.kind = "q2-world"; batch.lighting.world_positions = points; batch.lighting.world_position_count = 3; batch.lighting.normals = normals; batch.lighting.normal_count = 3;
  batch.lighting.pass = "texture"; batch.lighting.lights = &light; batch.lighting.light_count = 1; batch.lighting.atlas = &atlas;
  qa_software_renderer_draw(r, batch);
  uint8_t px[4]; pixel(r, px); CHECK(px[0] > 60);
  static const double positions[16] = {-1, -1, -0.5, 1, 1, -1, -0.5, 1, 1, 1, -0.5, 1, -1, 1, -0.5, 1};
  static const double indices[6] = {0, 1, 2, 0, 2, 3};
  qa_depth_atlas_draw draw; memset(&draw, 0, sizeof draw);
  draw.positions = positions; draw.position_count = 16; draw.indices.values = indices; draw.indices.count = 6; draw.cull = "none";
  qa_depth_atlas_pass pass; memset(&pass, 0, sizeof pass);
  pass.viewport = rect(0, 0, 8, 8); pass.has_clear_depth = true; pass.clear_depth = 1; pass.draws = &draw; pass.draw_count = 1;
  qa_render_operation op; memset(&op, 0, sizeof op); op.kind = QA_RENDER_OP_DEPTH_ATLAS; op.image = image; op.passes = &pass; op.pass_count = 1;
  qa_software_renderer_draw_immediate(r, op);
  qa_software_renderer_draw(r, batch);
  PIXEL_IS(r, 0, 0, 0, 255);
  tri t2; qa_draw_batch model = triangle(&t2, f.white, 0, v4(0.5, 0.5, 0.5, 1));
  qa_q2_model_shadow_light model_light; memset(&model_light, 0, sizeof model_light);
  model_light.origin = light.origin; model_light.radius = light.radius; model_light.shadow = light.shadow; model_light.fraction.x = 0.75; model_light.fraction.y = 0.5; model_light.fraction.z = 0;
  model.lighting.kind = "q2-model-shadow"; model.lighting.world_positions = points; model.lighting.world_position_count = 3; model.lighting.has_shade_scale = true; model.lighting.shade_scale = 4;
  model.lighting.atlas_value = atlas; model.lighting.model_shadow_lights = &model_light; model.lighting.model_shadow_light_count = 1;
  qa_software_renderer_draw(r, model);
  PIXEL_IS(r, 128, 255, 255, 255);
  /* lights over eight reject */
  qa_q2_fragment_light many[9]; for (int i = 0; i < 9; i++) many[i] = light;
  qa_draw_batch over = batch; over.lighting.lights = many; over.lighting.light_count = 9;
  CHECK_ABORTS(qa_software_renderer_draw(r, over));
  fixture_close(&f);
}
static void test_q2_fog(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer; tri t;
  qa_mat4 projection = qa_perspective_mat4(90, 1, 1, 128);
  qa_software_renderer_draw(r, triangle(&t, f.white, (projection.values[10] * -64 + projection.values[14]) / 64, v4(1, 0, 0, 1)));
  qa_render_operation op; memset(&op, 0, sizeof op); op.kind = QA_RENDER_OP_Q2_FOG;
  op.fog.camera.axis.vectors[0].z = 1; op.fog.camera.axis.vectors[1].y = 1; op.fog.camera.axis.vectors[2].x = -1;
  op.fog.camera.projection = projection; op.fog.camera.viewport = rect(0, 0, 8, 8);
  op.fog.far_depth = 1 - 1e-6; op.fog.sky_drawn = false;
  op.fog.fog.kind = "q2"; op.fog.fog.color.z = 1; op.fog.fog.density = 1; op.fog.fog.sky_factor = 0.25;
  op.fog.fog.height.start.color.x = 1; op.fog.fog.height.end.color.y = 1; op.fog.fog.height.end.distance = 128; op.fog.fog.height.density = 0.02; op.fog.fog.height.falloff = 1;
  qa_software_renderer_draw_immediate(r, op);
  /* Source global blend gives [95,0,160,195]; height tint includes extinction. */
  PIXEL_IS(r, 89, 37, 87, 159);
  const uint8_t *px = qa_software_renderer_pixels(r, NULL);
  CHECK(px[0] == 0 && px[1] == 0 && px[2] == 0 && px[3] == 0);
  double depth = qa_software_renderer_read_depth_pixel(r, 4, 3);
  op.fog.sky_drawn = true; op.fog.fog.density = 0; op.fog.fog.height.density = 0;
  qa_software_renderer_draw_immediate(r, op);
  px = qa_software_renderer_pixels(r, NULL);
  CHECK(px[0] == 0 && px[1] == 0 && px[2] == 64 && px[3] == 16);
  PIXEL_IS(r, 89, 37, 87, 159);
  CHECK(qa_software_renderer_read_depth_pixel(r, 4, 3) == depth);
  fixture_close(&f);
}
static void test_output_gamma(void) {
  fixture f; fixture_open(&f, 8); qa_software_renderer *r = &f.renderer;
  clear_view(r, rect(0, 0, 8, 8), NULL, v4(64.0 / 255, 64.0 / 255, 64.0 / 255, 0.25), 1, false);
  const uint8_t *neutral = qa_software_renderer_pixels(r, NULL);
  qa_software_renderer_set_output_gamma(r, 1); qa_software_renderer_finish(r);
  CHECK(qa_software_renderer_pixels(r, NULL) == neutral);
  const uint8_t *p = qa_software_renderer_pixels(r, NULL);
  CHECK(p[0] == 64 && p[1] == 64 && p[2] == 64 && p[3] == 64);
  qa_software_renderer_set_output_gamma(r, 2); qa_software_renderer_finish(r);
  p = qa_software_renderer_pixels(r, NULL);
  CHECK(p[0] == 128 && p[1] == 128 && p[2] == 128 && p[3] == 64);
  qa_image_level level; CHECK(qa_software_renderer_read_rgba(r, &level));
  CHECK(memcmp(level.pixels.bytes, qa_software_renderer_pixels(r, NULL), level.pixels.length) == 0);
  free((void *)level.pixels.bytes);
  clear_view(r, rect(0, 0, 8, 8), NULL, v4(16.0 / 255, 16.0 / 255, 16.0 / 255, 0.25), 1, false);
  qa_cpu_render_target target; qa_cpu_render_target_init(&target, r, NULL); qa_cpu_render_target_present(&target);
  p = qa_software_renderer_pixels(r, NULL);
  CHECK(p[0] == 64 && p[1] == 64 && p[2] == 64 && p[3] == 64);
  qa_software_renderer_set_output_gamma(r, 1);
  CHECK(qa_software_renderer_pixels(r, NULL) == neutral);
  p = qa_software_renderer_pixels(r, NULL);
  CHECK(p[0] == 16 && p[1] == 16 && p[2] == 16 && p[3] == 64);
  CHECK_ABORTS(qa_software_renderer_set_output_gamma(r, NAN));
  fixture_close(&f);
}

/* row-parallel.test.ts */
#define WIDTH 320
#define HEIGHT 240
typedef struct row_result { uint8_t *pixels; double batches; double depths[64]; int depth_count; } row_result;
static row_result render_rows(const char *workers, bool fragment_lit) {
  setenv("QTS_CPU_WORKERS", workers, 1);
  qa_identity_owner identity; CHECK(qa_identity_owner_create(&identity, "cpu parallel"));
  qa_renderer_resource_owner owner; memset(&owner, 0, sizeof owner); owner.identity = &identity; owner.session = identity.session;
  qa_software_renderer renderer; CHECK(qa_software_renderer_init(&renderer, WIDTH, HEIGHT, owner, 8, 8, 8));
  qa_renderer_image image = make_image(owner, 0, 4, 4);
  uint8_t pixels[64];
  for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
    int o = (y * 4 + x) * 4, bright = (x + y) % 2 == 0 ? 255 : 96;
    pixels[o] = (uint8_t)bright; pixels[o + 1] = (uint8_t)(255 - bright); pixels[o + 2] = 160; pixels[o + 3] = 255;
  }
  qa_image_level level = {4, 4, {pixels, 64}};
  qa_image_resource_operation create; memset(&create, 0, sizeof create);
  create.kind = QA_IMAGE_OP_CREATE; create.image = image; create.content.kind = QA_RENDER_IMAGE_RGBA8; create.content.levels = &level; create.content.level_count = 1;
  create.sampling.filter = "nearest"; create.sampling.wrap = "repeat";
  qa_software_renderer_apply_image_resource(&renderer, create);
  clear_view(&renderer, rect(0, 0, WIDTH, HEIGHT), NULL, v4(0.1, 0.2, 0.3, 1), 1, true);
  enum { COLS = 16, ROWS = 12 };
  static qa_render_vertex vertices[COLS * ROWS * 4]; static double indices[COLS * ROWS * 6];
  static qa_vec3 world[COLS * ROWS * 4], normals[COLS * ROWS * 4];
  int nv = 0, ni = 0;
  for (int row = 0; row < ROWS; row++) for (int col = 0; col < COLS; col++) {
    double x0 = -1 + ((double)col / COLS) * 2, x1 = -1 + ((col + 1.7) / COLS) * 2;
    double y0 = -1 + ((double)row / ROWS) * 2, y1 = -1 + ((row + 1.7) / ROWS) * 2;
    double z = ((col * 31 + row * 17) % 89) / 89.0 * 0.8 - 0.4;
    qa_vec4 color = v4(((col * 53) % 101) / 100.0, ((row * 41) % 103) / 102.0, ((col + row * 7) % 97) / 96.0, 1);
    int base = nv;
    double corners[4][4] = {{x0, y0, 0, 0}, {x1, y0, 2, 0}, {x0, y1, 0, 2}, {x1, y1, 2, 2}};
    for (int c = 0; c < 4; c++) {
      vertices[nv].position = v4(corners[c][0], corners[c][1], z, 1); vertices[nv].tex_coord.x = corners[c][2]; vertices[nv].tex_coord.y = corners[c][3]; vertices[nv].color = color;
      world[nv].x = corners[c][0] * 32; world[nv].y = corners[c][1] * 32; world[nv].z = z * 32 + 16; normals[nv].x = 0; normals[nv].y = 0; normals[nv].z = 1;
      nv++;
    }
    int quad[6] = {base, base + 1, base + 2, base + 1, base + 3, base + 2};
    for (int i = 0; i < 6; i++) indices[ni++] = quad[i];
  }
  qa_draw_batch batch; memset(&batch, 0, sizeof batch);
  batch.lighting.kind = "vertex"; batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; batch.texture.image = image; batch.indices.values = indices; batch.indices.count = (size_t)ni;
  batch.state = QA_CPU_OPAQUE_STATE; batch.vertices = vertices; batch.vertex_count = (size_t)nv;
  qa_q2_fragment_light light; qa_q2_shadow_atlas atlas; float depth_pixels[64];
  if (fragment_lit) {
    qa_renderer_image depth_image = make_image(owner, 1, 8, 8);
    for (int i = 0; i < 64; i++) depth_pixels[i] = 0.9f;
    qa_depth_image_level depth_level; depth_level.width = 8; depth_level.height = 8; depth_level.pixels = depth_pixels; depth_level.pixel_count = 64;
    qa_image_resource_operation depth_create; memset(&depth_create, 0, sizeof depth_create);
    depth_create.kind = QA_IMAGE_OP_CREATE; depth_create.image = depth_image; depth_create.content.kind = QA_RENDER_IMAGE_DEPTH32F; depth_create.content.depth_levels = &depth_level; depth_create.content.depth_level_count = 1;
    depth_create.sampling.filter = "nearest"; depth_create.sampling.wrap = "clamp";
    qa_software_renderer_apply_image_resource(&renderer, depth_create);
    atlas.image = depth_image; atlas.texel_size = 1.0 / 8; atlas.near_plane = 4;
    memset(&light, 0, sizeof light); light.origin.z = 64; light.radius = 128; light.color.x = 1; light.color.y = 0.5; light.color.z = 0.25; light.scale = 1;
    light.shadow.kind = "cone"; light.shadow.matrix = qa_identity_mat4(); light.shadow.atlas_rect = v4(0, 0, 1, 1);
    batch.lighting.kind = "q2-world"; batch.lighting.pass = "texture"; batch.lighting.lights = &light; batch.lighting.light_count = 1; batch.lighting.atlas = &atlas;
    batch.lighting.world_positions = world; batch.lighting.world_position_count = (size_t)nv; batch.lighting.normals = normals; batch.lighting.normal_count = (size_t)nv;
  }
  qa_software_renderer_draw(&renderer, batch);
  row_result out; memset(&out, 0, sizeof out);
  for (int y = 0; y < HEIGHT; y += 37) for (int x = 0; x < WIDTH; x += 41) if (out.depth_count < 64) out.depths[out.depth_count++] = qa_software_renderer_read_depth_pixel(&renderer, x, y);
  qa_image_level read; CHECK(qa_software_renderer_read_rgba(&renderer, &read));
  out.pixels = (uint8_t *)(uintptr_t)read.pixels.bytes; out.batches = qa_software_renderer_row_parallel_batches(&renderer);
  qa_software_renderer_close(&renderer); qa_identity_owner_destroy(&identity);
  return out;
}
static void test_row_parallel(void) {
  bool lit[2] = {false, true};
  for (int i = 0; i < 2; i++) {
    row_result serial = render_rows("0", lit[i]), parallel = render_rows("3", lit[i]);
    CHECK(serial.batches == 0);
    CHECK(parallel.batches > 0);
    CHECK(memcmp(parallel.pixels, serial.pixels, (size_t)WIDTH * HEIGHT * 4) == 0);
    CHECK(serial.depth_count == parallel.depth_count && memcmp(parallel.depths, serial.depths, sizeof serial.depths) == 0);
    free(serial.pixels); free(parallel.pixels);
  }
  unsetenv("QTS_CPU_WORKERS");
}

int main(void) {
  test_opacity_composites();
  test_opacity_dynamic_uploads();
  test_opacity_viewport_lines_nesting();
  test_opacity_portal_alpha();
  test_triangles_depth();
  test_paired_material();
  test_constant_material_pixels();
  test_source_primitives();
  test_source_pair_and_lifecycle();
  test_prepared_order_and_validation();
  test_world_space();
  test_retained_state_ops();
  test_sky_and_shadow_ops();
  test_q1_fog();
  test_depth_atlas_shadows();
  test_q2_fog();
  test_output_gamma();
  test_row_parallel();
  printf("cpu rasterizer: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
