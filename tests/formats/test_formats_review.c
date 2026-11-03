/*
 * Confirmation-gate review checks for src/formats. Each expected value
 * comes from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/images/bmp.h"
#include "qa/formats/images/gif.h"
#include "qa/formats/images/jpeg.h"
#include "qa/formats/images/jpeg_encoder.h"
#include "qa/formats/images/limits.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/images/png.h"
#include "qa/formats/images/q3_bmp.h"
#include "qa/formats/images/tga.h"
#include "qa/formats/q1_map.h"
#include "qa/formats/q2_map.h"
#include "qa/formats/q12_model.h"
#include "qa/formats/q12_model/animation.h"
#include "qa/formats/q12_model/pose_lru.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/queries.h"
#include "qa/formats/q3_map/decode.h"
#include "qa/formats/q3_model/lod.h"
#include "qa/formats/q3_model/md3.h"
#include "qa/formats/q3_model/md5.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/support.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

typedef struct decode_job {
  int kind; /* 0 tga, 1 bmp, 2 gif, 3 png */
  const uint8_t *bytes;
  size_t length;
} decode_job;

static void decode_run(void *raw) {
  decode_job *job = raw;
  if (job->kind == 0) {
    qa_tga_image image;
    if (qa_decode_tga(job->bytes, job->length, NULL, &image)) qa_tga_image_destroy(&image);
  } else if (job->kind == 1) {
    qa_bmp_image image;
    if (qa_decode_bmp(job->bytes, job->length, NULL, &image)) qa_bmp_image_destroy(&image);
  } else if (job->kind == 2) {
    qa_gif_image image;
    if (qa_decode_gif(job->bytes, job->length, NULL, &image)) qa_gif_image_destroy(&image);
  } else {
    qa_png_image image;
    if (qa_decode_png(job->bytes, job->length, NULL, &image)) qa_png_image_destroy(&image);
  }
}

static void check_rejects(int kind, const uint8_t *bytes, size_t length, const char *expected) {
  decode_job job = {kind, bytes, length};
  qa_caught_error caught;
  CHECK(!qa_try(decode_run, &job, &caught));
  CHECK(strstr(caught.message, expected) != NULL);
  if (strstr(caught.message, expected) == NULL) fprintf(stderr, "  got \"%s\"\n", caught.message);
}

/* limits.ts imageSizeAllowed and the decoders that use it (TS messages:
 * "<tga>:0: Unsupported TGA header", "<bmp>:18: Invalid BMP dimensions",
 * "<gif>:994: GIF exceeds the frame or decoded size limit"). */
static void test_image_limits(void) {
  CHECK(qa_image_size_allowed(16384, 4096));
  CHECK(!qa_image_size_allowed(16385, 1));
  CHECK(!qa_image_size_allowed(9000, 9000));
  CHECK(qa_image_size_allowed(8192, 8192));
  CHECK(!qa_image_size_allowed(0, 10));
  CHECK(!qa_image_size_allowed(1.5, 10));

  uint8_t tga[18] = {0};
  tga[2] = 2;
  tga[12] = 20000 & 255;
  tga[13] = 20000 >> 8;
  tga[14] = 10;
  tga[16] = 32;
  check_rejects(0, tga, sizeof tga, "<tga>:0: Unsupported TGA header");
  uint8_t big_tga[18] = {0};
  big_tga[2] = 2;
  big_tga[12] = big_tga[13] = big_tga[14] = big_tga[15] = 255;
  big_tga[16] = 32;
  check_rejects(0, big_tga, sizeof big_tga, "Unsupported TGA header");

  uint8_t bmp[54] = {0};
  bmp[0] = 'B';
  bmp[1] = 'M';
  bmp[10] = 54;
  bmp[14] = 40;
  bmp[18] = 9000 & 255;
  bmp[19] = 9000 >> 8;
  bmp[22] = 9000 & 255;
  bmp[23] = 9000 >> 8;
  bmp[26] = 1;
  bmp[28] = 24;
  check_rejects(1, bmp, sizeof bmp, "<bmp>:18: Invalid BMP dimensions");

  /* 1024x1024 canvas with 5000 one-pixel frames: the 65th frame passes the
   * 256 MiB retained-bytes limit. */
  size_t length = 19 + 5000 * 15 + 1;
  uint8_t *gif = malloc(length);
  size_t at = 0;
  const uint8_t head[] = {'G', 'I', 'F', '8', '9', 'a', 0, 4, 0, 4, 0x80, 0, 0, 0, 0, 0, 255, 255, 255};
  memcpy(gif, head, sizeof head);
  at = sizeof head;
  const uint8_t frame[] = {0x2c, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 0x01, 0};
  for (int i = 0; i < 5000; i++) {
    memcpy(gif + at, frame, sizeof frame);
    at += sizeof frame;
  }
  gif[at++] = 0x3b;
  check_rejects(2, gif, at, "<gif>:994: GIF exceeds the frame or decoded size limit");
  free(gif);
}

/* applyImageGamma with one profile record mutated in place (TS builds a
 * fresh profile object): [101,101,101,255] then [160,160,160,255]. */
static void test_gamma_profile_value(void) {
  uint8_t rgba[4] = {100, 100, 100, 255};
  qa_image_level level = {1, 1, {rgba, 4}};
  qa_gamma_profile profile = {QA_GAMMA_Q1_GL, 1, 1, 0, false};
  qa_image_level out;
  CHECK(qa_apply_image_gamma(&level, &profile, &out));
  CHECK(out.pixels.bytes[0] == 101 && out.pixels.bytes[3] == 255);
  free((void *)out.pixels.bytes);
  profile.gamma = 0.5;
  CHECK(qa_apply_image_gamma(&level, &profile, &out));
  CHECK(out.pixels.bytes[0] == 160 && out.pixels.bytes[1] == 160 && out.pixels.bytes[3] == 255);
  free((void *)out.pixels.bytes);
}

