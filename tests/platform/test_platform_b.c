/*
 * Port of tests/platform/audio.test.ts, controller.test.ts,
 * freetype.test.ts and vorbis.test.ts for the platform slice-b C
 * ports, plus contained/writable, GL table, theora and IPX logic
 * cases. Live-device cases run against SDL's dummy audio driver and
 * a real SDL virtual joystick, as the TypeScript does; a host with
 * no usable dummy device skips the audio body with a note. TypeScript
 * `toThrow` cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/platform/audio.h"
#include "qa/platform/controller.h"
#include "qa/platform/files/contained.h"
#include "qa/platform/files/writable.h"
#include "qa/platform/freetype.h"
#include "qa/platform/freetype_layout.h"
#include "qa/platform/gl.h"
#include "qa/platform/gl_framebuffers.h"
#include "qa/platform/gl_programs.h"
#include "qa/platform/ipx.h"
#include "qa/platform/theora.h"
#include "qa/platform/vorbis.h"

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

static void test_freetype_layout(void) {
  qa_freetype_layout linux_layout;
  qa_freetype_layout windows_layout;
  qa_freetype_layout again;
  CHECK(qa_freetype_layout_for("linux", "x64", "LE", &linux_layout));
  CHECK(qa_freetype_layout_for("win32", "x64", "LE", &windows_layout));
  CHECK(linux_layout.face_glyph == 152);
  CHECK(windows_layout.face_glyph == 120);
  CHECK(qa_freetype_layout_for("linux", "arm64", "LE", &again) &&
        again.face_glyph == linux_layout.face_glyph &&
        again.long_bytes == linux_layout.long_bytes);
  CHECK(qa_freetype_layout_for("darwin", "arm64", "LE", &again));
  CHECK(!qa_freetype_layout_for("linux", "x64", "BE", &again));
  CHECK(!qa_freetype_layout_for("linux", "ia32", "LE", &again));
  CHECK(!qa_freetype_layout_for("win32", "arm64", "LE", &again));

  uint8_t word[8] = {0};
  int64_t minus64 = -64;
  memcpy(word, &minus64, 8);
  CHECK(qa_freetype_metric(word, 8, 0, &linux_layout) == -64);
  CHECK(qa_freetype_metric(word, 8, 0, &windows_layout) == -64);
  int64_t big = 2147483648LL;
  memcpy(word, &big, 8);
  uint8_t big_copy[8];
  memcpy(big_copy, word, 8);
  CHECK_ABORTS((void)qa_freetype_metric(big_copy, 8, 0, &linux_layout));

  CHECK(qa_freetype_bitmap_length(9, 3, -2, 1) == 6);
  CHECK(qa_freetype_bitmap_length(9, 3, 3, 3) == 9);
  CHECK(qa_freetype_bitmap_length(9, 3, 5, 4) == 15);
  CHECK(qa_freetype_bitmap_length(9, 3, 36, 7) == 108);
  CHECK(qa_freetype_bitmap_length(0, 0, 0, 0) == 0);
  CHECK_ABORTS((void)qa_freetype_bitmap_length(9, 3, 1, 1));
  CHECK_ABORTS((void)qa_freetype_bitmap_length(10, 3, 9, 2));
  CHECK_ABORTS((void)qa_freetype_bitmap_length(1, 0x7fffffff, 1, 2));
  CHECK_ABORTS((void)qa_freetype_bitmap_length(1, 1, 4, 255));
  CHECK_ABORTS((void)qa_freetype_bitmap_length(1, 1, 0, 0));

  uint8_t rows[9] = {30, 31, 0, 20, 21, 0, 10, 11, 0};
  const uint8_t expect_rows[9] = {10, 11, 0, 20, 21, 0, 30, 31, 0};
  qa_normalize_freetype_bitmap_rows(rows, 9, 3, -3);
  CHECK(memcmp(rows, expect_rows, 9) == 0);
  qa_normalize_freetype_bitmap_rows(rows, 9, 3, 3);
  CHECK(memcmp(rows, expect_rows, 9) == 0);
  uint8_t rows_copy[9];
  memcpy(rows_copy, rows, 9);
  CHECK_ABORTS(qa_normalize_freetype_bitmap_rows(rows_copy, 9, 4, -3));
  qa_normalize_freetype_bitmap_rows(rows, 0, 0, 0);
}

static void test_contained_and_writable(void) {
  size_t count = 0;
  char **parts = qa_contained_file_parts("a/b/c.txt", &count);
  CHECK(parts != NULL && count == 3);
  CHECK(parts != NULL && strcmp(parts[2], "c.txt") == 0);
  qa_contained_file_parts_free(parts, count);
  size_t n = 0;
  CHECK_ABORTS((void)qa_contained_file_parts("", &n));
  CHECK_ABORTS((void)qa_contained_file_parts("../x", &n));
  CHECK_ABORTS((void)qa_contained_file_parts("a//b", &n));
  CHECK_ABORTS((void)qa_contained_file_parts("a\\b", &n));
  CHECK_ABORTS((void)qa_contained_file_parts("a:b", &n));
  CHECK_ABORTS((void)qa_contained_file_parts("./a", &n));

  char root[] = "/tmp/qa-platform-b-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char store_root[256];
  snprintf(store_root, sizeof store_root, "%s/store", root);

  qa_user_file_store store;
  CHECK(qa_user_file_store_init(&store, store_root));
  qa_writable_binary_file file;
  CHECK(qa_user_file_store_open(&store, "sub/data.bin", QA_WRITABLE_WRITE, NULL,
                                NULL, &file));
  const uint8_t payload[5] = {1, 2, 3, 4, 5};
  CHECK(qa_writable_binary_file_write(&file, payload, 5) == 5);
  qa_writable_file_checkpoint checkpoint;
  CHECK(qa_writable_binary_file_capture_checkpoint(&file, &checkpoint));
  CHECK(checkpoint.position == 5 && strcmp(checkpoint.path, "sub/data.bin") == 0);
  CHECK(qa_writable_binary_file_seek(&file, 1, QA_WRITABLE_SEEK_SET) == 0);
  const uint8_t patch[1] = {9};
  CHECK(qa_writable_binary_file_write(&file, patch, 1) == 1);
  CHECK(qa_writable_binary_file_seek(&file, -100, QA_WRITABLE_SEEK_SET) == -1);
  qa_writable_binary_file_close(&file);

  qa_writable_binary_file resumed;
  CHECK(qa_user_file_store_resume(&store, &checkpoint, NULL, NULL, &resumed));
  const uint8_t tail[1] = {7};
  CHECK(qa_writable_binary_file_write(&resumed, tail, 1) == 1);
  qa_writable_binary_file_close(&resumed);
  qa_writable_file_checkpoint_dispose(&checkpoint);

  char path[512];
  snprintf(path, sizeof path, "%s/sub/data.bin", store_root);
  FILE *read_back = fopen(path, "rb");
  CHECK(read_back != NULL);
  uint8_t bytes[8] = {0};
  size_t got = read_back != NULL ? fread(bytes, 1, sizeof bytes, read_back) : 0;
  if (read_back != NULL) fclose(read_back);
  const uint8_t expect[6] = {1, 9, 3, 4, 5, 7};
  CHECK(got == 6 && memcmp(bytes, expect, 6) == 0);

  qa_writable_binary_file appended;
  CHECK(qa_user_file_store_open(&store, "sub/data.bin", QA_WRITABLE_APPEND, NULL,
                                NULL, &appended));
  CHECK(qa_writable_binary_file_write(&appended, tail, 1) == 1);
  qa_writable_binary_file_close(&appended);
  struct stat info;
  CHECK(stat(path, &info) == 0 && info.st_size == 7);

  qa_writable_file_checkpoint bad = {0};
  bad.path = (char *)"x.bin";
  bad.mode = QA_WRITABLE_WRITE;
  bad.mode_valid = false;
  bad.position = 0;
  qa_writable_binary_file unused;
  CHECK_ABORTS((void)qa_user_file_store_resume(&store, &bad, NULL, NULL, &unused));
  bad.mode_valid = true;
  bad.position = -1;
  CHECK_ABORTS((void)qa_user_file_store_resume(&store, &bad, NULL, NULL, &unused));

  qa_user_file_store_destroy(&store);
  (void)rmdir(root);
}

static void put_u16(uint8_t *p, size_t off, uint16_t value) {
  p[off] = (uint8_t)(value >> 8);
  p[off + 1] = (uint8_t)value;
}
static void put_i16(uint8_t *p, size_t off, int16_t value) {
  put_u16(p, off, (uint16_t)value);
}
static void put_u32(uint8_t *p, size_t off, uint32_t value) {
  p[off] = (uint8_t)(value >> 24);
  p[off + 1] = (uint8_t)(value >> 16);
  p[off + 2] = (uint8_t)(value >> 8);
  p[off + 3] = (uint8_t)value;
}

/* Authored unhinted squares, as in freetype.test.ts squareFont(). */
static uint8_t *square_font(bool corrupt_b, bool symbol_charmap, size_t *out_len) {
  static uint8_t head[54];
  static uint8_t hhea[36];
  static uint8_t maxp[32];
  static uint8_t hmtx[12];
  static uint8_t loca[8];
  static uint8_t glyf[102];
  static uint8_t cmap[28 + 5 * 12];
  memset(head, 0, sizeof head); memset(hhea, 0, sizeof hhea);
  memset(maxp, 0, sizeof maxp); memset(hmtx, 0, sizeof hmtx);
  memset(loca, 0, sizeof loca); memset(glyf, 0, sizeof glyf);
  memset(cmap, 0, sizeof cmap);
  put_u32(head, 0, 0x10000); put_u32(head, 12, 0x5f0f3cf5);
  put_u16(head, 18, 1024); put_i16(head, 40, 640); put_i16(head, 42, 640);
  put_u16(head, 46, 8);
  put_u32(hhea, 0, 0x10000); put_i16(hhea, 4, 800); put_i16(hhea, 6, -200);
  put_u16(hhea, 10, 768); put_i16(hhea, 16, 640); put_i16(hhea, 18, 1);
  put_u16(hhea, 34, 3);
  put_u32(maxp, 0, 0x10000); put_u16(maxp, 4, 3); put_u16(maxp, 6, 4);
  put_u16(maxp, 8, 1); put_u16(maxp, 14, 1);
  for (int i = 0; i < 3; i++) {
    put_u16(hmtx, (size_t)i * 4, 768);
    put_u16(loca, (size_t)i * 2 + 2, (uint16_t)((i + 1) * 17));
    size_t off = (size_t)i * 34;
    put_i16(glyf, off, (int16_t)(corrupt_b && i == 2 ? 32767 : 1));
    put_i16(glyf, off + 6, 640); put_i16(glyf, off + 8, 640);
    put_u16(glyf, off + 10, 3);
    for (int point = 14; point < 18; point++) glyf[off + (size_t)point] = 1;
    put_i16(glyf, off + 20, 640); put_i16(glyf, off + 24, -640);
    put_i16(glyf, off + 30, 640);
  }
  const uint32_t codes[5] = {65, 66, 233, 937, 0x1f642};
  put_u16(cmap, 2, 1); put_u16(cmap, 4, 3);
  put_u16(cmap, 6, (uint16_t)(symbol_charmap ? 0 : 10));
  put_u32(cmap, 8, 12); put_u16(cmap, 12, 12);
  put_u32(cmap, 16, (uint32_t)(sizeof cmap - 12)); put_u32(cmap, 24, 5);
  for (int i = 0; i < 5; i++) {
    size_t off = 28 + (size_t)i * 12;
    put_u32(cmap, off, codes[i]); put_u32(cmap, off + 4, codes[i]);
    put_u32(cmap, off + 8, codes[i] == 66 ? 2 : 1);
  }
  const char *tags[7] = {"cmap", "glyf", "head", "hhea", "hmtx", "loca", "maxp"};
  const uint8_t *tables[7] = {cmap, glyf, head, hhea, hmtx, loca, maxp};
  const size_t sizes[7] = {sizeof cmap, sizeof glyf, sizeof head, sizeof hhea,
                           sizeof hmtx, sizeof loca, sizeof maxp};
  size_t total = 12 + 7 * 16;
  for (int i = 0; i < 7; i++) total += (sizes[i] + 3) / 4 * 4;
  uint8_t *bytes = (uint8_t *)calloc(total, 1);
  if (bytes == NULL) return NULL;
  put_u32(bytes, 0, 0x10000); put_u16(bytes, 4, 7);
  put_u16(bytes, 6, 64); put_u16(bytes, 8, 2); put_u16(bytes, 10, 48);
  size_t offset = 12 + 7 * 16;
  for (int i = 0; i < 7; i++) {
    size_t start = 12 + (size_t)i * 16;
    memcpy(bytes + start, tags[i], 4);
    put_u32(bytes, start + 8, (uint32_t)offset);
    put_u32(bytes, start + 12, (uint32_t)sizes[i]);
    memcpy(bytes + offset, tables[i], sizes[i]);
    offset += (sizes[i] + 3) / 4 * 4;
  }
  *out_len = total;
  return bytes;
}

