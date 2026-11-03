/*
 * Port of tests/persistence/value-base64.test.ts,
 * provider-ownership.test.ts (contract case), kex-saves.test.ts
 * (synthetic KEX server save + archive roundtrip; corpus and
 * installed-save cases are not ported), and the inline-data case of
 * source-saves.test.ts (rerelease field codecs), plus synthetic
 * roundtrips for q2-containers/q2-typescript derived from the TS
 * codecs. GPL-2.0-or-later.
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
#include <zlib.h>

#include "qa/persistence/kex_q2_save.h"
#include "qa/persistence/provider_ownership.h"
#include "qa/persistence/q2_containers.h"
#include "qa/persistence/q2_rerelease.h"
#include "qa/persistence/q2_typescript.h"
#include "qa/persistence/source_json.h"
#include "qa/persistence/value.h"

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

static int text_is(const char *got, const char *want) {
  return got != NULL && strcmp(got, want) == 0;
}

/* --- value-base64.test.ts --- */
static size_t tagged(const char *encoded, char *buffer, size_t size) {
  int written = snprintf(buffer, size, "{\"$qts\":\"bytes\",\"value\":\"%s\"}", encoded);
  return written > 0 ? (size_t)written : 0;
}
static void test_value_base64(void) {
  static const size_t lengths[] = {0, 1, 2, 3, 17, 5051961};
  for (size_t t = 0; t < sizeof lengths / sizeof lengths[0]; t++) {
    size_t length = lengths[t];
    uint8_t *input = length > 0 ? (uint8_t *)malloc(length) : NULL;
    for (size_t i = 0; i < length; i++) input[i] = (uint8_t)(i % 256);
    qa_save_value *object = qa_save_value_object();
    qa_save_value *bytes = qa_save_value_bytes(input, length);
    CHECK(object != NULL && bytes != NULL);
    CHECK(qa_save_value_set(object, "bytes", bytes));
    size_t encoded_length = 0;
    uint8_t *encoded = qa_encode_checkpoint_value(object, &encoded_length);
    CHECK(encoded != NULL);
    qa_save_value *decoded = qa_decode_checkpoint_value(encoded, encoded_length);
    CHECK(decoded != NULL && decoded->kind == QA_SAVE_OBJECT);
    const qa_save_value *output =
        decoded != NULL ? qa_save_value_get(decoded, "bytes") : NULL;
    CHECK(output != NULL && output->kind == QA_SAVE_BYTES);
    CHECK(output != NULL && output->byte_length == length);
    CHECK(output != NULL && length == output->byte_length &&
          (length == 0 || memcmp(input, output->bytes, length) == 0));
    qa_save_value_free(decoded);
    free(encoded);
    qa_save_value_free(object);
    free(input);
  }
  char buffer[4096];
  static const char *const invalid[] = {"A", "AA", "AAA", "=", "====", "A===",
      "AA=A", "AA==AA==", "AA===", "AAAA=", "AA-_", "AA\xff", "AA==\n",
      "AAAA\r\n", " AAAA", "AAAA "};
  for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
    /* The \u00ff and \u0000 TS cases cannot ride a C string / JSON
     * text here; the remaining spellings cover the same rejections. */
    size_t length = tagged(invalid[i], buffer, sizeof buffer);
    CHECK_ABORTS(qa_decode_checkpoint_value((const uint8_t *)buffer, length));
  }
  static const char *const valid_ts[] = {"", "AA==", "/w==", "AAA=", "//8=", "AAAA", "////", "+/+/" };
  for (size_t i = 0; i < sizeof valid_ts / sizeof valid_ts[0]; i++) {
    size_t length = tagged(valid_ts[i], buffer, sizeof buffer);
    qa_save_value *decoded = qa_decode_checkpoint_value((const uint8_t *)buffer, length);
    CHECK(decoded != NULL && decoded->kind == QA_SAVE_BYTES);
    /* Re-encoding the single bytes value reproduces the tag. */
    size_t re_length = 0;
    uint8_t *re = decoded != NULL ? qa_encode_checkpoint_value(decoded, &re_length) : NULL;
    CHECK(re != NULL && re_length == length && memcmp(re, buffer, length) == 0);
    free(re);
    qa_save_value_free(decoded);
  }
  const char *alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int digit = 0; digit < 64; digit++) {
    char encoded[8];
    (void)snprintf(encoded, sizeof encoded, "A%c==", alphabet[digit]);
    size_t length = tagged(encoded, buffer, sizeof buffer);
    if ((digit & 15) != 0) {
      CHECK_ABORTS(qa_decode_checkpoint_value((const uint8_t *)buffer, length));
    } else {
      qa_save_value *decoded = qa_decode_checkpoint_value((const uint8_t *)buffer, length);
      CHECK(decoded != NULL && decoded->kind == QA_SAVE_BYTES);
      qa_save_value_free(decoded);
    }
    (void)snprintf(encoded, sizeof encoded, "AA%c=", alphabet[digit]);
    length = tagged(encoded, buffer, sizeof buffer);
    if ((digit & 3) != 0) {
      CHECK_ABORTS(qa_decode_checkpoint_value((const uint8_t *)buffer, length));
    } else {
      qa_save_value *decoded = qa_decode_checkpoint_value((const uint8_t *)buffer, length);
      CHECK(decoded != NULL && decoded->kind == QA_SAVE_BYTES);
      qa_save_value_free(decoded);
    }
  }
}

