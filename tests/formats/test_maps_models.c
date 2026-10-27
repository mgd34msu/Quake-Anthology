/*
 * Port of the synthetic TypeScript cases for the map/model formats:
 * tests/formats/q1-map/{layouts,read}.test.ts (non-corpus cases),
 * tests/formats/q12-model/models.test.ts (grouped MDL/SPR + geometry),
 * tests/formats/q3-map/decode.test.ts with fixture.ts,
 * tests/formats/q3-model/{md4,md5}.test.ts. Retail/corpus cases
 * (q1 PAK maps/models, q2-map retail, q3 retail) need ~/qfiles and are
 * not ported. Object-identity expectations (TS WeakMap caches) have no
 * C counterpart; values are checked instead. GPL-2.0-or-later.
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

#include "qa/core/binary/index.h"
#include "qa/formats/q12_model.h"
#include "qa/formats/q1_map.h"
#include "qa/formats/q2_map.h"
#include "qa/formats/q3_map.h"
#include "qa/formats/q3_model.h"

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

static bool vec3_is(qa_vec3 v, double x, double y, double z) { return v.x == x && v.y == y && v.z == z; }
static bool text_is(const char *got, const char *want) { return got != NULL && strcmp(got, want) == 0; }
static bool close_to(double got, double want) { return fabs(got - want) < 1e-4; }

static uint8_t *writer_finish(qa_binary_writer *writer, size_t *out_length) {
  uint8_t *bytes = qa_binary_writer_finish(writer, out_length);
  qa_binary_writer_destroy(writer);
  return bytes;
}

static void writer_init(qa_binary_writer *writer, size_t capacity) {
  if (!qa_binary_writer_init(writer, (int64_t)capacity)) {
    fprintf(stderr, "writer init failed\n");
    exit(2);
  }
}

/* ---------- q1-map layouts ---------- */

static void test_q1_quake64(void) {
  qa_binary_writer texture;
  writer_init(&texture, 56);
  qa_binary_writer_i32(&texture, 1);
  qa_binary_writer_i32(&texture, 8);
  qa_binary_writer_bytes(&texture, (const uint8_t *)"stone", 5);
  uint8_t pad[11] = {0};
  qa_binary_writer_bytes(&texture, pad, 11);
  qa_binary_writer_u32(&texture, 2);
  qa_binary_writer_u32(&texture, 2);
  qa_binary_writer_u32(&texture, 2);
  qa_binary_writer_u32(&texture, 44);
  qa_binary_writer_u32(&texture, 0);
  qa_binary_writer_u32(&texture, 0);
  qa_binary_writer_u32(&texture, 0);
  uint8_t pixels[4] = {1, 2, 3, 4};
  qa_binary_writer_bytes(&texture, pixels, 4);
  size_t texture_length = 0;
  uint8_t *texture_bytes = writer_finish(&texture, &texture_length);

  qa_binary_writer plane;
  writer_init(&plane, 20);
  qa_binary_writer_f32(&plane, 0);
  qa_binary_writer_f32(&plane, 0);
  qa_binary_writer_f32(&plane, 1);
  qa_binary_writer_f32(&plane, 0);
  qa_binary_writer_i32(&plane, 2);
  size_t plane_length = 0;
  uint8_t *plane_bytes = writer_finish(&plane, &plane_length);

  qa_binary_writer face;
  writer_init(&face, 20);
  qa_binary_writer_u16(&face, 0);
  qa_binary_writer_u16(&face, 0);
  qa_binary_writer_i32(&face, 0);
  qa_binary_writer_u16(&face, 0);
  qa_binary_writer_u16(&face, 0);
  uint8_t styles[4] = {0, 255, 255, 255};
  qa_binary_writer_bytes(&face, styles, 4);
  qa_binary_writer_i32(&face, 2);
  size_t face_length = 0;
  uint8_t *face_bytes = writer_finish(&face, &face_length);

  uint8_t texinfo[40] = {0};
  uint8_t lighting[4] = {255, 255, 8, 64};
  const uint8_t *lumps[15] = {0};
  size_t lump_lengths[15] = {0};
  lumps[1] = plane_bytes; lump_lengths[1] = plane_length;
  lumps[2] = texture_bytes; lump_lengths[2] = texture_length;
  lumps[6] = texinfo; lump_lengths[6] = 40;
  lumps[7] = face_bytes; lump_lengths[7] = face_length;
  lumps[8] = lighting; lump_lengths[8] = 4;
  size_t total = 124;
  for (int i = 0; i < 15; i++) total += lump_lengths[i];
  qa_binary_writer writer;
  writer_init(&writer, total);
  qa_binary_writer_u32(&writer, 0x51363420);
  size_t offset = 124;
  for (int i = 0; i < 15; i++) {
    qa_binary_writer_i32(&writer, (int64_t)offset);
    qa_binary_writer_i32(&writer, (int64_t)lump_lengths[i]);
    offset += lump_lengths[i];
  }
  for (int i = 0; i < 15; i++)
    if (lump_lengths[i] > 0) qa_binary_writer_bytes(&writer, lumps[i], lump_lengths[i]);
  size_t length = 0;
  uint8_t *bytes = writer_finish(&writer, &length);

  qa_q1_map map;
  qa_q1_read_bsp(bytes, length, NULL, &map);
  CHECK(text_is(map.world.format, "quake64"));
  CHECK(map.world.texture_count == 1 && map.world.textures[0] != NULL);
  CHECK(map.world.textures[0]->width == 2 && map.world.textures[0]->height == 2);
  CHECK(map.world.textures[0]->has_quake64_shift && map.world.textures[0]->quake64_shift == 2);
  const uint8_t want_samples[6] = {248, 230, 252, 8, 2, 0};
  CHECK(map.world.lighting.is_rgb8 && text_is(map.world.lighting.source, "bsp"));
  CHECK(map.world.lighting.samples.length == 6 &&
        memcmp(map.world.lighting.samples.bytes, want_samples, 6) == 0);
  CHECK(map.world.face_count == 1 && map.world.faces[0].has_lighting_offset &&
        map.world.faces[0].lighting_offset == 1);
  CHECK(map.lumps[8].data_length == 4 && memcmp(map.lumps[8].data, lighting, 4) == 0);
  qa_q1_map_destroy(&map);
  free(bytes);
  free(texture_bytes);
  free(plane_bytes);
  free(face_bytes);
}

static void test_q1_bsp2_2psb(void) {
  const qa_q1_bsp_format formats[2] = {QA_Q1_FORMAT_BSP2, QA_Q1_FORMAT_2PSB};
  for (int f = 0; f < 2; f++) {
    bool bsp2 = f == 0;
    qa_binary_writer writer;
    writer_init(&writer, bsp2 ? 44 : 32);
    qa_binary_writer_i32(&writer, 70000);
    qa_binary_writer_i32(&writer, -1);
    qa_binary_writer_i32(&writer, -70001);
    const double bounds[6] = {-100, -200, -300, 100, 200, 300};
    for (int i = 0; i < 6; i++) {
      if (bsp2) qa_binary_writer_f32(&writer, bounds[i] + 0.5);
      else qa_binary_writer_i16(&writer, (int64_t)bounds[i]);
    }
    qa_binary_writer_u32(&writer, 80000);
    qa_binary_writer_u32(&writer, 90000);
    size_t length = 0;
    uint8_t *bytes = writer_finish(&writer, &length);
    qa_binary_reader reader;
    CHECK(qa_binary_reader_init(&reader, bytes, length, "test"));
    size_t count = 0;
    qa_bsp_node *nodes = qa_q1_read_nodes(&reader, formats[f], &count);
    CHECK(count == 1);
    CHECK(nodes[0].plane == 70000);
    CHECK(!nodes[0].children[1].is_node && nodes[0].children[1].index == 70000);
    CHECK(nodes[0].bounds.min.x == (bsp2 ? -99.5 : -100));
    CHECK(nodes[0].faces.first == 80000 && nodes[0].faces.count == 90000);
    qa_binary_reader_destroy(&reader);
    free(nodes);
    free(bytes);

    qa_binary_writer edge;
    writer_init(&edge, 8);
    qa_binary_writer_u32(&edge, 80000);
    qa_binary_writer_u32(&edge, 90000);
    bytes = writer_finish(&edge, &length);
    CHECK(qa_binary_reader_init(&reader, bytes, length, "test"));
    qa_bsp_edge *edges = qa_q1_read_edges(&reader, formats[f], &count);
    CHECK(count == 1 && edges[0].vertices[0] == 80000 && edges[0].vertices[1] == 90000);
    qa_binary_reader_destroy(&reader);
    free(edges);
    free(bytes);
  }
}