typedef struct print_log {
  char text[1024];
  size_t length;
} print_log;
static void log_print(const char *text, void *ctx) {
  print_log *log = (print_log *)ctx;
  size_t length = strlen(text);
  if (log->length + length < sizeof log->text) {
    memcpy(log->text + log->length, text, length + 1);
    log->length += length;
  }
}

static void test_freetype(void) {
  qa_freetype_font_library library;
  char error[160];
  print_log log = {{0}, 0};
  CHECK(qa_freetype_font_library_open(&library, log_print, &log, error,
                                      sizeof error) == QA_FREETYPE_READY);
  size_t font_len = 0;
  uint8_t *font = square_font(false, false, &font_len);
  CHECK(font != NULL);
  qa_freetype_face *face =
      qa_freetype_create_face(&library, font, font_len, 16, log_print, &log);
  CHECK(face != NULL);
  memset(font, 0, font_len); /* owned copy must not alias the input */
  const int64_t codes[4] = {65, 233, 937, 0x1f642};
  for (int i = 0; i < 4; i++) {
    CHECK(qa_freetype_glyph_index(&library, face, codes[i]) == 1);
    qa_font_glyph_bitmap glyph;
    CHECK(qa_freetype_render_glyph(&library, face, codes[i], NULL, NULL, &glyph));
    CHECK(glyph.width == 10 && glyph.height == 10 && glyph.pitch == 12 &&
          glyph.top == 11 && glyph.bottom == 0 && glyph.x_skip == 13);
    bool pixels_ok = glyph.pixel_length == 120;
    for (int row = 0; row < 10 && pixels_ok; row++) {
      for (int x = 0; x < 10; x++) {
        if (glyph.pixels[(size_t)row * 12 + (size_t)x] != 255) pixels_ok = false;
      }
      if (glyph.pixels[(size_t)row * 12 + 10] != 0 ||
          glyph.pixels[(size_t)row * 12 + 11] != 0)
        pixels_ok = false;
    }
    CHECK(pixels_ok);
    qa_font_glyph_bitmap_dispose(&glyph);
  }
  CHECK(qa_freetype_glyph_index(&library, face, 0x10ffff) == 0);

  qa_native_font_glyph_bitmap native;
  CHECK(qa_freetype_render_bitmap(&library, face, 65, NULL, NULL, &native));
  CHECK(native.width == 10 && native.height == 10 && native.pitch == 10 &&
        native.native_pitch == 10 && native.pixel_mode == 2 &&
        native.num_grays == 256 && native.left == 0 && native.top == 10 &&
        native.advance_x26 == 768 && native.advance_y26 == 0);
  bool native_ok = native.pixel_length == 100;
  for (size_t i = 0; i < native.pixel_length; i++) {
    if (native.pixels[i] != 255) native_ok = false;
  }
  CHECK(native_ok);
  qa_native_font_glyph_bitmap_dispose(&native);

  CHECK_ABORTS((void)qa_freetype_glyph_index(&library, face, -1));
  CHECK_ABORTS((void)qa_freetype_glyph_index(&library, face, 0xd800));
  CHECK_ABORTS((void)qa_freetype_glyph_index(&library, face, 0x110000));
  CHECK_ABORTS((void)qa_freetype_create_face(&library, font, font_len, 0, NULL, NULL));
  CHECK_ABORTS((void)qa_freetype_create_face(&library, font, font_len, 0x2000000, NULL, NULL));

  qa_freetype_release_face(&library, face);
  CHECK(qa_freetype_font_library_face_count(&library) == 0);
  CHECK_ABORTS((void)qa_freetype_glyph_index(&library, face, 65));
  free(font);

  size_t bad_len = 0;
  uint8_t *bad_font = square_font(true, false, &bad_len);
  log.length = 0;
  log.text[0] = '\0';
  CHECK(qa_freetype_create_face(&library, (const uint8_t *)"", 0, 16, log_print, &log) == NULL);
  uint8_t zeros[64] = {0};
  CHECK(qa_freetype_create_face(&library, zeros, 64, 16, log_print, &log) == NULL);
  size_t sym_len = 0;
  uint8_t *sym_font = square_font(false, true, &sym_len);
  CHECK(qa_freetype_create_face(&library, sym_font, sym_len, 16, log_print, &log) == NULL);
  free(sym_font);
  qa_freetype_face *damaged =
      qa_freetype_create_face(&library, bad_font, bad_len, 16, log_print, &log);
  CHECK(damaged != NULL);
  qa_font_glyph_bitmap glyph;
  CHECK(qa_freetype_render_glyph(&library, damaged, 65, log_print, &log, &glyph));
  CHECK(glyph.height == 10);
  qa_font_glyph_bitmap_dispose(&glyph);
  CHECK(!qa_freetype_render_glyph(&library, damaged, 66, log_print, &log, &glyph));
  CHECK(strstr(log.text, "FT_Load_Glyph failed") != NULL);
  qa_freetype_release_face(&library, damaged);
  free(bad_font);

  CHECK(qa_freetype_font_library_close(&library));
  CHECK(qa_freetype_font_library_close(&library));
  CHECK(qa_freetype_font_library_face_count(&library) == 0);
}