/* --- provider-ownership.test.ts --- */
static void test_provider_ownership(void) {
  qa_provider_checkpoint_ref contract =
      qa_save_provider_contract("session:campaign-unit", "q2:official");
  CHECK(text_is(contract.provider, "session:campaign-unit"));
  CHECK(text_is(contract.schema, "session:campaign-unit"));
  CHECK(contract.version == 1);
  qa_provider_checkpoint record;
  memset(&record, 0, sizeof record);
  record.provider = "session:campaign-unit";
  record.schema = "session:campaign-unit";
  record.version = 1;
  qa_validate_save_provider_owner(record, "q2:official");
  record.provider = "world:actors";
  record.schema = "world:source-slots";
  record.version = 1;
  qa_validate_save_provider_owner(record, "q2:official");
  static const struct { const char *provider; const char *schema; double version; }
      invalid[] = {
          {"q2:official", "session:campaign-unit", 1},
          {"session:campaign-unit", "session:campaign-unit", 2},
          {"session:campaign-unit", "q2:foundation", 1},
          {"session:unknown", "session:unknown", 1},
          {"q1:official", "q2:foundation", 1}};
  for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
    qa_provider_checkpoint bad;
    memset(&bad, 0, sizeof bad);
    bad.provider = invalid[i].provider;
    bad.schema = invalid[i].schema;
    bad.version = invalid[i].version;
    CHECK_ABORTS(qa_validate_save_provider_owner(bad, "q2:official"));
  }
}