static void test_q1_bspx_retention(void) {
  qa_binary_writer writer;
  writer_init(&writer, 199);
  qa_binary_writer_u32(&writer, 29);
  for (int i = 0; i < 30; i++) qa_binary_writer_u32(&writer, 0);
  qa_binary_writer_bytes(&writer, (const uint8_t *)"BSPX", 4);
  qa_binary_writer_u32(&writer, 2);
  const char *names[2] = {"FUTURE_LUMP", "EMPTY_LUMP"};
  const int offsets[2] = {196, 199};
  const int lengths[2] = {3, 0};
  for (int i = 0; i < 2; i++) {
    uint8_t name[24] = {0};
    memcpy(name, names[i], strlen(names[i]));
    qa_binary_writer_bytes(&writer, name, 24);
    qa_binary_writer_u32(&writer, (uint32_t)offsets[i]);
    qa_binary_writer_u32(&writer, (uint32_t)lengths[i]);
  }
  uint8_t payload[3] = {1, 2, 3};
  qa_binary_writer_bytes(&writer, payload, 3);
  size_t length = 0;
  uint8_t *bytes = writer_finish(&writer, &length);
  qa_q1_map map;
  qa_q1_read_bsp(bytes, length, NULL, &map);
  CHECK(map.bspx_count == 2);
  CHECK(text_is(map.bspx[0].name, "FUTURE_LUMP") && text_is(map.bspx[1].name, "EMPTY_LUMP"));
  CHECK(map.world.extension_count == 2);
  CHECK(map.world.extensions[0].bytes.length == 3 &&
        memcmp(map.world.extensions[0].bytes.bytes, payload, 3) == 0);
  CHECK(map.world.extensions[1].bytes.length == 0);
  qa_q1_map_destroy(&map);
  free(bytes);
}

static void test_q1_brush_and_decoupled(void) {
  qa_binary_writer brush;
  writer_init(&brush, 60);
  qa_binary_writer_u32(&brush, 1);
  qa_binary_writer_u32(&brush, 0);
  qa_binary_writer_u32(&brush, 1);
  qa_binary_writer_u32(&brush, 1);
  const double bounds[6] = {-1, -2, -3, 1, 2, 3};
  for (int i = 0; i < 6; i++) qa_binary_writer_f32(&brush, bounds[i]);
  qa_binary_writer_i16(&brush, -2);
  qa_binary_writer_u16(&brush, 1);
  const double plane[4] = {0.5, 0.5, 0, 0.25};
  for (int i = 0; i < 4; i++) qa_binary_writer_f32(&brush, plane[i]);
  size_t length = 0;
  uint8_t *bytes = writer_finish(&brush, &length);
  qa_q1_brush_list_model *models = NULL;
  size_t count = 0;
  CHECK(qa_q1_read_brush_list(true, bytes, length, 1, &models, &count));
  CHECK(count == 1 && models[0].brush_count == 1 && models[0].brushes[0].plane_count == 1);
  CHECK(vec3_is(models[0].brushes[0].planes[0].normal, 0.5, 0.5, 0));
  CHECK(models[0].brushes[0].planes[0].distance == 0.25);
  qa_q1_brush_list_destroy(models, count);
  free(bytes);

  qa_binary_writer light;
  writer_init(&light, 40);
  qa_binary_writer_u16(&light, 8);
  qa_binary_writer_u16(&light, 16);
  qa_binary_writer_u32(&light, 24);
  const double axes[8] = {0.25, 0, 0, 4, 0, -0.125, 0, 8};
  for (int i = 0; i < 8; i++) qa_binary_writer_f32(&light, axes[i]);
  bytes = writer_finish(&light, &length);
  qa_decoupled_lightmap *lightmaps = NULL;
  CHECK(qa_q1_read_decoupled_lightmaps(true, bytes, length, 1, &lightmaps, &count));
  CHECK(count == 1);
  CHECK(lightmaps[0].width == 8 && lightmaps[0].height == 16);
  CHECK(lightmaps[0].has_lighting_offset && lightmaps[0].lighting_offset == 24);
  CHECK(vec3_is(lightmaps[0].axes[0], 0.25, 0, 0) && vec3_is(lightmaps[0].axes[1], 0, -0.125, 0));
  CHECK(lightmaps[0].offset.x == 4 && lightmaps[0].offset.y == 8);
  free(lightmaps);
  free(bytes);
}

static uint8_t *make_lit(int samples, size_t *out_length) {
  qa_binary_writer writer;
  writer_init(&writer, 8 + (size_t)samples * 3);
  qa_binary_writer_bytes(&writer, (const uint8_t *)"QLIT", 4);
  qa_binary_writer_u32(&writer, 1);
  for (int i = 0; i < samples * 3; i++) qa_binary_writer_u8(&writer, 7);
  return writer_finish(&writer, out_length);
}

static void test_q1_lit_fallback(void) {
  uint8_t lighting[4] = {10, 20, 30, 40};
  qa_binary_writer writer;
  writer_init(&writer, 128);
  qa_binary_writer_i32(&writer, 29);
  size_t offset = 124;
  for (int i = 0; i < 15; i++) {
    qa_binary_writer_i32(&writer, (int64_t)offset);
    qa_binary_writer_i32(&writer, i == 8 ? 4 : 0);
    if (i == 8) offset += 4;
  }
  qa_binary_writer_bytes(&writer, lighting, 4);
  size_t length = 0;
  uint8_t *bytes = writer_finish(&writer, &length);

  size_t lit_length = 0;
  uint8_t *lit = make_lit(1, &lit_length);
  qa_q1_map_options options;
  memset(&options, 0, sizeof options);
  options.has_lit = true;
  options.lit = lit;
  options.lit_length = lit_length;
  qa_q1_map map;
  qa_q1_read_bsp(bytes, length, &options, &map);
  CHECK(!map.world.lighting.is_rgb8);
  CHECK(map.world.lighting.samples.length == 4 &&
        memcmp(map.world.lighting.samples.bytes, lighting, 4) == 0);
  qa_q1_map_destroy(&map);
  free(lit);

  lit = make_lit(4, &lit_length);
  options.lit = lit;
  options.lit_length = lit_length;
  qa_q1_read_bsp(bytes, length, &options, &map);
  CHECK(map.world.lighting.is_rgb8 && text_is(map.world.lighting.source, "lit"));
  CHECK(map.world.lighting.samples.length == 12);
  bool all_seven = true;
  for (size_t i = 0; i < 12; i++) all_seven = all_seven && map.world.lighting.samples.bytes[i] == 7;
  CHECK(all_seven);
  qa_q1_map_destroy(&map);
  free(lit);
  free(bytes);
}

/* ---------- q1-map read ---------- */

static void test_q1_read_rejects(void) {
  uint8_t tiny[4] = {0};
  CHECK_ABORTS(qa_q1_map m; qa_q1_read_bsp(tiny, 4, NULL, &m));
  uint8_t bytes[125] = {0};
  int32_t version = 29;
  memcpy(bytes, &version, 4);
  int32_t bad_offset = 125, bad_length = 1;
  memcpy(bytes + 4, &bad_offset, 4);
  memcpy(bytes + 8, &bad_length, 4);
  CHECK_ABORTS(qa_q1_map m; qa_q1_read_bsp(bytes, 124, NULL, &m));
  int32_t zero = 0;
  memcpy(bytes + 4, &zero, 4);
  memcpy(bytes + 8, &zero, 4);
  int32_t partial_offset = 124, partial_length = 1;
  memcpy(bytes + 12, &partial_offset, 4);
  memcpy(bytes + 16, &partial_length, 4);
  CHECK_ABORTS(qa_q1_map m; qa_q1_read_bsp(bytes, 125, NULL, &m));
}

static void test_q1_entities_pvs(void) {
  size_t count = 0;
  qa_q1_entity *entities =
      qa_q1_parse_entities("// comment\n{ \"classname\" \"worldspawn\" \"message\" \"one\\ntwo\" \"message\" "
                           "\"last\" }", &count);
  CHECK(count == 1);
  CHECK(entities[0].property_count == 3);
  CHECK(text_is(qa_q1_entity_value(&entities[0], "message"), "last"));
  qa_q1_entities_destroy(entities, count);
  entities = qa_q1_parse_entities("{\"key\" \"}\"}", &count);
  CHECK(count == 1 && text_is(entities[0].properties[0].value, "}"));
  qa_q1_entities_destroy(entities, count);

  uint8_t pvs_data[4] = {255, 0, 2, 1};
  size_t out_length = 0;
  uint8_t *pvs = qa_q1_decompress_pvs(pvs_data, 4, true, 0, 32, &out_length);
  const uint8_t want[4] = {255, 0, 0, 1};
  CHECK(out_length == 4 && memcmp(pvs, want, 4) == 0);
  free(pvs);
  uint8_t bad1[2] = {0, 0};
  uint8_t bad2[2] = {0, 2};
  CHECK_ABORTS(size_t n; free(qa_q1_decompress_pvs(bad1, 2, true, 0, 8, &n)));
  CHECK_ABORTS(size_t n; free(qa_q1_decompress_pvs(bad2, 2, true, 0, 8, &n)));
}