static void test_vorbis(void) {
  qa_vorbis_decoder decoder;
  qa_vorbis_error error;
  CHECK_ABORTS((void)qa_vorbis_decoder_open(&decoder, "", &error));
  CHECK(!qa_vorbis_decoder_open(&decoder, "/tmp/qa-no-such-track.ogg", &error));
  CHECK(strstr(error.message, "ov_fopen failed with Vorbis error") != NULL);

  char path[] = "/tmp/qa-invalid-XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  const uint8_t junk[8] = {79, 103, 103, 83, 0, 255, 0, 1};
  if (fd >= 0) {
    CHECK(write(fd, junk, sizeof junk) == (ssize_t)sizeof junk);
    close(fd);
  }
  CHECK(!qa_vorbis_decoder_open(&decoder, path, &error));
  CHECK(qa_vorbis_decoder_closed(&decoder));
  (void)unlink(path);

  /* Full decode runs only with an external fixture, mirroring the
   * TS skipIf(!hasFixture) cases. */
  const char *fixture = getenv("QUAKE_TEST_OGG");
  if (fixture == NULL || access(fixture, F_OK) != 0) {
    printf("vorbis: no QUAKE_TEST_OGG fixture; decode cases skipped\n");
    return;
  }
  CHECK(qa_vorbis_decoder_open(&decoder, fixture, &error));
  CHECK(decoder.metadata.sample_rate >= 8000);
  CHECK(decoder.metadata.channels == 1 || decoder.metadata.channels == 2);
  CHECK(decoder.metadata.total_frames > 4096);
  CHECK(fabs(decoder.metadata.duration_seconds -
             (double)decoder.metadata.total_frames /
                 (double)decoder.metadata.sample_rate) < 1e-12);
  qa_vorbis_pcm_chunk chunk;
  bool has = false;
  CHECK(qa_vorbis_decoder_read(&decoder, 4096, &chunk, &has, &error) && has);
  CHECK(chunk.frames == 4096);
  int16_t *retained = (int16_t *)malloc(chunk.sample_count * sizeof(int16_t));
  CHECK(retained != NULL);
  if (retained != NULL)
    memcpy(retained, chunk.samples, chunk.sample_count * sizeof(int16_t));
  size_t retained_count = chunk.sample_count;
  int64_t position = 0;
  CHECK(qa_vorbis_decoder_position_frames(&decoder, &position, &error) &&
        position == 4096);
  qa_vorbis_pcm_chunk scratch;
  CHECK(qa_vorbis_decoder_read(&decoder, 17, &scratch, &has, &error) && has);
  qa_vorbis_pcm_chunk_dispose(&scratch);
  CHECK(qa_vorbis_decoder_seek(&decoder, 0, &error));
  qa_vorbis_pcm_chunk repeated;
  CHECK(qa_vorbis_decoder_read(&decoder, 4096, &repeated, &has, &error) && has);
  CHECK(retained != NULL && repeated.sample_count == retained_count &&
        memcmp(repeated.samples, retained, retained_count * sizeof(int16_t)) == 0);
  CHECK(memcmp(chunk.samples, retained, retained_count * sizeof(int16_t)) == 0);
  qa_vorbis_pcm_chunk_dispose(&repeated);
  CHECK_ABORTS((void)qa_vorbis_decoder_read(&decoder, 0, &chunk, &has, &error));
  CHECK_ABORTS((void)qa_vorbis_decoder_seek(&decoder, -1, &error));
  CHECK(qa_vorbis_decoder_seek(&decoder, decoder.metadata.total_frames - 7, &error));
  qa_vorbis_pcm_chunk tail;
  CHECK(qa_vorbis_decoder_read(&decoder, 4096, &tail, &has, &error) && has &&
        tail.frames == 7);
  qa_vorbis_pcm_chunk_dispose(&tail);
  CHECK(qa_vorbis_decoder_read(&decoder, 4096, &tail, &has, &error) && !has);
  CHECK(qa_vorbis_decoder_close(&decoder, &error) == 0);
  CHECK(qa_vorbis_decoder_close(&decoder, &error) == 0);
  CHECK(qa_vorbis_decoder_closed(&decoder));
  CHECK(memcmp(chunk.samples, retained, retained_count * sizeof(int16_t)) == 0);
  qa_vorbis_pcm_chunk_dispose(&chunk);
  free(retained);
}

