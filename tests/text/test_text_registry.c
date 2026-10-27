/*
 * Cases for src/text/q3-font-registry.ts (no direct TS test file;
 * behaviors taken from the TS source and the fontImage cases in
 * tests/text/presentation.test.ts / tests/materials/
 * generated-font-picture.test.ts). Synthetic DAT records stand in
 * for corpus fonts; the generation cases use the real platform
 * FreeType port over a system TTF when one is present.
 * RangeError/Error/SaveReader cases run in a child and must abort.
 * GPL-2.0-or-later.
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

#include "qa/text/q3_font_registry.h"

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

#define RECORD QA_Q3_FONT_RECORD_BYTES

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static int32_t get_i32(const uint8_t *bytes, size_t offset) {
  uint32_t bits = 0;
  for (int i = 0; i < 4; i++) bits |= (uint32_t)bytes[offset + (size_t)i] << (8 * i);
  int32_t value;
  memcpy(&value, &bits, sizeof value);
  return value;
}
static void put_f32(uint8_t *bytes, size_t offset, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}

/* Synthetic fontInfo_t: glyph i is 8+i%5 tall, shader bigchars. */
static uint8_t dat_record[RECORD];
static void build_dat(void) {
  memset(dat_record, 0, sizeof dat_record);
  for (int i = 0; i < 256; i++) {
    size_t offset = (size_t)i * 80;
    put_i32(dat_record, offset + 0, 8 + i % 5);
    put_i32(dat_record, offset + 4, 6);
    put_i32(dat_record, offset + 8, -2);
    put_i32(dat_record, offset + 12, 7);
    put_i32(dat_record, offset + 16, 8);
    put_i32(dat_record, offset + 20, 7);
    put_i32(dat_record, offset + 24, 8 + i % 5);
    memcpy(dat_record + offset + 48, "gfx/2d/bigchars", 16);
  }
  put_f32(dat_record, 20480, 1.0f);
  memcpy(dat_record + 20484, "stale-name", 11);
}

/* A system TTF for the real-FreeType generation cases, if present. */
static uint8_t *ttf_bytes;
static size_t ttf_length;
static void load_ttf(void) {
  static const char *const candidates[] = {
      "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
  };
  for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
    FILE *file = fopen(candidates[i], "rb");
    if (file == NULL) continue;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0) {
      fclose(file);
      continue;
    }
    ttf_bytes = (uint8_t *)malloc((size_t)length);
    if (ttf_bytes != NULL && fread(ttf_bytes, 1, (size_t)length, file) == (size_t)length)
      ttf_length = (size_t)length;
    else {
      free(ttf_bytes);
      ttf_bytes = NULL;
    }
    fclose(file);
    if (ttf_bytes != NULL) return;
  }
}

struct fake_reader {
  bool serve_dat; /* fontImage_*.dat present at RECORD length */
  int length_calls;
  int retained_calls;
  int free_calls;
  char last_path[128];
};

static double fake_length(void *ctx, const char *path) {
  struct fake_reader *fake = (struct fake_reader *)ctx;
  fake->length_calls++;
  snprintf(fake->last_path, sizeof fake->last_path, "%s", path);
  return fake->serve_dat ? RECORD : 0;
}
static bool fake_retained(void *ctx, const char *path, qa_retained_font_file *out) {
  struct fake_reader *fake = (struct fake_reader *)ctx;
  fake->retained_calls++;
  snprintf(fake->last_path, sizeof fake->last_path, "%s", path);
  if (strcmp(path, "fonts/test.ttf") == 0) {
    if (ttf_bytes == NULL) return false;
    out->bytes = (qa_byte_view){ttf_bytes, ttf_length};
    out->length = (double)ttf_length;
    return true;
  }
  if (!fake->serve_dat) return false;
  out->bytes = (qa_byte_view){dat_record, RECORD};
  out->length = RECORD;
  return true;
}
static void fake_free(void *ctx, qa_retained_font_file file) {
  (void)file;
  ((struct fake_reader *)ctx)->free_calls++;
}
static qa_font_file_reader reader_of(struct fake_reader *fake) {
  return (qa_font_file_reader){fake, fake_length, fake_retained, fake_free};
}

struct fake_pictures {
  int calls;
  int syncs;
  char names[1600][64];
  char printed[1024];
};