/* ---------- q12-model ---------- */

static uint8_t *grouped_mdl(size_t *out_length) {
  qa_binary_writer writer;
  writer_init(&writer, 512);
  qa_binary_writer_bytes(&writer, (const uint8_t *)"IDPO", 4);
  qa_binary_writer_i32(&writer, 6);
  const double header_floats[10] = {1, 1, 1, 10, 20, 30, 20, 0, 0, 8};
  for (int i = 0; i < 10; i++) qa_binary_writer_f32(&writer, header_floats[i]);
  const int header_ints[8] = {1, 4, 1, 3, 1, 1, 1, 8};
  for (int i = 0; i < 8; i++) qa_binary_writer_i32(&writer, header_ints[i]);
  qa_binary_writer_f32(&writer, 2);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 2);
  qa_binary_writer_f32(&writer, 0.1);
  qa_binary_writer_f32(&writer, 0.3);
  uint8_t skin[8] = {1, 2, 224, 255, 5, 6, 7, 8};
  qa_binary_writer_bytes(&writer, skin, 8);
  const int coords[13] = {32, 0, 0, 0, 1, 0, 0, 2, 0, 0, 0, 1, 2};
  for (int i = 0; i < 13; i++) qa_binary_writer_i32(&writer, coords[i]);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 2);
  uint8_t group_bounds[8] = {0, 0, 0, 255, 10, 10, 10, 255};
  qa_binary_writer_bytes(&writer, group_bounds, 8);
  qa_binary_writer_f32(&writer, 0.2);
  qa_binary_writer_f32(&writer, 0.5);
  const int zs[2] = {0, 10};
  for (int f = 0; f < 2; f++) {
    uint8_t frame_bounds[8] = {0, 0, (uint8_t)zs[f], 255, 10, 10, (uint8_t)zs[f], 255};
    qa_binary_writer_bytes(&writer, frame_bounds, 8);
    uint8_t name[16] = {0};
    char pose[16];
    snprintf(pose, sizeof pose, "pose%d", zs[f]);
    memcpy(name, pose, strlen(pose));
    qa_binary_writer_bytes(&writer, name, 16);
    uint8_t verts[12] = {0, 0, (uint8_t)zs[f], 5, 10, 0, (uint8_t)zs[f], 5, 0, 10, (uint8_t)zs[f], 5};
    qa_binary_writer_bytes(&writer, verts, 12);
  }
  return writer_finish(&writer, out_length);
}

static void test_mdl_groups(void) {
  size_t length = 0;
  uint8_t *bytes = grouped_mdl(&length);
  qa_q1_alias_model model;
  qa_parse_mdl(bytes, length, NULL, &model);
  CHECK(model.flags == 8);
  CHECK(text_is(model.sync, "random"));
  CHECK(vec3_is(model.bounds.min, 10, 20, 30) && vec3_is(model.bounds.max, 20, 30, 40));
  const qa_byte_view *skin = qa_sample_timed_frame(&model.skins[0], 0.2, 0);
  const uint8_t want_skin[4] = {5, 6, 7, 8};
  CHECK(skin->length == 4 && memcmp(skin->bytes, want_skin, 4) == 0);
  skin = qa_sample_timed_frame(&model.skins[0], 0, 0.2);
  CHECK(skin->length == 4 && memcmp(skin->bytes, want_skin, 4) == 0);
  const qa_q1_alias_frame *start = qa_q1_sample_alias_frame(&model.frames[0], 0, 0);
  const qa_q1_alias_frame *next = qa_q1_sample_alias_frame(&model.frames[0], 0.3, 0);
  CHECK(text_is(start->frame.name, "pose0"));
  CHECK(text_is(next->frame.name, "pose10"));
  CHECK(text_is(qa_q1_sample_alias_frame(&model.frames[0], 0.5, 0)->frame.name, "pose0"));
  qa_alias_geometry geometry;
  qa_build_mdl_geometry(&model, start->frame.vertices, start->frame.vertex_count, &geometry);
  CHECK(geometry.vertex_count == 3);
  CHECK(geometry.vertices[0].tex_coord.x == 0.625 && geometry.vertices[0].tex_coord.y == 0.5);
  qa_alias_geometry_destroy(&geometry);
  size_t blended_count = 0;
  qa_vec3 zero = {0, 0, 0};
  qa_model_vertex *blended = qa_interpolate_alias_frames(&next->frame, &start->frame, 0.5, zero,
                                                         &blended_count);
  CHECK(blended_count == 3 && blended[0].position.z == 35);
  free(blended);
  CHECK(vec3_is(qa_decode_alias_normal(5), 0, 0, 1));
  CHECK_ABORTS(qa_decode_alias_normal(162));
  qa_q1_alias_model_destroy(&model);
  free(bytes);
}

static void test_spr_groups(void) {
  qa_binary_writer writer;
  writer_init(&writer, 128);
  qa_binary_writer_bytes(&writer, (const uint8_t *)"IDSP", 4);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 4);
  qa_binary_writer_f32(&writer, 2);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_f32(&writer, 3);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 2);
  qa_binary_writer_f32(&writer, 0.25);
  qa_binary_writer_f32(&writer, 0.75);
  const int pixels[2] = {224, 255};
  for (int i = 0; i < 2; i++) {
    qa_binary_writer_i32(&writer, 0);
    qa_binary_writer_i32(&writer, 1);
    qa_binary_writer_i32(&writer, 1);
    qa_binary_writer_i32(&writer, 1);
    qa_binary_writer_u8(&writer, pixels[i]);
  }
  size_t length = 0;
  uint8_t *bytes = writer_finish(&writer, &length);
  qa_q1_sprite_model model;
  qa_parse_spr(bytes, length, NULL, &model);
  CHECK(model.orientation == 4);
  CHECK(model.beam_length == 3);
  const qa_sprite_frame *frame = qa_sample_timed_frame(&model.frames[0], 0.25, 0);
  CHECK(frame->pixels.length == 1 && frame->pixels.bytes[0] == 255);
  frame = qa_sample_timed_frame(&model.frames[0], 0.75, 0);
  CHECK(frame->pixels.bytes[0] == 224);
  qa_q1_sprite_model_destroy(&model);
  free(bytes);
}

static void test_mdl_geometry_values(void) {
  size_t length = 0;
  uint8_t *bytes = grouped_mdl(&length);
  qa_q1_alias_model decoded;
  qa_parse_mdl(bytes, length, NULL, &decoded);
  /* Replace the single triangle with front/back pair, as the TS does. */
  qa_q1_alias_triangle triangles[2] = {{true, {0, 1, 2}}, {false, {0, 2, 1}}};
  qa_q1_alias_model model = decoded;
  model.triangles = triangles;
  model.triangle_count = 2;
  const qa_q1_alias_frame *first = qa_q1_sample_alias_frame(&model.frames[0], 0, 0);
  const qa_q1_alias_frame *next = qa_q1_sample_alias_frame(&model.frames[0], 0.3, 0);
  CHECK_ABORTS(qa_alias_geometry g; qa_build_mdl_geometry(&model, NULL, 0, &g));
  qa_alias_geometry cold, warm;
  qa_build_mdl_geometry(&model, first->frame.vertices, first->frame.vertex_count, &cold);
  qa_build_mdl_geometry(&model, next->frame.vertices, next->frame.vertex_count, &warm);
  const double want_indices[6] = {0, 1, 2, 3, 4, 5};
  CHECK(cold.index_count == 6 && memcmp(cold.indices, want_indices, sizeof want_indices) == 0);
  const int order[6] = {0, 1, 2, 0, 2, 1};
  bool positions_match = true, normals_match = true;
  for (int i = 0; i < 6; i++) {
    qa_vec3 want_pos = next->frame.vertices[order[i]].position;
    qa_vec3 want_nor = next->frame.vertices[order[i]].normal;
    positions_match = positions_match && vec3_is(warm.vertices[i].vertex.position, want_pos.x, want_pos.y,
                                                 want_pos.z);
    normals_match = normals_match && vec3_is(warm.vertices[i].vertex.normal, want_nor.x, want_nor.y, want_nor.z);
  }
  CHECK(positions_match && normals_match);
  CHECK(warm.vertices[0].tex_coord.x == 0.125 && warm.vertices[0].tex_coord.y == 0.5);
  CHECK(warm.vertices[3].tex_coord.x == 0.625 && warm.vertices[3].tex_coord.y == 0.5);
  CHECK(!vec3_is(cold.vertices[0].vertex.position, warm.vertices[0].vertex.position.x,
                 warm.vertices[0].vertex.position.y, warm.vertices[0].vertex.position.z));
  qa_alias_geometry_destroy(&cold);
  qa_alias_geometry_destroy(&warm);
  qa_q1_alias_model_destroy(&decoded);
  free(bytes);
}