static void test_theora(void) {
  qa_theora_decoder decoder;
  char error[160];
  qa_platform_ogg_packet packet = {0};
  CHECK(!qa_theora_decoder_init(&decoder, &packet, 1, error, sizeof error));
  CHECK(strstr(error, "three header packets") != NULL);
  qa_platform_ogg_packet headers[3] = {{0}, {0}, {0}};
  CHECK(!qa_theora_decoder_init(&decoder, headers, 3, error, sizeof error));
  qa_theora_picture picture;
  CHECK(!qa_theora_decoder_decode(&decoder, &packet, &picture, error, sizeof error));
  CHECK(strstr(error, "closed") != NULL);
  qa_theora_decoder_close(&decoder);
  /* Real packet decode needs a Theora fixture stream; the TS suite
   * has no theora platform test fixture either. */
}

typedef struct fake_gl {
  int retained;
  int released;
  int made_current;
  bool fail_retain;
  uint32_t last_shader;
  int32_t last_count;
  int32_t last_length;
} fake_gl;

static void fake_shader_source(uint32_t shader, int32_t count,
                               const char *const *string,
                               const int32_t *length) {
  (void)shader;
  (void)count;
  (void)string;
  (void)length;
}

static fake_gl *active_fake = NULL;
static void recording_shader_source(uint32_t shader, int32_t count,
                                    const char *const *string,
                                    const int32_t *length) {
  active_fake->last_shader = shader;
  active_fake->last_count = count;
  active_fake->last_length = length != NULL ? length[0] : -1;
  (void)string;
}