static size_t from_hex(const char *hex, uint8_t *out) {
  size_t count = 0;
  for (; hex[0] != 0 && hex[1] != 0; hex += 2) {
    unsigned value = 0;
    sscanf(hex, "%2x", &value);
    out[count++] = (uint8_t)value;
  }
  return count;
}

/* node inflateSync messages through decodePng (TS:
 * "<png>:8: PNG inflate: unexpected end of file",
 * "<png>:8: PNG inflate: Cannot create a Buffer larger than 5 bytes"). */
static void test_png_inflate_messages(void) {
  uint8_t bytes[128];
  size_t length = from_hex(
      "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000a49444154789c"
      "63606462660100005334eb000000000049454e44ae426082",
      bytes);
  check_rejects(3, bytes, length, "<png>:8: PNG inflate: unexpected end of file");
  length = from_hex(
      "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000f49444154789c"
      "636064626661650300003f001621bad4540000000049454e44ae426082",
      bytes);
  check_rejects(3, bytes, length, "<png>:8: PNG inflate: Cannot create a Buffer larger than 5 bytes");
}

static void q3_bmp_run(void *raw) {
  decode_job *job = raw;
  qa_image_data image;
  if (qa_decode_q3_bmp(job->bytes, job->length, NULL, &image) == 1) qa_image_data_destroy(&image);
}

/* TS: BmpDropError "LoadBMP: only uncompressed BMP files supported (<buffer>)\n"
 * is a catchable throw for the caller's ERR_DROP. */
static void test_q3_bmp_drop_is_catchable(void) {
  uint8_t bmp[54] = {0};
  bmp[0] = 66;
  bmp[1] = 77;
  bmp[2] = 54;
  bmp[18] = 1;
  bmp[22] = 1;
  bmp[28] = 24;
  bmp[30] = 1;
  decode_job job = {0, bmp, sizeof bmp};
  qa_caught_error caught;
  CHECK(!qa_try(q3_bmp_run, &job, &caught));
  CHECK(strcmp(caught.name, "BmpDropError") == 0);
  CHECK(strcmp(caught.message, "LoadBMP: only uncompressed BMP files supported (<buffer>)\n") == 0);
}

typedef struct entities_job {
  const char *text;
} entities_job;
static void entities_run(void *raw) {
  entities_job *job = raw;
  size_t count = 0;
  qa_q3_entity *entities = qa_q3_parse_entities(job->text, NULL, &count);
  qa_q3_entities_destroy(entities, count);
}

/* TS throws a catchable TextParseError (TS messages from bun). */
static void test_q3_entities_parse_error(void) {
  const char *texts[] = {"{ \"key\" }", "{ \"a\" \"b\" }\n{ \"c\" \"d\"\n x }", "x"};
  const char *messages[] = {"<entities>:1:9: missing value for entity key \"key\"",
                            "<entities>:3:4: missing value for entity key \"x\"",
                            "<entities>:1:1: expected \"{\""};
  for (int i = 0; i < 3; i++) {
    entities_job job = {texts[i]};
    qa_caught_error caught;
    CHECK(!qa_try(entities_run, &job, &caught));
    CHECK(strcmp(caught.name, "TextParseError") == 0);
    CHECK(strcmp(caught.message, messages[i]) == 0);
    if (strcmp(caught.message, messages[i]) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
  }
}

/* TS parseQ1Entities compares characters: bytes above 0x7f are token text,
 * not whitespace ([{"key":"m\u00e9ss","value":"v\u00e0l"}]). */
static void test_q1_entities_high_bytes(void) {
  size_t count = 0;
  qa_q1_entity *entities = qa_q1_parse_entities("{ m\xe9ss v\xe0l }", &count);
  CHECK(count == 1 && entities[0].property_count == 1);
  if (count == 1 && entities[0].property_count == 1) {
    CHECK(strcmp(entities[0].properties[0].key, "m\xe9ss") == 0);
    CHECK(strcmp(entities[0].properties[0].value, "v\xe0l") == 0);
  }
  qa_q1_entities_destroy(entities, count);
}

static uint32_t bitwise_crc(const uint8_t *bytes, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0 : 0xedb88320u);
  }
  return crc ^ 0xffffffffu;
}
static uint32_t be32(const uint8_t *at) {
  return ((uint32_t)at[0] << 24) | ((uint32_t)at[1] << 16) | ((uint32_t)at[2] << 8) | at[3];
}

/* Port of tests/formats/png-encoder.test.ts: every chunk carries the standard
 * CRC-32 of its type and data; chunks are IHDR, IDAT, IEND. */
static void test_png_encoder_chunks(void) {
  uint8_t rgba[8 * 4 * 4];
  for (size_t i = 0; i < sizeof rgba; i++) rgba[i] = (uint8_t)((i * 37) & 0xff);
  size_t length = 0;
  uint8_t *png = qa_encode_png(8, 4, rgba, sizeof rgba, &length);
  CHECK(png != NULL);
  const char *types[] = {"IHDR", "IDAT", "IEND"};
  int count = 0;
  for (size_t offset = 8; png != NULL && offset < length;) {
    uint32_t chunk = be32(png + offset);
    CHECK(count < 3 && memcmp(png + offset + 4, types[count < 3 ? count : 2], 4) == 0);
    CHECK(be32(png + offset + 8 + chunk) == bitwise_crc(png + offset + 4, 4 + chunk));
    offset += chunk + 12;
    count++;
  }
  CHECK(count == 3);
  free(png);
}

