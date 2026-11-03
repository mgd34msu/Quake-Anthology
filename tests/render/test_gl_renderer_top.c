/*
 * Checks for src/render/gl/renderer.ts. tests/render/gl/smoke.test.ts and
 * buffer-reuse.test.ts run only on a live context, so their scenarios run here twice
 * in spirit: a recording fake GL covers call order, caches and error paths, and the
 * real section reproduces the pixel assertions on an SDL offscreen context when one
 * opens. The real section reports plainly when it cannot run. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fake_gl_top.h"
#include "qa/contracts/identity.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/platform/sdl.h"
#include "qa/render/gl/renderer.h"
#include "qa/render/output_gamma.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_THROWS(part, ...) do { checks++; int pipe_[2]; if (pipe(pipe_) != 0) abort(); pid_t pid_ = fork(); \
  if (pid_ == 0) { (void)prctl(PR_SET_DUMPABLE, 0); close(pipe_[0]); dup2(pipe_[1], 2); __VA_ARGS__; _exit(0); } \
  close(pipe_[1]); char text_[2048]; size_t n_ = 0; ssize_t got_; while (n_ + 1 < sizeof text_ && (got_ = read(pipe_[0], text_ + n_, sizeof text_ - 1 - n_)) > 0) n_ += (size_t)got_; text_[n_] = 0; close(pipe_[0]); \
  int status_ = 0; waitpid(pid_, &status_, 0); if (!WIFSIGNALED(status_) || strstr(text_, part) == NULL) { failures++; fprintf(stderr, "FAIL %s:%d: expected throw \"%s\", got \"%s\"\n", __FILE__, __LINE__, part, text_); } } while (0)

/* --- fake render context; a sentinel ctx proves each leaf receives its context --- */
static int window_sentinel;
#define WINDOW_CTX ((void *)&window_sentinel)
static double win_width = 16, win_height = 16;
static int swaps, current_calls, rendering_calls, retains, releases;
static void *fake_proc(void *ctx, const char *name) { CHECK(ctx == WINDOW_CTX); return fake_gl_proc_lookup(name); }
static void fake_release(void *ctx) { CHECK(ctx == WINDOW_CTX); releases++; }
static bool fake_retain(void *ctx, qa_gl_release_fn *out, void **out_ctx) { CHECK(ctx == WINDOW_CTX); retains++; *out = fake_release; *out_ctx = ctx; return true; }
static void fake_current(void *ctx) { CHECK(ctx == WINDOW_CTX); current_calls++; }
static void fake_size(void *ctx, double *w, double *h) { CHECK(ctx == WINDOW_CTX); *w = win_width; *h = win_height; }
static void fake_rendering(void *ctx, bool on) { CHECK(ctx == WINDOW_CTX); CHECK(on); rendering_calls++; }
static void fake_swap(void *ctx) { CHECK(ctx == WINDOW_CTX); swaps++; }
static qa_gl_render_context fake_window(void) {
  qa_gl_render_context w; memset(&w, 0, sizeof w);
  w.ctx = WINDOW_CTX; w.gl_context.ctx = WINDOW_CTX; w.gl_context.get_gl_proc_address = fake_proc; w.gl_context.retain_procedures = fake_retain; w.gl_context.make_current = fake_current;
  w.make_current = fake_current; w.drawable_size = fake_size; w.set_rendering_enabled = fake_rendering; w.swap = fake_swap;
  return w;
}
static qa_identity_owner identity;
static qa_renderer_resource_owner test_owner(void) {
  qa_renderer_resource_owner o; memset(&o, 0, sizeof o); o.identity = &identity; o.session = identity.session; return o;
}
static qa_renderer_image make_image(double ordinal, double w, double h, const char *name) {
  qa_renderer_image i; memset(&i, 0, sizeof i); i.owner = test_owner(); i.ordinal = ordinal; i.width = w; i.height = h; i.source_name = name; return i;
}
static qa_gl_renderer open_fake(void) {
  qa_gl_renderer r; memset(&r, 0, sizeof r);
  win_width = win_height = 16; swaps = 0;
  CHECK(qa_gl_renderer_init(&r, fake_window(), test_owner()));
  return r;
}
static void create_rgba(qa_gl_renderer *r, qa_renderer_image image, const uint8_t *pixels, const char *filter) {
  static qa_image_level level; level.width = image.width; level.height = image.height; level.pixels.bytes = pixels; level.pixels.length = (size_t)(image.width * image.height * 4);
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_CREATE; op.image = image; op.content.kind = QA_RENDER_IMAGE_RGBA8; op.content.levels = &level; op.content.level_count = 1;
  op.content.border_color = (qa_vec4){1, 1, 1, 1}; op.sampling.wrap = "repeat"; op.sampling.filter = filter;
  qa_gl_renderer_apply_image_resource(r, op);
}
static void create_depth(qa_gl_renderer *r, qa_renderer_image image, float fill) {
  size_t n = (size_t)(image.width * image.height);
  float *pixels = (float *)malloc(n * sizeof *pixels);
  for (size_t i = 0; i < n; i++) pixels[i] = fill;
  qa_depth_image_level level; memset(&level, 0, sizeof level); level.width = image.width; level.height = image.height; level.pixels = pixels; level.pixel_count = n;
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_CREATE; op.image = image; op.content.kind = QA_RENDER_IMAGE_DEPTH32F; op.content.depth_levels = &level; op.content.depth_level_count = 1;
  op.sampling.wrap = "clamp"; op.sampling.filter = "nearest";
  qa_gl_renderer_apply_image_resource(r, op);
  free(pixels);
}
/* TS smoke.test.ts vertices/state: full-screen quad at depth 0. */
static const qa_render_vertex QUAD[4] = {
  {{-1, -1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}}, {{1, -1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}},
  {{1, 1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}}, {{-1, 1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}}};
static const double QUAD_INDICES[6] = {0, 1, 2, 0, 2, 3};
static qa_render_state base_state(void) {
  qa_render_state s; memset(&s, 0, sizeof s);
  s.blend.source = "one"; s.blend.destination = "zero"; s.depth_test = "less-equal"; s.depth_write = true; s.alpha_test = "none"; s.cull = "none"; s.depth_range[0] = 0; s.depth_range[1] = 1;
  return s;
}
static qa_draw_batch quad_batch(qa_renderer_image image) {
  qa_draw_batch b; memset(&b, 0, sizeof b);
  b.indices.values = QUAD_INDICES; b.indices.count = 6; b.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b.texture.image = image;
  b.state = base_state(); b.lighting.kind = "vertex"; b.vertices = QUAD; b.vertex_count = 4;
  return b;
}
static void draw_batch(qa_gl_renderer *r, qa_draw_batch b) {
  qa_prepared_backend_draw d = qa_gl_renderer_prepare_geometry(r, b);
  d.begin(d.ctx); d.apply_texture(d.ctx, 0, b.texture);
  if (b.texturing_is_pair) d.apply_texture(d.ctx, 1, b.second_texture.binding);
  d.draw(d.ctx); d.cleanup(d.ctx);
}
static qa_render_view_state plain_view(double w, double h) { qa_render_view_state v; memset(&v, 0, sizeof v); v.viewport.width = w; v.viewport.height = h; return v; }

/* --- fake GL --- */
static int opacity_ctx_sentinel, opacity_draws;
static qa_gl_renderer *opacity_renderer;
static void count_draw(void *ctx) { CHECK(ctx == &opacity_ctx_sentinel); opacity_draws++; }
static void nested_opacity(void *ctx) { (void)ctx; qa_gl_renderer_with_object_opacity(opacity_renderer, 0.5, count_draw, &opacity_ctx_sentinel); }
static void throwing_draw(void *ctx) { (void)ctx; qa_range_error("child failed"); }
static void opacity_throw_body(void *ctx) { qa_gl_renderer_with_object_opacity((qa_gl_renderer *)ctx, 0.5, throwing_draw, NULL); }