static void *fake_proc(void *ctx, const char *name) {
  (void)ctx;
  if (strcmp(name, "glShaderSource") == 0) {
    void (*fn)(uint32_t, int32_t, const char *const *, const int32_t *) =
        recording_shader_source;
    void *address = NULL;
    memcpy(&address, &fn, sizeof address);
    return address;
  }
  void (*fn)(uint32_t, int32_t, const char *const *, const int32_t *) =
      fake_shader_source;
  void *address = NULL;
  memcpy(&address, &fn, sizeof address);
  return address;
}
static void fake_release(void *ctx) { ((fake_gl *)ctx)->released++; }
static bool fake_retain(void *ctx, qa_gl_release_fn *out_release, void **out_ctx) {
  fake_gl *fake = (fake_gl *)ctx;
  if (fake->fail_retain) return false;
  fake->retained++;
  *out_release = fake_release;
  *out_ctx = fake;
  return true;
}
static void fake_current(void *ctx) { ((fake_gl *)ctx)->made_current++; }

static void test_gl_tables(void) {
  fake_gl fake = {0};
  active_fake = &fake;
  qa_gl_context context = {&fake, fake_proc, fake_retain, fake_current};
  qa_gl gl;
  CHECK(qa_load_gl(&context, &gl));
  CHECK(gl.glClear != NULL && gl.glTexImage2D != NULL && fake.retained == 1);
  qa_gl_close(&gl);
  qa_gl_close(&gl);
  CHECK(fake.released == 1);

  qa_gl_compiled_vertex_arrays arrays;
  CHECK(qa_load_gl_compiled_vertex_arrays(&context, &arrays));
  CHECK(arrays.glLockArraysEXT != NULL && arrays.glUnlockArraysEXT != NULL);
  qa_gl_compiled_vertex_arrays_close(&arrays);
  CHECK(fake.released == 2);

  qa_gl_framebuffers framebuffers;
  CHECK(qa_load_gl_framebuffers(&context, &framebuffers));
  CHECK(framebuffers.glGenFramebuffers != NULL &&
        framebuffers.glBlitFramebuffer != NULL);
  qa_gl_framebuffers_close(&framebuffers);

  qa_gl_programs programs;
  CHECK(qa_load_gl_programs(&context, &programs));
  qa_gl_programs_shader_source(&programs, 7, "void main() {}");
  CHECK(fake.made_current == 1 && fake.last_shader == 7 && fake.last_count == 1 &&
        fake.last_length == 14);
  CHECK_ABORTS(qa_gl_programs_shader_source(&programs, 0, "x"));
  qa_gl_programs_close(&programs);
  CHECK_ABORTS(qa_gl_programs_shader_source(&programs, 7, "x"));

  fake.fail_retain = true;
  CHECK(!qa_load_gl(&context, &gl));
  fake.fail_retain = false;
}

