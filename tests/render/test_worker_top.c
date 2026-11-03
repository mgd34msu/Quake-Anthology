/*
 * Checks for src/render/worker.ts, worker-protocol.ts and worker-runtime.ts, from
 * tests/render/worker-protocol.test.ts and worker-native.test.ts. The transport has
 * its own checks in test_worker_transport.c. GL handoff runs through a recording fake
 * and, when SDL can open one, on a real context. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fake_gl_top.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/platform/sdl.h"
#include "qa/render/commands/dynamic_texture.h"
#include "qa/render/cpu/rasterizer.h"
#include "qa/render/execution.h"
#include "qa/render/worker.h"
#include "qa/render/worker_protocol.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
/* Fork form: only for code with no live worker thread. */
#define CHECK_THROWS(part, ...) do { checks++; int pipe_[2]; if (pipe(pipe_) != 0) abort(); pid_t pid_ = fork(); \
  if (pid_ == 0) { (void)prctl(PR_SET_DUMPABLE, 0); close(pipe_[0]); dup2(pipe_[1], 2); __VA_ARGS__; _exit(0); } \
  close(pipe_[1]); char text_[2048]; size_t n_ = 0; ssize_t got_; while (n_ + 1 < sizeof text_ && (got_ = read(pipe_[0], text_ + n_, sizeof text_ - 1 - n_)) > 0) n_ += (size_t)got_; text_[n_] = 0; close(pipe_[0]); \
  int status_ = 0; waitpid(pid_, &status_, 0); if (!WIFSIGNALED(status_) || strstr(text_, part) == NULL) { failures++; fprintf(stderr, "FAIL %s:%d: expected throw \"%s\", got \"%s\"\n", __FILE__, __LINE__, part, text_); } } while (0)
static char caught[1024];
static bool throws(void (*fn)(void *), void *ctx, const char *part) {
  caught[0] = 0;
  if (qa_range_error_catch(fn, ctx, caught, sizeof caught)) return false;
  return strstr(caught, part) != NULL;
}

static qa_identity_owner id_a, id_b, id_c;
static qa_renderer_resource_owner owner_of(qa_identity_owner *id) { qa_renderer_resource_owner o; memset(&o, 0, sizeof o); o.identity = id; o.session = id->session; return o; }
static qa_renderer_image image_of(qa_renderer_resource_owner owner, double ordinal, const char *name) {
  qa_renderer_image i; memset(&i, 0, sizeof i); i.owner = owner; i.ordinal = ordinal; i.width = 1; i.height = 1; i.source_name = name; return i;
}
static qa_resolved_resource_reference palette_source(void) { qa_resolved_resource_reference p; memset(&p, 0, sizeof p); p.id = "resource:worker-palette"; p.requested_path = "worker/palette"; return p; }
static const qa_render_vertex ONE_VERTEX = {{0, 0, 0, 1}, {0, 0}, {1, 1, 1, 1}};
static const double ZERO_INDEX[1] = {0};
static qa_draw_batch wire_batch(qa_texture_binding texture) {
  qa_draw_batch b; memset(&b, 0, sizeof b);
  b.texture = texture; b.lighting.kind = "vertex"; b.indices.values = ZERO_INDEX; b.indices.count = 1; b.vertices = &ONE_VERTEX; b.vertex_count = 1;
  b.state.blend.source = "one"; b.state.blend.destination = "zero"; b.state.depth_test = "less-equal"; b.state.depth_write = true; b.state.alpha_test = "none"; b.state.cull = "back"; b.state.depth_range[1] = 1;
  return b;
}
static qa_texture_binding bind_of(qa_renderer_image image) { qa_texture_binding b; memset(&b, 0, sizeof b); b.kind = QA_TEXTURE_BINDING_BIND_IMAGE; b.image = image; return b; }

/* --- worker-protocol.test.ts --- */
static void protocol_identity(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; CHECK(qa_wire_encoder_init(&encoder, frontend));
  qa_wire_decoder decoder; CHECK(qa_wire_decoder_init(&decoder, receiver, NULL, NULL, palette_source()));
  qa_renderer_image original = image_of(frontend, 0, "wire test");
  qa_renderer_image first = qa_wire_decoder_image(&decoder, qa_wire_encoder_image(&encoder, original));
  qa_renderer_image second = qa_wire_decoder_image(&decoder, qa_wire_encoder_image(&encoder, original));
  CHECK(first.source_name == second.source_name); CHECK(first.owner.identity == receiver.identity); CHECK(first.source_name != original.source_name);
  CHECK(qa_wire_encoder_original_image(&encoder, 0).source_name == original.source_name);
  CHECK_THROWS("another renderer lifetime", { (void)qa_wire_encoder_image(&encoder, image_of(owner_of(&id_c), 0, "x")); });
  qa_renderer_image other = original; other.width = 2;
  CHECK_THROWS("another identity", { (void)qa_wire_encoder_image(&encoder, other); });
  qa_wire_image changed = {0, 2, 1, "wire test"};
  CHECK_THROWS("metadata changed", { (void)qa_wire_decoder_image(&decoder, changed); });
  qa_wire_image negative = {-1, 1, 1, "bad"};
  CHECK_THROWS("Invalid renderer wire integer", { (void)qa_wire_decoder_image(&decoder, negative); });
  qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}
static void protocol_snapshots(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; qa_wire_encoder_init(&encoder, frontend);
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, receiver, NULL, NULL, palette_source());
  uint8_t pixels[1] = {1}, colors[768]; memset(colors, 0, sizeof colors); colors[0] = 33;
  uint8_t translation_bytes[256]; memset(translation_bytes, 5, sizeof translation_bytes);
  qa_byte_view translation = {translation_bytes, 256}; qa_fullbright_range bright = {224, 255};
  qa_image_level level; memset(&level, 0, sizeof level); level.width = 1; level.height = 1; level.pixels.bytes = pixels; level.pixels.length = 1;
  qa_image_resource_operation creation; memset(&creation, 0, sizeof creation);
  creation.kind = QA_IMAGE_OP_CREATE; creation.image = image_of(frontend, 0, "a"); creation.sampling.filter = "nearest"; creation.sampling.wrap = "repeat";
  creation.content.kind = QA_RENDER_IMAGE_INDEXED8; creation.content.levels = &level; creation.content.level_count = 1;
  creation.content.palette.colors.bytes = colors; creation.content.palette.colors.length = 768;
  creation.content.palette.source.requested_path = "original/palette"; creation.content.transparency.kind = "opaque";
  creation.content.translation = &translation; creation.content.fullbright = &bright;
  qa_wire_image_operation wire = qa_wire_encoder_image_operation(&encoder, creation);
  pixels[0] = 9; colors[0] = 88; translation_bytes[0] = 6; bright.first = 0; level.width = 7;
  qa_image_resource_operation restored = qa_wire_decoder_image_operation(&decoder, wire);
  CHECK(restored.kind == QA_IMAGE_OP_CREATE && restored.content.kind == QA_RENDER_IMAGE_INDEXED8);
  CHECK(restored.content.levels[0].pixels.bytes[0] == 1 && restored.content.levels[0].width == 1);
  CHECK(restored.content.palette.colors.bytes[0] == 33);
  CHECK(restored.content.translation->bytes[0] == 5 && restored.content.fullbright->first == 224);
  CHECK(strcmp(restored.content.palette.source.requested_path, "worker/palette") == 0);
  /* The frontend's retained copy keeps its own provenance. */
  qa_image_resource_operation acknowledged = qa_wire_encoder_acknowledge(&encoder, qa_wire_decoder_acknowledgment(&decoder, &restored));
  CHECK(strcmp(acknowledged.content.palette.source.requested_path, "original/palette") == 0);
  qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}