static void test_constructor(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  CHECK(r.stencil_bits == 8 && r.depth_bits == 24 && r.color_bits == 24 && r.alpha_bits == 8 && r.max_texture_size == 1024 && r.texture_units == 8 && !r.stereo_enabled);
  CHECK(strcmp(r.driver.version, "4.6 fake") == 0);
  CHECK(qa_gl_renderer_width(&r) == 16 && qa_gl_renderer_height(&r) == 16);
  CHECK(rendering_calls > 0);
  CHECK_THROWS("already has an active OpenGL renderer", { qa_gl_renderer again; (void)qa_gl_renderer_init(&again, fake_window(), test_owner()); });
  CHECK(!fake_enabled(0x3000) && !fake_enabled(0x8037) && !fake_enabled(0xb90));
  CHECK(fake_count("glFrontFace") == 1 && fake_count("glShadeModel") == 1);
  CHECK(fake_count("glDisableClientState") >= 6); /* disableArrays */
  qa_gl_renderer_close(&r);
  qa_gl_renderer_close(&r);
  CHECK_THROWS("OpenGL renderer is closed", { qa_gl_renderer_finish(&r); });
  /* A closed renderer releases the context for the next one (TS activeContexts). */
  qa_gl_renderer next = open_fake(); qa_gl_renderer_destroy(&next);
  qa_gl_renderer_destroy(&r);
  fake_reset(); *fake_int(0x8871) = 3;
  CHECK_THROWS("four texture coordinate units", { qa_gl_renderer bad; (void)qa_gl_renderer_init(&bad, fake_window(), test_owner()); });
  fake_reset(); *fake_int(0xd56) = 0;
  CHECK_THROWS("four texture coordinate units", { qa_gl_renderer bad; (void)qa_gl_renderer_init(&bad, fake_window(), test_owner()); });
  fake_reset(); *fake_int(0xd33) = -1;
  CHECK_THROWS("invalid capability", { qa_gl_renderer bad; (void)qa_gl_renderer_init(&bad, fake_window(), test_owner()); });
}
static void test_state_caches(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  qa_renderer_image image = make_image(0, 1, 1, "white");
  static const uint8_t white[4] = {255, 255, 255, 255};
  create_rgba(&r, image, white, "nearest");
  qa_gl_renderer_begin_view(&r, plain_view(16, 16));
  qa_draw_batch b = quad_batch(image); b.state.cull = "back";
  qa_polygon_offset offset = {1, 2}; b.state.polygon_offset = &offset;
  fake_clear_trace();
  draw_batch(&r, b); draw_batch(&r, b);
  CHECK(fake_count("glCullFace") == 1 && FG.last_args[0] == 0x405);
  CHECK(fake_count("glDepthRange") == 1);
  CHECK(fake_count("glPolygonOffset") == 1);
  CHECK(fake_count("glDrawElements") == 2);
  /* A view invalidates the caches, so state is reapplied. */
  qa_gl_renderer_begin_view(&r, plain_view(16, 16));
  fake_clear_trace();
  draw_batch(&r, b);
  CHECK(fake_count("glCullFace") == 1 && fake_count("glDepthRange") == 1 && fake_count("glPolygonOffset") == 1);
  b.state.cull = "front"; fake_clear_trace(); draw_batch(&r, b);
  CHECK(FG.last_args[0] == 0x404);
  qa_polygon_offset infinite = {INFINITY, 0}; b.state.polygon_offset = &infinite;
  CHECK_THROWS("polygon offset must be finite", { draw_batch(&r, b); });
  qa_polygon_offset huge = {1e40, 0}; b.state.polygon_offset = &huge;
  CHECK_THROWS("polygon offset must be finite", { draw_batch(&r, b); });
  b.state.polygon_offset = NULL; b.state.depth_range[0] = NAN;
  CHECK_THROWS("depth range must be finite", { draw_batch(&r, b); });
  b.state.depth_range[0] = 0; b.state.blend.destination = "src-alpha-saturate";
  CHECK_THROWS("cannot use source alpha saturate", { draw_batch(&r, b); });
  qa_gl_renderer_destroy(&r);
}
static void test_views(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  qa_render_view_state v = plain_view(8, 4); v.viewport.x = 2; v.viewport.y = 3;
  qa_vec4 color = {0.1, 0.2, 0.3, 1}; qa_render_view_clear clear = {0.75, &color, true}; v.clear = &clear;
  fake_clear_trace();
  qa_gl_renderer_begin_view(&r, v);
  int32_t *viewport = fake_int(0xba2), *scissor = fake_int(0xc10);
  CHECK(viewport[0] == 2 && viewport[1] == 16 - 3 - 4 && viewport[2] == 8 && viewport[3] == 4);
  CHECK(scissor[0] == 2 && scissor[1] == 9 && fake_enabled(0xc11));
  CHECK(fake_count("glClear") == 1 && fake_count("glClearDepth") == 1 && fake_count("glClearColor") == 1 && fake_count("glClipPlane") == 0 && !fake_enabled(0x3000));
  qa_vec4 plane = {1, 0, 0, 0}; v.clip_plane = &plane; v.clear = NULL;
  qa_gl_renderer_begin_view(&r, v);
  CHECK(fake_enabled(0x3000) && fake_count("glClipPlane") == 1);
  qa_vec4 bad = {NAN, 0, 0, 0}; v.clip_plane = &bad;
  CHECK_THROWS("clip plane must be finite", { qa_gl_renderer_begin_view(&r, v); });
  v.clip_plane = NULL; v.viewport.width = 0;
  CHECK_THROWS("positive int32 dimensions", { qa_gl_renderer_begin_view(&r, v); });
  v.viewport.width = 1.5;
  CHECK_THROWS("positive int32 dimensions", { qa_gl_renderer_begin_view(&r, v); });
  v = plain_view(8, 8); v.has_view_projection = true; v.view_projection.values[0] = 1e40;
  CHECK_THROWS("view projection must be finite", { qa_gl_renderer_begin_view(&r, v); });
  qa_vec4 infinite_color = {1e40, 0, 0, 1}; qa_render_view_clear bad_clear = {1, &infinite_color, false};
  v = plain_view(8, 8); v.clear = &bad_clear;
  CHECK_THROWS("clear values must be finite", { qa_gl_renderer_begin_view(&r, v); });
  /* World batches need a projection and load it; clip batches restore identity. */
  qa_renderer_image image = make_image(0, 1, 1, "white");
  static const uint8_t white[4] = {255, 255, 255, 255};
  create_rgba(&r, image, white, "nearest");
  qa_draw_batch world = quad_batch(image); world.space = "world";
  qa_gl_renderer_begin_view(&r, plain_view(16, 16));
  CHECK_THROWS("world-space batch requires a view projection", { draw_batch(&r, world); });
  v = plain_view(16, 16); v.has_view_projection = true; v.view_projection = qa_identity_mat4();
  qa_gl_renderer_begin_view(&r, v);
  fake_clear_trace(); draw_batch(&r, world);
  CHECK(fake_count("glLoadMatrixf") == 1);
  qa_gl_renderer_select_draw_buffer(&r, "back", true);
  CHECK(fake_int(0xc01)[0] == 0x405);
  qa_gl_renderer_select_draw_buffer(&r, "front", false);
  CHECK(fake_int(0xc01)[0] == 0x404);
  CHECK_THROWS("requires a stereo context", { qa_gl_renderer_select_draw_buffer(&r, "back-right", false); });
  qa_gl_renderer_destroy(&r);
}
static void test_prepared_phases(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  qa_renderer_image image = make_image(0, 1, 1, "white");
  static const uint8_t white[4] = {255, 255, 255, 255};
  create_rgba(&r, image, white, "nearest");
  qa_draw_batch b = quad_batch(image);
  qa_prepared_backend_draw a = qa_gl_renderer_prepare_geometry(&r, b);
  CHECK_THROWS("texture order is invalid", { a.apply_texture(a.ctx, 0, b.texture); });
  CHECK_THROWS("unapplied texture slots", { a.draw(a.ctx); });
  a.begin(a.ctx);
  CHECK_THROWS("already active", { a.begin(a.ctx); });
  CHECK_THROWS("texture order is invalid", { a.apply_texture(a.ctx, 1, b.texture); });
  qa_prepared_backend_draw other = qa_gl_renderer_prepare_geometry(&r, b);
  CHECK_THROWS("already active", { other.begin(other.ctx); });
  CHECK_THROWS("gamma cannot interrupt a prepared draw", { qa_gl_renderer_set_output_gamma(&r, 2); });
  CHECK_THROWS("Object opacity cannot interrupt a prepared draw", { qa_gl_renderer_with_object_opacity(&r, 0.5, count_draw, &opacity_ctx_sentinel); });
  qa_render_operation fog; memset(&fog, 0, sizeof fog); fog.kind = QA_RENDER_OP_Q2_FOG;
  CHECK_THROWS("fog cannot interrupt a prepared draw", { qa_gl_renderer_draw_immediate(&r, fog); });
  qa_render_operation atlas; memset(&atlas, 0, sizeof atlas); atlas.kind = QA_RENDER_OP_DEPTH_ATLAS; atlas.image = image;
  CHECK_THROWS("depth atlas cannot interrupt a prepared draw", { qa_gl_renderer_draw_immediate(&r, atlas); });
  a.apply_texture(a.ctx, 0, b.texture);
  a.draw(a.ctx);
  CHECK(fake_count("glDrawElements") == 1);
  a.cleanup(a.ctx); a.cleanup(a.ctx); /* TS: a second cleanup is a no-op */
  /* TS abandoned draw: cleanup before begin touches no client state. */
  fake_clear_trace(); other.cleanup(other.ctx); other.cleanup(other.ctx);
  CHECK(fake_count("glDisableClientState") == 0 && fake_count("glLineWidth") == 0);
  /* Rejected pack keeps the buffer usable. */
  double bad_indices[3] = {0, 1, 99}; qa_draw_batch invalid = b; invalid.indices.values = bad_indices; invalid.indices.count = 3;
  CHECK_THROWS("", { (void)qa_gl_renderer_prepare_geometry(&r, invalid); });
  draw_batch(&r, b);
  /* Lines: width set for the draw and restored in cleanup, mode 1. */
  qa_draw_batch lines = b; lines.primitive_is_lines = true; lines.line_width = 2; lines.indices.count = 2;
  fake_clear_trace(); draw_batch(&r, lines);
  CHECK(fake_count("glLineWidth") == 2);
  /* A non-depth atlas image is rejected before any state changes. */
  qa_q2_shadow_atlas shadow; memset(&shadow, 0, sizeof shadow); shadow.image = image; shadow.texel_size = 1; shadow.near_plane = 1;
  qa_vec3 positions[4]; memset(positions, 0, sizeof positions);
  qa_draw_batch lit = b; lit.lighting.kind = "q2-model-shadow"; lit.lighting.atlas_value = shadow; lit.lighting.world_positions = positions; lit.lighting.world_position_count = 4;
  qa_prepared_backend_draw lit_draw = qa_gl_renderer_prepare_geometry(&r, lit);
  CHECK_THROWS("requires a depth32f image", { lit_draw.begin(lit_draw.ctx); });
  lit_draw.cleanup(lit_draw.ctx);
  /* Close with a live prepared draw: cleanup stays silent, other phases raise. */
  qa_prepared_backend_draw retained = qa_gl_renderer_prepare_geometry(&r, b);
  qa_gl_renderer_close(&r);
  CHECK_THROWS("OpenGL renderer is closed", { retained.begin(retained.ctx); });
  retained.cleanup(retained.ctx); retained.cleanup(retained.ctx);
  qa_gl_renderer_destroy(&r);
}
static void test_gamma(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  CHECK_THROWS("0.5 and 3", { qa_gl_renderer_set_output_gamma(&r, 0); });
  fake_clear_trace();
  qa_gl_renderer_set_output_gamma(&r, 1); /* unchanged, no pass */
  CHECK(fake_count("glGenFramebuffers") == 0);
  qa_gl_renderer_set_output_gamma(&r, 2);
  CHECK(fake_count("glGenFramebuffers") > 0);
  CHECK(fake_int(0x8ca6)[0] != 0); /* rendering targets the gamma framebuffer */
  int programs = fake_count("glCreateProgram");
  qa_gl_renderer_set_output_gamma(&r, 1.5); /* update keeps the pass */
  CHECK(fake_count("glCreateProgram") == programs);
  /* selectDrawBuffer routes through the pass instead of glDrawBuffer. */
  fake_clear_trace(); qa_gl_renderer_select_draw_buffer(&r, "back", false);
  CHECK(fake_count("glBindFramebuffer") + fake_count("glDrawBuffer") > 0);
  /* finish composites once; a repeat with nothing rendered only selects the default. */
  fake_clear_trace(); qa_gl_renderer_finish(&r);
  int first = fake_count("glDrawArrays") + fake_count("glBegin") + fake_count("glDrawElements");
  CHECK(first > 0 && fake_count("glFinish") == 1);
  CHECK(fake_int(0x8ca6)[0] == 0);
  fake_clear_trace(); qa_gl_renderer_finish(&r);
  CHECK(fake_count("glDrawArrays") + fake_count("glBegin") + fake_count("glDrawElements") == 0);
  /* Rendering makes it dirty again. */
  qa_gl_renderer_clear_color_buffer(&r);
  CHECK(fake_int(0x8ca6)[0] != 0);
  fake_clear_trace(); qa_gl_renderer_present(&r);
  CHECK(swaps == 1);
  CHECK(fake_count("glDrawArrays") + fake_count("glBegin") + fake_count("glDrawElements") > 0);
  /* Oversized targets are refused. */
  *fake_int(0xd33) = 1024; win_width = 2048;
  CHECK_THROWS("gamma target exceeds", { qa_gl_renderer_clear_color_buffer(&r); });
  win_width = 16;
  /* Disabling restores the default framebuffer and later draws use glDrawBuffer. */
  qa_gl_renderer_set_output_gamma(&r, 1);
  CHECK(fake_int(0x8ca6)[0] == 0);
  fake_clear_trace(); qa_gl_renderer_select_draw_buffer(&r, "back", false);
  CHECK(fake_count("glDrawBuffer") == 1);
  qa_gl_renderer_destroy(&r);
  /* TS: present with no gamma pass swaps directly, and invalidates caches. */
  fake_reset(); r = open_fake(); qa_gl_renderer_present(&r); CHECK(swaps == 1); qa_gl_renderer_destroy(&r);
  /* C: a context without swap cannot present. */
  fake_reset();
  qa_gl_render_context no_swap = fake_window(); no_swap.swap = NULL;
  CHECK(qa_gl_renderer_init(&r, no_swap, test_owner()));
  CHECK_THROWS("has no swap", { qa_gl_renderer_present(&r); });
  qa_gl_renderer_destroy(&r);
}
static void test_opacity(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  opacity_renderer = &r; opacity_draws = 0;
  CHECK_THROWS("Object opacity must be in 0..1", { qa_gl_renderer_with_object_opacity(&r, 2, count_draw, &opacity_ctx_sentinel); });
  CHECK_THROWS("Object opacity must be in 0..1", { qa_gl_renderer_with_object_opacity(&r, NAN, count_draw, &opacity_ctx_sentinel); });
  fake_clear_trace();
  qa_gl_renderer_with_object_opacity(&r, 0, count_draw, &opacity_ctx_sentinel);
  CHECK(opacity_draws == 0 && fake_count("glGenFramebuffers") == 0);
  qa_gl_renderer_with_object_opacity(&r, 1, count_draw, &opacity_ctx_sentinel);
  CHECK(opacity_draws == 1 && fake_count("glGenFramebuffers") == 0);
  qa_gl_renderer_with_object_opacity(&r, 0.5, count_draw, &opacity_ctx_sentinel);
  CHECK(opacity_draws == 2 && fake_count("glGenFramebuffers") > 0);
  /* The pass is real: it composites through its program with the opacity uniform. */
  CHECK(fake_count("glUniform1f") > 0);
  CHECK(fake_count("glBlitFramebuffer") + fake_count("glDrawArrays") + fake_count("glBegin") > 0);
  int programs = fake_count("glCreateProgram");
  qa_gl_renderer_with_object_opacity(&r, 0.25, count_draw, &opacity_ctx_sentinel);
  CHECK(fake_count("glCreateProgram") == programs && opacity_draws == 3); /* ??= keeps one pass */
  CHECK_THROWS("Nested OpenGL object opacity is unsupported", { qa_gl_renderer_with_object_opacity(&r, 0.5, nested_opacity, NULL); });
  win_width = 2048;
  CHECK_THROWS("opacity target exceeds", { qa_gl_renderer_with_object_opacity(&r, 0.5, count_draw, &opacity_ctx_sentinel); });
  win_width = 16;
  /* TS finally: a throw inside leaves the renderer usable for another scope. */
  char message[256];
  CHECK(!qa_range_error_catch(opacity_throw_body, &r, message, sizeof message));
  CHECK(strcmp(message, "child failed") == 0);
  qa_gl_renderer_with_object_opacity(&r, 1, count_draw, &opacity_ctx_sentinel);
  CHECK(opacity_draws == 4);
  qa_gl_renderer_destroy(&r);
}
static void test_passes_constructed(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  qa_renderer_image color = make_image(0, 1, 1, "white"), depth = make_image(1, 4, 4, "atlas");
  static const uint8_t white[4] = {255, 255, 255, 255};
  create_rgba(&r, color, white, "nearest");
  create_depth(&r, depth, 1);
  qa_render_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_RENDER_OP_DEPTH_ATLAS; op.image = color;
  CHECK_THROWS("requires a depth32f image", { qa_gl_renderer_draw_immediate(&r, op); });
  static const double positions[12] = {-1, -1, -0.5, 1, 1, -1, -0.5, 1, 0, 1, -0.5, 1};
  static const double indices[3] = {0, 1, 2};
  qa_depth_atlas_draw draw; memset(&draw, 0, sizeof draw); draw.positions = positions; draw.position_count = 12; draw.indices.values = indices; draw.indices.count = 3; draw.cull = "none";
  qa_depth_atlas_pass pass; memset(&pass, 0, sizeof pass); pass.viewport.width = 4; pass.viewport.height = 4; pass.has_clear_depth = true; pass.clear_depth = 1; pass.draws = &draw; pass.draw_count = 1;
  op.image = depth; op.passes = &pass; op.pass_count = 1;
  fake_clear_trace(); qa_gl_renderer_draw_immediate(&r, op);
  CHECK(fake_count("glDrawElements") == 1 && fake_count("glGenFramebuffers") > 0); /* DepthAtlasTarget */
  int framebuffers = fake_count("glGenFramebuffers");
  qa_gl_renderer_draw_immediate(&r, op);
  CHECK(fake_count("glGenFramebuffers") == framebuffers); /* ??= keeps one target */
  qa_render_operation fog; memset(&fog, 0, sizeof fog);
  fog.kind = QA_RENDER_OP_Q2_FOG; fog.fog.camera.projection = qa_perspective_mat4(90, 1, 4, 1024); fog.fog.camera.viewport.width = 16; fog.fog.camera.viewport.height = 16;
  fog.fog.camera.axis.vectors[0] = (qa_vec3){1, 0, 0}; fog.fog.camera.axis.vectors[1] = (qa_vec3){0, 1, 0}; fog.fog.camera.axis.vectors[2] = (qa_vec3){0, 0, 1};
  fog.fog.fog.kind = "q2"; fog.fog.fog.color = (qa_vec3){0, 0, 1}; fog.fog.fog.density = 16; fog.fog.far_depth = 1 - 1e-6;
  fake_clear_trace(); qa_gl_renderer_draw_immediate(&r, fog);
  CHECK(fake_count("glCreateProgram") > 0 && fake_count("glUseProgram") > 0); /* Q2FogPass */
  /* Sky, overdraw and shadows. */
  qa_vertex_position_tex_coord v[2]; memset(v, 0, sizeof v); v[0].position.w = 1; v[1].position.w = 1;
  qa_sky_strip strip = {v, 2};
  qa_render_operation sky; memset(&sky, 0, sizeof sky); sky.kind = QA_RENDER_OP_SKY_SIDE; sky.image = color; sky.strips = &strip; sky.strip_count = 1; sky.color = (qa_vec4){1, 1, 1, 1};
  fake_clear_trace(); qa_gl_renderer_draw_immediate(&r, sky);
  CHECK(fake_count("glBegin") == 1 && fake_count("glVertex4f") == 2);
  qa_sky_strip odd = {v, 1}; sky.strips = &odd;
  CHECK_THROWS("paired row vertices", { qa_gl_renderer_draw_immediate(&r, sky); });
  v[0].position.x = 1e40; sky.strips = &strip;
  CHECK_THROWS("sky vertices must be finite", { qa_gl_renderer_draw_immediate(&r, sky); });
  v[0].position.x = 0;
  qa_vec4 tri[3] = {{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}};
  double tri_indices[3] = {0, 1, 2};
  qa_render_operation shadow; memset(&shadow, 0, sizeof shadow); shadow.kind = QA_RENDER_OP_SHADOW_VOLUME; shadow.white_image = color; shadow.positions = tri; shadow.position_count = 3; shadow.indices.values = tri_indices; shadow.indices.count = 3;
  fake_clear_trace(); qa_gl_renderer_draw_immediate(&r, shadow);
  CHECK(fake_count("glBegin") == 2 && fake_count("glVertex4f") == 6 && fake_count("glStencilOp") == 2 && FG.last_args[0] == 0x404);
  CHECK(fake_int(0xc23)[0] == 1); /* colour mask restored */
  shadow.mirror = true; qa_gl_renderer_draw_immediate(&r, shadow);
  CHECK(FG.last_args[0] == 0x405);
  shadow.indices.count = 2;
  CHECK_THROWS("triangles are incomplete", { qa_gl_renderer_draw_immediate(&r, shadow); });
  double bad_index[3] = {0, 1, 3}; shadow.indices.values = bad_index; shadow.indices.count = 3;
  CHECK_THROWS("vertex index is invalid", { qa_gl_renderer_draw_immediate(&r, shadow); });
  shadow.kind = QA_RENDER_OP_SHADOW_FINISH; memcpy(shadow.finish_positions, tri, sizeof tri); shadow.finish_positions[3] = tri[0];
  fake_clear_trace(); qa_gl_renderer_draw_immediate(&r, shadow);
  CHECK(fake_count("glBegin") == 1 && fake_count("glVertex4f") == 4 && !fake_enabled(0xb90));
  qa_gl_renderer_set_overdraw_measurement(&r, true);
  CHECK(fake_enabled(0xb90));
  uint8_t small[4];
  CHECK_THROWS("stencil destination is too small", { qa_gl_renderer_read_stencil_overdraw(&r, small, 4); });
  CHECK_THROWS("outside the framebuffer", { (void)qa_gl_renderer_read_depth_pixel(&r, 16, 0); });
  CHECK_THROWS("outside the framebuffer", { (void)qa_gl_renderer_read_depth_pixel(&r, 0.5, 0); });
  CHECK(qa_gl_renderer_read_depth_pixel(&r, 3, 3) == 0.5);
  qa_gl_renderer_set_overdraw_measurement(&r, false);
  CHECK(!fake_enabled(0xb90));
  /* readPixels flips to a top-left origin: the fake fills rows with the GL row index. */
  size_t length = 0; uint8_t *pixels = qa_gl_renderer_read_pixels(&r, &length);
  CHECK(length == 16 * 16 * 4 && pixels[0] == 15 && pixels[15 * 16 * 4] == 0);
  free(pixels);
  float storage[16]; qa_depth_image_level level;
  CHECK(!qa_gl_renderer_read_depth_image(&r, depth, &level, storage, 4));
  CHECK(qa_gl_renderer_read_depth_image(&r, depth, &level, storage, 16) && level.pixel_count == 16 && level.width == 4);
  CHECK_THROWS("requires a depth32f image", { (void)qa_gl_renderer_read_depth_image(&r, color, &level, storage, 16); });
  qa_rect huge = {1e40, 0, 1, 1};
  CHECK_THROWS("image grid rectangle must be finite", { qa_gl_renderer_draw_show_image(&r, color, huge, false); });
  fake_clear_trace(); qa_rect rect = {0, 0, 4, 4}; qa_gl_renderer_draw_show_image(&r, color, rect, false);
  CHECK(fake_count("glOrtho") == 1 && fake_count("glVertex2f") == 4);
  /* Close releases every pass exactly once. */
  int before = releases; qa_gl_renderer_close(&r); CHECK(releases > before);
  qa_gl_renderer_destroy(&r);
  fake_reset(); *fake_int(0xd57) = 2; r = open_fake();
  create_rgba(&r, color, white, "nearest");
  /* RB_ShadowTessEnd and RB_ShadowFinish draw nothing without a stencil buffer (TS returns). */
  fake_clear_trace(); qa_gl_renderer_draw_immediate(&r, shadow);
  CHECK(fake_count("glVertex4f") == 0);
  qa_gl_renderer_destroy(&r);
  fake_reset(); *fake_int(0xd57) = 0; r = open_fake();
  CHECK_THROWS("requires a stencil framebuffer", { qa_gl_renderer_set_overdraw_measurement(&r, true); });
  qa_gl_renderer_destroy(&r);
}
/* Dynamic textures go through the prepared draw's resolver and the renderer's upload path. */
static int dynamic_calls; static qa_renderer_image dynamic_image;
static qa_renderer_image dynamic_resolve(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  CHECK(ctx == &dynamic_calls); dynamic_calls++;
  static const uint8_t pixels[4] = {1, 2, 3, 4};
  static qa_image_level level = {1, 1, {pixels, 4}};
  qa_image_resource_operation op; memset(&op, 0, sizeof op); op.kind = QA_IMAGE_OP_UPDATE; op.image = dynamic_image; op.update_level = &level;
  apply(apply_ctx, op);
  return dynamic_image;
}
static void test_dynamic_texture(void) {
  fake_reset();
  qa_gl_renderer r = open_fake();
  dynamic_image = make_image(0, 1, 1, "dynamic"); dynamic_calls = 0;
  static const uint8_t white[4] = {255, 255, 255, 255};
  create_rgba(&r, dynamic_image, white, "nearest");
  qa_draw_batch b = quad_batch(dynamic_image);
  b.texture.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE; b.texture.source.ctx = &dynamic_calls; b.texture.source.resolve = dynamic_resolve;
  fake_clear_trace(); draw_batch(&r, b);
  CHECK(dynamic_calls == 1 && fake_count("glTexSubImage2D") + fake_count("glTexImage2D") > 0 && fake_count("glDrawElements") == 1);
  qa_gl_renderer_destroy(&r);
}