static void test_ipx(void) {
  qa_platform_native_ipx_capability capability = qa_bun_native_ipx_capability();
#ifdef __linux__
#if defined(__x86_64__) || defined(__aarch64__)
  CHECK(capability.kind == QA_NATIVE_IPX_AVAILABLE);
#endif
#endif
  const uint8_t node[6] = {1, 2, 3, 4, 5, 6};
  qa_platform_ipx_address address =
      qa_platform_ipx_address_make(0x10, node, 0x4000);
  CHECK(address.network == 0x10 && address.port == 0x4000 && address.node[5] == 6);
  CHECK_ABORTS((void)qa_platform_ipx_address_make(0, node, 0));
  CHECK_ABORTS((void)qa_platform_ipx_address_make(0, node, 65536));

  qa_platform_ipx_bind_options options = {0, 4, true};
  qa_native_ipx_transport transport;
  qa_platform_ipx_error error;
  bool bound = qa_native_ipx_capability_bind(&capability, &options, &transport,
                                             &error);
  if (!bound) {
    /* Kernels without AF_IPX report the family missing (unsupported)
     * or a wrapped bind failure; either is the TS outcome there. */
    printf("ipx: bind unavailable on this host: %s\n", error.message);
    CHECK(error.message[0] != '\0');
    return;
  }
  CHECK(qa_native_ipx_transport_max_datagram_bytes(&transport) == 546);
  CHECK(!qa_native_ipx_transport_closed(&transport));
  qa_platform_ipx_receive_event event;
  qa_native_ipx_transport_poll(&transport, &event);
  CHECK(event.kind == QA_PLATFORM_IPX_RECEIVE_NONE ||
        event.kind == QA_PLATFORM_IPX_RECEIVE_PACKET);
  qa_platform_ipx_receive_event_dispose(&event);
  qa_native_ipx_transport_close(&transport);
  CHECK(qa_native_ipx_transport_closed(&transport));
}

