/*
 * Port of key cases from tests/render/q3-hardware.test.ts,
 * tests/render/cpu/rasterizer.test.ts, tests/render/cpu/row-parallel.test.ts
 * (encode/decode identity), tests/render/gl/buffer-reuse.test.ts
 * (packGeometry), and tests/render/worker-protocol.test.ts /
 * worker-transport.test.ts (direct/pumped round trips).
 * TypeScript `toThrow(RangeError)` cases run in a child process and
 * must abort. GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/render.h"
#include "qa/render/commands/dynamic_texture.h"
#include "qa/render/cpu/row_parallel.h"
#include "qa/render/gl/buffers.h"
#include "qa/render/gl/fog_shader.h"
#include "qa/render/gl/programs.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static const char *g_renderer_name;
static qa_q3_hardware read_hw(void *ctx) {
  (void)ctx;
  return qa_q3_hardware_classify(g_renderer_name);
}

static void test_q3_hardware(void) {
  /* q3-hardware.test.ts: effect profiles follow renderer changes. */
  qa_q3_effect_hardware profile = qa_q3_effect_hardware_make(read_hw, NULL);
  g_renderer_name = "";
  CHECK(strcmp(qa_q3_effect_hardware_hardware(&profile), "generic") == 0);
  CHECK(strcmp(qa_q3_effect_hardware_type(&profile), "generic") == 0);
  g_renderer_name = "ATI Rage Pro";
  CHECK(strcmp(qa_q3_effect_hardware_hardware(&profile), "ragepro") == 0);
  CHECK(strcmp(qa_q3_effect_hardware_type(&profile), "rage-pro") == 0);
  g_renderer_name = "NVIDIA RIVA 128";
  CHECK(strcmp(qa_q3_effect_hardware_hardware(&profile), "generic") == 0);
  g_renderer_name = "ATI RagePro";
  CHECK(strcmp(qa_q3_effect_hardware_hardware(&profile), "ragepro") == 0);
  /* Classification precedence and enum values. */
  static const struct { const char *name; const char *hw; double value; } cases[] = {
    {"NVIDIA GeForce RTX 5060 Ti", "generic", 0},
    {"Mesa Voodoo_Graphics", "3dfx", 1},
    {"Banshee Rage Pro", "3dfx", 1},
    {"NVIDIA RIVA 128", "riva128", 2},
    {"ATI RAGE PRO", "ragepro", 3},
    {"ATI RagePro", "ragepro", 3},
    {"3Dlabs Permedia2", "permedia2", 4},
    {"", "generic", 0},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    qa_q3_hardware hw = qa_q3_hardware_classify(cases[i].name);
    CHECK(strcmp(hw, cases[i].hw) == 0);
    CHECK(qa_q3_hardware_number(hw) == cases[i].value);
  }
}

static void test_gamma_and_clip(void) {
  uint8_t table[256];
  CHECK(qa_output_gamma_table(1, table) == false);
  CHECK(qa_output_gamma_table(2, table) == true);
  CHECK(table[0] == 0 && table[255] == 255);
  CHECK_ABORTS((void)qa_output_gamma_table(0.25, table));
  CHECK_ABORTS((void)qa_output_gamma_table(NAN, table));

  qa_clipped_picture out;
  qa_rect rect = {-2, -2, 8, 8};
  qa_texture_rect uv = {0, 0, 1, 1};
  qa_rect clip = {0, 0, 4, 4};
  CHECK(qa_clip_picture(rect, uv, clip, &out));
  CHECK(out.rect.x == 0 && out.rect.y == 0 && out.rect.width == 4 && out.rect.height == 4);
  CHECK(out.uv.s1 == 0.25 && out.uv.t1 == 0.25 && out.uv.s2 == 0.75 && out.uv.t2 == 0.75);
  qa_rect zero = {0, 0, 0, 4};
  CHECK(!qa_clip_picture(zero, uv, clip, &out));
  qa_rect away = {10, 10, 2, 2};
  CHECK(!qa_clip_picture(away, uv, clip, &out));
}