static uint8_t *hex_bytes(const char *hex, size_t *length) {
  size_t count = strlen(hex) / 2;
  uint8_t *bytes = malloc(count == 0 ? 1 : count);
  for (size_t i = 0; i < count; i++) {
    unsigned value = 0;
    sscanf(hex + i * 2, "%2x", &value);
    bytes[i] = (uint8_t)value;
  }
  *length = count;
  return bytes;
}

/* Fixtures built by a bun generator over readQ1Bsp (review-b). */
static const char q1_cycle_hex[] =
    "1d00000000000000000000007c0000001400000000000000000000000000000000000000000000000000000090000000"
    "300000000000000000000000000000000000000000000000000000000000000000000000c00000001c00000000000000"
    "0000000000000000000000000000000000000000dc000000400000000000803f00000000000000000000000000000000"
    "000000000100ffff00000000000000000000000000000000000000000000ffff00000000000000000000000000000000"
    "00000000ffffffff00000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000ffffffffffffffffffffffff000000000000000000000000";

static const char q1_bspx_hex[] =
    "1d00000000000000000000007c0000001400000000000000000000000000000000000000000000000000000090000000"
    "180000000000000000000000000000000000000000000000000000000000000000000000a80000001c00000000000000"
    "0000000000000000000000000000000000000000c4000000400000000000803f00000000000000000000000000000000"
    "00000000ffffffff0000000000000000000000000000000000000000ffffffff00000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "ffffffffffffffff00000000000000000000000042535058020000004c4d534849465400000000000000000000000000"
    "0000000054010000030000005645525445584e4f524d414c530000000000000000000000570100000500000000000000"
    "000000000000000000000000";

static const char q1_plain_hex[] =
    "1d00000000000000000000007c0000001400000000000000000000000000000000000000000000000000000090000000"
    "180000000000000000000000000000000000000000000000000000000000000000000000a80000001c00000000000000"
    "0000000000000000000000000000000000000000c4000000400000000000803f00000000000000000000000000000000"
    "00000000ffffffff0000000000000000000000000000000000000000ffffffff00000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "ffffffffffffffff000000000000000000000000";

static const char q1_order_hex[] =
    "1d00000000000000000000007c0000001400000000000000000000000000000000000000000000000000000090000000"
    "190000000000000000000000000000000000000000000000000000000000000000000000a90000001c00000000000000"
    "0000000000000000000000000000000000000000c5000000400000000000803f00000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000ffffffff000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000ffffff"
    "ffffffffffffffffff00000000000000000000000000000042535058010000005247424c49474854494e470000000000"
    "0000000000000000300100000400000000000000";

typedef struct q1_bsp_job {
  const uint8_t *bytes;
  size_t length;
  const char *source;
  qa_q1_map map;
} q1_bsp_job;

static void q1_bsp_run(void *raw) {
  q1_bsp_job *job = raw;
  qa_q1_map_options options = {0};
  options.source = job->source;
  qa_q1_read_bsp(job->bytes, job->length, &options, &job->map);
}

static void check_q1_rejects(const char *hex, const char *source, const char *expected) {
  size_t length = 0;
  uint8_t *bytes = hex_bytes(hex, &length);
  q1_bsp_job job;
  memset(&job, 0, sizeof job);
  job.bytes = bytes;
  job.length = length;
  job.source = source;
  qa_caught_error caught;
  bool ok = qa_try(q1_bsp_run, &job, &caught);
  CHECK(!ok);
  if (!ok) {
    CHECK(strcmp(caught.name, "BinaryError") == 0);
    CHECK(strcmp(caught.message, expected) == 0);
    if (strcmp(caught.message, expected) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
  }
  qa_q1_map_destroy(&job.map);
  free(bytes);
}

/* TS requireAcyclic, readBspxMetadata's hoisted exact() checks, and the map
 * literal reading nodes before selecting lighting (messages from bun). */
static void test_q1_bsp_validation_order(void) {
  check_q1_rejects(q1_cycle_hex, "c.bsp", "c.bsp:drawing nodes:0: cycle in BSP nodes");
  check_q1_rejects(q1_bspx_hex, "x.bsp", "BSPX VERTEXNORMALS:0: expected 0 bytes, got 5");
  check_q1_rejects(q1_order_hex, "o.bsp", "o.bsp:nodes:0: lump length 25 is not a multiple of 24");
  size_t length = 0;
  uint8_t *bytes = hex_bytes(q1_plain_hex, &length);
  q1_bsp_job job;
  memset(&job, 0, sizeof job);
  job.bytes = bytes;
  job.length = length;
  job.source = "p.bsp";
  qa_caught_error caught;
  CHECK(qa_try(q1_bsp_run, &job, &caught));
  CHECK(job.map.world.node_count == 1);
  qa_q1_map_destroy(&job.map);
  free(bytes);
}

/* TS q1LeafPvs catches a BinaryError from a corrupt row and returns every
 * leaf visible (bun: ffffff for 20 leaves over the row 00 00). */
static void test_q1_leaf_pvs_corrupt_row(void) {
  qa_q1_leaf leaves[2];
  memset(leaves, 0, sizeof leaves);
  leaves[1].has_visibility_offset = true;
  leaves[1].visibility_offset = 0;
  qa_q1_world_model model;
  memset(&model, 0, sizeof model);
  model.visible_leaves = 20;
  uint8_t visibility[2] = {0, 0};
  qa_q1_world_geometry world;
  memset(&world, 0, sizeof world);
  world.leaves = leaves;
  world.leaf_count = 2;
  world.models = &model;
  world.model_count = 1;
  world.visibility.bytes = visibility;
  world.visibility.length = 2;
  size_t length = 0;
  uint8_t *row = qa_q1_leaf_pvs(&world, 1, &length);
  CHECK(length == 3 && row[0] == 255 && row[1] == 255 && row[2] == 255);
  free(row);
}
static const char q2_bspx_hex[] =
    "4942535026000000a000000000000000a000000014000000b400000000000000b400000000000000b40000001c000000"
    "d000000000000000d000000000000000d000000000000000d00000001c000000ec00000000000000ec00000000000000"
    "ec00000000000000ec00000000000000ec000000300000001c010000000000001c010000000000001c01000000000000"
    "1c0100000800000024010000000000000000803f0000000000000000000000000000000000000000ffffffffffffffff"
    "0000000000000000000000000000000001000000ffff0000000000000000000000000000000000000000000000000000"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "0000000042535058030000004445434f55504c45445f4c4d0000000000000000000000008c010000290000004c494748"
    "54475249445f4f43545245450000000000000000b50100005d000000464143454e4f524d414c53000000000000000000"
    "000000001202000004000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "00000000000000803f0000803f0000803f00000000000000000000000000000000000000000000000001000000000100"
    "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
    "000005000000";

/* TS toQ2WorldGeometry turns a BinaryError from each BSPX extension into a
 * diagnostic; readQ2Lightgrid checks for cycles even with no leaves
 * (diagnostics from bun). */
static void test_q2_bspx_diagnostics(void) {
  size_t length = 0;
  uint8_t *bytes = hex_bytes(q2_bspx_hex, &length);
  qa_q2_decoded_map map;
  qa_decode_q2_map(bytes, length, "t.bsp", NULL, &map);
  const char *expected[] = {"BSPX DECOUPLED_LM:0: length 41 is not a multiple of record size 40",
                            "BSPX LIGHTGRID_OCTREE:37: invalid or repeated lightgrid node",
                            "BSPX FACENORMALS:4: range of 60 bytes exceeds 4-byte input"};
  CHECK(map.diagnostics.count == 3);
  for (size_t i = 0; i < 3 && i < map.diagnostics.count; i++) {
    CHECK(strcmp(map.diagnostics.values[i], expected[i]) == 0);
    if (strcmp(map.diagnostics.values[i], expected[i]) != 0) fprintf(stderr, "  got \"%s\"\n", map.diagnostics.values[i]);
  }
  CHECK(map.world.lightgrid == NULL && map.face_normals == NULL && map.world.decoupled_lightmaps == NULL);
  qa_q2_decoded_map_destroy(&map);
  free(bytes);
}

/* Mirrors the bun generator: header then lumps in order, empty lumps at 0. */
typedef struct q3_lump_spec {
  int index;
  int count, stride;
  void (*fill)(uint8_t *record, int i);
} q3_lump_spec;

static void put_i32(uint8_t *at, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) at[i] = (uint8_t)(bits >> (8 * i));
}