static int paired_calls; static qa_wire_encoder *paired_encoder; static qa_wire_decoder *paired_decoder; static qa_renderer_image paired_original;
static qa_renderer_image paired_source(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) { (void)ctx; (void)apply; (void)apply_ctx; paired_calls++; return paired_original; }
static void paired_noop(void *ctx, qa_image_resource_operation op) { (void)ctx; (void)op; }
static qa_renderer_image paired_reached(void *ctx, double token, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  CHECK(ctx == &paired_calls); (void)apply; (void)apply_ctx;
  qa_dynamic_image_source source = qa_wire_encoder_source(paired_encoder, token);
  return qa_wire_decoder_image(paired_decoder, qa_wire_encoder_image(paired_encoder, source.resolve(source.ctx, paired_noop, NULL)));
}
static void protocol_paired_dynamic(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; qa_wire_encoder_init(&encoder, frontend);
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, receiver, paired_reached, &paired_calls, palette_source());
  paired_encoder = &encoder; paired_decoder = &decoder; paired_original = image_of(frontend, 0, "wire test"); paired_calls = 0;
  qa_texture_binding dynamic; memset(&dynamic, 0, sizeof dynamic); dynamic.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE; dynamic.source.ctx = &paired_calls; dynamic.source.resolve = paired_source;
  qa_multitexture_vertex vertex; vertex.vertex = ONE_VERTEX; vertex.tex_coord2 = (qa_vec2){1, 1};
  qa_draw_batch paired = wire_batch(dynamic); paired.vertices = NULL; paired.vertex_count = 0; paired.pair_vertices = &vertex; paired.pair_vertex_count = 1; paired.texturing_is_pair = true;
  paired.second_texture.environment = "modulate"; paired.second_texture.binding = dynamic;
  qa_wire_arena *arena = qa_wire_arena_create();
  qa_wire_batch wire = qa_wire_encoder_batch(&encoder, arena, paired);
  qa_draw_batch decoded = qa_wire_decoder_batch(&decoder, &wire);
  CHECK(paired_calls == 0);
  CHECK(decoded.texture.kind == QA_TEXTURE_BINDING_DYNAMIC_IMAGE && decoded.second_texture.binding.kind == QA_TEXTURE_BINDING_DYNAMIC_IMAGE);
  CHECK(decoded.texture.source.ctx == decoded.second_texture.binding.source.ctx);
  qa_draw_batch resolved;
  CHECK(qa_resolve_draw_textures(decoded, paired_noop, NULL, &resolved));
  CHECK(paired_calls == 1);
  qa_wire_arena_destroy(arena); qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}
typedef struct decode_scope { qa_wire_decoder *d; qa_wire_batch *b; } decode_scope;
static void decode_batch_body(void *ctx) { decode_scope *s = ctx; (void)qa_wire_decoder_batch(s->d, s->b); }
static void protocol_prefix(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; qa_wire_encoder_init(&encoder, frontend);
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, receiver, NULL, NULL, palette_source());
  qa_draw_batch batches[2] = {wire_batch(bind_of(image_of(frontend, 0, "ok"))), wire_batch(bind_of(image_of(owner_of(&id_c), 1, "other")))};
  qa_render_operation before; memset(&before, 0, sizeof before); before.kind = QA_RENDER_OP_CULL; before.cull = "none";
  qa_render_operation draw; memset(&draw, 0, sizeof draw); draw.kind = QA_RENDER_OP_DRAW; draw.batches = batches; draw.batch_count = 2;
  qa_render_command command; memset(&command, 0, sizeof command); command.kind = QA_RENDER_COMMAND_VIEW;
  command.view.state.viewport.width = 10; command.view.state.viewport.height = 10;
  command.view.before_view = &before; command.view.before_view_count = 1; command.view.operations = &draw; command.view.operation_count = 1;
  qa_wire_arena *arena = qa_wire_arena_create();
  qa_wire_command wire = qa_wire_encoder_command(&encoder, arena, command, NULL, 0);
  CHECK(wire.failure == NULL && qa_wire_decoder_command(&decoder, &wire).kind == QA_EXEC_COMMAND_VIEW);
  qa_execution_operation first = qa_wire_decoder_operation(&decoder, &wire.view.before_view[0]);
  CHECK(first.kind == QA_RENDER_OP_CULL && strcmp(first.cull, "none") == 0);
  qa_execution_operation op = qa_wire_decoder_operation(&decoder, &wire.view.operations[0]);
  CHECK(op.kind == QA_RENDER_OP_DRAW && wire.view.operations[0].batch_count == 2);
  decode_scope ok = {&decoder, &wire.view.operations[0].batches[0]}, bad = {&decoder, &wire.view.operations[0].batches[1]};
  char message[256];
  CHECK(qa_range_error_catch(decode_batch_body, &ok, message, sizeof message));
  CHECK(!qa_range_error_catch(decode_batch_body, &bad, message, sizeof message) && strstr(message, "another renderer lifetime") != NULL);
  qa_wire_arena_destroy(arena); qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}
static void protocol_immediate_families(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; qa_wire_encoder_init(&encoder, frontend);
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, receiver, NULL, NULL, palette_source());
  qa_renderer_image white = image_of(frontend, 0, "white");
  qa_vec4 v = {0, 0, 0, 1};
  double positions[4] = {0, 0, 0, 1}; double indices[1] = {0};
  qa_depth_atlas_draw atlas_draw; memset(&atlas_draw, 0, sizeof atlas_draw); atlas_draw.positions = positions; atlas_draw.position_count = 4; atlas_draw.indices.values = indices; atlas_draw.indices.count = 1; atlas_draw.cull = "none";
  qa_depth_atlas_pass pass; memset(&pass, 0, sizeof pass); pass.viewport.width = 1; pass.viewport.height = 1; pass.has_clear_depth = true; pass.clear_depth = 1; pass.draws = &atlas_draw; pass.draw_count = 1;
  qa_vertex_position_tex_coord sky_vertex; memset(&sky_vertex, 0, sizeof sky_vertex); sky_vertex.position = v;
  qa_sky_strip strip = {&sky_vertex, 1};
  qa_polygon_offset offset = {1, 2};
  qa_render_operation ops[9]; memset(ops, 0, sizeof ops);
  ops[0].kind = QA_RENDER_OP_DISABLE_PORTAL_CLIP;
  ops[1].kind = QA_RENDER_OP_DEPTH_RANGE; ops[1].range[1] = 1;
  ops[2].kind = QA_RENDER_OP_CULL; ops[2].cull = "back";
  ops[3].kind = QA_RENDER_OP_POLYGON_OFFSET; ops[3].polygon_offset = &offset;
  ops[4].kind = QA_RENDER_OP_DEPTH_ATLAS; ops[4].image = white; ops[4].passes = &pass; ops[4].pass_count = 1;
  ops[5].kind = QA_RENDER_OP_SKY_SIDE; ops[5].image = white; ops[5].color = v; ops[5].strips = &strip; ops[5].strip_count = 1;
  ops[6].kind = QA_RENDER_OP_SHADOW_VOLUME; ops[6].white_image = white; ops[6].positions = &v; ops[6].position_count = 1; ops[6].indices.values = indices; ops[6].indices.count = 1;
  ops[7].kind = QA_RENDER_OP_SHADOW_FINISH; ops[7].white_image = white; ops[7].finish_positions[0] = v;
  ops[8].kind = QA_RENDER_OP_Q2_FOG; ops[8].fog.fog.kind = "q2"; ops[8].fog.far_depth = 1; ops[8].fog.sky_drawn = true; ops[8].fog.fog.density = 0.1;
  qa_wire_arena *arena = qa_wire_arena_create();
  for (int i = 0; i < 9; i++) {
    qa_wire_operation wire = qa_wire_encoder_operation(&encoder, arena, ops[i]);
    qa_execution_operation back = qa_wire_decoder_operation(&decoder, &wire);
    CHECK(back.kind == ops[i].kind);
    if (i == 3) { CHECK(back.polygon_offset != &offset && back.polygon_offset->units == 2); }
    if (i == 4) { CHECK(back.image.owner.identity == receiver.identity && back.passes[0].draws[0].positions != positions && back.passes[0].draws[0].positions[3] == 1); }
    if (i == 5) { CHECK(back.strips[0].vertices != &sky_vertex && back.strips[0].vertex_count == 1); }
    if (i == 6 || i == 7) { CHECK(back.white_image.owner.identity == receiver.identity); }
    if (i == 8) { CHECK(back.fog.sky_drawn && back.fog.fog.density == 0.1); }
  }
  ops[8].fog.fog.kind = "q1";
  qa_wire_operation bad_fog = qa_wire_encoder_operation(&encoder, arena, ops[8]);
  CHECK_THROWS("Invalid Q2 fog kind", { (void)qa_wire_decoder_operation(&decoder, &bad_fog); });
  ops[2].cull = "sideways";
  qa_wire_operation bad_cull = qa_wire_encoder_operation(&encoder, arena, ops[2]);
  CHECK_THROWS("Invalid renderer wire variant", { (void)qa_wire_decoder_operation(&decoder, &bad_cull); });
  qa_wire_arena_destroy(arena); qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}