struct strip_log { char text[256]; size_t length; };
static void strip_begin(void *ctx) { struct strip_log *l = ctx; l->length += (size_t)snprintf(l->text + l->length, sizeof l->text - l->length, "B"); }
static void strip_element(void *ctx, double index) { struct strip_log *l = ctx; l->length += (size_t)snprintf(l->text + l->length, sizeof l->text - l->length, "%.0f,", index); }
static void strip_end(void *ctx) { struct strip_log *l = ctx; l->length += (size_t)snprintf(l->text + l->length, sizeof l->text - l->length, "E"); }

static void test_source(void) {
  CHECK(strcmp(qa_source_primitive_mode_for(0, true), "elements") == 0);
  CHECK(strcmp(qa_source_primitive_mode_for(0, false), "array-strips") == 0);
  CHECK(strcmp(qa_source_primitive_mode_for(1, true), "array-strips") == 0);
  CHECK(strcmp(qa_source_primitive_mode_for(2, false), "elements") == 0);
  CHECK(strcmp(qa_source_primitive_mode_for(3, false), "discrete-strips") == 0);
  CHECK(strcmp(qa_source_primitive_mode_for(9, false), "none") == 0);

  struct strip_log log = {{0}, 0};
  qa_source_strip_emitter emit = {&log, strip_begin, strip_element, strip_end};
  /* Two triangles sharing edge (1,2): second continues the strip. */
  double indices[] = {0, 1, 2, 2, 1, 3};
  qa_emit_source_triangle_strips(indices, 6, emit);
  CHECK(strcmp(log.text, "B0,1,2,3,E") == 0);
  double bad[] = {0, 1};
  CHECK_ABORTS(qa_emit_source_triangle_strips(bad, 2, emit));
}

static void test_debug_graph(void) {
  qa_source_debug_graph graph;
  qa_source_debug_graph_init(&graph);
  qa_debug_graph_settings settings = {0, 1, 0, 50, 1, 0};
  qa_source_debug_graph_add_frame(&graph, 0.1, settings);
  qa_rect rect = {0, 0, 4, 100};
  qa_debug_graph_bar bars[8];
  size_t count = qa_source_debug_graph_bars(&graph, rect, settings, bars, 8);
  CHECK(count == 5);
  CHECK(bars[0].color == 8);
  /* fround(fround(0.1)*300) = 30 -> newest bar height 30. */
  CHECK(bars[1].rect.height == 30);
  settings.timegraph = 0;
  CHECK(qa_source_debug_graph_bars(&graph, rect, settings, bars, 8) == 0);

  uint8_t colors[768];
  memset(colors, 0, sizeof colors);
  colors[3] = 255; colors[4] = 128;
  qa_palette palette = {{colors, 768}, {0}};
  qa_vec4 color = qa_debug_graph_color(palette, 1);
  CHECK(color.x == 1 && color.y == 128.0 / 255 && color.w == 1);
  qa_palette short_palette = {{colors, 3}, {0}};
  CHECK_ABORTS((void)qa_debug_graph_color(short_palette, 1));
}

static qa_renderer_resource_owner test_owner(qa_identity_owner *identity, const void *token) {
  qa_renderer_resource_owner owner;
  memset(&owner, 0, sizeof owner);
  owner.identity = token;
  owner.session = identity->session;
  owner.generation = 0;
  return owner;
}

static qa_renderer_image test_image(qa_renderer_resource_owner owner, double ordinal, double w, double h) {
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.owner = owner;
  image.ordinal = ordinal;
  image.source_is_resource = false;
  image.source_name = "white";
  image.width = w;
  image.height = h;
  return image;
}

static void upload_white(qa_software_renderer *renderer, qa_renderer_image white) {
  static const uint8_t px[4] = {255, 255, 255, 255};
  qa_image_level level = {1, 1, {px, 4}};
  qa_image_resource_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_CREATE;
  op.image = white;
  op.content.kind = QA_RENDER_IMAGE_RGBA8;
  op.content.levels = &level;
  op.content.level_count = 1;
  op.sampling.wrap = "repeat";
  op.sampling.filter = "nearest";
  qa_software_renderer_apply_image_resource(renderer, op);
}