/* --- kex-saves.test.ts (synthetic fixtures) --- */
static void put_u32(uint8_t *bytes, size_t offset, uint32_t value) {
  bytes[offset] = (uint8_t)value;
  bytes[offset + 1] = (uint8_t)(value >> 8);
  bytes[offset + 2] = (uint8_t)(value >> 16);
  bytes[offset + 3] = (uint8_t)(value >> 24);
}
static size_t put_length_string(uint8_t *bytes, size_t offset, const char *value) {
  size_t length = strlen(value);
  put_u32(bytes, offset, (uint32_t)length);
  memcpy(bytes + offset + 4, value, length);
  return offset + 4 + length;
}
static size_t kex_server_save(uint8_t *bytes) {
  memcpy(bytes, "EVAS", 4);
  size_t offset = 4;
  bytes[offset++] = 1;
  offset = put_length_string(bytes, offset, "Widow's Lair");
  put_u32(bytes, offset, 1717012859u);
  put_u32(bytes, offset + 4, 0);
  offset += 8;
  offset = put_length_string(bytes, offset, "rboss");
  offset = put_length_string(bytes, offset, "gamename");
  offset = put_length_string(bytes, offset, "baseq2");
  offset = put_length_string(bytes, offset, "");
  return offset;
}
static void test_kex_server(void) {
  uint8_t bytes[512];
  size_t length = kex_server_save(bytes);
  qa_kex_q2_save_info info;
  CHECK(qa_decode_kex_q2_server_save(bytes, length, &info));
  CHECK(text_is(info.comment, "Widow's Lair"));
  CHECK(text_is(info.map_command, "rboss"));
  CHECK(text_is(info.game_directory, "baseq2"));
  CHECK(info.timestamp == 1717012859ULL);
  qa_kex_q2_save_info_free(&info);
  CHECK_ABORTS(qa_decode_kex_q2_server_save((const uint8_t *)"NOPE", 4, &info));
}
static uint8_t *gzip_data(const uint8_t *data, size_t length, size_t *out_length) {
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                   Z_DEFAULT_STRATEGY) != Z_OK)
    return NULL;
  size_t capacity = length + 64;
  uint8_t *out = (uint8_t *)malloc(capacity);
  if (out == NULL) { deflateEnd(&stream); return NULL; }
  stream.next_in = (Bytef *)data;
  stream.avail_in = (uInt)length;
  stream.next_out = out;
  stream.avail_out = (uInt)capacity;
  if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
    deflateEnd(&stream);
    free(out);
    return NULL;
  }
  *out_length = (size_t)stream.total_out;
  deflateEnd(&stream);
  return out;
}
static void test_kex_archive(void) {
  uint8_t server[512];
  size_t server_length = kex_server_save(server);
  const char *game = "{}";
  size_t data_offset = 12 + server_length + strlen(game);
  size_t pak_length = data_offset + 2 * 64;
  uint8_t *pak = (uint8_t *)calloc(pak_length, 1);
  CHECK(pak != NULL);
  if (pak == NULL) return;
  memcpy(pak, "PACK", 4);
  put_u32(pak, 4, (uint32_t)data_offset);
  put_u32(pak, 8, 128);
  memcpy(pak + 12, server, server_length);
  memcpy(pak + 12 + server_length, game, strlen(game));
  memcpy(pak + data_offset, "server.ssv", 10);
  put_u32(pak, data_offset + 56, 12);
  put_u32(pak, data_offset + 60, (uint32_t)server_length);
  memcpy(pak + data_offset + 64, "game.ssv", 8);
  put_u32(pak, data_offset + 64 + 56, (uint32_t)(12 + server_length));
  put_u32(pak, data_offset + 64 + 60, (uint32_t)strlen(game));
  size_t gz_length = 0;
  uint8_t *gz = gzip_data(pak, pak_length, &gz_length);
  CHECK(gz != NULL);
  const char *path = "/tmp/qa-kex-save0.pak.gz";
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  if (file != NULL) {
    CHECK(fwrite(gz, 1, gz_length, file) == gz_length);
    fclose(file);
  }
  qa_kex_q2_save_archive archive;
  CHECK(qa_read_kex_q2_save_archive(path, &archive));
  CHECK(text_is(archive.info.map_command, "rboss"));
  CHECK(text_is(archive.info.game_directory, "baseq2"));
  CHECK(archive.file_count == 2);
  CHECK(qa_kex_q2_save_restorable(&archive));
  qa_kex_q2_save_archive_free(&archive);
  qa_kex_q2_save_info info;
  CHECK(qa_read_kex_q2_save_info(path, &info));
  CHECK(text_is(info.comment, "Widow's Lair"));
  qa_kex_q2_save_info_free(&info);
  remove(path);
  free(gz);
  free(pak);
}