static void protocol_malformed(void) {
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, owner_of(&id_b), NULL, NULL, palette_source());
  qa_wire_command invented; memset(&invented, 0, sizeof invented); invented.kind = QA_RENDER_COMMAND_DRAW_BUFFER; invented.buffer = "invented";
  CHECK_THROWS("Invalid renderer wire variant", { (void)qa_wire_decoder_command(&decoder, &invented); });
  uint8_t pixels[4] = {0};
  qa_image_level bad; memset(&bad, 0, sizeof bad); bad.width = 2; bad.height = 1; bad.pixels.bytes = pixels; bad.pixels.length = 4;
  CHECK_THROWS("Invalid renderer RGBA readback", { (void)qa_decode_wire_image_level(bad); });
  bad.width = 1;
  CHECK(qa_decode_wire_image_level(bad).width == 1);
  qa_wire_command failed; memset(&failed, 0, sizeof failed); failed.failure = "encoding failed here";
  CHECK_THROWS("encoding failed here", { (void)qa_wire_decoder_command(&decoder, &failed); });
  qa_wire_decoder_destroy(&decoder);
}
static void protocol_acknowledgments(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; qa_wire_encoder_init(&encoder, frontend);
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, receiver, NULL, NULL, palette_source());
  qa_renderer_image original = image_of(frontend, 0, "wire test");
  uint8_t pixels[4] = {4, 3, 2, 1};
  qa_image_level level; memset(&level, 0, sizeof level); level.width = 1; level.height = 1; level.pixels.bytes = pixels; level.pixels.length = 4;
  qa_image_resource_operation op; memset(&op, 0, sizeof op);
  op.kind = QA_IMAGE_OP_CREATE; op.image = original; op.sampling.filter = "nearest"; op.sampling.wrap = "repeat"; op.content.kind = QA_RENDER_IMAGE_RGBA8; op.content.levels = &level; op.content.level_count = 1;
  qa_image_resource_operation decoded = qa_wire_decoder_image_operation(&decoder, qa_wire_encoder_image_operation(&encoder, op));
  memset(pixels, 0, 4);
  double token = qa_wire_decoder_acknowledgment(&decoder, &decoded);
  qa_image_resource_operation acknowledged = qa_wire_encoder_acknowledge(&encoder, token);
  CHECK(acknowledged.kind == QA_IMAGE_OP_CREATE && acknowledged.image.source_name == original.source_name);
  CHECK(acknowledged.content.levels[0].pixels.bytes[0] == 4 && acknowledged.content.levels[0].pixels.bytes[3] == 1);
  CHECK_THROWS("Unknown renderer image acknowledgment", { (void)qa_wire_encoder_acknowledge(&encoder, token); });
  CHECK_THROWS("has no acknowledgment", { (void)qa_wire_decoder_acknowledgment(&decoder, &decoded); });
  /* "image operations preserve depth storage, update levels, filters and acknowledgments" */
  float depth[1] = {0.75f};
  qa_depth_image_level depth_level; memset(&depth_level, 0, sizeof depth_level); depth_level.width = 1; depth_level.height = 1; depth_level.pixels = depth; depth_level.pixel_count = 1;
  qa_image_resource_operation ops[4]; memset(ops, 0, sizeof ops);
  ops[0].kind = QA_IMAGE_OP_CREATE; ops[0].image = original; ops[0].content.kind = QA_RENDER_IMAGE_DEPTH32F; ops[0].content.depth_levels = &depth_level; ops[0].content.depth_level_count = 1; ops[0].sampling.wrap = "clamp"; ops[0].sampling.filter = "linear";
  ops[1].kind = QA_IMAGE_OP_UPDATE; ops[1].image = original; ops[1].update_depth_level = &depth_level;
  ops[2].kind = QA_IMAGE_OP_TEXTURE_MODE; ops[2].filter = "linear-mipmap-linear";
  ops[3].kind = QA_IMAGE_OP_RELEASE; ops[3].image = original;
  for (int i = 0; i < 4; i++) {
    qa_image_resource_operation back = qa_wire_decoder_image_operation(&decoder, qa_wire_encoder_image_operation(&encoder, ops[i]));
    CHECK(back.kind == ops[i].kind);
    if (i == 0) CHECK(back.content.depth_levels[0].pixels[0] == 0.75f && back.content.depth_levels[0].pixels != depth);
    if (i == 1) CHECK(back.update_depth_level->pixels[0] == 0.75f);
    if (i == 2) CHECK(strcmp(back.filter, "linear-mipmap-linear") == 0);
    qa_image_resource_operation done = qa_wire_encoder_acknowledge(&encoder, qa_wire_decoder_acknowledgment(&decoder, &back));
    CHECK(done.kind == ops[i].kind);
  }
  /* A deferred encoding failure consumes no acknowledgment and raises when decoded. */
  qa_image_resource_operation foreign; memset(&foreign, 0, sizeof foreign); foreign.kind = QA_IMAGE_OP_RELEASE; foreign.image = image_of(owner_of(&id_c), 0, "x");
  qa_wire_image_operation failed = qa_wire_encoder_image_operation(&encoder, foreign);
  CHECK(failed.failure != NULL);
  CHECK_THROWS("another renderer lifetime", { (void)qa_wire_decoder_image_operation(&decoder, failed); });
  qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}
static void protocol_lighting(void) {
  qa_renderer_resource_owner frontend = owner_of(&id_a), receiver = owner_of(&id_b);
  qa_wire_encoder encoder; qa_wire_encoder_init(&encoder, frontend);
  qa_wire_decoder decoder; qa_wire_decoder_init(&decoder, receiver, NULL, NULL, palette_source());
  qa_renderer_image white = image_of(frontend, 0, "white");
  qa_q2_shadow_atlas atlas; memset(&atlas, 0, sizeof atlas); atlas.image = white; atlas.texel_size = 0.01; atlas.near_plane = 4;
  qa_vec3 position = {1, 2, 3}, fraction = {0.1, 0.2, 0.3};
  qa_q2_light_cone cone = {{0, 0, 1}, 0.5};
  qa_q2_fragment_light light; memset(&light, 0, sizeof light); light.origin = position; light.radius = 20; light.color = (qa_vec3){-1, 1, 1}; light.scale = 1; light.cone = &cone; light.shadow.kind = "point"; light.shadow.atlas_rect = (qa_vec4){0, 0, 1, 1}; light.fraction = fraction;
  qa_q2_model_shadow_light model; memset(&model, 0, sizeof model); model.origin = position; model.radius = 20; model.fraction = fraction; model.shadow.kind = "point";
  const char *passes[4] = {"lightmap", "texture", "material-lightmap", "model"};
  qa_batch_fog fog; memset(&fog, 0, sizeof fog); fog.kind = "exp2"; fog.color = fraction; fog.density = 0.1; fog.effect = "rgba";
  qa_wire_arena *arena = qa_wire_arena_create();
  for (int i = 0; i < 5; i++) {
    qa_draw_batch original = wire_batch(bind_of(white));
    original.primitive_is_lines = true; original.line_width = 2; original.fog = &fog; original.texture_effect = "luminance-alpha"; original.space = "world";
    original.lighting.world_positions = &position; original.lighting.world_position_count = 1;
    if (i < 4) { original.lighting.kind = "q2-world"; original.lighting.pass = passes[i]; original.lighting.normals = &position; original.lighting.normal_count = 1; original.lighting.lights = &light; original.lighting.light_count = 1; original.lighting.atlas = i == 0 ? NULL : &atlas; }
    else { original.lighting.kind = "q2-model-shadow"; original.lighting.atlas_value = atlas; original.lighting.model_shadow_lights = &model; original.lighting.model_shadow_light_count = 1; original.lighting.has_shade_scale = true; original.lighting.shade_scale = 0.5; }
    qa_wire_batch *wire = qa_wire_arena_copy(arena, &(qa_wire_batch){0}, sizeof(qa_wire_batch));
    *wire = qa_wire_encoder_batch(&encoder, arena, original);
    position.x = 99; light.radius = 99; cone.cos_half_angle = 0.9; /* the snapshot must not see these */
    qa_draw_batch restored = qa_wire_decoder_batch(&decoder, wire);
    position.x = 1; light.radius = 20; cone.cos_half_angle = 0.5;
    CHECK(restored.primitive_is_lines && restored.line_width == 2 && strcmp(restored.fog->effect, "rgba") == 0 && restored.fog->density == 0.1 && strcmp(restored.texture_effect, "luminance-alpha") == 0);
    CHECK(restored.space == NULL); /* TS WireDecoder.batch rebuilds the record without space */
    CHECK(restored.lighting.world_positions[0].x == 1);
    if (i < 4) {
      CHECK(strcmp(restored.lighting.pass, passes[i]) == 0 && restored.lighting.lights[0].radius == 20 && restored.lighting.lights[0].cone->cos_half_angle == 0.5 && restored.lighting.lights[0].fraction.z == 0.3);
      if (i == 0) CHECK(restored.lighting.atlas == NULL);
      else CHECK(restored.lighting.atlas->image.owner.identity == receiver.identity && restored.lighting.atlas->near_plane == 4);
    } else { CHECK(restored.lighting.atlas_value.image.owner.identity == receiver.identity && restored.lighting.shade_scale == 0.5 && restored.lighting.model_shadow_lights[0].radius == 20); }
    /* Re-encoding the restored batch through a receiver encoder is stable. */
    qa_wire_encoder again; qa_wire_encoder_init(&again, receiver);
    qa_wire_batch twice = qa_wire_encoder_batch(&again, arena, restored);
    CHECK(twice.failure == NULL && twice.has_atlas == wire->has_atlas && twice.batch.indices.count == wire->batch.indices.count && twice.batch.line_width == wire->batch.line_width);
    qa_wire_encoder_destroy(&again);
  }
  qa_wire_arena_destroy(arena); qa_wire_decoder_destroy(&decoder); qa_wire_encoder_destroy(&encoder);
}