static qa_draw_batch triangle_batch(qa_renderer_image white, double z, qa_vec4 color,
                                    qa_render_vertex *verts, double *indices) {
  static const double xs[3] = {-1, 1, 0};
  static const double ys[3] = {-1, -1, 1};
  for (int i = 0; i < 3; i++) {
    verts[i].position.x = xs[i];
    verts[i].position.y = ys[i];
    verts[i].position.z = z;
    verts[i].position.w = 1;
    verts[i].tex_coord.x = 0.5;
    verts[i].tex_coord.y = 0.5;
    verts[i].color = color;
  }
  indices[0] = 0; indices[1] = 1; indices[2] = 2;
  qa_draw_batch batch;
  memset(&batch, 0, sizeof batch);
  batch.lighting.kind = "vertex";
  batch.indices.values = indices;
  batch.indices.count = 3;
  batch.texture.kind = QA_TEXTURE_BINDING_BIND_IMAGE;
  batch.texture.image = white;
  batch.state = QA_CPU_OPAQUE_STATE;
  batch.vertices = verts;
  batch.vertex_count = 3;
  return batch;
}

static void center_pixel(qa_software_renderer *renderer, uint8_t out[4]) {
  size_t length = 0;
  const uint8_t *pixels = qa_software_renderer_pixels(renderer, &length);
  memcpy(out, pixels + (4 * 8 + 4) * 4, 4);
}

static void test_cpu_triangle(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cpu test"));
  qa_renderer_resource_owner owner = test_owner(&identity, &identity);
  qa_software_renderer renderer;
  CHECK(qa_software_renderer_init(&renderer, 8, 8, owner, 8, 8, 8));
  qa_renderer_image white = test_image(owner, 0, 1, 1);
  upload_white(&renderer, white);

  qa_vec4 clear = {0.2, 0.4, 0.6, 1};
  qa_render_view_clear clear_value = {0.9, &clear, true};
  qa_render_view_state view;
  memset(&view, 0, sizeof view);
  view.viewport = (qa_rect){0, 0, 8, 8};
  view.clear = &clear_value;
  qa_software_renderer_begin_view(&renderer, view);
  uint8_t px[4];
  center_pixel(&renderer, px);
  /* byte(0.2)=51, byte(0.4)=102, byte(0.6)=153 */
  CHECK(px[0] == 51 && px[1] == 102 && px[2] == 153 && px[3] == 255);
  CHECK(qa_software_renderer_read_depth_pixel(&renderer, 4, 4) == (double)(float)0.9);

  qa_render_vertex verts[3];
  double indices[3];
  qa_vec4 red = {1, 0, 0, 1};
  qa_software_renderer_draw(&renderer, triangle_batch(white, 0, red, verts, indices));
  center_pixel(&renderer, px);
  CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);
  CHECK(qa_software_renderer_read_depth_pixel(&renderer, 4, 3) == 0.5);
  qa_software_renderer_close(&renderer);
  qa_identity_owner_destroy(&identity);
}

struct opacity_draw_ctx { qa_software_renderer *renderer; qa_renderer_image white; };
static void draw_red(void *ctx) {
  struct opacity_draw_ctx *c = ctx;
  qa_render_vertex verts[3];
  double indices[3];
  qa_vec4 red = {1, 0, 0, 1};
  qa_software_renderer_draw(c->renderer, triangle_batch(c->white, 0, red, verts, indices));
}

static void test_cpu_opacity_and_errors(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "cpu opacity"));
  qa_renderer_resource_owner owner = test_owner(&identity, &identity);
  qa_software_renderer renderer;
  CHECK(qa_software_renderer_init_default(&renderer, 8, 8, owner));
  qa_renderer_image white = test_image(owner, 0, 1, 1);
  upload_white(&renderer, white);
  qa_vec4 black = {0, 0, 0, 1};
  qa_render_view_clear clear_value = {1, &black, false};
  qa_render_view_state view;
  memset(&view, 0, sizeof view);
  view.viewport = (qa_rect){0, 0, 8, 8};
  view.clear = &clear_value;
  qa_software_renderer_begin_view(&renderer, view);
  struct opacity_draw_ctx ctx = {&renderer, white};
  qa_software_renderer_with_object_opacity(&renderer, 0.5, draw_red, &ctx);
  uint8_t px[4];
  center_pixel(&renderer, px);
  /* 0.5 * red over black backdrop: round(127.5) = 128. */
  CHECK(px[0] == 128 && px[1] == 0 && px[2] == 0);
  /* Scene depth is preserved by the opacity scope. */
  CHECK(qa_software_renderer_read_depth_pixel(&renderer, 4, 3) == 1);
  CHECK_ABORTS(qa_software_renderer_with_object_opacity(&renderer, 1.5, draw_red, &ctx));
  CHECK_ABORTS(qa_software_renderer_read_depth_pixel(&renderer, 99, 0));
  qa_software_renderer_close(&renderer);
  qa_identity_owner_destroy(&identity);
}

