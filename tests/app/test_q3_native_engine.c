/* Side-by-side check of src/app/bootstrap/simulation/q3/native_guest_source.c against
 * tests/app/oracles/q3_native_engine.ts (bun, TS Q3NativeGuestSource): the authored Windows PE (with and without
 * per-frame heap allocation) and the synthetic Linux ELF qagame of the TS tests/support/q3-modules.ts run
 * GAME_INIT, connect, begin, frames, think, console and client commands with every server trap answered 0; the
 * native-guest checkpoint (process regions, heap image: layout, allocator, heap blocks, Windows heap/TLS/FLS tables,
 * registers, host record) is encoded and hashed, restored into a fresh process, re-checkpointed and continued. The
 * transcript must equal q3_native_engine.txt line for line. Run from the repository root. GPL-2.0-or-later. */
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/native_guest_source.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

static char **lines;
static size_t line_count, line_capacity;
static void emit(const char *format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  if (line_count == line_capacity) {
    line_capacity = line_capacity ? line_capacity * 2 : 64;
    lines = realloc(lines, line_capacity * sizeof *lines);
    if (lines == NULL) abort();
  }
  lines[line_count++] = strdup(buffer);
}
static void hex_digest(const uint8_t *bytes, size_t length, char out[65]) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(bytes, length, digest);
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) snprintf(out + 2 * i, 3, "%02x", digest[i]);
}
static uint8_t *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) { fprintf(stderr, "missing %s\n", path); exit(1); }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = malloc((size_t)size);
  if (bytes == NULL || fread(bytes, 1, (size_t)size, file) != (size_t)size) abort();
  fclose(file);
  *length = (size_t)size;
  return bytes;
}

#define TRAPS_SENTINEL ((void *)0x5157)
#define HOST_SENTINEL ((void *)0x4853)
static int sentinel_failures;
static qa_qvm_host_result traps(void *ctx, const qa_qvm_host_call *call) {
  if (ctx != TRAPS_SENTINEL) sentinel_failures++;
  int32_t words[4];
  for (int i = 0; i < 4; i++) {
    const uint8_t *w = call->base.words + 4 * (i + 1);
    words[i] = (int32_t)((uint32_t)w[0] | (uint32_t)w[1] << 8 | (uint32_t)w[2] << 16 | (uint32_t)w[3] << 24);
  }
  emit("trap %s %d %d %d %d %d", call->kind, call->code, words[0], words[1], words[2], words[3]);
  return 0;
}
static qa_module_identity host_module;
static const uint8_t host_bytes[3] = {1, 2, 3};
static qa_qvm_host_checkpoint host_checkpoint(void *ctx) {
  if (ctx != HOST_SENTINEL) sentinel_failures++;
  qa_qvm_host_checkpoint out;
  memset(&out, 0, sizeof out);
  out.state.module = host_module;
  out.state.format = "q3:qagame-host";
  out.state.bytes.bytes = host_bytes;
  out.state.bytes.length = 3;
  return out;
}
static void host_restore(void *ctx, const qa_qvm_host_checkpoint *state) {
  if (ctx != HOST_SENTINEL) sentinel_failures++;
  char text[64] = "";
  for (size_t i = 0; i < state->state.bytes.length; i++)
    snprintf(text + strlen(text), sizeof text - strlen(text), "%s%u", i ? "," : "", state->state.bytes.bytes[i]);
  emit("host-restore %s %s", state->state.format, text);
}
static void encode(const qa_guest_checkpoint *checkpoint, size_t *length, char digest[65]) {
  qa_save_value *value = qa_save_guest_checkpoint_value(checkpoint);
  uint8_t *bytes = qa_encode_checkpoint_value(value, length);
  if (bytes == NULL) abort();
  hex_digest(bytes, *length, digest);
  free(bytes);
  qa_save_value_free(value);
}