static bool fake_register_picture(void *ctx, const char *path,
                                  qa_material_picture *out) {
  struct fake_pictures *fake = (struct fake_pictures *)ctx;
  int slot = fake->calls < 1600 ? fake->calls : 1599;
  snprintf(fake->names[slot], sizeof fake->names[slot], "%s", path);
  out->name = fake->names[slot];
  out->order = 100 + fake->calls;
  out->compiled = NULL;
  fake->calls++;
  return true;
}
static void fake_sync(void *ctx) { ((struct fake_pictures *)ctx)->syncs++; }
static void fake_print(void *ctx, const char *text) {
  struct fake_pictures *fake = (struct fake_pictures *)ctx;
  snprintf(fake->printed, sizeof fake->printed, "%s", text);
}

struct fake_generation {
  int images;
  int writes;
  bool save;
  bool any_alpha;
  char last_write[128];
  uint8_t first_rgb;
  char names[8][128];
};

static bool gen_image(void *ctx, const char *name, qa_byte_view rgba,
                      qa_material_picture *out) {
  struct fake_generation *fake = (struct fake_generation *)ctx;
  int slot = fake->images < 8 ? fake->images : 7;
  snprintf(fake->names[slot], sizeof fake->names[slot], "%s", name);
  if (fake->images == 0 && rgba.length >= 4) fake->first_rgb = rgba.bytes[0];
  for (size_t i = 3; i < rgba.length; i += 4)
    if (rgba.bytes[i] != 0) fake->any_alpha = true;
  out->name = fake->names[slot];
  out->order = 500 + fake->images;
  out->compiled = NULL;
  fake->images++;
  return true;
}
static bool gen_save(void *ctx) { return ((struct fake_generation *)ctx)->save; }
static void gen_write(void *ctx, const char *name, qa_byte_view bytes) {
  struct fake_generation *fake = (struct fake_generation *)ctx;
  (void)bytes;
  snprintf(fake->last_write, sizeof fake->last_write, "%s", name);
  fake->writes++;
}
static qa_q3_font_generation_services generation_of(struct fake_generation *fake) {
  return (qa_q3_font_generation_services){fake, gen_image, gen_save, gen_write};
}

static uint8_t destination_bytes[RECORD];
static qa_q3_font_record_view good_destination(void *ctx) {
  (void)ctx;
  return (qa_q3_font_record_view){destination_bytes, RECORD};
}
static qa_q3_font_record_view short_destination(void *ctx) {
  (void)ctx;
  return (qa_q3_font_record_view){destination_bytes, 10};
}

/* Generated-atlas restore without generation services must fail. */
static void restore_without_generation(const qa_save_value *tree) {
  struct fake_reader reader = {true, 0, 0, 0, ""};
  struct fake_pictures pictures = {0};
  qa_q3_renderer_font_registry registry;
  qa_q3_renderer_font_registry_init(&registry, reader_of(&reader),
                                    fake_register_picture, &pictures, fake_sync,
                                    &pictures, NULL);
  qa_q3_renderer_font_registry_restore_checkpoint(&registry, tree);
}