static void test_pack_geometry(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "pack"));
  qa_renderer_resource_owner owner = test_owner(&identity, &identity);
  qa_render_vertex verts[3];
  double indices[3];
  qa_vec4 white_color = {1, 1, 1, 1};
  qa_draw_batch batch = triangle_batch(test_image(owner, 0, 1, 1), 0, white_color, verts, indices);
  qa_gl_geometry_arrays arrays = qa_pack_gl_geometry(batch);
  CHECK(arrays.position_count == 12 && arrays.index_count == 3);
  CHECK(arrays.positions[0] == -1.0f && arrays.positions[5] == -1.0f);
  CHECK(arrays.indices[0] == 0 && arrays.indices[2] == 2);
  CHECK(arrays.world_position_count == 0);
  qa_gl_geometry_arrays_destroy(&arrays);

  /* Buffer-reuse pack serves the same values. */
  qa_gl_geometry_buffer buffer;
  CHECK(qa_gl_geometry_buffer_init(&buffer));
  qa_gl_geometry_arrays reused = qa_gl_geometry_buffer_pack(&buffer, batch);
  CHECK(reused.positions[2] == 0.0f && reused.indices[1] == 1);
  qa_gl_geometry_buffer_destroy(&buffer);

  double bad_indices[] = {0, 1, 9};
  batch.indices.values = bad_indices;
  CHECK_ABORTS((void)qa_pack_gl_geometry(batch));
  qa_identity_owner_destroy(&identity);
}

static void test_shader_sources(void) {
  char *vertex = qa_build_fog_vertex_shader_source(QA_FOG_PASS_HEIGHT);
  CHECK(strstr(vertex, "v_ray") != NULL);
  CHECK(strstr(vertex, "#version 110") == vertex);
  free(vertex);
  char *fragment = qa_build_fog_fragment_shader_source(QA_FOG_PASS_SKY);
  CHECK(strstr(fragment, "if (d < u_far_depth) discard;") != NULL);
  free(fragment);
  size_t count = 0;
  const char *const *uniforms = qa_fog_uniforms_for(QA_FOG_PASS_GLOBAL, &count);
  CHECK(count == 4 && strcmp(uniforms[3], "u_fog_color") == 0);
  char *stage = qa_gl_stage_fragment_shader();
  CHECK(strstr(stage, "dynamicLights") != NULL);
  CHECK(strstr(stage, "lproj.z - 0.0005") != NULL);
  CHECK(strstr(stage, "lproj.z - 0.0025") != NULL);
  free(stage);
}

static qa_renderer_image dynamic_resolve(void *ctx,
    void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  (void)apply; (void)apply_ctx;
  int *calls = ctx;
  (*calls)++;
  qa_renderer_image image;
  memset(&image, 0, sizeof image);
  image.ordinal = 7;
  return image;
}
static int g_apply_calls;
static void counting_apply(void *ctx, qa_image_resource_operation op) {
  (void)ctx; (void)op; g_apply_calls++;
}