static void run_case(const char *name) {
  char path[256];
  snprintf(path, sizeof path, "tests/app/oracles/q3_native_engine/%s.bin", name);
  size_t length = 0;
  uint8_t *bytes = read_file(path, &length);
  char digest[65];
  hex_digest(bytes, length, digest);
  qa_app_q3_native_profile_name profile = qa_app_q3_native_profile(bytes, length);
  emit("case %s %s %zu %s", name, profile, length, digest);
  host_module.id = "q3:native-test";
  host_module.artifact_path = strncmp(name, "pe", 2) == 0 ? "qagamex86.dll" : "qagamei386.so";
  host_module.digest = digest;
  host_module.revision = "test:1";
  qa_app_q3_native_image image = {bytes, length, profile, host_module};
  qa_qvm_host_state host_state = {HOST_SENTINEL, host_checkpoint, host_restore};
  qa_app_q3_native_guest_options options;
  memset(&options, 0, sizeof options);
  options.prepared = &image; options.traps_ctx = TRAPS_SENTINEL; options.traps = traps; options.host_state = &host_state;
  qa_app_q3_native_guest_source *source = qa_app_q3_native_guest_source_create(&options);
  qa_app_q3_game_engine *engine = qa_app_q3_native_guest_source_engine(source);
  qa_qvm_game_data_set_client_count(engine->data, 8);
  engine->ops->initialize(engine, 0, 7, false);
  emit("located %s %s", QA_JS_NUMBER(qa_qvm_game_data_get_num_entities(engine->data)),
       QA_JS_NUMBER(qa_qvm_game_data_get_num_clients(engine->data)));
  char *denied = engine->ops->client_connect(engine, 0, true, false);
  emit("connect %s", denied != NULL ? denied : "null");
  free(denied);
  engine->ops->client_begin(engine, 0);
  engine->ops->run_frame(engine, 100);
  engine->ops->client_think(engine, 0);
  const char *console_argv[] = {"synthetic"};
  emit("console %s", engine->ops->console_command(engine, console_argv, 1) ? "true" : "false");
  const char *say[] = {"say", "hi"};
  engine->ops->client_command_async(engine, 0, say, 2);
  engine->ops->run_frame(engine, 200);
  const qa_guest_checkpoint *checkpoint = engine->ops->checkpoint(engine);
  size_t encoded_length = 0;
  char encoded[65];
  encode(checkpoint, &encoded_length, encoded);
  char windows[64] = "null";
  if (checkpoint->heap->windows != NULL)
    snprintf(windows, sizeof windows, "%zu/%zu/%zu", checkpoint->heap->windows->heap_count, checkpoint->heap->windows->tls_count,
             checkpoint->heap->windows->fls_count);
  emit("checkpoint %zu %zu %zu %s %zu %s", checkpoint->region_count, checkpoint->heap->layout_count, checkpoint->heap->block_count,
       windows, encoded_length, encoded);
  qa_app_q3_native_guest_source *restored_source = qa_app_q3_native_guest_source_create(&options);
  qa_app_q3_game_engine *restored = qa_app_q3_native_guest_source_engine(restored_source);
  qa_qvm_game_data_set_client_count(restored->data, 8);
  restored->ops->restore(restored, checkpoint);
  encode(restored->ops->checkpoint(restored), &encoded_length, encoded);
  emit("restored %zu %s", encoded_length, encoded);
  restored->ops->run_frame(restored, 300);
  engine->ops->run_frame(engine, 300);
  size_t left_length = 0, right_length = 0;
  char left[65], right[65];
  encode(engine->ops->checkpoint(engine), &left_length, left);
  encode(restored->ops->checkpoint(restored), &right_length, right);
  emit("continued %zu %s %zu %s", left_length, left, right_length, right);
  qa_app_q3_native_guest_source_close(source);
  restored->ops->retire(restored);
  emit("closed %s", name);
  engine->ops->destroy(engine);
  restored->ops->destroy(restored);
  free(bytes);
}

int main(void) {
  run_case("pe");
  run_case("pe-heap");
  run_case("elf");
  FILE *file = fopen("tests/app/oracles/q3_native_engine.txt", "r");
  if (file == NULL) { fprintf(stderr, "missing tests/app/oracles/q3_native_engine.txt\n"); return 1; }
  char expected[4096];
  size_t index = 0, mismatches = 0;
  while (fgets(expected, sizeof expected, file) != NULL) {
    expected[strcspn(expected, "\n")] = 0;
    const char *actual = index < line_count ? lines[index] : "<missing>";
    if (strcmp(expected, actual) != 0) {
      if (mismatches < 20) fprintf(stderr, "line %zu\n  ts: %s\n   c: %s\n", index + 1, expected, actual);
      mismatches++;
    }
    index++;
  }
  fclose(file);
  if (index != line_count) { fprintf(stderr, "line count ts %zu c %zu\n", index, line_count); mismatches++; }
  if (sentinel_failures != 0) { fprintf(stderr, "callback ctx mismatches: %d\n", sentinel_failures); mismatches++; }
  printf("q3 native engine: %zu lines, %zu mismatches\n", line_count, mismatches);
  for (size_t i = 0; i < line_count; i++) free(lines[i]);
  free(lines);
  return mismatches == 0 ? 0 : 1;
}