static void test_md2_geometry_values(void) {
  qa_vec3 position = {1, 2, 3}, normal = {0, 0, 1};
  qa_model_vertex pose[2] = {{position, normal}, {{4, 5, 6}, normal}};
  qa_vec2 coordinates[2] = {{0, 0}, {3, 2}};
  qa_q2_alias_triangle triangle = {{1, 0, 1}, {0, 1, 1}};
  qa_q2_alias_model model;
  memset(&model, 0, sizeof model);
  model.bounds.min = position;
  model.bounds.max = position;
  model.skin_width = 8;
  model.skin_height = 4;
  model.texture_coordinates = coordinates;
  model.texture_coordinate_count = 2;
  model.triangles = &triangle;
  model.triangle_count = 1;
  CHECK_ABORTS(qa_alias_geometry g; qa_build_md2_geometry(&model, NULL, 0, &g));
  qa_alias_geometry cold;
  qa_build_md2_geometry(&model, pose, 2, &cold);
  qa_model_vertex next_pose[2];
  for (int i = 0; i < 2; i++) {
    next_pose[i].position = pose[i].position;
    next_pose[i].position.z = 9;
    next_pose[i].normal.x = 0;
    next_pose[i].normal.y = 1;
    next_pose[i].normal.z = 0;
  }
  qa_alias_geometry warm;
  qa_build_md2_geometry(&model, next_pose, 2, &warm);
  const double want_indices[3] = {0, 1, 2};
  CHECK(warm.index_count == 3 && memcmp(warm.indices, want_indices, sizeof want_indices) == 0);
  CHECK(warm.vertices[0].tex_coord.x == 0.0625 && warm.vertices[0].tex_coord.y == 0.125);
  CHECK(warm.vertices[1].tex_coord.x == 0.4375 && warm.vertices[1].tex_coord.y == 0.625);
  CHECK(warm.vertices[2].tex_coord.x == 0.4375 && warm.vertices[2].tex_coord.y == 0.625);
  const int order[3] = {1, 0, 1};
  bool positions_match = true;
  for (int i = 0; i < 3; i++) {
    qa_vec3 want = next_pose[order[i]].position;
    positions_match = positions_match && vec3_is(warm.vertices[i].vertex.position, want.x, want.y, want.z);
  }
  CHECK(positions_match);
  CHECK(vec3_is(warm.vertices[0].vertex.normal, 0, 1, 0));
  CHECK(cold.vertices[0].vertex.position.z == 6);
  qa_alias_geometry_destroy(&cold);
  qa_alias_geometry_destroy(&warm);
}

/* ---------- q3-map (fixture.ts port) ---------- */

static void put_i32(uint8_t *data, size_t offset, int32_t value) { memcpy(data + offset, &value, 4); }
static int32_t get_i32(const uint8_t *data, size_t offset) {
  int32_t value;
  memcpy(&value, data + offset, 4);
  return value;
}

static uint8_t *q3_fixture(size_t *out_length) {
  uint8_t *lumps[17] = {0};
  size_t lump_lengths[17] = {0};
  const char *entities = "{\n\"classname\" \"worldspawn\"\n\"message\" \"BSP test\"\n}\n";
  lumps[0] = malloc(strlen(entities) + 1);
  memcpy(lumps[0], entities, strlen(entities) + 1);
  lump_lengths[0] = strlen(entities) + 1;

  qa_binary_writer w;
  writer_init(&w, 72);
  qa_binary_writer_bytes(&w, (const uint8_t *)"textures/test/wall", 18);
  uint8_t pad46[46] = {0};
  qa_binary_writer_bytes(&w, pad46, 46);
  qa_binary_writer_i32(&w, 0x80);
  qa_binary_writer_i32(&w, 1);
  lumps[1] = writer_finish(&w, &lump_lengths[1]);

  writer_init(&w, 16);
  qa_binary_writer_f32(&w, 1);
  qa_binary_writer_f32(&w, 0);
  qa_binary_writer_f32(&w, 0);
  qa_binary_writer_f32(&w, 16);
  lumps[2] = writer_finish(&w, &lump_lengths[2]);

  writer_init(&w, 36);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, -1);
  qa_binary_writer_i32(&w, -1);
  const int box[6] = {-32, -32, -32, 32, 32, 32};
  for (int i = 0; i < 6; i++) qa_binary_writer_i32(&w, box[i]);
  lumps[3] = writer_finish(&w, &lump_lengths[3]);

  writer_init(&w, 48);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 0);
  for (int i = 0; i < 6; i++) qa_binary_writer_i32(&w, box[i]);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 4);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 1);
  lumps[4] = writer_finish(&w, &lump_lengths[4]);

  writer_init(&w, 16);
  for (int i = 0; i < 4; i++) qa_binary_writer_i32(&w, i);
  lumps[5] = writer_finish(&w, &lump_lengths[5]);

  writer_init(&w, 4);
  qa_binary_writer_i32(&w, 0);
  lumps[6] = writer_finish(&w, &lump_lengths[6]);

  writer_init(&w, 40);
  for (int i = 0; i < 6; i++) qa_binary_writer_f32(&w, (double)box[i]);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 4);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 1);
  lumps[7] = writer_finish(&w, &lump_lengths[7]);

  writer_init(&w, 12);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 6);
  qa_binary_writer_i32(&w, 0);
  lumps[8] = writer_finish(&w, &lump_lengths[8]);

  writer_init(&w, 48);
  for (int i = 0; i < 6; i++) {
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, 0);
  }
  lumps[9] = writer_finish(&w, &lump_lengths[9]);

  writer_init(&w, 44 * 12);
  for (int i = 0; i < 12; i++) {
    qa_binary_writer_f32(&w, i);
    qa_binary_writer_f32(&w, i + 0.5);
    qa_binary_writer_f32(&w, -i);
    qa_binary_writer_f32(&w, 0.25);
    qa_binary_writer_f32(&w, 0.75);
    qa_binary_writer_f32(&w, 0.125);
    qa_binary_writer_f32(&w, 0.875);
    qa_binary_writer_f32(&w, 0);
    qa_binary_writer_f32(&w, 0);
    qa_binary_writer_f32(&w, 1);
    qa_binary_writer_u8(&w, 12);
    qa_binary_writer_u8(&w, 34);
    qa_binary_writer_u8(&w, 56);
    qa_binary_writer_u8(&w, 255);
  }
  lumps[10] = writer_finish(&w, &lump_lengths[10]);

  writer_init(&w, 12);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 1);
  qa_binary_writer_i32(&w, 2);
  lumps[11] = writer_finish(&w, &lump_lengths[11]);

  writer_init(&w, 72);
  qa_binary_writer_bytes(&w, (const uint8_t *)"fog", 3);
  uint8_t pad61[61] = {0};
  qa_binary_writer_bytes(&w, pad61, 61);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, -1);
  lumps[12] = writer_finish(&w, &lump_lengths[12]);

  writer_init(&w, 104 * 4);
  for (int type = 1; type <= 4; type++) {
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, type == 4 ? -1 : 0);
    qa_binary_writer_i32(&w, type);
    qa_binary_writer_i32(&w, type == 2 ? 3 : 0);
    qa_binary_writer_i32(&w, type == 2 ? 9 : type == 4 ? 0 : 3);
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, type == 2 || type == 4 ? 0 : 3);
    qa_binary_writer_i32(&w, type == 4 ? -1 : 0);
    qa_binary_writer_i32(&w, 8);
    qa_binary_writer_i32(&w, 16);
    qa_binary_writer_i32(&w, 32);
    qa_binary_writer_i32(&w, 64);
    for (int field = 0; field < 12; field++) qa_binary_writer_f32(&w, field + 0.5);
    qa_binary_writer_i32(&w, type == 2 ? 3 : 0);
    qa_binary_writer_i32(&w, type == 2 ? 3 : 0);
  }
  lumps[13] = writer_finish(&w, &lump_lengths[13]);

  lumps[14] = malloc(128 * 128 * 3);
  memset(lumps[14], 127, 128 * 128 * 3);
  lump_lengths[14] = 128 * 128 * 3;

  lumps[15] = malloc(8);
  for (int i = 0; i < 8; i++) lumps[15][i] = (uint8_t)(i + 1);
  lump_lengths[15] = 8;

  writer_init(&w, 9);
  qa_binary_writer_i32(&w, 1);
  qa_binary_writer_i32(&w, 1);
  qa_binary_writer_u8(&w, 1);
  lumps[16] = writer_finish(&w, &lump_lengths[16]);

  size_t total = 144;
  for (int i = 0; i < 17; i++) total += lump_lengths[i];
  qa_binary_writer output;
  writer_init(&output, total);
  qa_binary_writer_bytes(&output, (const uint8_t *)"IBSP", 4);
  qa_binary_writer_i32(&output, 46);
  size_t offset = 144;
  for (int i = 0; i < 17; i++) {
    qa_binary_writer_i32(&output, (int64_t)offset);
    qa_binary_writer_i32(&output, (int64_t)lump_lengths[i]);
    offset += lump_lengths[i];
  }
  for (int i = 0; i < 17; i++) qa_binary_writer_bytes(&output, lumps[i], lump_lengths[i]);
  uint8_t *result = writer_finish(&output, out_length);
  for (int i = 0; i < 17; i++) free(lumps[i]);
  return result;
}