static uint8_t *q3_build(int version, int count, const q3_lump_spec *specs, int spec_count, size_t *length) {
  size_t head = 8 + (size_t)count * 8, total = head;
  for (int i = 0; i < spec_count; i++) total += (size_t)specs[i].count * (size_t)specs[i].stride;
  uint8_t *out = calloc(total, 1);
  put_i32(out, 0x50534249);
  put_i32(out + 4, version);
  size_t off = head;
  for (int lump = 0; lump < count; lump++) {
    for (int i = 0; i < spec_count; i++) {
      if (specs[i].index != lump) continue;
      size_t size = (size_t)specs[i].count * (size_t)specs[i].stride;
      put_i32(out + 8 + lump * 8, size > 0 ? (int32_t)off : 0);
      put_i32(out + 12 + lump * 8, (int32_t)size);
      for (int r = 0; r < specs[i].count; r++) {
        if (specs[i].fill != NULL) specs[i].fill(out + off + (size_t)r * (size_t)specs[i].stride, r);
      }
      off += size;
    }
  }
  *length = total;
  return out;
}

static uint32_t fnv1a(const uint8_t *bytes, size_t length) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < length; i++) h = (h ^ bytes[i]) * 16777619u;
  return h;
}

static void fill_leaf_area(uint8_t *r, int i) { (void)i; put_i32(r, -1); put_i32(r + 4, 1024); }
static void fill_flare(uint8_t *r, int i) { (void)i; put_i32(r + 4, -1); put_i32(r + 8, 4); put_i32(r + 16, 1000); }
static void fill_polygon44(uint8_t *r, int i) { (void)i; put_i32(r + 68, -1); put_i32(r + 76, 3); }
static void fill_leaf44(uint8_t *r, int i) { (void)i; put_i32(r + 36, 20000); }
static void fill_model44(uint8_t *r, int i) { (void)i; put_i32(r + 36, -1); }
static void fill_triangles44(uint8_t *r, int i) { (void)i; put_i32(r + 68, -1); put_i32(r + 84, 1); }

typedef struct q3_bsp_job {
  const uint8_t *bytes;
  size_t length;
} q3_bsp_job;

static void q3_bsp_run(void *raw) {
  q3_bsp_job *job = raw;
  qa_q3_bsp_map map;
  qa_q3_parse_bsp(job->bytes, job->length, "t.bsp", &map);
  qa_q3_bsp_map_destroy(&map);
}

/* TS parseQ3Bsp bounds leaf areas by MAX_MAP_AREAS and caps overlapping
 * surface ranges; normalizeQ3Bsp range-checks polygon vertices and caps model
 * walks (sizes, FNV-1a and messages from bun). */