/* --- worker frontend over a CPU backend: no SDL needed --- */
static int window_sentinel, swap_sentinel, failed_sentinel;
static int detach_calls, restore_calls, swap_calls, failed_calls;
static char failed_message[1024];
static pthread_t main_thread;
static bool host_on_main = true;
static uint8_t *last_frame; static size_t last_frame_length; static double last_ids[8]; static size_t last_id_count;
static void win_size(void *ctx, double *w, double *h) { CHECK(ctx == &window_sentinel); *w = 16; *h = 16; }
static qa_sdl_render_context_transfer win_detach(void *ctx) { CHECK(ctx == &window_sentinel); detach_calls++; qa_sdl_render_context_transfer t; memset(&t, 0, sizeof t); t.window_id = 77; return t; }
static void win_restore(void *ctx) { CHECK(ctx == &window_sentinel); CHECK(pthread_equal(pthread_self(), main_thread)); restore_calls++; }
static qa_worker_window test_window(bool is_gl) { qa_worker_window w; memset(&w, 0, sizeof w); w.ctx = &window_sentinel; w.is_gl = is_gl; w.drawable_size = win_size; w.detach_render_context = win_detach; w.restore_render_context = win_restore; return w; }
static void on_swap(void *ctx, const qa_image_level *image, const double *ids, size_t count) {
  CHECK(ctx == &swap_sentinel);
  if (!pthread_equal(pthread_self(), main_thread)) host_on_main = false;
  swap_calls++;
  free(last_frame); last_frame = NULL; last_frame_length = 0;
  if (image != NULL) { last_frame = malloc(image->pixels.length); memcpy(last_frame, image->pixels.bytes, image->pixels.length); last_frame_length = image->pixels.length; }
  last_id_count = count < 8 ? count : 8; for (size_t i = 0; i < last_id_count; i++) last_ids[i] = ids[i];
}
static void on_failed(void *ctx, const char *message) { CHECK(ctx == &failed_sentinel); failed_calls++; (void)snprintf(failed_message, sizeof failed_message, "%s", message); }
static void reset_window_counts(void) { detach_calls = restore_calls = swap_calls = failed_calls = 0; failed_message[0] = 0; host_on_main = true; free(last_frame); last_frame = NULL; last_frame_length = 0; last_id_count = 0; }

static const qa_render_vertex QUAD[4] = {
  {{-1, -1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}}, {{1, -1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}},
  {{1, 1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}}, {{-1, 1, 0, 1}, {0.5, 0.5}, {1, 1, 1, 1}}};
static const double QUAD_INDICES[6] = {0, 1, 2, 0, 2, 3};
static qa_draw_batch quad_batch(qa_texture_binding texture) {
  qa_draw_batch b = wire_batch(texture); b.indices.values = QUAD_INDICES; b.indices.count = 6; b.vertices = QUAD; b.vertex_count = 4; b.state.cull = "none"; return b;
}
static qa_multitexture_vertex PAIR[4];
static qa_image_resource_operation create_op(qa_renderer_image image, const uint8_t *pixels, qa_image_level *level) {
  memset(level, 0, sizeof *level); level->width = image.width; level->height = image.height; level->pixels.bytes = pixels; level->pixels.length = (size_t)(image.width * image.height * 4);
  qa_image_resource_operation op; memset(&op, 0, sizeof op); op.kind = QA_IMAGE_OP_CREATE; op.image = image; op.content.kind = QA_RENDER_IMAGE_RGBA8; op.content.levels = level; op.content.level_count = 1; op.sampling.wrap = "clamp"; op.sampling.filter = "nearest";
  return op;
}
/* TS worker-native.test.ts dynamic sources; order[] records the ctx-visible sequence. */
static char order_log[256]; static qa_renderer_image dyn_first, dyn_second; static uint8_t dyn_first_value[4], dyn_second_value[4];
static void note(const char *s) { strncat(order_log, s, sizeof order_log - strlen(order_log) - 1); strncat(order_log, " ", sizeof order_log - strlen(order_log) - 1); }
static qa_image_level dyn_levels[4]; static int dyn_level_cursor;
static qa_renderer_image resolve_with(qa_renderer_image image, const uint8_t *value, void (*apply)(void *, qa_image_resource_operation), void *apply_ctx) {
  qa_image_level *level = &dyn_levels[dyn_level_cursor++ % 4]; memset(level, 0, sizeof *level); level->width = 1; level->height = 1; level->pixels.bytes = value; level->pixels.length = 4;
  qa_image_resource_operation op; memset(&op, 0, sizeof op); op.kind = QA_IMAGE_OP_UPDATE; op.image = image; op.update_level = level;
  apply(apply_ctx, op);
  return image;
}
static int first_ctx, second_ctx, callback_ctx;
static qa_renderer_image source_first(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *a) {
  CHECK(ctx == &first_ctx); CHECK(pthread_equal(pthread_self(), main_thread)); note("first"); return resolve_with(dyn_first, dyn_first_value, apply, a); }
static qa_renderer_image source_second(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *a) {
  CHECK(ctx == &second_ctx); note("second"); return resolve_with(dyn_second, dyn_second_value, apply, a); }
static qa_renderer_image source_callback(void *ctx, void (*apply)(void *, qa_image_resource_operation), void *a) {
  CHECK(ctx == &callback_ctx); note("dynamic"); qa_renderer_image i = resolve_with(dyn_first, dyn_first_value, apply, a); note("uploaded"); return i; }