static uint8_t *q3_test_fixture(size_t *out_length) {
  size_t original_length = 0;
  uint8_t *original = q3_fixture(&original_length);
  uint8_t *parts[15];
  size_t part_lengths[15];
  for (int i = 0; i < 15; i++) {
    parts[i] = NULL;
    part_lengths[i] = 0;
  }
  /* lump(i) copies from the v46 fixture. */
  #define LUMP_COPY(idx, len)                                                    \
    (memcpy(malloc((len) > 0 ? (len) : 1),                                       \
            original + get_i32(original, 8 + (idx) * 8), (len)), (len))
  size_t len = 0;
  parts[0] = malloc(len = (size_t)get_i32(original, 12));
  memcpy(parts[0], original + get_i32(original, 8), len);
  part_lengths[0] = len;
  /* planes: 20-byte records wrapping the 16-byte plane + i32 0 */
  qa_binary_writer w;
  writer_init(&w, 20);
  qa_binary_writer_bytes(&w, original + get_i32(original, 8 + 2 * 8), 16);
  qa_binary_writer_i32(&w, 0);
  parts[1] = writer_finish(&w, &part_lengths[1]);
  parts[2] = malloc(len = (size_t)get_i32(original, 12 + 3 * 8));
  memcpy(parts[2], original + get_i32(original, 8 + 3 * 8), len);
  part_lengths[2] = len;
  parts[3] = malloc(len = (size_t)get_i32(original, 12 + 4 * 8));
  memcpy(parts[3], original + get_i32(original, 8 + 4 * 8), len);
  part_lengths[3] = len;
  put_i32(parts[3], 36, 3);
  parts[4] = malloc(12);
  memcpy(parts[4], original + get_i32(original, 8 + 5 * 8), 12);
  part_lengths[4] = 12;
  parts[5] = malloc(len = (size_t)get_i32(original, 12 + 6 * 8));
  memcpy(parts[5], original + get_i32(original, 8 + 6 * 8), len);
  part_lengths[5] = len;
  writer_init(&w, 48);
  qa_binary_writer_bytes(&w, original + get_i32(original, 8 + 7 * 8), 24);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 999);
  qa_binary_writer_i32(&w, 999);
  parts[6] = writer_finish(&w, &part_lengths[6]);
  writer_init(&w, 12);
  qa_binary_writer_i32(&w, 0);
  qa_binary_writer_i32(&w, 6);
  qa_binary_writer_i32(&w, 1);
  parts[7] = writer_finish(&w, &part_lengths[7]);
  writer_init(&w, 48);
  for (int i = 0; i < 6; i++) {
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, 128);
  }
  parts[8] = writer_finish(&w, &part_lengths[8]);
  parts[9] = malloc(len = (size_t)get_i32(original, 12 + 14 * 8));
  memcpy(parts[9], original + get_i32(original, 8 + 14 * 8), len);
  part_lengths[9] = len;
  parts[10] = malloc(len = (size_t)get_i32(original, 12 + 16 * 8));
  memcpy(parts[10], original + get_i32(original, 8 + 16 * 8), len);
  part_lengths[10] = len;
  parts[11] = malloc(len = (size_t)get_i32(original, 12 + 10 * 8));
  memcpy(parts[11], original + get_i32(original, 8 + 10 * 8), len);
  part_lengths[11] = len;
  /* surfaces: 164-byte v44 records */
  const uint8_t *surfaces = original + get_i32(original, 8 + 13 * 8);
  const uint8_t *shader = original + get_i32(original, 8 + 1 * 8);
  writer_init(&w, 164 * 3);
  for (int i = 0; i < 3; i++) {
    qa_binary_writer_bytes(&w, shader, 64);
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, get_i32(surfaces, (size_t)i * 104 + 12));
    qa_binary_writer_i32(&w, get_i32(surfaces, (size_t)i * 104 + 16));
    qa_binary_writer_i32(&w, 0);
    qa_binary_writer_i32(&w, i == 2 ? 3 : 0);
    qa_binary_writer_i32(&w, i == 1 ? 3 : 0);
    qa_binary_writer_i32(&w, i == 1 ? 3 : 0);
    qa_binary_writer_bytes(&w, surfaces + (size_t)i * 104 + 28, 68);
  }
  parts[12] = writer_finish(&w, &part_lengths[12]);
  parts[13] = malloc(68);
  memcpy(parts[13], original + get_i32(original, 8 + 12 * 8), 68);
  part_lengths[13] = 68;
  parts[14] = malloc(len = (size_t)get_i32(original, 12 + 11 * 8));
  memcpy(parts[14], original + get_i32(original, 8 + 11 * 8), len);
  part_lengths[14] = len;
  #undef LUMP_COPY
  size_t total = 128;
  for (int i = 0; i < 15; i++) total += part_lengths[i];
  qa_binary_writer output;
  writer_init(&output, total);
  qa_binary_writer_u32(&output, 0x50534249);
  qa_binary_writer_i32(&output, 44);
  size_t offset = 128;
  for (int i = 0; i < 15; i++) {
    qa_binary_writer_i32(&output, (int64_t)offset);
    qa_binary_writer_i32(&output, (int64_t)part_lengths[i]);
    offset += part_lengths[i];
  }
  for (int i = 0; i < 15; i++) qa_binary_writer_bytes(&output, parts[i], part_lengths[i]);
  uint8_t *result = writer_finish(&output, out_length);
  for (int i = 0; i < 15; i++) free(parts[i]);
  free(original);
  return result;
}

static void test_q3_ibsp44(void) {
  size_t length = 0;
  uint8_t *bytes = q3_test_fixture(&length);
  qa_q3_bsp_map map;
  qa_q3_parse_bsp(bytes, length, NULL, &map);
  CHECK(map.surface_count == 3);
  CHECK(text_is(map.surfaces[0].type, "planar") && text_is(map.surfaces[1].type, "patch") &&
        text_is(map.surfaces[2].type, "triangles"));
  CHECK(map.models[0].first_surface == 0 && map.models[0].surface_count == 3);
  CHECK(map.models[0].first_brush == 0 && map.models[0].brush_count == 1);
  const double want_indices[6] = {0, 1, 2, 0, 1, 2};
  CHECK(map.index_count == 6 && memcmp(map.indices, want_indices, sizeof want_indices) == 0);
  CHECK(map.shaders[(size_t)map.brushes[0].shader].content_flags == 1);
  CHECK(map.shaders[(size_t)map.brush_sides[0].shader].surface_flags == 128);
  CHECK(map.fog_count == 1 && map.fogs[0].visible_side == -1);
  qa_q3_world_geometry world;
  qa_q3_adapt_bsp(&map, &world);
  CHECK(world.surface_count == 3);
  qa_q3_adapted_world_destroy(&world);
  qa_q3_bsp_map_destroy(&map);
  put_i32(bytes, 12 + 12 * 8, 1);
  CHECK_ABORTS(qa_q3_bsp_map m; qa_q3_parse_bsp(bytes, length, NULL, &m));
  free(bytes);
}