static void test_wire_and_resolver(void) {
  /* createTextureResolver resolves each source once per resolver. */
  int calls = 0;
  g_apply_calls = 0;
  qa_texture_resolver resolver;
  CHECK(qa_texture_resolver_init(&resolver, counting_apply, NULL));
  qa_texture_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE;
  binding.source.ctx = &calls;
  binding.source.resolve = dynamic_resolve;
  qa_texture_binding first = qa_texture_resolver_resolve(&resolver, binding);
  qa_texture_binding second = qa_texture_resolver_resolve(&resolver, binding);
  CHECK(calls == 1);
  CHECK(first.kind == QA_TEXTURE_BINDING_BIND_IMAGE && first.image.ordinal == 7);
  CHECK(second.image.ordinal == 7);
  qa_texture_resolver_destroy(&resolver);

  /* Encoder/decoder image identity + acknowledgment round trip. */
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "wire"));
  qa_renderer_resource_owner owner = test_owner(&identity, &identity);
  qa_wire_encoder encoder;
  CHECK(qa_wire_encoder_init(&encoder, owner));
  qa_renderer_image image = test_image(owner, 3, 4, 2);
  qa_wire_image wire = qa_wire_encoder_image(&encoder, image);
  CHECK(wire.ordinal == 3 && strcmp(wire.name, "white") == 0);
  qa_image_resource_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_TEXTURE_MODE;
  op.filter = "linear";
  qa_wire_image_operation encoded = qa_wire_encoder_image_operation(&encoder, op);
  qa_image_resource_operation retained = qa_wire_encoder_acknowledge(&encoder, encoded.acknowledgment);
  CHECK(retained.kind == QA_IMAGE_OP_TEXTURE_MODE && strcmp(retained.filter, "linear") == 0);
  CHECK_ABORTS((void)qa_wire_encoder_acknowledge(&encoder, encoded.acknowledgment));
  qa_wire_encoder_destroy(&encoder);

  qa_resolved_resource_reference palette_source;
  memset(&palette_source, 0, sizeof palette_source);
  qa_wire_decoder decoder;
  CHECK(qa_wire_decoder_init(&decoder, owner, NULL, NULL, palette_source));
  qa_renderer_image decoded = qa_wire_decoder_image(&decoder, wire);
  CHECK(decoded.ordinal == 3 && decoded.width == 4 && !decoded.source_is_resource);
  qa_renderer_image again = qa_wire_decoder_image(&decoder, wire);
  CHECK(again.ordinal == decoded.ordinal && again.source_name == decoded.source_name);
  qa_wire_decoder_destroy(&decoder);
  qa_identity_owner_destroy(&identity);
}

static double first_texture_id(void *ctx, const qa_bound_texture *texture) {
  (void)ctx;
  return texture->kind == QA_BOUND_TEXTURE_INCOMPLETE ? -1 : 0;
}
static double vertex_lighting_id(void *ctx, const qa_batch_lighting *lighting) {
  (void)ctx; (void)lighting;
  return 0;
}
static double no_depth_id(void *ctx, const qa_depth_image_level *depth) {
  (void)ctx;
  return depth == NULL ? -1 : 0;
}
static qa_bound_texture incomplete_texture(void *ctx, double id) {
  (void)ctx; (void)id;
  qa_bound_texture texture;
  memset(&texture, 0, sizeof texture);
  texture.kind = QA_BOUND_TEXTURE_INCOMPLETE;
  return texture;
}
static qa_batch_lighting vertex_lighting(void *ctx, double id) {
  (void)ctx; (void)id;
  qa_batch_lighting lighting;
  memset(&lighting, 0, sizeof lighting);
  lighting.kind = "vertex";
  return lighting;
}
static const qa_depth_image_level *no_depth(void *ctx, double id) {
  (void)ctx; (void)id;
  return NULL;
}