/* --- source-saves.test.ts inline rerelease case --- */
static void test_rerelease_fields(void) {
  const char *text = "{\"save_version\":1,\"level\":{\"valid_poi\":true,"
      "\"current_poi\":[1,2,3],\"current_poi_stage\":4,\"current_poi_image\":5,"
      "\"current_dynamic_poi\":7},\"entities\":{\"0\":{\"fog.color\":[0.1,0.2,0.3],"
      "\"fog.density\":0.4,\"heightfog.density\":0.5,\"bmodel_anim.start\":3,"
      "\"bmodel_anim.end\":9,\"bmodel_anim.enabled\":true,\"bmodel_anim.alternate\":true,"
      "\"bmodel_anim.currently_alternate\":true,\"bmodel_anim.next_tick\":1500,"
      "\"private_flags\":18446744069414584320}}}";
  qa_q2_rerelease_level_save save;
  CHECK(qa_decode_q2_rerelease_level(text, &save));
  CHECK(save.entity_count == 1);
  qa_save_json *entity = save.entities[0].record;
  qa_q2_fog_save fog = qa_read_q2_fog(entity);
  qa_q2_height_fog_save height = qa_read_q2_height_fog(entity);
  qa_q2_brush_animation_save animation = qa_read_q2_brush_animation(entity);
  qa_q2_poi_save poi = qa_read_q2_poi(save.level);
  CHECK(fog.density == 0.4 && fog.color.x == 0.1);
  CHECK(height.density == 0.5);
  CHECK(animation.start == 3 && animation.end == 9 && animation.enabled &&
        animation.alternate && animation.currently_alternate && animation.next_tick == 1500);
  CHECK(poi.valid && poi.point.x == 1 && poi.stage == 4 && poi.image == 5 &&
        poi.has_dynamic_entity && poi.dynamic_entity == 7);
  CHECK(qa_write_q2_fog(entity, fog));
  CHECK(qa_write_q2_height_fog(entity, height));
  CHECK(qa_write_q2_brush_animation(entity, animation));
  CHECK(qa_write_q2_poi(save.level, poi));
  char *encoded = qa_encode_q2_rerelease_level(&save);
  CHECK(encoded != NULL);
  qa_q2_rerelease_level_save roundtrip;
  CHECK(qa_decode_q2_rerelease_level(encoded, &roundtrip));
  qa_save_json *after = roundtrip.entities[0].record;
  qa_q2_fog_save fog2 = qa_read_q2_fog(after);
  qa_q2_height_fog_save height2 = qa_read_q2_height_fog(after);
  qa_q2_brush_animation_save animation2 = qa_read_q2_brush_animation(after);
  qa_q2_poi_save poi2 = qa_read_q2_poi(roundtrip.level);
  CHECK(fog2.density == fog.density && fog2.color.y == fog.color.y);
  CHECK(height2.density == height.density);
  CHECK(animation2.next_tick == animation.next_tick && animation2.enabled == animation.enabled);
  CHECK(poi2.stage == poi.stage && poi2.dynamic_entity == poi.dynamic_entity);
  const qa_save_json *flags = qa_save_json_get_const(after, "private_flags");
  CHECK(flags != NULL && flags->kind == QA_SAVE_JSON_NUMBER);
  CHECK(flags != NULL && qa_save_number_bigint_u64(flags) == 18446744069414584320ULL);
  free(encoded);
  qa_save_json_free(save.root);
  free(save.entities);
  qa_save_json_free(roundtrip.root);
  free(roundtrip.entities);

  qa_q2_rerelease_game_save game;
  CHECK(qa_decode_q2_rerelease_game(
      "{\"save_version\":1,\"game\":{},\"clients\":[{\"pers\":{\"max_ammo\":[200,100,50,50,200,50,120]}}]}",
      &game));
  qa_save_json *persistent = qa_source_object(
      qa_save_json_get(game.clients[0], "pers"), "pers");
  size_t count = 0;
  double *capacity = qa_read_q2_ammo_capacity(persistent, &count);
  CHECK(count == 7 && capacity != NULL && capacity[0] == 200 && capacity[6] == 120);
  CHECK(qa_write_q2_ammo_capacity(persistent, capacity, count));
  char *game_text = qa_encode_q2_rerelease_game(&game);
  qa_q2_rerelease_game_save game2;
  CHECK(qa_decode_q2_rerelease_game(game_text, &game2));
  size_t count2 = 0;
  double *capacity2 = qa_read_q2_ammo_capacity(
      qa_source_object_const(qa_save_json_get_const(game2.clients[0], "pers"), "pers"),
      &count2);
  CHECK(count2 == count && capacity2 != NULL &&
        memcmp(capacity, capacity2, count * sizeof *capacity) == 0);
  free(capacity);
  free(capacity2);
  free(game_text);
  qa_save_json_free(game.root);
  free(game.clients);
  qa_save_json_free(game2.root);
  free(game2.clients);
}