static void test_q3_ibsp46(void) {
  size_t length = 0;
  uint8_t *bytes = q3_fixture(&length);
  qa_q3_bsp_map map;
  qa_q3_parse_bsp(bytes, length, NULL, &map);
  CHECK(map.entity_record_count == 1);
  CHECK(text_is(qa_q3_entity_get(&map.entity_records[0], "classname"), "worldspawn"));
  CHECK(strstr(map.entities, "\"message\" \"BSP test\"") != NULL);
  CHECK(map.shader_count == 1 && text_is(map.shaders[0].name, "textures/test/wall"));
  CHECK(map.shaders[0].surface_flags == 128 && map.shaders[0].content_flags == 1);
  CHECK(map.plane_count == 1 && vec3_is(map.planes[0].normal, 1, 0, 0) && map.planes[0].distance == 16);
  CHECK(map.nodes[0].children[0] == -1 && map.nodes[0].children[1] == -1);
  CHECK(map.leaves[0].cluster == 0 && map.leaves[0].area == 0);
  CHECK(vec3_is(map.leaves[0].bounds.min, -32, -32, -32) && vec3_is(map.leaves[0].bounds.max, 32, 32, 32));
  CHECK(map.leaves[0].first_surface == 0 && map.leaves[0].surface_count == 4);
  CHECK(map.leaves[0].first_brush == 0 && map.leaves[0].brush_count == 1);
  const double want_leaf_surfaces[4] = {0, 1, 2, 3};
  CHECK(map.leaf_surface_count == 4 &&
        memcmp(map.leaf_surfaces, want_leaf_surfaces, sizeof want_leaf_surfaces) == 0);
  CHECK(map.leaf_brush_count == 1 && map.leaf_brushes[0] == 0);
  CHECK(map.models[0].brush_count == 1);
  CHECK(map.brushes[0].first_side == 0 && map.brushes[0].side_count == 6 && map.brushes[0].shader == 0);
  CHECK(map.brush_side_count == 6);
  const qa_q3_bsp_vertex *vertex = &map.vertices[1];
  CHECK(vec3_is(vertex->position, 1, 1.5, -1));
  CHECK(vertex->tex_coord.x == 0.25 && vertex->tex_coord.y == 0.75);
  CHECK(vertex->lightmap_coord.x == 0.125 && vertex->lightmap_coord.y == 0.875);
  CHECK(vec3_is(vertex->normal, 0, 0, 1));
  CHECK(vertex->color.x == 12 && vertex->color.y == 34 && vertex->color.z == 56 && vertex->color.w == 255);
  const double want_indices[3] = {0, 1, 2};
  CHECK(map.index_count == 3 && memcmp(map.indices, want_indices, sizeof want_indices) == 0);
  CHECK(map.fog_count == 1 && text_is(map.fogs[0].shader, "fog") && map.fogs[0].brush == 0 &&
        map.fogs[0].visible_side == -1);
  CHECK(text_is(map.surfaces[0].type, "planar") && text_is(map.surfaces[1].type, "patch") &&
        text_is(map.surfaces[2].type, "triangles") && text_is(map.surfaces[3].type, "flare"));
  CHECK(vec3_is(map.surfaces[1].lightmap_vectors[0], 3.5, 4.5, 5.5));
  CHECK(vec3_is(map.surfaces[1].lightmap_vectors[1], 6.5, 7.5, 8.5));
  CHECK(vec3_is(map.surfaces[1].lightmap_vectors[2], 9.5, 10.5, 11.5));
  CHECK(map.lightmap_count == 1 && map.lightmaps[0].length == 49152 &&
        map.lightmaps[0].bytes[49151] == 127);
  CHECK(map.light_grid_count == 1 && vec3_is(map.light_grid[0].ambient, 1, 2, 3) &&
        vec3_is(map.light_grid[0].directed, 4, 5, 6) && map.light_grid[0].lat_long.x == 7 &&
        map.light_grid[0].lat_long.y == 8);
  CHECK(map.visibility != NULL && map.visibility->cluster_count == 1 &&
        map.visibility->bytes_per_cluster == 1 && map.visibility->bits.length == 1 &&
        map.visibility->bits.bytes[0] == 1);

  qa_q3_world_geometry world;
  qa_q3_adapt_bsp(&map, &world);
  CHECK(vec3_is(world.planes[0].plane.normal, 1, 0, 0) && world.planes[0].plane.distance == 16 &&
        world.planes[0].type == 0 && world.planes[0].signbits == 0);
  CHECK(!world.nodes[0].children[0].is_node && world.nodes[0].children[0].index == 0);
  CHECK(world.models[0].surfaces.first == 0 && world.models[0].surfaces.count == 4);
  CHECK(world.models[0].brushes.first == 0 && world.models[0].brushes.count == 1);
  CHECK(world.brushes[0].sides.first == 0 && world.brushes[0].sides.count == 6);
  const qa_q3_bsp_surface *patch = &world.surfaces[1];
  CHECK(text_is(patch->kind, "patch") && patch->width == 3 && patch->height == 3);
  CHECK(patch->shader == 0 && patch->fog == 0);
  CHECK(patch->vertices.first == 3 && patch->vertices.count == 9);
  CHECK(patch->indices.first == 0 && patch->indices.count == 0);
  CHECK(patch->lightmap.image == 0 && patch->lightmap.x == 8 && patch->lightmap.y == 16);
  CHECK(patch->lightmap.width == 32 && patch->lightmap.height == 64);
  CHECK(vec3_is(patch->lightmap.origin, 0.5, 1.5, 2.5));
  qa_q3_adapted_world_destroy(&world);
  qa_q3_bsp_map_destroy(&map);

  qa_q3_decoded_world decoded;
  qa_q3_decode_world(bytes, length, NULL, &decoded);
  CHECK(decoded.world.surface_count == 4);
  qa_q3_decoded_world_destroy(&decoded);
  free(bytes);
}

static void test_q3_empty_pvs(void) {
  size_t length = 0;
  uint8_t *bytes = q3_fixture(&length);
  put_i32(bytes, 12 + 16 * 8, 0);
  uint8_t *wrapped = malloc(length + 10);
  memset(wrapped, 0, length + 10);
  memcpy(wrapped + 3, bytes, length);
  qa_q3_decoded_world world;
  qa_q3_decode_world(wrapped + 3, length, NULL, &world);
  memset(wrapped, 0, length + 10);
  CHECK(world.world.visibility == NULL);
  CHECK(world.world.lightmaps[0].bytes[0] == 127);
  CHECK(world.world.vertices[0].color.w == 255);
  qa_q3_decoded_world_destroy(&world);
  free(wrapped);
  free(bytes);
}

static void test_q3_rejects(void) {
  size_t length = 0;
  uint8_t *bytes = q3_fixture(&length);
  CHECK_ABORTS(qa_q3_bsp_map m; qa_q3_parse_bsp(bytes, 100, NULL, &m));
  CHECK_ABORTS(qa_q3_bsp_map m; qa_q3_parse_bsp(bytes, length - 1, NULL, &m));
  uint8_t *copy = malloc(length);
  memcpy(copy, bytes, length);
  put_i32(copy, 4, 45);
  CHECK_ABORTS(qa_q3_bsp_map m; qa_q3_parse_bsp(copy, length, NULL, &m));
  memcpy(copy, bytes, length);
  put_i32(copy, 8, -1);
  CHECK_ABORTS(qa_q3_bsp_map m; qa_q3_parse_bsp(copy, length, NULL, &m));
  memcpy(copy, bytes, length);
  put_i32(copy, (size_t)get_i32(copy, 8 + 11 * 8), 12);
  CHECK_ABORTS(qa_q3_bsp_map m; qa_q3_parse_bsp(copy, length, NULL, &m));
  free(copy);
  free(bytes);
}

static void test_q3_entities(void) {
  size_t count = 0;
  qa_q3_entity *entities = qa_q3_parse_entities(
      "// header\n{ \"classname\" \"worldspawn\" /* comment */ \"key\" \"old\" \"key\" \"new\" \"message\" "
      "\"caf\xc3\xa9\\nnext\" }",
      NULL, &count);
  CHECK(count == 1);
  CHECK(text_is(qa_q3_entity_get(&entities[0], "key"), "new"));
  CHECK(text_is(qa_q3_entity_get(&entities[0], "message"), "caf\xc3\xa9\\nnext"));
  qa_q3_entities_destroy(entities, count);
  CHECK_ABORTS(size_t n; qa_q3_entities_destroy(qa_q3_parse_entities("{ \"key\" }", NULL, &n), n));
}

/* ---------- q3-model md4 (md4.test.ts fixture) ---------- */