static void test_audio(void) {
  /* The TS suite runs these in a child with the dummy driver. */
  setenv("SDL_AUDIODRIVER", "dummy", 0);
  setenv("SDL_AUDIO_DRIVER", "dummy", 0);
  qa_sdl_audio_device device;
  char error[256];
  qa_sdl_audio_options options = {48000, 2, 0, NULL, 256};

  qa_sdl_audio_options bad = options;
  bad.sample_rate = 7999;
  CHECK_ABORTS((void)qa_sdl_audio_device_open(&bad, &device, error, sizeof error));
  bad = options;
  bad.sample_rate = 48000.5;
  CHECK_ABORTS((void)qa_sdl_audio_device_open(&bad, &device, error, sizeof error));
  bad = options;
  bad.buffer_frames = 32769;
  CHECK_ABORTS((void)qa_sdl_audio_device_open(&bad, &device, error, sizeof error));
  bad = options;
  bad.device_name = "";
  CHECK_ABORTS((void)qa_sdl_audio_device_open(&bad, &device, error, sizeof error));
  bad = options;
  bad.channels = 3;
  CHECK_ABORTS((void)qa_sdl_audio_device_open(&bad, &device, error, sizeof error));

  size_t name_count = 0;
  char **names = qa_sdl_audio_output_device_names(&name_count);
  CHECK(name_count >= 1);
  const char *device_name = name_count != 0 ? names[0] : NULL;

  qa_sdl_audio_open_result result =
      qa_sdl_audio_device_open(&options, &device, error, sizeof error);
  if (result != QA_SDL_AUDIO_OPEN_OK) {
    printf("audio: no usable dummy device (%s); live cases skipped\n", error);
    qa_sdl_audio_device_names_free(names, name_count);
    return;
  }
  CHECK(qa_sdl_audio_state_of(&device) == QA_SDL_AUDIO_PAUSED);
  CHECK(device.max_queued_frames == 96000);
  int16_t samples[20] = {0};
  qa_sdl_audio_queue16(&device, samples + 2, 10);
  CHECK(qa_sdl_audio_queued_frames(&device) == 5);
  size_t again_count = 0;
  char **again = qa_sdl_audio_output_device_names(&again_count);
  CHECK(again_count >= 1);
  qa_sdl_audio_device_names_free(again, again_count);
  CHECK(qa_sdl_audio_queued_frames(&device) == 5);
  CHECK_ABORTS(qa_sdl_audio_queue8(&device, (const uint8_t *)samples, 2));
  CHECK_ABORTS(qa_sdl_audio_queue16(&device, samples, 3));
  qa_sdl_audio_clear(&device);
  CHECK(qa_sdl_audio_queued_frames(&device) == 0);
  qa_sdl_audio_queue16(&device, samples, 0);
  CHECK(device.buffer_frames > 0);
  qa_sdl_audio_start(&device);
  qa_sdl_audio_start(&device);
  CHECK(qa_sdl_audio_state_of(&device) == QA_SDL_AUDIO_PLAYING);
  qa_sdl_audio_pause(&device);
  qa_sdl_audio_pause(&device);
  CHECK(qa_sdl_audio_state_of(&device) == QA_SDL_AUDIO_PAUSED);
  int64_t played = qa_sdl_audio_playback_frames(&device);
  qa_sdl_audio_clear(&device);
  CHECK(qa_sdl_audio_playback_frames(&device) == played);
  qa_sdl_audio_close(&device);
  qa_sdl_audio_close(&device);
  CHECK(qa_sdl_audio_state_of(&device) == QA_SDL_AUDIO_CLOSED);
  CHECK_ABORTS((void)qa_sdl_audio_queued_frames(&device));

  qa_sdl_audio_options missing = options;
  missing.device_name = "quake-typescript-no-such-output";
  CHECK(qa_sdl_audio_device_open(&missing, &device, error, sizeof error) ==
        QA_SDL_AUDIO_OPEN_UNAVAILABLE);

  if (device_name != NULL) {
    qa_sdl_audio_options u8_options = {22050, 1, 8, device_name, 256};
    result = qa_sdl_audio_device_open(&u8_options, &device, error, sizeof error);
    /* The TS suite reaches this combination through SDL3, whose
     * stream reports exact queued input frames; SDL2-compat's
     * resampler does not at 22050 Hz, so the TS open probe itself
     * reports this combination unavailable rather than opening it. */
    CHECK(result == QA_SDL_AUDIO_OPEN_OK ||
          result == QA_SDL_AUDIO_OPEN_UNAVAILABLE);
    if (result == QA_SDL_AUDIO_OPEN_UNAVAILABLE)
      printf("audio: U8 22050 probe unavailable under SDL2 (%s)\n", error);
    if (result == QA_SDL_AUDIO_OPEN_OK) {
      uint8_t silence[10];
      memset(silence, 128, sizeof silence);
      qa_sdl_audio_queue8(&device, silence + 1, 5);
      CHECK(qa_sdl_audio_queued_frames(&device) == 5);
      CHECK_ABORTS(qa_sdl_audio_queue16(&device, samples, 1));
      qa_sdl_audio_close(&device);
    }
  }
  qa_sdl_audio_device_names_free(names, name_count);
}

static const qa_controller_device *find_test_device(
    const qa_controller_device *devices, size_t count, int32_t instance) {
  for (size_t i = 0; i < count; i++) {
    if (devices[i].instance == instance) return &devices[i];
  }
  return NULL;
}