static void test_row_setup_codec(void) {
  qa_cpu_triangle_lighting lighting;
  memset(&lighting, 0, sizeof lighting);
  lighting.base.parameters.kind = "vertex";
  lighting.positions[1] = (qa_vec3){1, 2, 3};
  qa_triangle_setup setup;
  memset(&setup, 0, sizeof setup);
  setup.lighting = &lighting;
  setup.min_x = 1; setup.max_x = 9; setup.min_y = 2; setup.max_y = 8;
  setup.inverse_area = 0.5;
  setup.blend.source = "one"; setup.blend.destination = "zero";
  setup.alpha_bits = 8;
  setup.blending = "opaque"; setup.depth_test = "less-equal"; setup.alpha_test = "none";
  setup.stencil_function = "always";
  setup.stencil_depth_fail = "keep"; setup.stencil_depth_pass = "increment";
  setup.texture.kind = QA_BOUND_TEXTURE_INCOMPLETE;
  setup.secondary_texture.kind = QA_BOUND_TEXTURE_INCOMPLETE;
  double data[QA_ROW_SETUP_STRIDE];
  qa_row_registries registries = {NULL, first_texture_id, vertex_lighting_id, no_depth_id};
  qa_encode_row_setup(data, QA_ROW_SETUP_STRIDE, 0, &setup, registries);
  CHECK(data[0] == 1 && data[4] == 0.5 && data[83] == -1);
  qa_row_decode_storage storage;
  qa_row_decode_registries decode = {NULL, incomplete_texture, vertex_lighting, no_depth};
  qa_triangle_setup decoded = qa_decode_row_setup(data, QA_ROW_SETUP_STRIDE, 0, decode, &storage);
  CHECK(decoded.min_x == 1 && decoded.max_y == 8 && decoded.inverse_area == 0.5);
  CHECK(strcmp(decoded.blending, "opaque") == 0);
  CHECK(decoded.lighting->positions[1].x == 1 && decoded.lighting->positions[1].z == 3);
}

struct exec_ctx { int images; int swaps; };
static void exec_image(void *ctx, qa_image_resource_operation op) {
  (void)op;
  ((struct exec_ctx *)ctx)->images++;
}
static void exec_swap(void *ctx, const double *captures, size_t count) {
  (void)captures; (void)count;
  ((struct exec_ctx *)ctx)->swaps++;
}

static void test_executor(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "executor"));
  qa_renderer_resource_owner owner = test_owner(&identity, &identity);
  qa_software_renderer renderer;
  CHECK(qa_software_renderer_init_default(&renderer, 8, 8, owner));
  struct exec_ctx services_ctx = {0, 0};
  qa_execution_services services = {&services_ctx, exec_image, exec_swap};
  qa_render_executor executor;
  qa_render_executor_init(&executor, qa_software_renderer_backend(&renderer), services);

  qa_renderer_image white = test_image(owner, 0, 1, 1);
  static const uint8_t px[4] = {255, 255, 255, 255};
  qa_image_level level = {1, 1, {px, 4}};
  qa_execution_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_EXEC_COMMAND_IMAGE_RESOURCE;
  command.operation.kind = QA_IMAGE_OP_CREATE;
  command.operation.image = white;
  command.operation.content.kind = QA_RENDER_IMAGE_RGBA8;
  command.operation.content.levels = &level;
  command.operation.content.level_count = 1;
  command.operation.sampling.wrap = "repeat";
  command.operation.sampling.filter = "nearest";
  qa_render_executor_execute(&executor, command);
  CHECK(services_ctx.images == 1);

  qa_render_vertex verts[3];
  double indices[3];
  qa_vec4 green = {0, 1, 0, 1};
  qa_draw_batch batch = triangle_batch(white, 0, green, verts, indices);
  qa_execution_operation operation;
  memset(&operation, 0, sizeof operation);
  operation.kind = QA_RENDER_OP_DRAW;
  operation.batches = &batch;
  operation.batch_count = 1;
  memset(&command, 0, sizeof command);
  command.kind = QA_EXEC_COMMAND_VIEW;
  command.view.state.viewport = (qa_rect){0, 0, 8, 8};
  command.view.operations = &operation;
  command.view.operation_count = 1;
  qa_render_executor_execute(&executor, command);
  uint8_t out[4];
  center_pixel(&renderer, out);
  CHECK(out[0] == 0 && out[1] == 255 && out[2] == 0);

  memset(&command, 0, sizeof command);
  command.kind = QA_EXEC_COMMAND_SWAP_BUFFERS;
  qa_render_executor_execute(&executor, command);
  CHECK(services_ctx.swaps == 1);
  qa_software_renderer_close(&renderer);
  qa_identity_owner_destroy(&identity);
}

int main(void) {
  test_q3_hardware();
  test_gamma_and_clip();
  test_source();
  test_debug_graph();
  test_cpu_triangle();
  test_cpu_opacity_and_errors();
  test_pack_geometry();
  test_shader_sources();
  test_wire_and_resolver();
  test_row_setup_codec();
  test_executor();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d render checks passed\n", checks);
  return 0;
}