static void md4_string(qa_binary_writer *writer, const char *value) {
  uint8_t bytes[64] = {0};
  memcpy(bytes, value, strlen(value));
  qa_binary_writer_bytes(writer, bytes, 64);
}

static void md4_surface(qa_binary_writer *writer, size_t start, const char *name) {
  qa_binary_writer_i32(writer, 123);
  md4_string(writer, name);
  md4_string(writer, "Textures/Armor");
  qa_binary_writer_i32(writer, -33);
  qa_binary_writer_i32(writer, -(int64_t)start);
  qa_binary_writer_i32(writer, 3);
  qa_binary_writer_i32(writer, 184);
  qa_binary_writer_i32(writer, 1);
  qa_binary_writer_i32(writer, 168);
  qa_binary_writer_i32(writer, 1);
  qa_binary_writer_i32(writer, 180);
  qa_binary_writer_i32(writer, 316);
  const int triangle[4] = {2, 0, 1, 0};
  for (int i = 0; i < 4; i++) qa_binary_writer_i32(writer, triangle[i]);
  const int counts[3] = {2, 0, 1};
  for (int v = 0; v < 3; v++) {
    const double values[5] = {0, -0.0, 2, -0.25, 1.5};
    for (int i = 0; i < 5; i++) qa_binary_writer_f32(writer, values[i]);
    qa_binary_writer_i32(writer, counts[v]);
    for (int weight = 0; weight < counts[v]; weight++) {
      qa_binary_writer_i32(writer, weight == 0 ? 1 : 0);
      qa_binary_writer_f32(writer, weight == 0 ? 0.25 : 1.25);
      const double offset[3] = {0.1, 2, -3};
      for (int i = 0; i < 3; i++) qa_binary_writer_f32(writer, offset[i]);
    }
  }
}

static uint8_t *md4_fixture(size_t *out_length) {
  qa_binary_writer writer;
  writer_init(&writer, 1344 + 7);
  qa_binary_writer_u32(&writer, 0x34504449);
  qa_binary_writer_i32(&writer, 1);
  md4_string(&writer, "Models/Test.MD4");
  qa_binary_writer_i32(&writer, 2);
  qa_binary_writer_i32(&writer, 2);
  qa_binary_writer_i32(&writer, INT32_MIN);
  qa_binary_writer_i32(&writer, 100);
  qa_binary_writer_i32(&writer, 2);
  qa_binary_writer_i32(&writer, 372);
  qa_binary_writer_i32(&writer, 1344);
  for (int frame = 0; frame < 2; frame++) {
    const double values[10] = {-1, -2, -3, 4, 5, 6, 0.5, 1.5, 2.5, 7};
    for (int i = 0; i < 10; i++) qa_binary_writer_f32(&writer, values[i] + frame);
    for (int bone = 0; bone < 2; bone++)
      for (int entry = 1; entry <= 12; entry++)
        qa_binary_writer_f32(&writer, frame * 100 + bone * 20 + entry);
  }
  qa_binary_writer_i32(&writer, 2);
  qa_binary_writer_i32(&writer, 12);
  qa_binary_writer_i32(&writer, 644);
  md4_surface(&writer, 384, "BODY_1");
  md4_surface(&writer, 700, "HEAD");
  qa_binary_writer_i32(&writer, 1);
  qa_binary_writer_i32(&writer, 12);
  qa_binary_writer_i32(&writer, 328);
  md4_surface(&writer, 1028, "LOD_1");
  uint8_t tail[7] = {255, 254, 253, 252, 251, 250, 249};
  qa_binary_writer_bytes(&writer, tail, 7);
  return writer_finish(&writer, out_length);
}

static void test_md4(void) {
  size_t length = 0;
  uint8_t *bytes = md4_fixture(&length);
  qa_decoded_md4_model model;
  qa_parse_md4(bytes, length, NULL, &model);
  CHECK(model.model.byte_length == 1344);
  CHECK(model.model.lod_count == 2);
  CHECK(model.model.lods[0].surface_count == 2 && model.model.lods[1].surface_count == 1);
  const qa_md4_surface *surface = &model.model.lods[0].surfaces[0];
  CHECK(surface->bone_references.count == 1 && surface->bone_references.values[0] == 0);
  CHECK(surface->vertices[0].weights[0].bone_index == 1);
  CHECK(surface->vertices[0].weight_count == 2 && surface->vertices[1].weight_count == 0 &&
        surface->vertices[2].weight_count == 1);
  size_t vertex_count = 0;
  qa_model_vertex *vertices = qa_skin_md4_surface(&model.model, surface, 1, 0, 0.5, &vertex_count);
  CHECK(vertex_count == 3);
  CHECK(close_to(vertices[0].position.x, 0.25 * 6.1 + 1.25 * 4.1));
  CHECK(vec3_is(vertices[0].normal, 169, 181, 193));
  CHECK(vec3_is(vertices[1].position, 0, 0, 0));
  free(vertices);
  memset(bytes, 0, length);
  CHECK(model.bytes[0] == 73);
  qa_decoded_md4_model_destroy(&model);
  free(bytes);

  bytes = md4_fixture(&length);
  CHECK_ABORTS(qa_decoded_md4_model m; qa_parse_md4(bytes, 1300, NULL, &m));
  free(bytes);
}

/* ---------- q3-model md5 (md5.test.ts) ---------- */

static const char *md5_mesh_text =
    "MD5Version 10\n"
    "commandline \"\"\n"
    "numJoints 2\n"
    "numMeshes 1\n"
    "joints {\n"
    " \"Root\" -1 ( 1 0 0 ) ( 0 0 0 )\n"
    " \"Child\" 0 ( 1 1 0 ) ( 0 0 0 )\n"
    "}\n"
    "mesh {\n"
    " shader \"not-the-replacement-skin\"\n"
    " numverts 3\n"
    " vert 0 ( 0 0 ) 0 1\n"
    " vert 1 ( 1 0 ) 1 1\n"
    " vert 2 ( 0 1 ) 2 1\n"
    " numtris 1\n"
    " tri 0 0 1 2\n"
    " numweights 3\n"
    " weight 0 1 1 ( 1 0 0 )\n"
    " weight 1 1 1 ( 0 1 0 )\n"
    " weight 2 1 1 ( 0 0 0 )\n"
    "}";

static const char *md5_anim_text =
    "MD5Version 10\n"
    "commandline \"test\"\n"
    "numFrames 2\n"
    "numJoints 2\n"
    "frameRate 20\n"
    "numAnimatedComponents 1\n"
    "hierarchy {\n"
    " \"Root\" -1 1 0\n"
    " \"Child\" 0 0 1\n"
    "}\n"
    "bounds { ( 0 0 0 ) ( 4 4 4 ) ( 0 0 0 ) ( 8 8 8 ) }\n"
    "baseframe { ( 1 0 0 ) ( 0 0 0 ) ( 0 1 0 ) ( 0 0 0 ) }\n"
    "frame 1 { 2 }\n"
    "frame 0 { 1 }\n";

static void test_md5(void) {
  qa_md5_scale_source scale = {"test.md5scale",
                               "{\"Root\":{\"scale_positions\":true,\"1\":2},\"Child\":{\"1\":3}}"};
  qa_md5_mesh_file mesh;
  qa_md5_animation animation;
  qa_parse_md5_mesh(md5_mesh_text, NULL, &mesh);
  qa_parse_md5_anim(md5_anim_text, "test.md5anim", &scale, &animation);
  qa_decoded_md5_model model;
  qa_create_md5_model(&mesh, &animation, NULL, &model);
  CHECK(model.model.frames[0].joints[0].position.x == 1);
  CHECK(model.model.frames[1].joints[0].position.x == 4);
  CHECK(vec3_is(model.model.frames[1].joints[1].position, 4, 1, 0));
  CHECK(model.animation->frames[1].local_joints[0].position.x == 2);
  bool owned = false;
  size_t pose_count = 0;
  const qa_skeleton_joint_pose *pose = qa_sample_md5_pose(&model.model, 1, 0, 0.5, &owned, &pose_count);
  CHECK(owned && pose_count == 2);
  CHECK(pose[1].scale == 3);
  size_t vertex_count = 0;
  qa_model_vertex *skinned = qa_skin_md5_mesh(&model.model.meshes[0], pose, pose_count, &vertex_count);
  CHECK(vertex_count == 3);
  CHECK(vec3_is(skinned[0].position, 5.5, 1, 0));
  free(skinned);
  free((void *)pose);
  CHECK(model.model.joints[0].scale_positions && !model.model.joints[1].scale_positions);
  qa_decoded_md5_model_destroy(&model);
  qa_md5_animation_destroy(&animation);
  qa_md5_mesh_file_destroy(&mesh);
}