int main(void) {
  build_dat();
  load_ttf();

  /* DAT registration, cache identity, destination record. */
  {
    struct fake_reader reader = {true, 0, 0, 0, ""};
    struct fake_pictures pictures = {0};
    qa_q3_renderer_font_registry registry;
    qa_q3_renderer_font_registry_init(&registry, reader_of(&reader),
                                      fake_register_picture, &pictures, fake_sync,
                                      &pictures, NULL);
    const qa_q3_registered_font *font =
        qa_q3_renderer_font_registry_register_font(&registry, NULL, 12, fake_print,
                                                   &pictures, NULL, NULL);
    CHECK(font != NULL);
    if (font != NULL) {
      CHECK(strcmp(font->name, "fonts/fontImage_12.dat") == 0);
      CHECK(font->glyph_scale == 1.0);
      CHECK(font->glyph_count == 256);
      CHECK(font->glyphs[0].metrics.height == 8);
      CHECK(font->glyphs[3].metrics.height == 11);
      CHECK(font->glyphs[0].picture != NULL);
      if (font->glyphs[0].picture != NULL)
        CHECK(font->glyphs[0].picture->material.order == 100);
      CHECK(font->glyphs[255].picture == NULL);
    }
    CHECK(pictures.calls == 255);
    CHECK(pictures.syncs == 1);
    CHECK(reader.retained_calls == 1);
    CHECK(reader.free_calls == 0); /* retained until close */

    /* Truncated/default point sizes resolve to the cached 12pt font. */
    memset(destination_bytes, 0xAB, sizeof destination_bytes);
    const qa_q3_registered_font *again =
        qa_q3_renderer_font_registry_register_font(&registry, NULL, 12.9, fake_print,
                                                   &pictures, good_destination, NULL);
    CHECK(again == font);
    CHECK(pictures.calls == 255);
    CHECK(get_i32(destination_bytes, 44) == 100); /* cached order write */
    CHECK(memcmp(destination_bytes + 20484, "fonts/fontImage_12.dat", 22) == 0);
    CHECK(qa_q3_renderer_font_registry_register_font(&registry, NULL, 0, fake_print,
                                                     &pictures, NULL, NULL) == font);
    CHECK(qa_q3_renderer_font_registry_register_font(&registry, NULL, -3, fake_print,
                                                     &pictures, NULL, NULL) == font);

    CHECK_ABORTS(qa_q3_renderer_font_registry_register_font(
        &registry, NULL, NAN, fake_print, &pictures, NULL, NULL));
    CHECK_ABORTS(qa_q3_renderer_font_registry_register_font(
        &registry, NULL, INFINITY, fake_print, &pictures, NULL, NULL));
    CHECK_ABORTS(qa_q3_renderer_font_registry_register_font(
        &registry, NULL, 12, fake_print, &pictures, short_destination, NULL));

    /* Checkpoint round trip into an empty owner. */
    qa_save_value *tree = qa_q3_renderer_font_registry_capture_checkpoint(&registry);
    CHECK(tree != NULL);
    if (tree != NULL) {
      const qa_save_value *kind = qa_save_value_get(tree, "freeType");
      CHECK(kind != NULL && kind->kind == QA_SAVE_NULL);
      const qa_save_value *fonts = qa_save_value_get(tree, "fonts");
      CHECK(fonts != NULL && fonts->item_count == 1);
      const qa_save_value *generated = qa_save_value_get(tree, "generated");
      CHECK(generated != NULL && generated->item_count == 0);
      struct fake_reader reader2 = {true, 0, 0, 0, ""};
      struct fake_pictures pictures2 = {0};
      qa_q3_renderer_font_registry restored;
      qa_q3_renderer_font_registry_init(&restored, reader_of(&reader2),
                                        fake_register_picture, &pictures2, fake_sync,
                                        &pictures2, NULL);
      CHECK(qa_q3_renderer_font_registry_restore_checkpoint(&restored, tree));
      CHECK(pictures2.calls == 255); /* names re-registered by name */
      const qa_q3_registered_font *restored_font =
          qa_q3_renderer_font_registry_register_font(&restored, NULL, 12, fake_print,
                                                     &pictures2, NULL, NULL);
      CHECK(restored_font != NULL && pictures2.calls == 255);
      if (restored_font != NULL) {
        CHECK(strcmp(restored_font->name, "fonts/fontImage_12.dat") == 0);
        CHECK(restored_font->glyphs[255].picture == NULL);
      }
      CHECK_ABORTS(qa_q3_renderer_font_registry_restore_checkpoint(&restored, tree));
      qa_q3_renderer_font_registry_destroy(&restored);
      qa_save_value_free(tree);
    }
    qa_q3_renderer_font_registry_close(&registry);
    CHECK(reader.free_calls == 1);
    qa_q3_renderer_font_registry_close(&registry); /* idempotent */
    CHECK(reader.free_calls == 1);
    CHECK_ABORTS(qa_q3_renderer_font_registry_register_font(
        &registry, NULL, 12, fake_print, &pictures, NULL, NULL));
    CHECK_ABORTS(qa_q3_renderer_font_registry_capture_checkpoint(&registry));
    qa_q3_renderer_font_registry_destroy(&registry);
  }

  /* Six-font cache bound. */
  {
    struct fake_reader reader = {true, 0, 0, 0, ""};
    struct fake_pictures pictures = {0};
    qa_q3_renderer_font_registry registry;
    qa_q3_renderer_font_registry_init(&registry, reader_of(&reader),
                                      fake_register_picture, &pictures, fake_sync,
                                      &pictures, NULL);
    for (int size = 12; size <= 17; size++)
      CHECK(qa_q3_renderer_font_registry_register_font(&registry, NULL, size,
                                                       fake_print, &pictures, NULL,
                                                       NULL) != NULL);
    CHECK(registry.font_count == 6);
    CHECK(qa_q3_renderer_font_registry_register_font(&registry, NULL, 18, fake_print,
                                                     &pictures, NULL, NULL) == NULL);
    CHECK(strcmp(pictures.printed,
                 "RE_RegisterFont: Too many fonts registered already.\n") == 0);
    qa_q3_renderer_font_registry_destroy(&registry);
  }

  /* Missing DAT without generation services. */
  {
    struct fake_reader reader = {false, 0, 0, 0, ""};
    struct fake_pictures pictures = {0};
    qa_q3_renderer_font_registry registry;
    qa_q3_renderer_font_registry_init(&registry, reader_of(&reader),
                                      fake_register_picture, &pictures, fake_sync,
                                      &pictures, NULL);
    CHECK(qa_q3_renderer_font_registry_register_font(&registry, "fonts/test.ttf", 20,
                                                     fake_print, &pictures, NULL,
                                                     NULL) == NULL);
    CHECK(strcmp(pictures.printed,
                 "RE_RegisterFont: FreeType code not available\n") == 0);
    qa_q3_renderer_font_registry_destroy(&registry);
  }

  /* FreeType generation + generated-atlas checkpoint (real library;
   * skipped when no system TTF is available to rasterize). */
  if (ttf_bytes == NULL) {
    printf("test_text_registry: no system TTF; generation cases skipped\n");
  } else {
    struct fake_reader reader = {false, 0, 0, 0, ""};
    struct fake_pictures pictures = {0};
    struct fake_generation generation = {0};
    generation.save = true;
    qa_q3_font_generation_services services = generation_of(&generation);
    qa_q3_renderer_font_registry registry;
    qa_q3_renderer_font_registry_init(&registry, reader_of(&reader),
                                      fake_register_picture, &pictures, fake_sync,
                                      &pictures, &services);
    const qa_q3_registered_font *font =
        qa_q3_renderer_font_registry_register_font(&registry, "fonts/test.ttf", 20,
                                                   fake_print, &pictures, NULL, NULL);
    CHECK(font != NULL);
    if (font != NULL) {
      CHECK(strcmp(font->name, "fonts/fontImage_20.dat") == 0);
      CHECK(font->glyph_scale == (double)(float)(48.0 / (float)20.0));
      CHECK(font->glyph_count == 256);
      CHECK(font->glyphs[0].picture != NULL);
      CHECK(font->glyphs[255].picture == NULL);
      CHECK(strcmp(font->glyphs[0].metrics.shader_name,
                   "fonts/fontImage_0_20.tga") == 0);
      CHECK(font->glyphs['A'].metrics.height > 0);
      CHECK(font->glyphs['A'].metrics.pitch > 0);
      CHECK(font->glyphs['A'].metrics.x_skip > 0);
    }
    CHECK(reader.free_calls == 1); /* ttf freed in finally */
    CHECK(generation.images >= 1);
    CHECK(generation.writes >= 2); /* tga(s) plus the dat record */
    CHECK(strcmp(generation.last_write, "fonts/fontImage_20.dat") == 0);
    CHECK(generation.first_rgb == 255);
    CHECK(generation.any_alpha); /* real glyph coverage scaled in */

    qa_save_value *tree = qa_q3_renderer_font_registry_capture_checkpoint(&registry);
    CHECK(tree != NULL);
    if (tree != NULL) {
      const qa_save_value *kind = qa_save_value_get(tree, "freeType");
      CHECK(kind != NULL && kind->text != NULL && strcmp(kind->text, "ready") == 0);
      const qa_save_value *generated = qa_save_value_get(tree, "generated");
      CHECK(generated != NULL && generated->item_count >= 1);
      if (generated != NULL && generated->item_count >= 1) {
        const qa_save_value *rgba = qa_save_value_get(generated->items[0], "rgba");
        CHECK(rgba != NULL && rgba->byte_length == 256u * 256u * 4u);
      }
      CHECK_ABORTS(restore_without_generation(tree));
      /* Restore reuses generated pictures, not register_picture. */
      struct fake_reader reader2 = {false, 0, 0, 0, ""};
      struct fake_pictures pictures2 = {0};
      struct fake_generation generation2 = {0};
      qa_q3_font_generation_services services2 = generation_of(&generation2);
      qa_q3_renderer_font_registry restored;
      qa_q3_renderer_font_registry_init(&restored, reader_of(&reader2),
                                        fake_register_picture, &pictures2, fake_sync,
                                        &pictures2, &services2);
      CHECK(qa_q3_renderer_font_registry_restore_checkpoint(&restored, tree));
      CHECK(generation2.images >= 1);
      CHECK(pictures2.calls == 0);
      qa_q3_renderer_font_registry_destroy(&restored);
      qa_save_value_free(tree);
    }
    qa_q3_renderer_font_registry_destroy(&registry);
  }

  free(ttf_bytes);
  printf("test_text_registry: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