typedef struct nested_scope { qa_renderer_backend backend; qa_draw_batch plain; } nested_scope;
static void nested_draw(void *ctx) {
  nested_scope *s = ctx;
  note("outer");
  qa_prepared_backend_draw d = s->backend.prepare_geometry(s->backend.ctx, s->plain);
  d.begin(d.ctx);
  qa_texture_binding dynamic; memset(&dynamic, 0, sizeof dynamic); dynamic.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE; dynamic.source.ctx = &callback_ctx; dynamic.source.resolve = source_callback;
  d.apply_texture(d.ctx, 0, dynamic);
  d.draw(d.ctx); d.cleanup(d.ctx);
  note("return");
}
static qa_render_command view_command(const qa_render_operation *ops, size_t count, qa_render_view_clear *clear, qa_vec4 *color) {
  color->x = 0; color->y = 0; color->z = 0; color->w = 1; clear->depth = 1; clear->color = color; clear->stencil = true;
  qa_render_command c; memset(&c, 0, sizeof c); c.kind = QA_RENDER_COMMAND_VIEW; c.view.state.viewport.width = 16; c.view.state.viewport.height = 16; c.view.state.clear = clear; c.view.operations = ops; c.view.operation_count = count;
  return c;
}
/* The scenario on one backend, worker or serial; results returned for comparison. */
typedef struct scenario_result { uint8_t packet[1024], nested[1024], stencil[256]; double depth; } scenario_result;
static void pair_batch_setup(qa_draw_batch *pair) {
  for (int i = 0; i < 4; i++) { PAIR[i].vertex = QUAD[i]; PAIR[i].tex_coord2 = QUAD[i].tex_coord; }
  pair->vertices = NULL; pair->vertex_count = 0; pair->pair_vertices = PAIR; pair->pair_vertex_count = 4; pair->texturing_is_pair = true;
  pair->texture.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE; pair->texture.source.ctx = &first_ctx; pair->texture.source.resolve = source_first;
  pair->second_texture.environment = "add"; memset(&pair->second_texture.binding, 0, sizeof pair->second_texture.binding);
  pair->second_texture.binding.kind = QA_TEXTURE_BINDING_DYNAMIC_IMAGE; pair->second_texture.binding.source.ctx = &second_ctx; pair->second_texture.binding.source.resolve = source_second;
}
static void worker_scenario(scenario_result *out) {
  reset_window_counts();
  qa_renderer_resource_owner owner = owner_of(&id_a);
  dyn_first = image_of(owner, 0, "dynamic first"); dyn_second = image_of(owner, 1, "dynamic second");
  qa_render_image_journal journal; CHECK(qa_render_image_journal_init(&journal));
  qa_worker_renderer r;
  CHECK(qa_worker_renderer_open(&r, test_window(false), owner, 1.25, NULL, &journal, on_swap, &swap_sentinel, on_failed, &failed_sentinel, NULL));
  CHECK(detach_calls == 0);
  qa_worker_description d = qa_worker_renderer_description(&r, NULL, NULL);
  CHECK(strcmp(d.backend, "cpu") == 0 && d.width == 16 && d.height == 16 && d.stencil_bits == 8 && d.swap_interval == 0 && d.driver == NULL && d.gl_config == NULL);
  qa_renderer_backend backend = qa_worker_renderer_backend(&r);
  static const uint8_t black[4] = {0, 0, 0, 255};
  qa_image_level l1, l2;
  qa_image_resource_operation c1 = create_op(dyn_first, black, &l1), c2 = create_op(dyn_second, black, &l2);
  qa_render_command setup[2]; memset(setup, 0, sizeof setup);
  setup[0].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; setup[0].operation = c1; setup[1].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; setup[1].operation = c2;
  qa_worker_renderer_execute(&r, setup, 2, NULL, 0, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  qa_journal_image_description rows[4];
  CHECK(qa_render_image_journal_describe(&journal, rows, 4) == 2); /* image-applied reached the frontend journal */
  order_log[0] = 0; memset(dyn_first_value, 0, 4); dyn_first_value[0] = 64; dyn_first_value[3] = 255; memset(dyn_second_value, 0, 4); dyn_second_value[1] = 64; dyn_second_value[3] = 255;
  qa_draw_batch pair = quad_batch(bind_of(dyn_first)); pair_batch_setup(&pair);
  backend.set_overdraw_measurement(backend.ctx, true);
  qa_render_operation draw; memset(&draw, 0, sizeof draw); draw.kind = QA_RENDER_OP_DRAW; draw.batches = &pair; draw.batch_count = 1;
  qa_render_view_clear clear; qa_vec4 color; qa_render_command view = view_command(&draw, 1, &clear, &color);
  qa_render_command buffer; memset(&buffer, 0, sizeof buffer); buffer.kind = QA_RENDER_COMMAND_DRAW_BUFFER; buffer.buffer = "back"; buffer.clear = false;
  qa_render_command frame[2] = {buffer, view};
  qa_worker_renderer_execute(&r, frame, 2, NULL, 0, NULL, 0);
  out->depth = backend.read_depth_pixel(backend.ctx, 8, 8);
  CHECK(fabs(out->depth - 0.5) < 1e-5);
  backend.read_stencil_overdraw(backend.ctx, out->stencil, 256);
  bool ones = true; for (int i = 0; i < 256; i++) if (out->stencil[i] != 1) ones = false; CHECK(ones);
  qa_render_command swap; memset(&swap, 0, sizeof swap); swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  double ids[1] = {41};
  qa_worker_renderer_execute(&r, &swap, 1, ids, 1, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(swap_calls == 1 && last_id_count == 1 && last_ids[0] == 41 && host_on_main);
  CHECK(last_frame_length == 1024);
  CHECK(strcmp(order_log, "first second ") == 0);
  memcpy(out->packet, last_frame, 1024);
  CHECK(out->packet[0] > 0 && out->packet[1] > 0 && out->packet[2] == 0);
  backend.set_overdraw_measurement(backend.ctx, false);
  /* TS beginView + withObjectOpacity with a dynamic source in the nested draw. */
  order_log[0] = 0; memset(dyn_first_value, 0, 4); dyn_first_value[0] = 32; dyn_first_value[3] = 255;
  backend.begin_view(backend.ctx, view.view.state);
  nested_scope scope = {backend, quad_batch(bind_of(dyn_first))};
  backend.with_object_opacity(backend.ctx, 0.5, nested_draw, &scope);
  double ids2[1] = {42};
  qa_worker_renderer_execute(&r, &swap, 1, ids2, 1, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(strcmp(order_log, "outer dynamic uploaded return ") == 0);
  CHECK(swap_calls == 2 && last_ids[0] == 42);
  memcpy(out->nested, last_frame, 1024);
  CHECK(out->nested[0] > 0 && out->nested[0] < out->packet[0]);
  /* Barriers, gamma, pixels. */
  qa_image_level read; qa_worker_renderer_read_pixels(&r, &read);
  CHECK(read.width == 16 && read.pixels.length == 1024 && memcmp(read.pixels.bytes, out->nested, 1024) == 0);
  free((void *)read.pixels.bytes);
  qa_worker_renderer_set_output_gamma(&r, 1.25);
  qa_worker_renderer_set_swap_interval(&r, 1);
  CHECK(qa_worker_renderer_description(&r, NULL, NULL).swap_interval == 0); /* TS: no context, stays 0 */
  /* TS CPU resize replaces the renderer and replays the worker's journal. */
  qa_worker_renderer_resize(&r, 32, 8);
  CHECK(r.width == 32 && r.height == 8);
  qa_render_command pic; memset(&pic, 0, sizeof pic); pic.kind = QA_RENDER_COMMAND_STRETCH_PIC; pic.image = dyn_first; pic.rect = (qa_rect){0, 0, 32, 8}; pic.uv = (qa_texture_rect){0, 0, 1, 1};
  qa_render_command pic_frame[2] = {pic, swap};
  qa_worker_renderer_execute(&r, pic_frame, 2, NULL, 0, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(last_frame_length == 32 * 8 * 4 && last_frame[0] > 0);
  qa_worker_renderer_resize(&r, 16, 16);
  CHECK(qa_worker_renderer_close(&r));
  CHECK(qa_worker_renderer_closed(&r) && qa_worker_renderer_close(&r));
  CHECK(failed_calls == 0 && restore_calls == 0);
  qa_worker_renderer_destroy(&r);
  qa_render_image_journal_destroy(&journal);
}
/* The same sequence on a serial SoftwareRenderer: TS expects results[1] toEqual results[0]. */
static void serial_image(void *ctx, qa_image_resource_operation op) { (void)ctx; (void)op; }
static void serial_swap(void *ctx, const double *captures, size_t count) { (void)ctx; (void)captures; (void)count; }
static void serial_scenario(scenario_result *out) {
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_software_renderer cpu; CHECK(qa_software_renderer_init_default(&cpu, 16, 16, owner));
  qa_software_renderer_set_output_gamma(&cpu, 1.25);
  qa_renderer_backend backend = qa_software_renderer_backend(&cpu);
  dyn_first = image_of(owner, 0, "dynamic first"); dyn_second = image_of(owner, 1, "dynamic second");
  static const uint8_t black[4] = {0, 0, 0, 255};
  qa_image_level l1, l2; qa_image_resource_operation c1 = create_op(dyn_first, black, &l1), c2 = create_op(dyn_second, black, &l2);
  backend.apply_image_resource(backend.ctx, c1); backend.apply_image_resource(backend.ctx, c2);
  qa_execution_services services = {NULL, serial_image, serial_swap};
  qa_render_executor executor; qa_render_executor_init(&executor, backend, services);
  memset(dyn_first_value, 0, 4); dyn_first_value[0] = 64; dyn_first_value[3] = 255; memset(dyn_second_value, 0, 4); dyn_second_value[1] = 64; dyn_second_value[3] = 255;
  qa_draw_batch pair = quad_batch(bind_of(dyn_first)); pair_batch_setup(&pair);
  backend.set_overdraw_measurement(backend.ctx, true);
  qa_render_view_clear clear; qa_vec4 color; qa_render_operation none; memset(&none, 0, sizeof none);
  qa_render_command view = view_command(&none, 0, &clear, &color);
  backend.select_draw_buffer(backend.ctx, "back", false);
  backend.begin_view(backend.ctx, view.view.state);
  qa_render_executor_draw(&executor, pair);
  out->depth = backend.read_depth_pixel(backend.ctx, 8, 8);
  backend.read_stencil_overdraw(backend.ctx, out->stencil, 256);
  qa_image_level read; qa_software_renderer_finish(&cpu); CHECK(qa_software_renderer_read_rgba(&cpu, &read)); memcpy(out->packet, read.pixels.bytes, 1024); free((void *)read.pixels.bytes);
  backend.set_overdraw_measurement(backend.ctx, false);
  memset(dyn_first_value, 0, 4); dyn_first_value[0] = 32; dyn_first_value[3] = 255;
  backend.begin_view(backend.ctx, view.view.state);
  nested_scope scope = {backend, quad_batch(bind_of(dyn_first))};
  order_log[0] = 0;
  backend.with_object_opacity(backend.ctx, 0.5, nested_draw, &scope);
  qa_software_renderer_finish(&cpu); CHECK(qa_software_renderer_read_rgba(&cpu, &read)); memcpy(out->nested, read.pixels.bytes, 1024); free((void *)read.pixels.bytes);
  qa_software_renderer_close(&cpu);
}
static void test_serial_matches_worker(void) {
  static scenario_result serial, worker;
  serial_scenario(&serial);
  worker_scenario(&worker);
  CHECK(memcmp(serial.packet, worker.packet, 1024) == 0);
  CHECK(memcmp(serial.nested, worker.nested, 1024) == 0);
  CHECK(memcmp(serial.stencil, worker.stencil, 256) == 0);
  CHECK(serial.depth == worker.depth);
}
/* Mutated frontend data never reaches the worker; the registry's reached operations join the suffix. */
static void test_snapshots_and_registry(void) {
  reset_window_counts();
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_scene_image_registry images; qa_scene_image_registry_init(&images, owner, NULL);
  qa_render_image_journal journal; qa_render_image_journal_init(&journal);
  qa_worker_renderer r;
  CHECK(qa_worker_renderer_open(&r, test_window(false), owner, 1, &images, &journal, on_swap, &swap_sentinel, on_failed, &failed_sentinel, NULL));
  qa_renderer_image image = image_of(owner, 0, "scoped");
  static const uint8_t base[4] = {0, 0, 0, 255};
  qa_image_level level; qa_image_resource_operation create = create_op(image, base, &level);
  qa_render_command pic; memset(&pic, 0, sizeof pic); pic.kind = QA_RENDER_COMMAND_STRETCH_PIC; pic.image = image; pic.rect = (qa_rect){0, 0, 16, 16}; pic.uv = (qa_texture_rect){0, 0, 1, 1};
  qa_render_command swap; memset(&swap, 0, sizeof swap); swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  /* TS execute's suffix applies after the commands of the final packet. */
  qa_worker_renderer_execute(&r, NULL, 0, NULL, 0, &create, 1);
  qa_worker_renderer_synchronize(&r);
  uint8_t mutable_pixels[4] = {96, 0, 0, 255};
  qa_image_level update_level; memset(&update_level, 0, sizeof update_level); update_level.width = 1; update_level.height = 1; update_level.pixels.bytes = mutable_pixels; update_level.pixels.length = 4;
  qa_image_resource_operation update; memset(&update, 0, sizeof update); update.kind = QA_IMAGE_OP_UPDATE; update.image = image; update.update_level = &update_level;
  qa_worker_renderer_execute(&r, NULL, 0, NULL, 0, &update, 1);
  memset(mutable_pixels, 0, 4); update_level.width = 9; /* TS: mutable.fill(0) right after submit */
  qa_worker_renderer_synchronize(&r);
  qa_render_command frame[2] = {pic, swap};
  qa_worker_renderer_execute(&r, frame, 2, NULL, 0, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(last_frame != NULL && last_frame[0] == 96);
  qa_journal_image_description rows[2];
  CHECK(qa_render_image_journal_describe(&journal, rows, 2) == 1 && rows[0].width == 1);
  /* Packet split: oversized image commands run alone yet in order; last one wins. */
  size_t big = 2048 * 2048 * 4;
  /* Opaque texels: the stretch-pic blends with source alpha. */
  uint8_t *a = malloc(big), *b = malloc(big), *c = malloc(big);
  for (size_t i = 0; i < big; i += 4) { a[i] = a[i + 1] = a[i + 2] = 10; b[i] = b[i + 1] = b[i + 2] = 20; c[i] = c[i + 1] = c[i + 2] = 30; a[i + 3] = b[i + 3] = c[i + 3] = 255; }
  qa_renderer_image large = image_of(owner, 1, "large"); large.width = 2048; large.height = 2048;
  qa_image_level la, lb, lc; qa_image_resource_operation ca = create_op(large, a, &la);
  memset(&lb, 0, sizeof lb); lb.width = 2048; lb.height = 2048; lb.pixels.bytes = b; lb.pixels.length = big;
  memset(&lc, 0, sizeof lc); lc.width = 2048; lc.height = 2048; lc.pixels.bytes = c; lc.pixels.length = big;
  qa_image_resource_operation ub = update; ub.image = large; ub.update_level = &lb; qa_image_resource_operation uc = ub; uc.update_level = &lc;
  qa_render_command big_frame[6]; memset(big_frame, 0, sizeof big_frame);
  big_frame[0].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; big_frame[0].operation = ca;
  big_frame[1].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; big_frame[1].operation = ub;
  big_frame[2].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; big_frame[2].operation = uc;
  big_frame[3] = pic; big_frame[3].image = large;
  big_frame[4] = swap;
  qa_worker_renderer_execute(&r, big_frame, 5, NULL, 0, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(last_frame != NULL && last_frame[0] == 30);
  free(a); free(b); free(c);
  CHECK(qa_worker_renderer_close(&r));
  qa_worker_renderer_destroy(&r);
  qa_render_image_journal_destroy(&journal);
  qa_scene_image_registry_destroy(&images);
}
/* TS "native worker failure rejects an armed capture without another frame or barrier". */
static void late_read(void *ctx) { qa_renderer_backend *b = ctx; (void)b->read_depth_pixel(b->ctx, 0, 0); }
static void test_failure(void) {
  reset_window_counts();
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_worker_renderer r;
  CHECK(qa_worker_renderer_open(&r, test_window(false), owner, 1, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, NULL));
  qa_renderer_backend backend = qa_worker_renderer_backend(&r);
  qa_renderer_image missing = image_of(owner, 100, "not resident");
  qa_render_command pic; memset(&pic, 0, sizeof pic); pic.kind = QA_RENDER_COMMAND_STRETCH_PIC; pic.image = missing; pic.rect = (qa_rect){0, 0, 16, 16}; pic.uv = (qa_texture_rect){0, 0, 1, 1};
  qa_render_command swap; memset(&swap, 0, sizeof swap); swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_render_command frame[2] = {pic, swap};
  qa_worker_renderer_execute(&r, frame, 2, NULL, 0, NULL, 0);
  /* No barrier: event-loop turns alone deliver the failure, within TS's 3 s race. */
  for (int i = 0; i < 3000 && failed_calls == 0; i++) { qa_worker_renderer_pump(&r); struct timespec ms = {0, 1000000}; nanosleep(&ms, NULL); }
  CHECK(failed_calls == 1);
  CHECK(strlen(failed_message) > 0);
  fprintf(stderr, "worker failure message: %s\n", failed_message);
  CHECK(swap_calls == 0);
  /* TS: expect(() => renderer.close()).toThrow(), and the worker still retires. */
  CHECK(!qa_worker_renderer_close(&r));
  CHECK(qa_worker_renderer_closed(&r));
  CHECK(strlen(qa_worker_renderer_last_error(&r)) > 0);
  CHECK(failed_calls == 1);
  CHECK(throws(late_read, &backend, "not open"));
  qa_worker_renderer_destroy(&r);
}
/* Host throw: identity crosses as a host failure; the callback's error reaches the caller. */
static void throwing_opacity(void *ctx) { (void)ctx; qa_range_error("draw closure failed"); }
typedef struct opacity_call { qa_renderer_backend b; } opacity_call;
static void opacity_call_body(void *ctx) { opacity_call *c = ctx; c->b.with_object_opacity(c->b.ctx, 0.5, throwing_opacity, NULL); }
static void test_host_throw(void) {
  reset_window_counts();
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_worker_renderer r;
  CHECK(qa_worker_renderer_open(&r, test_window(false), owner, 1, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, NULL));
  opacity_call call = {qa_worker_renderer_backend(&r)};
  qa_render_view_state view; memset(&view, 0, sizeof view); view.viewport.width = 16; view.viewport.height = 16;
  call.b.begin_view(call.b.ctx, view);
  CHECK(throws(opacity_call_body, &call, "draw closure failed"));
  CHECK(failed_calls == 1 && strstr(failed_message, "draw closure failed") != NULL);
  CHECK(!qa_worker_renderer_close(&r)); /* TS: the terminal failure is thrown again */
  CHECK(qa_worker_renderer_closed(&r));
  qa_worker_renderer_destroy(&r);
}
/* Startup failure: unknown dimensions reach createRenderWorkerRuntime on the worker. */
static void empty_size(void *ctx, double *w, double *h) { (void)ctx; *w = 0; *h = 16; }
static void test_startup_failure(void) {
  reset_window_counts();
  qa_worker_window window = test_window(false); window.drawable_size = empty_size;
  qa_worker_renderer r;
  CHECK(!qa_worker_renderer_open(&r, window, owner_of(&id_a), 1, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, NULL));
  CHECK(failed_calls == 1 && strstr(failed_message, "Empty render worker dimension") != NULL);
  CHECK(r.impl == NULL);
}

/* --- GL handoff through the fake --- */
static int gl_ctx_sentinel, platform_sentinel;
static pthread_t adopt_thread, release_thread, current_thread; static bool saw_current_elsewhere;
static int adopts, releases, parks, resumes, gl_swaps, interval_sets; static int32_t fake_interval; static uint32_t adopted_window_id; static bool adopt_fails;
static void *gl_proc(void *ctx, const char *name) { CHECK(ctx == &gl_ctx_sentinel); return fake_gl_proc_lookup(name); }
static void gl_release(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); }
static bool gl_retain(void *ctx, qa_gl_release_fn *out, void **out_ctx) { CHECK(ctx == &gl_ctx_sentinel); *out = gl_release; *out_ctx = ctx; return true; }
static void gl_current(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); current_thread = pthread_self(); if (pthread_equal(current_thread, main_thread)) saw_current_elsewhere = true; }
static void gl_size(void *ctx, double *w, double *h) { CHECK(ctx == &gl_ctx_sentinel); *w = 16; *h = 16; }
static void gl_rendering(void *ctx, bool on) { CHECK(ctx == &gl_ctx_sentinel); (void)on; }
static void gl_swap(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); CHECK(!pthread_equal(pthread_self(), main_thread)); gl_swaps++; }
static void ctx_park(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); CHECK(!pthread_equal(pthread_self(), main_thread)); parks++; }
static void ctx_resume(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); resumes++; }
static void ctx_set_interval(void *ctx, int32_t value) { CHECK(ctx == &gl_ctx_sentinel); interval_sets++; fake_interval = value; }
static int32_t ctx_interval(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); return fake_interval; }
static void ctx_release(void *ctx) { CHECK(ctx == &gl_ctx_sentinel); release_thread = pthread_self(); releases++; }
static bool platform_adopt(void *ctx, const qa_sdl_render_context_transfer *transfer, qa_render_worker_context *out) {
  CHECK(ctx == &platform_sentinel);
  adopt_thread = pthread_self(); adopted_window_id = transfer->window_id; adopts++;
  if (adopt_fails) return false;
  memset(out, 0, sizeof *out);
  out->ctx = &gl_ctx_sentinel; out->park = ctx_park; out->resume = ctx_resume; out->set_swap_interval = ctx_set_interval; out->swap_interval = ctx_interval; out->release = ctx_release;
  out->render.ctx = &gl_ctx_sentinel; out->render.gl_context.ctx = &gl_ctx_sentinel; out->render.gl_context.get_gl_proc_address = gl_proc; out->render.gl_context.retain_procedures = gl_retain; out->render.gl_context.make_current = gl_current;
  out->render.make_current = gl_current; out->render.drawable_size = gl_size; out->render.set_rendering_enabled = gl_rendering; out->render.swap = gl_swap;
  return true;
}
static int parked_ops, op_ctx_sentinel; static qa_renderer_backend parked_backend; static bool parked_call_threw;
static void parked_finish(void *ctx) { qa_renderer_backend *b = ctx; b->finish(b->ctx); }
static void parked_op(void *ctx) {
  CHECK(ctx == &op_ctx_sentinel); parked_ops++;
  CHECK(parks == 1 && resumes == 0);
  parked_call_threw = throws(parked_finish, &parked_backend, "parked for an SDL window change");
}
static void failing_op(void *ctx) { (void)ctx; qa_range_error("window change failed"); }
static void parked_failing(void *ctx) { (void)ctx; qa_worker_renderer_with_parked_contexts(failing_op, NULL); }
static void test_gl_handoff(void) {
  fake_reset(); reset_window_counts();
  adopts = releases = parks = resumes = gl_swaps = interval_sets = 0; fake_interval = 1; adopt_fails = false; saw_current_elsewhere = false;
  qa_render_worker_platform platform = {&platform_sentinel, platform_adopt};
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_worker_renderer r;
  CHECK(qa_worker_renderer_open(&r, test_window(true), owner, 2, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, &platform));
  CHECK(detach_calls == 1 && adopts == 1 && adopted_window_id == 77);
  CHECK(!pthread_equal(adopt_thread, main_thread));
  qa_gl_driver_info driver; qa_worker_gl_config config;
  qa_worker_description d = qa_worker_renderer_description(&r, &driver, &config);
  CHECK(strcmp(d.backend, "gl") == 0 && d.width == 16 && d.stencil_bits == 8 && d.swap_interval == 1);
  CHECK(d.driver != NULL && strcmp(d.driver->version, "4.6 fake") == 0);
  CHECK(d.gl_config != NULL && d.gl_config->max_texture_size == 1024 && d.gl_config->texture_units == 8 && d.gl_config->color_bits == 24 && d.gl_config->depth_bits == 24 && !d.gl_config->stereo_enabled);
  /* The render thread owned every GL call, starting with the gamma pass from init. */
  CHECK(fake_count("glGenFramebuffers") > 0);
  qa_renderer_image image = image_of(owner, 0, "white");
  static const uint8_t white[4] = {255, 255, 255, 255};
  qa_image_level level; qa_image_resource_operation create = create_op(image, white, &level);
  qa_render_command swap; memset(&swap, 0, sizeof swap); swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_draw_batch batch = quad_batch(bind_of(image));
  qa_render_operation draw; memset(&draw, 0, sizeof draw); draw.kind = QA_RENDER_OP_DRAW; draw.batches = &batch; draw.batch_count = 1;
  qa_render_view_clear clear; qa_vec4 color; qa_render_command view = view_command(&draw, 1, &clear, &color);
  qa_render_command frame[3]; memset(frame, 0, sizeof frame);
  frame[0].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; frame[0].operation = create; frame[1] = view; frame[2] = swap;
  /* No capture: GL presents with no pixels back to the frontend. */
  qa_worker_renderer_execute(&r, frame, 3, NULL, 0, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(gl_swaps == 1 && swap_calls == 0 && fake_count("glDrawElements") >= 1);
  /* An armed capture reads back before presenting. */
  double ids[1] = {7};
  qa_worker_renderer_execute(&r, &swap, 1, ids, 1, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(gl_swaps == 2 && swap_calls == 1 && last_ids[0] == 7 && last_frame_length == 1024 && last_frame[0] == 15);
  qa_worker_renderer_set_swap_interval(&r, 0);
  CHECK(interval_sets == 1 && fake_interval == 0);
  CHECK(qa_worker_renderer_description(&r, NULL, NULL).swap_interval == 0);
  /* Resizing a GL worker is the window's business: TS ignores it in the runtime. */
  qa_worker_renderer_resize(&r, 8, 8);
  CHECK(r.width == 16 && r.height == 16);
  /* withParkedContexts */
  parked_backend = qa_worker_renderer_backend(&r); parked_ops = 0;
  qa_worker_renderer_with_parked_contexts(parked_op, &op_ctx_sentinel);
  CHECK(parked_ops == 1 && parks == 1 && resumes == 1 && parked_call_threw);
  /* A failing window change still resumes and rethrows. */
  CHECK(throws(parked_failing, NULL, "window change failed"));
  CHECK(parks == 2 && resumes == 2);
  CHECK(!FG.wrong_thread);
  CHECK(FG.thread_set && !pthread_equal(FG.thread, main_thread));
  CHECK(!saw_current_elsewhere);
  /* Close releases on the render thread and restores the context on this one. */
  CHECK(qa_worker_renderer_close(&r));
  CHECK(releases == 1 && !pthread_equal(release_thread, main_thread) && restore_calls == 1);
  CHECK(!FG.wrong_thread);
  qa_worker_renderer_destroy(&r);
  /* Adoption failure: the transfer's context comes back to the window owner. */
  fake_reset(); reset_window_counts(); adopt_fails = true;
  CHECK(!qa_worker_renderer_open(&r, test_window(true), owner, 1, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, &platform));
  CHECK(detach_calls == 1 && restore_calls == 1 && failed_calls == 1 && strstr(failed_message, "adopt") != NULL);
  adopt_fails = false;
  /* Backend failure after adoption releases the context on the render thread first. */
  fake_reset(); reset_window_counts(); releases = 0; *fake_int(0x8871) = 2;
  CHECK(!qa_worker_renderer_open(&r, test_window(true), owner, 1, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, &platform));
  CHECK(releases == 1 && restore_calls == 1 && strstr(failed_message, "four texture coordinate units") != NULL);
}

/* --- real context: the transferred SDL context, then NativeRenderer --- */
static void real_win_size(void *ctx, double *w, double *h) { qa_sdl_size s = qa_sdl_window_drawable_size(ctx); *w = s.width; *h = s.height; }
static qa_sdl_render_context_transfer real_detach(void *ctx) { return qa_sdl_window_detach_render_context(ctx); }
static void real_restore(void *ctx) { qa_sdl_window_restore_render_context(ctx); }
static void real_worker_handoff(void) {
  qa_sdl_window_options options; memset(&options, 0, sizeof options);
  options.title = "worker top"; options.width = 16; options.height = 16; options.hidden = true; options.resizable = true; options.backend = QA_SDL_BACKEND_GL; options.stencil_bits = 8; options.display_index = -1;
  qa_sdl_window window; memset(&window, 0, sizeof window);
  CHECK(qa_sdl_window_open(&options, &window));
  reset_window_counts();
  qa_worker_window w; memset(&w, 0, sizeof w); w.ctx = &window; w.is_gl = true; w.drawable_size = real_win_size; w.detach_render_context = real_detach; w.restore_render_context = real_restore;
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_worker_renderer r;
  CHECK(qa_worker_renderer_open(&r, w, owner, 1, NULL, NULL, on_swap, &swap_sentinel, on_failed, &failed_sentinel, NULL));
  qa_gl_driver_info driver; qa_worker_gl_config config;
  qa_worker_description d = qa_worker_renderer_description(&r, &driver, &config);
  fprintf(stderr, "real worker GL: renderer=%s version=%s stencil=%g\n", d.driver->renderer, d.driver->version, d.stencil_bits);
  CHECK(strcmp(d.backend, "gl") == 0 && d.width == 16 && strlen(d.driver->vendor) > 0);
  qa_renderer_image image = image_of(owner, 0, "green");
  static const uint8_t green[4] = {0, 255, 0, 255};
  qa_image_level level; qa_image_resource_operation create = create_op(image, green, &level);
  qa_draw_batch batch = quad_batch(bind_of(image));
  qa_render_operation draw; memset(&draw, 0, sizeof draw); draw.kind = QA_RENDER_OP_DRAW; draw.batches = &batch; draw.batch_count = 1;
  qa_render_view_clear clear; qa_vec4 color; qa_render_command view = view_command(&draw, 1, &clear, &color);
  qa_render_command buffer; memset(&buffer, 0, sizeof buffer); buffer.kind = QA_RENDER_COMMAND_DRAW_BUFFER; buffer.buffer = "back";
  qa_render_command swap; memset(&swap, 0, sizeof swap); swap.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_render_command frame[4]; memset(frame, 0, sizeof frame);
  frame[0].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; frame[0].operation = create; frame[1] = buffer; frame[2] = view; frame[3] = swap;
  double ids[1] = {1};
  qa_worker_renderer_execute(&r, frame, 4, ids, 1, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(swap_calls == 1 && last_frame_length == 1024);
  CHECK(last_frame != NULL && last_frame[0] == 0 && last_frame[1] == 255 && last_frame[2] == 0 && last_frame[3] == 255);
  qa_renderer_backend backend = qa_worker_renderer_backend(&r);
  CHECK(fabs(backend.read_depth_pixel(backend.ctx, 8, 8) - 0.5) < 1e-5);
  qa_worker_renderer_set_output_gamma(&r, 2);
  qa_worker_renderer_execute(&r, frame + 1, 3, (double[]){2}, 1, NULL, 0);
  qa_worker_renderer_synchronize(&r);
  CHECK(last_frame[1] == 255 && last_frame[0] == 0);
  CHECK(failed_calls == 0);
  CHECK(qa_worker_renderer_close(&r));
  qa_worker_renderer_destroy(&r);
  /* The main thread owns the context again. */
  qa_sdl_window_make_current(&window);
  qa_sdl_window_close(&window);
}
static uint8_t native_capture[1024]; static size_t native_capture_length; static int native_rejects;
static void native_resolve(void *ctx, qa_image_level image) { (void)ctx; native_capture_length = image.pixels.length; memcpy(native_capture, image.pixels.bytes, image.pixels.length < 1024 ? image.pixels.length : 1024); free((void *)image.pixels.bytes); }
static void native_reject(void *ctx, const char *error) { (void)ctx; (void)error; native_rejects++; }
static void native_picture(qa_app_native_renderer *r, qa_renderer_resource_owner owner, qa_renderer_image image, uint8_t *out) {
  qa_app_frame_capture_request request; memset(&request, 0, sizeof request); request.resolve = native_resolve; request.reject = native_reject;
  qa_app_native_renderer_capture_next_frame(r, request);
  qa_render_command frame[3]; memset(frame, 0, sizeof frame);
  frame[0].kind = QA_RENDER_COMMAND_DRAW_BUFFER; frame[0].buffer = "back"; frame[0].clear = true;
  frame[1].kind = QA_RENDER_COMMAND_STRETCH_PIC; frame[1].image = image; frame[1].rect = (qa_rect){0, 0, 16, 16}; frame[1].uv = (qa_texture_rect){0, 0, 1, 1};
  frame[2].kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_render_frame f; memset(&f, 0, sizeof f); f.owner = owner; f.commands = frame; f.command_count = 3;
  native_capture_length = 0;
  qa_app_native_renderer_execute(r, f);
  qa_app_native_renderer_synchronize(r);
  CHECK(native_capture_length == 1024);
  memcpy(out, native_capture, 1024);
}
/* TS worker-native: --render-worker 0 and 1 give the same pictures across restarts. */
static void real_native(const char *kind) {
  qa_renderer_resource_owner owner = owner_of(&id_a);
  qa_app_native_renderer_host host; memset(&host, 0, sizeof host);
  qa_app_native_renderer *r = qa_app_native_renderer_open(host, kind, 16, 16, true, 1.25, false, owner, NULL);
  CHECK(r != NULL);
  CHECK(!qa_app_native_renderer_render_worker(r));
  qa_scene_image_registry *images = qa_app_native_renderer_images(r);
  static const uint8_t base[4] = {96, 0, 0, 255};
  qa_image_level level; memset(&level, 0, sizeof level); level.width = 1; level.height = 1; level.pixels.bytes = base; level.pixels.length = 4;
  qa_image_level storage; qa_render_image content = qa_scene_rgba_image(level, &storage);
  qa_texture_sampling sampling = {"clamp", "nearest"};
  const qa_scene_image_handle *handle = qa_scene_image_registry_register_generated(images, "native", content, sampling);
  qa_renderer_image image = handle->image;
  size_t op_count = 0; qa_image_resource_operation *ops = qa_scene_image_registry_drain_operations(images, &op_count);
  qa_render_command *commands = calloc(op_count ? op_count : 1, sizeof *commands);
  for (size_t i = 0; i < op_count; i++) { commands[i].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE; commands[i].operation = ops[i]; }
  qa_render_frame f; memset(&f, 0, sizeof f); f.owner = owner; f.commands = commands; f.command_count = op_count;
  qa_app_native_renderer_execute(r, f);
  free(commands); free(ops);
  static uint8_t serial[1024], worker[1024], back[1024];
  native_picture(r, owner, image, serial);
  CHECK(serial[0] > 0 && serial[3] == 255);
  qa_app_prepared_renderer_restart *to_worker = qa_app_native_renderer_prepare_restart(r, kind, true, true);
  CHECK(to_worker != NULL);
  qa_app_prepared_renderer_restart_publish(to_worker); qa_app_prepared_renderer_restart_retire(to_worker);
  CHECK(qa_app_native_renderer_render_worker(r));
  CHECK(qa_app_native_renderer_output_gamma(r) == 1.25);
  if (strcmp(kind, "gl") == 0) CHECK(qa_app_native_renderer_driver(r) != NULL && qa_app_native_renderer_gl_config(r) != NULL);
  native_picture(r, owner, image, worker);
  CHECK(memcmp(serial, worker, 1024) == 0);
  fprintf(stderr, "real native %s: serial r=%d worker r=%d\n", kind, serial[0], worker[0]);
  /* The registry feeds the worker's suffix across a frame; journal survives restart. */
  qa_app_prepared_renderer_restart *to_serial = qa_app_native_renderer_prepare_restart(r, kind, true, false);
  CHECK(to_serial != NULL);
  qa_app_prepared_renderer_restart_publish(to_serial); qa_app_prepared_renderer_restart_retire(to_serial);
  CHECK(!qa_app_native_renderer_render_worker(r));
  native_picture(r, owner, image, back);
  CHECK(memcmp(serial, back, 1024) == 0);
  CHECK(native_rejects == 0);
  qa_app_native_renderer_destroy(r);
}
static bool test_real(void) {
  (void)setenv("SDL_VIDEODRIVER", "offscreen", 0);
  qa_sdl_window_options options; memset(&options, 0, sizeof options);
  options.title = "probe"; options.width = 16; options.height = 16; options.hidden = true; options.backend = QA_SDL_BACKEND_GL; options.stencil_bits = 8; options.display_index = -1;
  qa_sdl_window probe; memset(&probe, 0, sizeof probe);
  if (!qa_sdl_window_open(&options, &probe)) return false;
  qa_sdl_window_close(&probe);
  real_worker_handoff();
  real_native("cpu");
  real_native("gl");
  return true;
}
/* --render-worker 0|1 through the option parser. */
static void parsed_options(const char *value, bool *has, bool *on) {
  const char *argv[] = {"--render-worker", value};
  qa_content_root_search search; memset(&search, 0, sizeof search);
  qa_app_options_host host; memset(&host, 0, sizeof host);
  qa_app_application_command command = qa_app_parse_application_command(argv, 2, search, &host);
  *has = command.options.has_render_worker; *on = command.options.render_worker;
  qa_app_application_command_destroy(&command);
}
static void test_option(void) {
  bool has = false, on = false;
  parsed_options("1", &has, &on); CHECK(has && on);
  parsed_options("0", &has, &on); CHECK(has && !on);
}

int main(void) {
  main_thread = pthread_self();
  CHECK(qa_identity_owner_create(&id_a, "worker a") && qa_identity_owner_create(&id_b, "worker b") && qa_identity_owner_create(&id_c, "worker c"));
  protocol_identity();
  protocol_snapshots();
  protocol_paired_dynamic();
  protocol_prefix();
  protocol_immediate_families();
  protocol_malformed();
  protocol_acknowledgments();
  protocol_lighting();
  test_serial_matches_worker();
  test_snapshots_and_registry();
  test_failure();
  test_host_throw();
  test_startup_failure();
  test_gl_handoff();
  test_option();
  bool real = test_real();
  printf("real GL context: %s\n", real ? "ran" : "NOT AVAILABLE, real section skipped");
  free(last_frame);
  qa_identity_owner_destroy(&id_a); qa_identity_owner_destroy(&id_b); qa_identity_owner_destroy(&id_c);
  printf("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