static void test_md5_replacements(void) {
  qa_md5_paths paths;
  qa_md5_paths_for("models/monster/tris.md2", "q2", &paths);
  CHECK(text_is(paths.mesh_path, "models/monster/md5/tris.md5mesh"));
  qa_md5_paths_destroy(&paths);
  qa_md5_paths_for("progs/player.mdl", "q1", &paths);
  CHECK(text_is(paths.mesh_path, "progs/player.md5mesh"));
  qa_md5_paths_destroy(&paths);
  const char *skins[1] = {"players/male/grunt.pcx"};
  qa_md5_skin_selection selection;
  qa_md2_replacement_skin_selection(skins, 1, 0, "scale.md5scale", NULL, 0, &selection);
  CHECK(text_is(selection.kind, "q2-md2-replacement"));
  CHECK(selection.skins.count == 1 && text_is(selection.skins.values[0], "players/male/md5/grunt.pcx"));
  CHECK(selection.source_frame_count == 0);
  CHECK(text_is(selection.scale_source, "scale.md5scale"));
  CHECK(selection.diagnostics.count == 0);
  qa_md5_skin_selection_destroy(&selection);
  CHECK(!qa_md5_replacement_allowed(true, 0, true, 1));
  CHECK(qa_md5_replacement_allowed(true, 2, true, 1));
}

static void test_md5_rejects(void) {
  char *bad_mesh = malloc(strlen(md5_mesh_text) + 8);
  const char *at = strstr(md5_mesh_text, "0 1\n vert 1");
  size_t prefix = (size_t)(at - md5_mesh_text);
  memcpy(bad_mesh, md5_mesh_text, prefix);
  strcpy(bad_mesh + prefix, "9 1\n vert 1");
  strcat(bad_mesh, at + strlen("0 1\n vert 1"));
  CHECK_ABORTS(qa_md5_mesh_file f; qa_parse_md5_mesh(bad_mesh, NULL, &f));
  free(bad_mesh);
  char *bad_anim = malloc(strlen(md5_anim_text) + 1);
  strcpy(bad_anim, md5_anim_text);
  char *frame0 = strstr(bad_anim, "frame 0");
  frame0[6] = '1';
  CHECK_ABORTS(qa_md5_animation a; qa_parse_md5_anim(bad_anim, NULL, NULL, &a));
  free(bad_anim);
}

static void test_md5_quaternion_rows(void) {
  const qa_vec4 orientations[5] = {{0, 0, 0, -1}, {-0.0, 0, -0.0, 1}, {0.25, -0.5, 0.125, -0.75},
                                   {1, 2, 3, 4}, {0, 0, 0, 0}};
  const qa_vec3 vectors[3] = {{0, -0.0, 0}, {1, 0, -0.0}, {-2.5, 17, 0.125}};
  for (int o = 0; o < 5; o++) {
    for (int v = 0; v < 3; v++) {
      qa_vec3 via_rows = qa_rotate_quaternion_rows(qa_quaternion_rotation_rows_of(orientations[o]),
                                                   vectors[v]);
      qa_vec3 via_axis = qa_rotate_quaternion_axis(orientations[o], vectors[v]);
      /* Bit equality including signed zeros, as toEqual distinguishes them. */
      CHECK(memcmp(&via_rows, &via_axis, sizeof via_rows) == 0);
    }
  }
}

static void test_md5_skin_reference(void) {
  qa_md5_mesh_file mesh_file;
  qa_md5_animation animation;
  qa_parse_md5_mesh(md5_mesh_text, NULL, &mesh_file);
  qa_parse_md5_anim(md5_anim_text, NULL, NULL, &animation);
  qa_decoded_md5_model model;
  qa_create_md5_model(&mesh_file, &animation, NULL, &model);
  qa_md5_mesh surface = model.model.meshes[0];
  qa_md5_mesh_vertex *vertices = malloc(surface.vertex_count * sizeof *vertices);
  for (size_t i = 0; i < surface.vertex_count; i++) {
    vertices[i] = surface.vertices[i];
    vertices[i].weights.first = 0;
    vertices[i].weights.count = 3;
  }
  surface.vertices = vertices;
  const double selections[2][3] = {{0, 0, 0}, {1, 0, 0.375}};
  qa_model_vertex *previous_result = NULL;
  size_t previous_count = 0;
  for (int round = 0; round < 2; round++) {
    bool owned = false;
    size_t pose_count = 0;
    const qa_skeleton_joint_pose *pose =
        qa_sample_md5_pose(&model.model, selections[round][0], selections[round][1], selections[round][2],
                           &owned, &pose_count);
    /* Reference skinning through rotateQuaternionAxis, mirroring the TS test. */
    qa_model_vertex *reference = malloc(surface.vertex_count * sizeof *reference);
    for (size_t v = 0; v < surface.vertex_count; v++) {
      qa_vec3 position = {0, 0, 0}, normal = {0, 0, 0};
      for (int i = 0; i < 3; i++) {
        const qa_md5_mesh_weight *weight = &surface.weights[i];
        const qa_skeleton_joint_pose *joint = &pose[(size_t)weight->joint];
        qa_vec3 rotated = qa_rotate_quaternion_axis(joint->orientation, weight->position);
        qa_vec3 point = {joint->position.x + joint->scale * rotated.x,
                         joint->position.y + joint->scale * rotated.y,
                         joint->position.z + joint->scale * rotated.z};
        qa_vec3 direction = qa_rotate_quaternion_axis(joint->orientation, surface.vertices[v].normal);
        position.x += weight->bias * point.x;
        position.y += weight->bias * point.y;
        position.z += weight->bias * point.z;
        normal.x += weight->bias * direction.x;
        normal.y += weight->bias * direction.y;
        normal.z += weight->bias * direction.z;
      }
      reference[v].position = position;
      reference[v].normal = normal;
    }
    size_t count = 0;
    qa_model_vertex *skinned = qa_skin_md5_mesh(&surface, pose, pose_count, &count);
    bool equal = count == surface.vertex_count;
    for (size_t v = 0; v < count && equal; v++) {
      equal = vec3_is(skinned[v].position, reference[v].position.x, reference[v].position.y,
                      reference[v].position.z) &&
              vec3_is(skinned[v].normal, reference[v].normal.x, reference[v].normal.y,
                      reference[v].normal.z);
    }
    CHECK(equal);
    if (previous_result != NULL) {
      CHECK(memcmp(skinned, previous_result, count * sizeof *skinned) != 0);
      free(previous_result);
    }
    previous_result = skinned;
    previous_count = count;
    (void)previous_count;
    free(reference);
    if (owned) free((void *)pose);
  }
  free(previous_result);
  CHECK_ABORTS(size_t n; free(qa_skin_md5_mesh(&surface, NULL, 0, &n)));
  qa_md5_mesh no_weights = surface;
  no_weights.weights = NULL;
  no_weights.weight_count = 0;
  CHECK_ABORTS(size_t n; free(qa_skin_md5_mesh(&no_weights, NULL, 0, &n)));
  free(vertices);
  qa_decoded_md5_model_destroy(&model);
  qa_md5_animation_destroy(&animation);
  qa_md5_mesh_file_destroy(&mesh_file);
}

/* ---------- q2-map smoke (retail cases need the corpus) ---------- */

static void test_q2_visibility_smoke(void) {
  size_t length = 0;
  uint8_t *row = qa_decompress_q2_visibility(NULL, -1, "pvs", 16, &length);
  CHECK(length == 2 && row[0] == 0 && row[1] == 0);
  free(row);
  row = qa_decompress_q2_visibility(NULL, 0, "pvs", 16, &length);
  CHECK(length == 2 && row[0] == 255 && row[1] == 255);
  free(row);
}

int main(void) {
  test_q1_quake64();
  test_q1_bsp2_2psb();
  test_q1_bspx_retention();
  test_q1_brush_and_decoupled();
  test_q1_lit_fallback();
  test_q1_read_rejects();
  test_q1_entities_pvs();
  test_mdl_groups();
  test_spr_groups();
  test_mdl_geometry_values();
  test_md2_geometry_values();
  test_q3_ibsp44();
  test_q3_ibsp46();
  test_q3_empty_pvs();
  test_q3_rejects();
  test_q3_entities();
  test_md4();
  test_md5();
  test_md5_replacements();
  test_md5_rejects();
  test_md5_quaternion_rows();
  test_md5_skin_reference();
  test_q2_visibility_smoke();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