/* --- real context --- */
static qa_sdl_window real_window;
static void real_make_current(void *ctx) { qa_sdl_window_make_current((qa_sdl_window *)ctx); }
static void real_size(void *ctx, double *w, double *h) { qa_sdl_size s = qa_sdl_window_drawable_size((qa_sdl_window *)ctx); *w = s.width; *h = s.height; }
static void real_rendering(void *ctx, bool on) { qa_sdl_window_set_rendering_enabled((qa_sdl_window *)ctx, on); }
static void real_swap(void *ctx) { qa_sdl_window_swap((qa_sdl_window *)ctx); }
static qa_gl_render_context real_context(void) {
  qa_gl_render_context c; memset(&c, 0, sizeof c);
  c.ctx = &real_window; c.gl_context = qa_gl_context_from_window(&real_window);
  c.make_current = real_make_current; c.drawable_size = real_size; c.set_rendering_enabled = real_rendering; c.swap = real_swap;
  return c;
}
static void uniform_rgba(qa_gl_renderer *r, qa_renderer_image image, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
  uint8_t pixels[4] = {cr, cg, cb, ca};
  qa_image_level level; memset(&level, 0, sizeof level); level.width = 1; level.height = 1; level.pixels.bytes = pixels; level.pixels.length = 4;
  qa_image_resource_operation op; memset(&op, 0, sizeof op); op.kind = QA_IMAGE_OP_UPDATE; op.image = image; op.update_level = &level;
  qa_gl_renderer_apply_image_resource(r, op);
}
static bool all_pixels(const uint8_t *p, size_t n, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
  for (size_t i = 0; i + 4 <= n; i += 4) if (p[i] != cr || p[i + 1] != cg || p[i + 2] != cb || p[i + 3] != ca) return false;
  return true;
}
static uint8_t *read_all(qa_gl_renderer *r, size_t *n) { return qa_gl_renderer_read_pixels(r, n); }
#define PIXEL(p, x, y) ((p) + ((size_t)(y) * 16 + (size_t)(x)) * 4)
static qa_render_view_state clear_view(qa_render_view_clear *clear, qa_vec4 *color, double depth) {
  color->x = 0; color->y = 0; color->z = 0; color->w = 1;
  clear->depth = depth; clear->color = color; clear->stencil = true;
  qa_render_view_state v = plain_view(16, 16); v.clear = clear;
  return v;
}
static int real_opacity_draws; static qa_gl_renderer *real_renderer; static qa_draw_batch real_batch;
static void real_opacity(void *ctx) { CHECK(ctx == &real_opacity_draws); real_opacity_draws++; draw_batch(real_renderer, real_batch); }
/* TS "GLSL stages, shadow atlases, ordered fog and renderer restart". */
static void real_stages(void) {
  qa_gl_renderer r; memset(&r, 0, sizeof r);
  qa_renderer_resource_owner owner = test_owner();
  CHECK(qa_gl_renderer_init(&r, real_context(), owner));
  fprintf(stderr, "real GL: vendor=%s renderer=%s version=%s stencil=%g depth=%g\n", r.driver.vendor, r.driver.renderer, r.driver.version, r.stencil_bits, r.depth_bits);
  CHECK(strlen(r.driver.vendor) > 0 && strlen(r.driver.shading_language) > 0);
  qa_renderer_image image = make_image(0, 1, 1, "green"), secondary = make_image(1, 1, 1, "blue");
  static const uint8_t green[4] = {0, 255, 0, 255}, blue[4] = {0, 0, 255, 255};
  create_rgba(&r, image, green, "nearest"); create_rgba(&r, secondary, blue, "nearest");
  qa_draw_batch batch = quad_batch(image);
  qa_vec4 color; qa_render_view_clear clear; qa_render_view_state view = clear_view(&clear, &color, 1);
  qa_gl_renderer_select_draw_buffer(&r, "back", false);
  qa_gl_renderer_begin_view(&r, view);
  size_t n; uint8_t *p = read_all(&r, &n);
  CHECK(n == 16 * 16 * 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255); free(p);
  draw_batch(&r, batch);
  p = read_all(&r, &n); CHECK(all_pixels(p, n, 0, 255, 0, 255)); free(p);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 8, 8) - 0.5) < 1e-5);
  qa_rect corner = {16, 16, 1, 1};
  qa_gl_renderer_draw_show_image(&r, image, corner, false);
  qa_gl_renderer_clear_color_buffer(&r);
  draw_batch(&r, batch);
  p = read_all(&r, &n); CHECK(all_pixels(p, n, 0, 255, 0, 255)); free(p);
  qa_render_operation none; memset(&none, 0, sizeof none); none.kind = QA_RENDER_OP_SHADOW_VOLUME; none.white_image = image;
  qa_gl_renderer_draw_immediate(&r, none);
  qa_gl_renderer_clear_color_buffer(&r);
  draw_batch(&r, batch);
  p = read_all(&r, &n); CHECK(all_pixels(p, n, 0, 255, 0, 255)); free(p);
  uniform_rgba(&r, image, 255, 0, 0, 255);
  qa_vec4 plane = {1, 0, 0, 0}; view.clip_plane = &plane;
  qa_gl_renderer_begin_view(&r, view);
  draw_batch(&r, batch);
  p = read_all(&r, &n);
  CHECK(PIXEL(p, 4, 8)[0] == 0 && PIXEL(p, 4, 8)[3] == 255);
  CHECK(PIXEL(p, 12, 8)[0] == 255 && PIXEL(p, 12, 8)[1] == 0);
  free(p);
  view.clip_plane = NULL;
  qa_render_operation disable; memset(&disable, 0, sizeof disable); disable.kind = QA_RENDER_OP_DISABLE_PORTAL_CLIP;
  qa_gl_renderer_draw_immediate(&r, disable);
  qa_multitexture_vertex pair[4]; for (int i = 0; i < 4; i++) { pair[i].vertex = QUAD[i]; pair[i].tex_coord2 = QUAD[i].tex_coord; }
  qa_draw_batch paired = batch; paired.texturing_is_pair = true; paired.vertices = NULL; paired.vertex_count = 0; paired.pair_vertices = pair; paired.pair_vertex_count = 4;
  paired.second_texture.binding.kind = QA_TEXTURE_BINDING_BIND_IMAGE; paired.second_texture.binding.image = secondary; paired.second_texture.environment = "add";
  draw_batch(&r, paired);
  p = read_all(&r, &n); CHECK(p[0] == 255 && p[1] == 0 && p[2] == 255 && p[3] == 255); free(p);
  uniform_rgba(&r, image, 0, 255, 0, 0);
  qa_draw_batch alpha_tested = batch; alpha_tested.state.alpha_test = "gt0";
  draw_batch(&r, alpha_tested);
  p = read_all(&r, &n); CHECK(p[0] == 255 && p[1] == 0 && p[2] == 255 && p[3] == 255); free(p);
  uniform_rgba(&r, image, 255, 0, 0, 255);
  if (r.stencil_bits > 0) {
    qa_gl_renderer_set_overdraw_measurement(&r, true);
    qa_gl_renderer_begin_view(&r, view);
    draw_batch(&r, batch); draw_batch(&r, batch);
    uint8_t stencil[16 * 16]; qa_gl_renderer_read_stencil_overdraw(&r, stencil, sizeof stencil);
    bool twice = true; for (size_t i = 0; i < sizeof stencil; i++) if (stencil[i] != 2) twice = false;
    CHECK(twice);
    qa_gl_renderer_set_overdraw_measurement(&r, false);
  }
  /* Fragment light. */
  uniform_rgba(&r, image, 0, 0, 0, 255);
  qa_vec3 world[4], normals[4];
  for (int i = 0; i < 4; i++) { world[i] = (qa_vec3){QUAD[i].position.x * 128, QUAD[i].position.y * 128, 0}; normals[i] = (qa_vec3){0, 0, 1}; }
  qa_q2_light_cone cone = {{0, 0, -1}, 0.5};
  qa_q2_fragment_light light; memset(&light, 0, sizeof light);
  light.origin = (qa_vec3){0, 0, 32}; light.radius = 256; light.color = (qa_vec3){0, 1, 0}; light.scale = 1; light.cone = &cone; light.shadow.kind = "none";
  qa_draw_batch lit = batch; lit.lighting.kind = "q2-world"; lit.lighting.pass = "lightmap"; lit.lighting.world_positions = world; lit.lighting.world_position_count = 4; lit.lighting.normals = normals; lit.lighting.normal_count = 4; lit.lighting.lights = &light; lit.lighting.light_count = 1;
  draw_batch(&r, lit);
  uint8_t *lit_pixels = read_all(&r, &n);
  CHECK(PIXEL(lit_pixels, 8, 8)[1] > 100);
  CHECK(lit_pixels[1] < PIXEL(lit_pixels, 8, 8)[1]);
  /* Depth atlas. */
  qa_renderer_image atlas = make_image(2, 96, 96, "shadow atlas");
  create_depth(&r, atlas, 1);
  double positions[16]; for (int i = 0; i < 4; i++) { positions[i * 4] = QUAD[i].position.x; positions[i * 4 + 1] = QUAD[i].position.y; positions[i * 4 + 2] = -0.5; positions[i * 4 + 3] = 1; }
  qa_depth_atlas_draw atlas_draw; memset(&atlas_draw, 0, sizeof atlas_draw); atlas_draw.positions = positions; atlas_draw.position_count = 16; atlas_draw.indices = batch.indices; atlas_draw.cull = "none";
  qa_depth_atlas_pass atlas_pass; memset(&atlas_pass, 0, sizeof atlas_pass); atlas_pass.viewport.width = 96; atlas_pass.viewport.height = 96; atlas_pass.has_clear_depth = true; atlas_pass.clear_depth = 1; atlas_pass.draws = &atlas_draw; atlas_pass.draw_count = 1;
  qa_render_operation atlas_op; memset(&atlas_op, 0, sizeof atlas_op); atlas_op.kind = QA_RENDER_OP_DEPTH_ATLAS; atlas_op.image = atlas; atlas_op.passes = &atlas_pass; atlas_op.pass_count = 1;
  qa_gl_renderer_draw_immediate(&r, atlas_op);
  float *depth_storage = (float *)malloc(96 * 96 * sizeof(float)); qa_depth_image_level depth_level;
  CHECK(qa_gl_renderer_read_depth_image(&r, atlas, &depth_level, depth_storage, 96 * 96));
  bool quarter = true; for (size_t i = 0; i < 96 * 96; i++) if (fabs(depth_storage[i] - 0.25) >= 0.00001) quarter = false;
  CHECK(quarter); free(depth_storage);
  p = read_all(&r, &n); CHECK(memcmp(p, lit_pixels, n) == 0); free(p); free(lit_pixels);
  qa_q2_shadow_atlas shadow_atlas; memset(&shadow_atlas, 0, sizeof shadow_atlas); shadow_atlas.image = atlas; shadow_atlas.texel_size = 1.0 / 96; shadow_atlas.near_plane = 4;
  qa_q2_fragment_light shadowed = light; shadowed.shadow.kind = "cone"; shadowed.shadow.atlas_rect = (qa_vec4){0, 0, 1, 1};
  static const double matrix[16] = {1.0 / 256, 0, 0, 0, 0, 1.0 / 256, 0, 0, 0, 0, 0, 0, 0.5, 0.5, 0.75, 1};
  memcpy(shadowed.shadow.matrix.values, matrix, sizeof matrix);
  qa_draw_batch shadow_lit = lit; shadow_lit.lighting.lights = &shadowed; shadow_lit.lighting.atlas = &shadow_atlas;
  draw_batch(&r, shadow_lit);
  p = read_all(&r, &n); CHECK(PIXEL(p, 8, 8)[0] == 0 && PIXEL(p, 8, 8)[1] == 0 && PIXEL(p, 8, 8)[2] == 0 && PIXEL(p, 8, 8)[3] == 255); free(p);
  uniform_rgba(&r, image, 255, 255, 255, 255);
  qa_vec3 zero[4]; memset(zero, 0, sizeof zero);
  qa_q2_model_shadow_light model_light; memset(&model_light, 0, sizeof model_light);
  model_light.origin = light.origin; model_light.radius = 256; model_light.fraction = (qa_vec3){1, 0, 0}; model_light.shadow.kind = "point"; model_light.shadow.atlas_rect = (qa_vec4){0, 0, 1, 2.0 / 3};
  qa_draw_batch model = batch; model.lighting.kind = "q2-model-shadow"; model.lighting.world_positions = zero; model.lighting.world_position_count = 4; model.lighting.model_shadow_lights = &model_light; model.lighting.model_shadow_light_count = 1; model.lighting.has_shade_scale = true; model.lighting.shade_scale = 1; model.lighting.atlas_value = shadow_atlas;
  draw_batch(&r, model);
  p = read_all(&r, &n); CHECK(p[0] == 0 && p[1] == 255 && p[2] == 255 && p[3] == 255); free(p);
  /* Fog. */
  qa_render_operation fog; memset(&fog, 0, sizeof fog); fog.kind = QA_RENDER_OP_Q2_FOG;
  fog.fog.camera.origin = (qa_vec3){0, 0, 10}; fog.fog.camera.axis.vectors[0] = (qa_vec3){1, 0, 0}; fog.fog.camera.axis.vectors[1] = (qa_vec3){0, 1, 0}; fog.fog.camera.axis.vectors[2] = (qa_vec3){0, 0, 1};
  fog.fog.camera.projection = qa_perspective_mat4(90, 1, 4, 1024); fog.fog.camera.viewport.width = 16; fog.fog.camera.viewport.height = 16;
  fog.fog.fog.kind = "q2"; fog.fog.fog.color = (qa_vec3){0, 0, 1}; fog.fog.fog.density = 16; fog.fog.fog.sky_factor = 0.5;
  fog.fog.fog.height.start.color = (qa_vec3){1, 0, 0}; fog.fog.fog.height.end.color = (qa_vec3){0, 0, 1}; fog.fog.fog.height.end.distance = 100;
  fog.fog.far_depth = 1 - 1e-6; fog.fog.sky_drawn = false;
  qa_vec4 c2; qa_render_view_clear half; qa_render_view_state half_view = clear_view(&half, &c2, 0.5);
  qa_gl_renderer_begin_view(&r, half_view);
  qa_gl_renderer_draw_immediate(&r, fog);
  p = read_all(&r, &n);
  CHECK(p[0] == 0); CHECK(fabs((double)p[2] - 160) < 5);
  fprintf(stderr, "real GL: fog blue=%d (TS toBeCloseTo(160,-1))\n", p[2]);
  free(p);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 8, 8) - 0.5) < 1e-5);
  qa_gl_renderer_begin_view(&r, half_view);
  qa_render_operation height_fog = fog; height_fog.fog.fog.density = 0; height_fog.fog.fog.height.density = 0.3; height_fog.fog.fog.height.falloff = 0.1;
  qa_gl_renderer_draw_immediate(&r, height_fog);
  p = read_all(&r, &n); CHECK(PIXEL(p, 8, 8)[0] > 100 && PIXEL(p, 8, 8)[2] < PIXEL(p, 8, 8)[0]); free(p);
  qa_gl_renderer_begin_view(&r, view);
  qa_gl_renderer_draw_immediate(&r, fog);
  p = read_all(&r, &n); CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255); free(p);
  qa_render_operation sky_fog = fog; sky_fog.fog.sky_drawn = true;
  qa_gl_renderer_draw_immediate(&r, sky_fog);
  p = read_all(&r, &n); CHECK(p[2] == 127 || p[2] == 128); free(p);
  /* Object opacity through the real pass. */
  qa_gl_renderer_begin_view(&r, view);
  real_renderer = &r; real_batch = batch; real_opacity_draws = 0;
  qa_gl_renderer_with_object_opacity(&r, 0.5, real_opacity, &real_opacity_draws);
  CHECK(real_opacity_draws == 1);
  p = read_all(&r, &n);
  for (int i = 0; i < 3; i++) CHECK(p[i] == 127 || p[i] == 128);
  free(p);
  draw_batch(&r, batch);
  p = read_all(&r, &n); CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255 && p[3] == 255); free(p);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 8, 8) - 0.5) < 1e-5);
  qa_gl_renderer_finish(&r);
  CHECK(qa_gl_renderer_get_error(&r) == 0);
  qa_gl_renderer_present(&r);
  qa_gl_renderer_destroy(&r);
  /* Restart on the same window, with a resize. */
  qa_renderer_resource_owner next_owner = owner; next_owner.generation = 1;
  qa_gl_renderer restarted; memset(&restarted, 0, sizeof restarted);
  CHECK(qa_gl_renderer_init(&restarted, real_context(), next_owner));
  qa_sdl_window_set_size(&real_window, 12, 8);
  size_t events = 0; free(qa_sdl_window_poll_events(&real_window, &events));
  CHECK(qa_gl_renderer_width(&restarted) == 12 && qa_gl_renderer_height(&restarted) == 8);
  qa_render_view_state small = plain_view(12, 8); small.clear = &clear;
  qa_gl_renderer_begin_view(&restarted, small);
  p = read_all(&restarted, &n);
  CHECK(n == 12 * 8 * 4 && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 255); free(p);
  CHECK(qa_gl_renderer_get_error(&restarted) == 0);
  qa_gl_renderer_destroy(&restarted);
  qa_sdl_window_set_size(&real_window, 16, 16);
  free(qa_sdl_window_poll_events(&real_window, &events));
}
/* TS "GL output gamma follows blending and preserves raw buffers across capture, resize and disable". */
static void real_gamma(void) {
  qa_gl_renderer r; memset(&r, 0, sizeof r);
  CHECK(qa_gl_renderer_init(&r, real_context(), test_owner()));
  qa_renderer_image image = make_image(0, 1, 1, "gamma sample");
  static const uint8_t sample[4] = {64, 128, 192, 128};
  create_rgba(&r, image, sample, "nearest");
  qa_draw_batch batch = quad_batch(image); batch.state.blend.source = "src-alpha"; batch.state.blend.destination = "one-minus-src-alpha";
  qa_vec4 color; qa_render_view_clear clear; qa_render_view_state view = clear_view(&clear, &color, 1);
  qa_gl_renderer_select_draw_buffer(&r, "back", false);
  bool has_stencil = r.stencil_bits > 0;
  if (has_stencil) qa_gl_renderer_set_overdraw_measurement(&r, true);
  qa_gl_renderer_begin_view(&r, view);
  draw_batch(&r, batch);
  size_t n; uint8_t *raw = read_all(&r, &n); uint8_t stencil[16 * 16];
  if (has_stencil) { qa_gl_renderer_read_stencil_overdraw(&r, stencil, sizeof stencil); bool ones = true; for (size_t i = 0; i < sizeof stencil; i++) if (stencil[i] != 1) ones = false; CHECK(ones); }
  qa_gl_renderer_set_output_gamma(&r, 1); qa_gl_renderer_finish(&r);
  uint8_t *p = read_all(&r, &n); CHECK(memcmp(p, raw, n) == 0); free(p);
  qa_gl_renderer_set_output_gamma(&r, 2); qa_gl_renderer_finish(&r);
  CHECK(qa_gl_renderer_get_error(&r) == 0);
  uint8_t table[256]; CHECK(qa_output_gamma_table(2, table));
  uint8_t *corrected = (uint8_t *)malloc(n);
  for (size_t i = 0; i < n; i++) corrected[i] = i % 4 == 3 ? raw[i] : table[raw[i]];
  p = read_all(&r, &n); CHECK(memcmp(p, corrected, n) == 0); free(p);
  qa_gl_renderer_finish(&r); qa_gl_renderer_finish(&r);
  p = read_all(&r, &n); CHECK(memcmp(p, corrected, n) == 0); free(p);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 12, 8) - 0.5) < 1e-5);
  if (has_stencil) { qa_gl_renderer_read_stencil_overdraw(&r, stencil, sizeof stencil); bool ones = true; for (size_t i = 0; i < sizeof stencil; i++) if (stencil[i] != 1) ones = false; CHECK(ones); }
  qa_vec4 red = {0.25, 0, 0, 1}; qa_render_view_clear red_clear = {0.75, &red, true};
  qa_render_view_state half = plain_view(8, 16); half.clear = &red_clear;
  qa_gl_renderer_begin_view(&r, half);
  qa_gl_renderer_finish(&r);
  uint8_t *partial = read_all(&r, &n);
  CHECK(memcmp(PIXEL(partial, 12, 8), PIXEL(corrected, 12, 8), 4) == 0);
  CHECK(partial[0] == table[64] && partial[1] == table[0] && partial[2] == table[0]);
  free(partial);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 4, 8) - 0.75) < 1e-5);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 12, 8) - 0.5) < 1e-5);
  qa_gl_renderer_set_output_gamma(&r, 1);
  p = read_all(&r, &n);
  CHECK(p[0] == 64 && p[1] == 0 && p[2] == 0);
  CHECK(memcmp(PIXEL(p, 12, 8), PIXEL(raw, 12, 8), 4) == 0);
  free(p); free(raw); free(corrected);
  CHECK(fabs(qa_gl_renderer_read_depth_pixel(&r, 4, 8) - 0.75) < 1e-5);
  qa_gl_renderer_set_output_gamma(&r, 2);
  qa_sdl_window_set_size(&real_window, 12, 8);
  size_t events = 0; free(qa_sdl_window_poll_events(&real_window, &events));
  qa_vec4 mix = {0.25, 0.5, 0.75, 1}; qa_render_view_clear mix_clear = {1, &mix, true};
  qa_render_view_state resized_view = plain_view(12, 8); resized_view.clear = &mix_clear;
  qa_gl_renderer_begin_view(&r, resized_view);
  qa_gl_renderer_finish(&r);
  uint8_t *resized = read_all(&r, &n);
  qa_gl_renderer_set_output_gamma(&r, 1);
  uint8_t *resized_raw = read_all(&r, &n);
  CHECK(n == 12 * 8 * 4);
  bool same = true; for (size_t i = 0; i < n; i++) if (resized[i] != (i % 4 == 3 ? resized_raw[i] : table[resized_raw[i]])) same = false;
  CHECK(same); free(resized); free(resized_raw);
  qa_gl_renderer_set_output_gamma(&r, 2); qa_gl_renderer_finish(&r);
  CHECK(qa_gl_renderer_get_error(&r) == 0);
  qa_gl_renderer_present(&r);
  qa_gl_renderer_destroy(&r);
  qa_renderer_resource_owner next = test_owner(); next.generation = 1;
  qa_gl_renderer restarted; memset(&restarted, 0, sizeof restarted);
  CHECK(qa_gl_renderer_init(&restarted, real_context(), next));
  qa_gl_renderer_set_output_gamma(&restarted, 2);
  qa_render_view_state small = plain_view(12, 8); small.clear = &clear;
  qa_gl_renderer_begin_view(&restarted, small);
  qa_gl_renderer_finish(&restarted); CHECK(qa_gl_renderer_get_error(&restarted) == 0);
  qa_gl_renderer_destroy(&restarted);
  qa_sdl_window_set_size(&real_window, 16, 16);
  free(qa_sdl_window_poll_events(&real_window, &events));
}
/* TS "GL retained prepared buffers, failure cleanup and close preserve fresh color and depth". */
static qa_draw_batch retained_triangle(qa_renderer_image image, qa_render_vertex *storage, double red, double depth) {
  static const double xy[3][2] = {{-1, -1}, {1, -1}, {0, 1}};
  for (int i = 0; i < 3; i++) { storage[i].position = (qa_vec4){xy[i][0], xy[i][1], depth, 1}; storage[i].color = (qa_vec4){red, 1 - red, 0.25, 1}; storage[i].tex_coord = (qa_vec2){0.5, 0.5}; }
  static const double indices[3] = {0, 1, 2};
  qa_draw_batch b = quad_batch(image); b.vertices = storage; b.vertex_count = 3; b.indices.values = indices; b.indices.count = 3;
  b.state.cull = "none";
  return b;
}
static uint8_t *retained_frame(qa_gl_renderer *r, qa_draw_batch a_batch, qa_draw_batch b_batch, qa_draw_batch abandoned_batch, double *depth) {
  qa_vec4 color; qa_render_view_clear clear; qa_render_view_state view = clear_view(&clear, &color, 1);
  qa_gl_renderer_begin_view(r, view);
  qa_prepared_backend_draw a = qa_gl_renderer_prepare_geometry(r, a_batch), b = qa_gl_renderer_prepare_geometry(r, b_batch);
  b.begin(b.ctx); b.apply_texture(b.ctx, 0, b_batch.texture); b.draw(b.ctx); b.cleanup(b.ctx);
  qa_prepared_backend_draw abandoned = qa_gl_renderer_prepare_geometry(r, abandoned_batch); abandoned.cleanup(abandoned.ctx); abandoned.cleanup(abandoned.ctx);
  a.begin(a.ctx); a.apply_texture(a.ctx, 0, a_batch.texture); a.draw(a.ctx); a.cleanup(a.ctx); a.cleanup(a.ctx);
  size_t n; uint8_t *p = read_all(r, &n);
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) depth[y * 16 + x] = qa_gl_renderer_read_depth_pixel(r, x, y);
  CHECK(qa_gl_renderer_get_error(r) == 0);
  return p;
}
static void real_retained(void) {
  qa_gl_renderer r; memset(&r, 0, sizeof r);
  CHECK(qa_gl_renderer_init(&r, real_context(), test_owner()));
  qa_renderer_image image = make_image(0, 1, 1, "white");
  static const uint8_t white[4] = {255, 255, 255, 255};
  create_rgba(&r, image, white, "nearest");
  qa_gl_renderer_select_draw_buffer(&r, "back", false);
  qa_render_vertex va[3], vb[3], vc[3];
  qa_draw_batch a = retained_triangle(image, va, 0.25, 0), b = retained_triangle(image, vb, 0.75, 0.5), c = retained_triangle(image, vc, 1, -0.5);
  double d1[256], d2[256], d3[256];
  uint8_t *fresh = retained_frame(&r, a, b, c, d1), *second = retained_frame(&r, a, b, c, d2), *third = retained_frame(&r, a, b, c, d3);
  CHECK(memcmp(fresh, second, 16 * 16 * 4) == 0 && memcmp(fresh, third, 16 * 16 * 4) == 0);
  CHECK(memcmp(d1, d2, sizeof d1) == 0 && memcmp(d1, d3, sizeof d1) == 0);
  free(fresh); free(second); free(third);
  qa_prepared_backend_draw retained = qa_gl_renderer_prepare_geometry(&r, a);
  qa_gl_renderer_close(&r); retained.cleanup(retained.ctx); retained.cleanup(retained.ctx);
  CHECK_THROWS("OpenGL renderer is closed", { retained.begin(retained.ctx); });
  qa_gl_renderer_destroy(&r);
}
static bool test_real_context(void) {
  (void)setenv("SDL_VIDEODRIVER", "offscreen", 0);
  qa_sdl_window_options options; memset(&options, 0, sizeof options);
  options.title = "renderer top"; options.width = 16; options.height = 16; options.hidden = true; options.resizable = true;
  options.backend = QA_SDL_BACKEND_GL; options.stencil_bits = 8; options.display_index = -1;
  memset(&real_window, 0, sizeof real_window);
  if (!qa_sdl_window_open(&options, &real_window)) return false;
  real_stages();
  real_gamma();
  real_retained();
  qa_sdl_window_close(&real_window);
  return true;
}
int main(void) {
  CHECK(qa_identity_owner_create(&identity, "gl renderer top"));
  test_constructor();
  test_state_caches();
  test_views();
  test_prepared_phases();
  test_gamma();
  test_opacity();
  test_passes_constructed();
  test_dynamic_texture();
  bool real = test_real_context();
  printf("real GL context: %s\n", real ? "ran" : "NOT AVAILABLE, real section skipped");
  qa_identity_owner_destroy(&identity);
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