static void test_controller(void) {
  qa_sdl_runtime runtime = qa_sdl_controllers_runtime();
  CHECK(runtime.version[0] != '\0');
  qa_sdl_controllers *controllers = qa_sdl_controllers_open();
  CHECK(controllers != NULL);
  if (controllers == NULL) return;
  CHECK(!qa_sdl_controllers_closed(controllers));
  char version[16];
  qa_sdl_controllers_version(controllers, version, sizeof version);
  CHECK(strcmp(version, runtime.version) == 0);
  CHECK_ABORTS((void)qa_sdl_controllers_open());

  qa_virtual_sdl_controller *first =
      qa_virtual_sdl_controller_attach("Quake controller fixture", false);
  qa_virtual_sdl_controller *second =
      qa_virtual_sdl_controller_attach("Quake controller fixture B", false);
  CHECK(first != NULL && second != NULL);
  size_t event_count = 0;
  qa_controller_event *events =
      qa_sdl_controllers_poll_events(controllers, &event_count);
  int connected_first = 0;
  for (size_t i = 0; i < event_count; i++) {
    if (events[i].kind == QA_CONTROLLER_EVENT_CONNECTED &&
        events[i].device.instance == qa_virtual_sdl_controller_instance(first))
      connected_first++;
  }
  CHECK(connected_first == 1);
  qa_controller_events_free(events, event_count);

  size_t device_count = 0;
  qa_controller_device *devices =
      qa_sdl_controllers_devices(controllers, &device_count);
  const qa_controller_device *first_device =
      find_test_device(devices, device_count,
                       qa_virtual_sdl_controller_instance(first));
  const qa_controller_device *second_device =
      find_test_device(devices, device_count,
                       qa_virtual_sdl_controller_instance(second));
  CHECK(first_device != NULL && second_device != NULL);
  CHECK(first_device != NULL && first_device->is_virtual &&
        strcmp(first_device->name, "Quake controller fixture") == 0);
  CHECK(first_device != NULL && first_device->guid != NULL &&
        strlen(first_device->guid) == 32);

  if (first_device != NULL && second_device != NULL && first_device->guid != NULL &&
      second_device->guid != NULL) {
    qa_controller_selection selections[4];
    memset(selections, 0, sizeof selections);
    selections[0].kind = QA_CONTROLLER_SELECTION_AUTOMATIC;
    selections[1].kind = QA_CONTROLLER_SELECTION_DEVICE;
    snprintf(selections[1].guid, sizeof selections[1].guid, "%s",
             first_device->guid);
    selections[1].ordinal = (double)first_device->ordinal;
    selections[2] = selections[1];
    selections[3].kind = QA_CONTROLLER_SELECTION_NONE;
    qa_sdl_controllers_set_assignments(controllers, selections, 4);
    size_t route_count = 0;
    int64_t *routes = qa_sdl_controllers_assignments(controllers, &route_count);
    CHECK(route_count == 4 && routes != NULL &&
          routes[0] == second_device->instance &&
          routes[1] == first_device->instance && routes[2] == -1 &&
          routes[3] == -1);
    free(routes);
    events = qa_sdl_controllers_poll_events(controllers, &event_count);
    qa_controller_events_free(events, event_count);

    qa_virtual_sdl_controller_set_button(first, 0, true);
    qa_virtual_sdl_controller_set_button(first, 0, false);
    qa_virtual_sdl_controller_set_button(second, 1, true);
    qa_virtual_sdl_controller_set_axis(first, 0, -32768);
    qa_virtual_sdl_controller_set_axis(second, 0, 32767);
    events = qa_sdl_controllers_poll_events(controllers, &event_count);
    bool saw_button_down = false, saw_button_up = false, saw_axis = false;
    for (size_t i = 0; i < event_count; i++) {
      if (events[i].kind == QA_CONTROLLER_EVENT_BUTTON &&
          events[i].instance == first_device->instance && events[i].button == 0) {
        if (events[i].down && events[i].slot == 1) saw_button_down = true;
        if (!events[i].down) saw_button_up = true;
      }
      if (events[i].kind == QA_CONTROLLER_EVENT_AXIS &&
          events[i].instance == second_device->instance && events[i].axis == 0 &&
          events[i].value == 32767 && events[i].slot == 0)
        saw_axis = true;
    }
    CHECK(saw_button_down && saw_button_up && saw_axis);
    qa_controller_events_free(events, event_count);
    qa_controller_state snapshot;
    CHECK(qa_sdl_controllers_snapshot(controllers, first_device->instance,
                                      &snapshot));
    CHECK(snapshot.buttons[0] == false && snapshot.axes[0] == -32768);
    CHECK(qa_sdl_controllers_snapshot(controllers, second_device->instance,
                                      &snapshot));
    CHECK(snapshot.buttons[1] == true && snapshot.axes[0] == 32767);
    CHECK(!qa_sdl_controllers_snapshot(controllers, 0x7fffffff, &snapshot));
    qa_controller_operation_result unknown =
        qa_sdl_controllers_rumble(controllers, 0x7fffffff, 0.5, 0.5, 100);
    CHECK(unknown.kind == QA_CONTROLLER_OPERATION_DISCONNECTED);
    CHECK_ABORTS((void)qa_sdl_controllers_rumble(controllers,
                                                 first_device->instance, 2, 0, 0));
  }
  qa_controller_devices_free(devices, device_count);

  qa_sdl_controllers_close(controllers);
  qa_sdl_controllers_close(controllers);
  CHECK(qa_sdl_controllers_closed(controllers));
  CHECK_ABORTS((void)qa_sdl_controllers_poll_events(controllers, &event_count));
  free(controllers);

  qa_virtual_sdl_controller_close(second);
  qa_virtual_sdl_controller_close(first);
  CHECK(qa_virtual_sdl_controller_closed(first));
  free(first);
  free(second);
}

int main(void) {
  test_freetype_layout();
  test_contained_and_writable();
  test_freetype();
  test_vorbis();
  test_theora();
  test_gl_tables();
  test_ipx();
  test_audio();
  test_controller();
  printf("platform-b: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}