/* --- synthetic container / typescript roundtrips --- */
static void test_containers(void) {
  qa_q2_rerelease_server_save server;
  memset(&server, 0, sizeof server);
  server.comment = "base1";
  server.map_command = "map base1";
  server.timestamp = 0x1122334455667788ULL;
  server.kind = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_rerelease_server(&server, &length);
  CHECK(bytes != NULL);
  qa_q2_rerelease_server_save decoded;
  CHECK(qa_decode_q2_rerelease_server(bytes, length, &decoded));
  CHECK(text_is(decoded.map_command, "map base1"));
  CHECK(decoded.timestamp == server.timestamp && decoded.kind == 1);
  size_t length2 = 0;
  uint8_t *bytes2 = qa_encode_q2_rerelease_server(&decoded, &length2);
  CHECK(bytes2 != NULL && length2 == length && memcmp(bytes, bytes2, length) == 0);
  free(bytes2);
  qa_q2_rerelease_server_save_free(&decoded);
  free(bytes);

  qa_q2_level_metadata metadata;
  memset(&metadata, 0, sizeof metadata);
  qa_q2_configstring entries[1] = {{3, (char *)"sky"}};
  uint8_t portal[2] = {1, 2};
  metadata.configstrings = entries;
  metadata.configstring_count = 1;
  metadata.portal_bytes = portal;
  metadata.portal_length = 2;
  bytes = qa_encode_q2_rerelease_level_metadata(&metadata, 12448, &length);
  CHECK(bytes != NULL);
  qa_q2_level_metadata decoded_meta;
  CHECK(qa_decode_q2_rerelease_level_metadata(bytes, length, 12448, &decoded_meta));
  CHECK(decoded_meta.configstring_count == 1 &&
        text_is(decoded_meta.configstrings[0].value, "sky"));
  bytes2 = qa_encode_q2_rerelease_level_metadata(&decoded_meta, 12448, &length2);
  CHECK(bytes2 != NULL && length2 == length && memcmp(bytes, bytes2, length) == 0);
  free(bytes2);
  qa_q2_level_metadata_free(&decoded_meta);
  free(bytes);

  /* q2-containers drift: a configstring of exactly `width` bytes is written
   * with no terminator (SetConfigstring spill); longer still fails. Bytes
   * from bun encodeQ2ClassicLevelMetadata(..., 3, 8). */
  qa_q2_configstring spill[2] = {{0, (char *)"xxxxxxxx"}, {1, (char *)"ab"}};
  uint8_t spill_portal[1] = {7};
  metadata.configstrings = spill;
  metadata.configstring_count = 2;
  metadata.portal_bytes = spill_portal;
  metadata.portal_length = 1;
  bytes = qa_encode_q2_classic_level_metadata(&metadata, 3, 8, &length);
  static const uint8_t expected[25] = {120, 120, 120, 120, 120, 120, 120, 120, 97, 98, 0, 0, 0,
                                       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0, 7};
  CHECK(bytes != NULL && length == 25 && memcmp(bytes, expected, 25) == 0);
  CHECK(qa_decode_q2_classic_level_metadata(bytes, length, 3, 8, &decoded_meta));
  CHECK(decoded_meta.configstring_count == 2 &&
        text_is(decoded_meta.configstrings[0].value, "xxxxxxxx") &&
        text_is(decoded_meta.configstrings[1].value, "ab"));
  qa_q2_level_metadata_free(&decoded_meta);
  free(bytes);
  qa_q2_configstring too_long[1] = {{0, (char *)"xxxxxxxxx"}};
  metadata.configstrings = too_long;
  metadata.configstring_count = 1;
  metadata.portal_length = 0;
  CHECK_ABORTS(free(qa_encode_q2_classic_level_metadata(&metadata, 1, 8, &length)));
}
static void test_q2_typescript(void) {
  qa_q2_typescript_game_save game;
  CHECK(qa_decode_q2_typescript_game(
      "{\"stamp\":\"quake-2-ts:g_save:v1\",\"clients\":[{\"health\":100}]}", &game));
  CHECK(game.client_count == 1);
  char *text = qa_encode_q2_typescript_game(&game);
  qa_q2_typescript_game_save game2;
  CHECK(qa_decode_q2_typescript_game(text, &game2));
  free(text);
  qa_save_json_free(game.root);
  free(game.clients);
  qa_save_json_free(game2.root);
  free(game2.clients);
  CHECK_ABORTS(qa_decode_q2_typescript_game("{\"stamp\":\"nope\",\"clients\":[]}", &game));

}

int main(void) {
  test_value_base64();
  test_provider_ownership();
  test_kex_server();
  test_kex_archive();
  test_rerelease_fields();
  test_containers();
  test_q2_typescript();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