static void test_q3_bsp_budgets(void) {
  const q3_lump_spec a[] = {{4, 1, 48, fill_leaf_area}};
  const q3_lump_spec b[] = {{1, 1, 72, NULL}, {10, 1000, 44, NULL}, {13, 70, 104, fill_flare}};
  const q3_lump_spec c[] = {{12, 1, 164, fill_polygon44}};
  const q3_lump_spec d[] = {{3, 1, 48, fill_leaf44}, {4, 20000, 4, NULL}, {6, 8, 48, fill_model44},
                            {12, 1, 164, fill_triangles44}, {14, 1, 4, NULL}};
  struct {
    int version, count;
    const q3_lump_spec *specs;
    int spec_count;
    size_t length;
    uint32_t hash;
    const char *message;
  } cases[] = {
      {46, 17, a, 1, 192, 3436397615u, "t.bsp:148: invalid leaf area"},
      {46, 17, b, 3, 51496, 3400609242u, "t.bsp:51404: surface ranges overlap excessively"},
      {44, 15, c, 1, 292, 1911801966u, "t.bsp:0: invalid IBSP44 record range"},
      {44, 15, d, 5, 80728, 3699447141u, "t.bsp:0: IBSP44 model node walks overlap excessively"},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    size_t length = 0;
    uint8_t *bytes = q3_build(cases[i].version, cases[i].count, cases[i].specs, cases[i].spec_count, &length);
    CHECK(length == cases[i].length && fnv1a(bytes, length) == cases[i].hash);
    q3_bsp_job job = {bytes, length};
    qa_caught_error caught;
    bool ok = qa_try(q3_bsp_run, &job, &caught);
    CHECK(!ok);
    if (!ok) {
      CHECK(strcmp(caught.message, cases[i].message) == 0);
      if (strcmp(caught.message, cases[i].message) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
    }
    free(bytes);
  }
}

typedef struct q12_job {
  const uint8_t *bytes;
  size_t length;
  const char *source;
} q12_job;

static void q12_run(void *raw) {
  q12_job *job = raw;
  qa_q12_model model;
  qa_parse_q12_model(job->bytes, job->length, job->source, &model);
  qa_q12_model_destroy(&model);
}

static void check_q12_rejects(const uint8_t *bytes, size_t length, const char *source, const char *expected) {
  q12_job job = {bytes, length, source};
  qa_caught_error caught;
  bool ok = qa_try(q12_run, &job, &caught);
  CHECK(!ok);
  if (!ok) {
    CHECK(strcmp(caught.message, expected) == 0);
    if (strcmp(caught.message, expected) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
  }
}

/* TS dataView checks are over JS numbers (a skin product past int64 reports
 * as a JS number), and the unknown-magic text is JSON.stringify (bun). */
static void test_q12_model_messages(void) {
  uint8_t mdl[84] = {'I', 'D', 'P', 'O', 6};
  for (int i = 0; i < 3; i++) put_i32(mdl + 48 + i * 4, 0x7fffffff);
  for (int i = 0; i < 3; i++) put_i32(mdl + 60 + i * 4, 1);
  check_q12_rejects(mdl, sizeof mdl, "m.mdl", "m.mdl:84: range of 9.903520300447984e+27 bytes exceeds 84-byte input");
  const uint8_t magic[] = {1, 0x22, 0x5c, 0x41, 5};
  check_q12_rejects(magic, sizeof magic, "x", "x:0: Unknown Q1/Q2 model magic \"\\u0001\\\"\\\\A\"");
}

static void put_f32(uint8_t *at, float value) { memcpy(at, &value, 4); }

/* A grouped MDL skin or SPR frame that raises partway: the caught BinaryError carries the TS text
   (bun: "m.mdl:104: range of 4 bytes exceeds 104-byte input", "s.spr:77: range of 4 bytes exceeds
   77-byte input") and the frames read so far are released (LeakSanitizer run). */
static void test_q12_group_raise_releases(void) {
  uint8_t mdl[104] = {'I', 'D', 'P', 'O', 6};
  put_i32(mdl + 48, 1);
  put_i32(mdl + 52, 2);
  put_i32(mdl + 56, 2);
  put_i32(mdl + 60, 1);
  put_i32(mdl + 64, 1);
  put_i32(mdl + 68, 1);
  put_i32(mdl + 84, 1);
  put_i32(mdl + 88, 2);
  put_f32(mdl + 92, 0.1f);
  put_f32(mdl + 96, 0.2f);
  check_q12_rejects(mdl, sizeof mdl, "m.mdl", "m.mdl:104: range of 4 bytes exceeds 104-byte input");
  uint8_t spr[77] = {'I', 'D', 'S', 'P', 1};
  put_i32(spr + 8, 2);
  put_i32(spr + 16, 1);
  put_i32(spr + 20, 1);
  put_i32(spr + 24, 1);
  put_i32(spr + 36, 1);
  put_i32(spr + 40, 2);
  put_f32(spr + 44, 0.1f);
  put_f32(spr + 48, 0.2f);
  put_i32(spr + 60, 1);
  put_i32(spr + 64, 1);
  check_q12_rejects(spr, sizeof spr, "s.spr", "s.spr:77: range of 4 bytes exceeds 77-byte input");
}

/* The alias interpolation PoseLru keys by frame address: once a frame is forgotten (its model destroyed), a
   new frame at the same address must not return the old pose (TS WeakMap identity dies with the frame). */
static void test_alias_interpolation_forget(void) {
  qa_model_vertex current_vertex = {{0, 0, 0}, {0, 0, 1}}, previous_vertex = {{10, 0, 0}, {0, 0, 1}};
  qa_model_frame current, previous;
  memset(&current, 0, sizeof current);
  memset(&previous, 0, sizeof previous);
  current.vertices = &current_vertex;
  current.vertex_count = 1;
  previous.vertices = &previous_vertex;
  previous.vertex_count = 1;
  qa_vec3 zero = {0, 0, 0};
  size_t count = 0;
  const qa_model_vertex *pose = qa_interpolate_alias_frames_cached(&current, &previous, 0.5, zero, &count);
  CHECK(count == 1 && pose[0].position.x == 5);
  qa_alias_interpolation_forget(&current);
  qa_alias_interpolation_forget(&previous);
  previous_vertex.position.x = 20;
  pose = qa_interpolate_alias_frames_cached(&current, &previous, 0.5, zero, &count);
  CHECK(count == 1 && pose[0].position.x == 10);
  qa_alias_interpolation_forget(&current);
  qa_alias_interpolation_forget(&previous);
  qa_alias_interpolation_cache_clear();
}

static void release_number(void *raw) { free(raw); }

static int *boxed(int value) {
  int *box = malloc(sizeof *box);
  *box = value;
  return box;
}

/* Port of tests/formats/q12-model/pose-lru.test.ts. */
static void test_pose_lru(void) {
  qa_pose_lru cache;
  qa_pose_lru_init(&cache, 3, release_number);
  int owner_storage = 0;
  const void *owner[1] = {&owner_storage};
  for (int i = 0; i < 100; i++) {
    char suffix[16];
    snprintf(suffix, sizeof suffix, "%d", i);
    char *key = qa_pose_lru_key(&cache, owner, 1, suffix);
    qa_pose_lru_set(&cache, key, boxed(i));
    free(key);
  }
  CHECK(qa_pose_lru_size(&cache) == 3);
  char *key = qa_pose_lru_key(&cache, owner, 1, "99");
  CHECK(strcmp(key, "0|99") == 0);
  int *hit = qa_pose_lru_get(&cache, key);
  CHECK(hit != NULL && *hit == 99);
  free(key);
  key = qa_pose_lru_key(&cache, owner, 1, "0");
  CHECK(qa_pose_lru_get(&cache, key) == NULL);
  free(key);
  qa_pose_lru_destroy(&cache);

  /* alias interpolation reuses repeated lerps and stays bounded */
  qa_model_vertex current_vertex = {{0, 0, 0}, {0, 0, 1}}, previous_vertex = {{10, 0, 0}, {0, 0, 1}};
  qa_model_frame current, previous;
  memset(&current, 0, sizeof current);
  memset(&previous, 0, sizeof previous);
  current.vertices = &current_vertex;
  current.vertex_count = 1;
  previous.vertices = &previous_vertex;
  previous.vertex_count = 1;
  qa_vec3 zero = {0, 0, 0};
  size_t count = 0;
  const qa_model_vertex *first = qa_interpolate_alias_frames_cached(&current, &previous, 0.25, zero, &count);
  CHECK(first == qa_interpolate_alias_frames_cached(&current, &previous, 0.25, zero, &count));
  for (int i = 0; i < 20000; i++)
    (void)qa_interpolate_alias_frames_cached(&current, &previous, (double)i / 20000.0, zero, &count);
  CHECK(qa_alias_interpolation_cache_size() == 256);
  const qa_model_vertex *half = qa_interpolate_alias_frames_cached(&current, &previous, 0.5, zero, &count);
  CHECK(count == 1 && half[0].position.x == 5);
  qa_alias_interpolation_forget(&current);
  qa_alias_interpolation_forget(&previous);
  qa_alias_interpolation_cache_clear();
}

/* TS SkinTokenizer runs an unterminated block comment to the end of the
 * text (bun: "/\* x" -> [], "a,b /\* x" -> [{a,b}]). */
static void test_skin_unterminated_comment(void) {
  size_t count = 0;
  qa_skin_surface *surfaces = qa_parse_skin("/* x", &count);
  CHECK(count == 0);
  qa_skin_surfaces_destroy(surfaces, count);
  surfaces = qa_parse_skin("a,b /* x", &count);
  CHECK(count == 1 && strcmp(surfaces[0].name, "a") == 0 && strcmp(surfaces[0].shader, "b") == 0);
  qa_skin_surfaces_destroy(surfaces, count);
}

/* Mirrors the bun generator's md5anim text (joint 0 is "jé", UTF-8 here). */
static char *md5_anim_text(int frames, int joints) {
  size_t capacity = 64 + (size_t)frames * 64 + (size_t)joints * 64;
  char *t = malloc(capacity);
  size_t n = (size_t)snprintf(t, capacity,
                              "MD5Version 10 commandline \"\" numFrames %d numJoints %d frameRate 24 "
                              "numAnimatedComponents 0 hierarchy {", frames, joints);
  for (int i = 0; i < joints; i++) {
    if (i == 0) n += (size_t)snprintf(t + n, capacity - n, " \"j\xc3\xa9\" -1 0 0");
    else n += (size_t)snprintf(t + n, capacity - n, " \"j%d\" %d 0 0", i, i - 1);
  }
  n += (size_t)snprintf(t + n, capacity - n, " } bounds {");
  for (int i = 0; i < frames; i++) n += (size_t)snprintf(t + n, capacity - n, " ( 0 0 0 ) ( 1 1 1 )");
  n += (size_t)snprintf(t + n, capacity - n, " } baseframe {");
  for (int i = 0; i < joints; i++) n += (size_t)snprintf(t + n, capacity - n, " ( 0 0 0 ) ( 0 0 0 )");
  n += (size_t)snprintf(t + n, capacity - n, " }");
  for (int i = 0; i < frames; i++) n += (size_t)snprintf(t + n, capacity - n, " frame %d { }", i);
  return t;
}

typedef struct md5_job {
  const char *text, *source;
  const qa_md5_scale_source *scale;
  bool mesh;
} md5_job;

static void md5_run(void *raw) {
  md5_job *job = raw;
  if (job->mesh) {
    qa_md5_mesh_file mesh;
    qa_parse_md5_mesh(job->text, job->source, &mesh);
    qa_md5_mesh_file_destroy(&mesh);
  } else {
    qa_md5_animation animation;
    qa_parse_md5_anim(job->text, job->source, job->scale, &animation);
    qa_md5_animation_destroy(&animation);
  }
}

static void check_md5_rejects(md5_job job, const char *expected) {
  qa_caught_error caught;
  bool ok = qa_try(md5_run, &job, &caught);
  CHECK(!ok);
  if (!ok) {
    CHECK(strcmp(caught.name, "ModelTextError") == 0);
    CHECK(strcmp(caught.message, expected) == 0);
    if (strcmp(caught.message, expected) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
  }
}

static void check_md5_scales(const char *base, const char *json, const char *const *expected, size_t count) {
  qa_md5_scale_source scale = {"s.json", json};
  qa_md5_animation animation;
  qa_parse_md5_anim(base, "s.md5anim", &scale, &animation);
  CHECK(animation.diagnostics.count == count);
  for (size_t i = 0; i < count && i < animation.diagnostics.count; i++) {
    CHECK(strcmp(animation.diagnostics.values[i], expected[i]) == 0);
    if (strcmp(animation.diagnostics.values[i], expected[i]) != 0)
      fprintf(stderr, "  got \"%s\"\n", animation.diagnostics.values[i]);
  }
  qa_md5_animation_destroy(&animation);
}

/* TS ModelTextError is catchable (model loaders keep the MDL); md5anim has
 * an expansion budget; scale JSON is JSON.parse + Object.entries order
 * (messages and diagnostics from bun). */
static void test_md5_review(void) {
  char *big = md5_anim_text(70, 70);
  check_md5_rejects((md5_job){big, "a.md5anim", NULL, false}, "a.md5anim:91: animation expands far beyond its data");
  free(big);
  check_md5_rejects((md5_job){"MD5Version 11", "m.md5mesh", NULL, true}, "m.md5mesh:13: expected \"10\", got \"11\"");
  char *base = md5_anim_text(3, 2);
  const char *j1[] = {"s.json: Invalid JSON scale object"};
  check_md5_scales(base, "{\"j1\": {\"0\": 2}} x", j1, 1);
  const char *j2[] = {"s.json: Invalid frame scale j1/1", "s.json: Invalid frame scale j1/2",
                      "s.json: Invalid frame scale j1/9", "s.json: Invalid frame scale j1/zz"};
  check_md5_scales(base, "{\"j1\": {\"9\": 1, \"2\": \"x\", \"zz\": 1, \"1\": 3, \"1\": \"y\"}}", j2, 4);
  const char *j3[] = {"s.json: No such joint nope"};
  check_md5_scales(base, "{\"j\xc3\xa9\": {\"0\": 2}, \"nope\": {}}", j3, 1);
  check_md5_scales(base, "{\"j1\": {\"0\": \"a\x01\"}}", j1, 1);
  const char *j5[] = {"s.json: No such joint 1", "s.json: No such joint 5", "s.json: Invalid scale entry for j1"};
  check_md5_scales(base, "{\"5\": {}, \"j1\": 3, \"1\": {}}", j5, 3);
  qa_md5_scale_source scale = {"s.json", "{\"j1\": {\"1\": 2, \"1.0\": 4}}"};
  qa_md5_animation animation;
  qa_parse_md5_anim(base, "s.md5anim", &scale, &animation);
  CHECK(animation.diagnostics.count == 0 && animation.frames[1].local_joints[1].scale == 4);
  qa_md5_animation_destroy(&animation);
  free(base);
}

typedef struct lod_files {
  const uint8_t *data[3]; /* m.md3, m_1.md3, m_2.md3 */
  size_t length[3];
} lod_files;

static uint8_t *lod_read(void *ctx, const char *path, size_t *length) {
  lod_files *files = ctx;
  int slot = strcmp(path, "m.md3") == 0 ? 0 : strcmp(path, "m_1.md3") == 0 ? 1 : strcmp(path, "m_2.md3") == 0 ? 2 : -1;
  if (slot < 0 || files->data[slot] == NULL) return NULL;
  uint8_t *copy = malloc(files->length[slot]);
  memcpy(copy, files->data[slot], files->length[slot]);
  *length = files->length[slot];
  return copy;
}

/* TS loadMd3Lods catches a BinaryError per LOD (bun results). */
static void test_md3_lod_catch(void) {
  uint8_t valid[164] = {0};
  put_i32(valid, 0x33504449);
  put_i32(valid + 4, 15);
  put_i32(valid + 76, 1);
  put_i32(valid + 92, 108);
  put_i32(valid + 96, 164);
  put_i32(valid + 100, 164);
  put_i32(valid + 104, 164);
  const uint8_t bad[] = {0x49, 0x44, 0x50, 0x33, 1, 0, 0, 0};
  const uint8_t tiny[] = {1, 2};
  lod_files files = {{valid, bad, valid}, {sizeof valid, sizeof bad, sizeof valid}};
  qa_md3_model_reader reader = {&files, lod_read};
  qa_md3_lod_model model;
  CHECK(qa_load_md3_lods("m.md3", &reader, &model));
  CHECK(model.slots[2].kind == QA_MD3_LOD_LOADED && model.slots[1].kind == QA_MD3_LOD_INVALID &&
        model.slots[0].kind == QA_MD3_LOD_ALIAS && model.slots[0].source_slot == 1);
  CHECK(model.slots[1].error != NULL && strcmp(model.slots[1].error, "m_1.md3:4: expected MD3 version 15") == 0);
  CHECK(model.load_order_count == 2 && model.load_order[0] == 2 && model.load_order[1] == 1);
  CHECK(model.num_lods == 2 && model.byte_length == 164);
  qa_md3_lod_model_destroy(&model);
  lod_files second = {{valid, NULL, bad}, {sizeof valid, 0, sizeof bad}};
  reader.ctx = &second;
  CHECK(!qa_load_md3_lods("m.md3", &reader, &model));
  lod_files third = {{tiny, valid, NULL}, {sizeof tiny, sizeof valid, 0}};
  reader.ctx = &third;
  CHECK(!qa_load_md3_lods("m.md3", &reader, &model));
}

typedef struct jpeg_job {
  const uint8_t *bytes;
  size_t length;
  uint32_t hash;
  double width;
} jpeg_job;

static void jpeg_run(void *raw) {
  jpeg_job *job = raw;
  qa_jpeg_image image;
  memset(&image, 0, sizeof image);
  if (qa_decode_jpeg(job->bytes, job->length, "j", NULL, NULL, &image)) {
    job->hash = fnv1a(image.pixels.bytes, image.pixels.length);
    job->width = image.width;
    free((void *)image.pixels.bytes);
  }
}

static void check_jpeg(const uint8_t *bytes, size_t length, const char *expected, int code, uint32_t hash) {
  jpeg_job job = {bytes, length, 0, 0};
  qa_caught_error caught;
  bool ok = qa_try(jpeg_run, &job, &caught);
  if (expected == NULL) {
    CHECK(ok && job.width == 16 && job.hash == hash);
    return;
  }
  CHECK(!ok);
  if (!ok) {
    CHECK(strcmp(caught.name, code == QA_JPEG_SOURCE_ERROR_CODE ? "JpegSourceError" : "BinaryError") == 0 && caught.code == code);
    CHECK(strcmp(caught.message, expected) == 0);
    if (strcmp(caught.message, expected) != 0) fprintf(stderr, "  got \"%s\"\n", caught.message);
  }
}

/* TS JpegSourceError (name "JpegSourceError") is a catchable BinaryError; entropy synthesis past the
 * data is bounded; a stream has at most 256 scans (bun: encoder output and
 * messages from decodeJpeg). */
static void test_jpeg_review(void) {
  uint8_t pixels[16 * 16 * 4];
  for (size_t i = 0; i < sizeof pixels; i++) pixels[i] = (uint8_t)((i * 37) & 255);
  qa_jpeg_image image;
  memset(&image, 0, sizeof image);
  image.width = 16;
  image.height = 16;
  image.pixels.bytes = pixels;
  image.pixels.length = sizeof pixels;
  size_t length = 0;
  uint8_t *base = qa_encode_jpeg(&image, 90, NULL, &length);
  CHECK(length == 815 && fnv1a(base, length) == 3548417633u);
  check_jpeg(base, length, NULL, 0, 1776621483u);
  const uint8_t not_jpeg[] = {0x12, 0x34};
  check_jpeg(not_jpeg, 2, "j:2: JPEG: Not a JPEG file: starts with 0x12 0x34", QA_JPEG_SOURCE_ERROR_CODE, 0);
  uint8_t *big = malloc(length);
  memcpy(big, base, length);
  big[158 + 5] = 0x0f;
  big[158 + 6] = 0xa0;
  big[158 + 7] = 0x0f;
  big[158 + 8] = 0xa0;
  check_jpeg(big, length, "j:815: JPEG: entropy data ends far short of the declared image", 0, 0);
  free(big);
  const uint8_t scan[] = {0xff, 0xda, 0, 8, 1, 1, 0, 0, 63, 0, 0x28, 0xa2, 0x8a};
  for (int scans = 257; scans >= 3; scans -= 254) {
    size_t total = 609 + (size_t)scans * sizeof scan + 2;
    uint8_t *many = malloc(total);
    memcpy(many, base, 609);
    for (int i = 0; i < scans; i++) memcpy(many + 609 + (size_t)i * sizeof scan, scan, sizeof scan);
    many[total - 2] = 0xff;
    many[total - 1] = 0xd9;
    if (scans == 257) check_jpeg(many, total, "j:3939: JPEG: Too many scans", QA_JPEG_SOURCE_ERROR_CODE, 0);
    else check_jpeg(many, total, NULL, 0, 2268530117u);
    free(many);
  }
  free(base);

  /* limits.test.ts: encodeJpeg(16x16 filled 128, 80) with the SOF0 frame
   * size patched (SOF0 at 158, 629 bytes; messages from bun). */
  uint8_t gray[16 * 16 * 4];
  memset(gray, 128, sizeof gray);
  image.pixels.bytes = gray;
  image.pixels.length = sizeof gray;
  uint8_t *small = qa_encode_jpeg(&image, 80, NULL, &length);
  CHECK(length == 629 && small[158] == 0xff && small[159] == 0xc0);
  const int sizes[2] = {40000, 6000};
  for (int i = 0; i < 2; i++) {
    small[158 + 5] = (uint8_t)(sizes[i] >> 8);
    small[158 + 6] = (uint8_t)(sizes[i] & 255);
    small[158 + 7] = (uint8_t)(sizes[i] >> 8);
    small[158 + 8] = (uint8_t)(sizes[i] & 255);
    if (i == 0) check_jpeg(small, length, "j:623: JPEG: JPEG dimensions 40000x40000 exceed the decoder limit", QA_JPEG_SOURCE_ERROR_CODE, 0);
    else check_jpeg(small, length, "j:629: JPEG: entropy data ends far short of the declared image", 0, 0);
  }
  free(small);
}

int main(void) {
  test_jpeg_review();
  test_md3_lod_catch();
  test_md5_review();
  test_skin_unterminated_comment();
  test_q12_model_messages();
  test_pose_lru();
  test_q12_group_raise_releases();
  test_alias_interpolation_forget();
  test_q3_bsp_budgets();
  test_q2_bspx_diagnostics();
  test_q1_bsp_validation_order();
  test_q1_leaf_pvs_corrupt_row();
  test_png_encoder_chunks();
  test_q1_entities_high_bytes();
  test_q3_entities_parse_error();
  test_q3_bmp_drop_is_catchable();
  test_png_inflate_messages();
  test_gamma_profile_value();
  test_image_limits();
  printf("%d checks, %d failures\n", checks, failures);
  fflush(stdout);
  return failures == 0 ? 0 : 1;
}
