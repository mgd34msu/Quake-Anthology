/*
 * Port of tests/text/server-localization.test.ts and the localization
 * cases in tests/text/accessibility.test.ts /
 * presentation.test.ts, plus synthetic q3-font record and metric
 * cases (the TS q3-font cases need corpus font DATs). Server/overlay
 * resource loading (localization-resources.ts) is not in this slice;
 * its reload+merge sequence is reproduced directly on the table.
 * RangeError/BinaryError cases run in a child and must abort.
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

#include "qa/text/localization.h"
#include "qa/text/q3_font.h"

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

static qa_byte_view bytes_of(const char *text) {
  return (qa_byte_view){(const uint8_t *)text, strlen(text)};
}
static bool eq_free(char *got, const char *want) {
  bool ok = got != NULL && strcmp(got, want) == 0;
  free(got);
  return ok;
}
static char *loc(qa_localization_table *t, const char *base, const char *const *args,
                 size_t n) {
  return qa_localization_table_localize(t, base, args, n, true, 1024, NULL);
}

struct log_capture {
  int warns;
  int infos;
  char last[256];
};
static void capture_warn(void *ctx, const char *text) {
  struct log_capture *cap = (struct log_capture *)ctx;
  cap->warns++;
  snprintf(cap->last, sizeof cap->last, "%s", text);
}
static void capture_info(void *ctx, const char *text) {
  struct log_capture *cap = (struct log_capture *)ctx;
  cap->infos++;
  snprintf(cap->last, sizeof cap->last, "%s", text);
}

static void test_server_overlay(void) {
  qa_localization_table table;
  qa_localization_table_init(&table, QA_LOC_Q1_RERELEASE);
  CHECK(qa_localization_table_reload(&table,
          bytes_of("m_bot_chat_connected_0=\"Hello\"\n"
                   "m_bot_chat_connected_1=\"Ready\\nNow\"\nplain=\"Fallback\""),
          NULL) == 3);
  qa_localization_table_merge(&table, bytes_of("m_bot_chat_connected_0=\"Bonjour\""), NULL);
  qa_localization_table_merge(&table, bytes_of("m_bot_chat_connected_0=\"Salut\""), NULL);
  CHECK(eq_free(qa_localization_table_lookup(&table, "m_bot_chat_connected_0", NULL, 0),
                "Salut"));
  CHECK(eq_free(qa_localization_table_lookup(&table, "$m_bot_chat_connected_1", NULL, 0),
                "Ready\nNow"));
  CHECK(qa_localization_table_lookup(&table, "m_bot_chat_connected_2", NULL, 0) == NULL);
  CHECK(eq_free(qa_localization_table_lookup(&table, "$plain", NULL, 0), "Fallback"));
  CHECK(qa_localization_table_lookup(&table, "unknown", NULL, 0) == NULL);
  qa_localization_table_destroy(&table);

  qa_localization_table_init(&table, QA_LOC_Q1_RERELEASE);
  CHECK(qa_localization_table_reload(&table, (qa_byte_view){NULL, 0}, NULL) == 0);
  CHECK(qa_localization_table_size(&table) == 0);
  CHECK(qa_localization_table_lookup(&table, "$m_bot_chat_connected_0", NULL, 0) == NULL);
  qa_localization_table_destroy(&table);
}

static void test_accessibility_overlay_and_catalog(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "localization-compatibility"));
  qa_seat_id seat = qa_identity_owner_seat(&owner, 1);
  qa_localization_catalog catalog;
  qa_localization_catalog_init(&catalog, seat, QA_LOC_Q2_RERELEASE);
  CHECK(qa_same_seat(catalog.seat, seat));
  CHECK(strcmp(qa_localization_table_profile_name(&catalog.table), "q2-rerelease") == 0);
  qa_localization_table_reload(&catalog.table, bytes_of("line=\"Hello {0}\""), NULL);
  const char *args[] = {"Marine"};
  CHECK(eq_free(loc(&catalog.table, "$line", args, 1), "Hello Marine"));
  qa_localization_catalog_destroy(&catalog);

  qa_localization_table_init(&catalog.table, QA_LOC_Q1_RERELEASE);
  qa_localization_table_reload(&catalog.table,
                               bytes_of("title = \"Choose\"\nfallback = \"Back\""), NULL);
  qa_localization_table_merge(&catalog.table, bytes_of("title = \"Choisir\""), NULL);
  qa_localization_table_merge(&catalog.table,
                              bytes_of("title = \"\xc3\x89quipe\\nChoisir\""), NULL);
  CHECK(eq_free(loc(&catalog.table, "$title", NULL, 0), "\xc3\x89quipe\nChoisir"));
  CHECK(eq_free(loc(&catalog.table, "$fallback", NULL, 0), "Back"));
  qa_localization_table_destroy(&catalog.table);
  qa_identity_owner_destroy(&owner);
}

static void test_localization_semantics(void) {
  qa_localization_table q1, q2;
  qa_localization_table_init(&q1, QA_LOC_Q1_RERELEASE);
  qa_localization_table_init(&q2, QA_LOC_Q2_RERELEASE);
  qa_localization_table_reload(&q1, bytes_of("key=\"first\"\nkey=\"last\""), NULL);
  qa_localization_table_reload(&q2, bytes_of("key=\"first\"\nkey=\"last\""), NULL);
  CHECK(eq_free(loc(&q1, "$key", NULL, 0), "first"));
  CHECK(eq_free(loc(&q2, "$key", NULL, 0), "last"));

  qa_localization_table_reload(&q1,
      bytes_of("mod_name=\"Player\"\nmod_message=\"{0}: {1}\"\nseq=\"{} and {}\""),
      NULL);
  const char *nested[] = {"$mod_name", "Test"};
  CHECK(eq_free(loc(&q1, "$mod_message", nested, 2), "Player: Test"));
  CHECK(eq_free(loc(&q2, "$mod_name", NULL, 0), "mod_name"));
  const char *pair[] = {"a", "b"};
  CHECK(eq_free(loc(&q1, "$seq", pair, 2), "a and b"));
  CHECK(eq_free(loc(&q1, "{1} {0}", pair, 2), "b a"));

  struct log_capture cap = {0, 0, ""};
  qa_lib_log log = {capture_warn, capture_info, &cap};
  char *short_args = qa_localization_table_localize(&q1, "$mod_message", pair, 1, true,
                                                    1024, &log);
  CHECK(eq_free(short_args, "mod_message"));
  CHECK(cap.warns == 1);
  CHECK(strstr(cap.last, "too few arguments") != NULL);

  CHECK(strcmp(qa_loc_language_from_locale("ru_RU.UTF-8"), "russian") == 0);
  CHECK(strcmp(qa_loc_language_from_locale(NULL), "english") == 0);
  CHECK(strcmp(qa_loc_language_from_locale("fr-CA"), "french") == 0);
  CHECK(strcmp(qa_loc_language_from_locale("xx_YY"), "english") == 0);
  CHECK(strcmp(qa_loc_known_languages[0], "english") == 0);

  char *cut = qa_localization_table_localize(&q1, "\xc3\xa9\xc3\xa9", NULL, 0, true, 4,
                                             NULL);
  CHECK(eq_free(cut, "\xc3\xa9"));
  size_t out_length = 0;
  uint8_t *raw = qa_localization_table_localize_bytes(&q1, "\xc3\xa9\xc3\xa9", NULL, 0,
                                                      true, 4, &out_length);
  CHECK(raw != NULL && out_length == 3 && raw[0] == 0xc3 && raw[1] == 0xa9 &&
        raw[2] == 0xc3);
  free(raw);

  /* Comments, unquoted tokens, platform tags. */
  qa_loc_reload_options win = {"WIN32", NULL, QA_LOC_DUPLICATES_UNSPECIFIED};
  qa_localization_table_reload(
      &q1, bytes_of("// note\na=plain /* block */ \nb <win32> = \"yes\"\nc <linux> = \"no\""),
      &win);
  CHECK(eq_free(loc(&q1, "$a", NULL, 0), "plain"));
  CHECK(eq_free(loc(&q1, "$b", NULL, 0), "yes"));
  CHECK(qa_localization_table_find(&q1, "c") == NULL);

  /* loadOrdered falls back when the primary base is null. */
  qa_loc_load_tier primary = {false, {NULL, 0}, NULL, 0};
  qa_byte_view mods[] = {bytes_of("k=\"mod\"")};
  qa_loc_load_tier fallback = {true, bytes_of("k=\"base\"\nonly=\"x\""), mods, 1};
  CHECK(qa_localization_table_load_ordered(&q1, primary, fallback, NULL) == 2);
  CHECK(eq_free(loc(&q1, "$k", NULL, 0), "mod"));

  CHECK_ABORTS((void)qa_localization_table_localize(&q1, "x", NULL, 0, true, -1, NULL));
  qa_localization_table_destroy(&q1);
  qa_localization_table_destroy(&q2);
}

static void put_i32(uint8_t *bytes, size_t offset, int32_t value) {
  uint32_t bits = (uint32_t)value;
  for (int i = 0; i < 4; i++) bytes[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}
static void put_f32(uint8_t *bytes, size_t offset, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  put_i32(bytes, offset, (int32_t)bits);
}
static void build_font_record(uint8_t *bytes) {
  memset(bytes, 0, QA_Q3_FONT_RECORD_BYTES);
  for (int i = 0; i < 256; i++) {
    size_t off = (size_t)i * 80;
    put_i32(bytes, off + 0, 10);
    put_i32(bytes, off + 4, 8);
    put_i32(bytes, off + 8, -2);
    put_i32(bytes, off + 12, 6);
    put_i32(bytes, off + 16, 7);
    put_i32(bytes, off + 20, 6);
    put_i32(bytes, off + 24, 10);
    put_f32(bytes, off + 28, 0.25f);
    put_f32(bytes, off + 32, 0.5f);
    put_f32(bytes, off + 36, 0.75f);
    put_f32(bytes, off + 40, 1.0f);
    memcpy(bytes + off + 48, "gfx/font", 8);
  }
  put_f32(bytes, 20480, 1.5f);
  memcpy(bytes + 20484, "fonts/test.dat", 14);
}

static void test_font_data(void) {
  static uint8_t record[QA_Q3_FONT_RECORD_BYTES];
  build_font_record(record);
  qa_q3_font_data data;
  CHECK(qa_q3_parse_font_data(record, sizeof record, NULL, &data));
  CHECK(strcmp(data.name, "fonts/test.dat") == 0);
  CHECK(data.glyph_scale == 1.5);
  CHECK(data.glyphs[65].height == 10 && data.glyphs[65].x_skip == 7);
  CHECK(data.glyphs[65].s == 0.25 && data.glyphs[65].t2 == 1.0);
  CHECK(strcmp(data.glyphs[255].shader_name, "gfx/font") == 0);

  static uint8_t bad[QA_Q3_FONT_RECORD_BYTES];
  build_font_record(bad);
  put_i32(bad, 0, -1);
  CHECK_ABORTS((void)qa_q3_parse_font_data(bad, sizeof bad, NULL, &data));
  build_font_record(bad);
  put_f32(bad, 28, NAN);
  CHECK_ABORTS((void)qa_q3_parse_font_data(bad, sizeof bad, NULL, &data));
  build_font_record(bad);
  put_f32(bad, 20480, 0.0f);
  CHECK_ABORTS((void)qa_q3_parse_font_data(bad, sizeof bad, NULL, &data));
  CHECK_ABORTS((void)qa_q3_read_font_data(record, 100, NULL, &data));
}

static void fill_font(qa_q3_registered_font *font, int32_t x_skip, int32_t height) {
  memset(font, 0, sizeof *font);
  font->glyph_scale = 1;
  font->glyph_count = 256;
  for (int i = 0; i < 256; i++) {
    font->glyphs[i].metrics.x_skip = x_skip;
    font->glyphs[i].metrics.height = height;
  }
}

static void test_font_metrics(void) {
  qa_q3_atlas_metric space = qa_q3_prop_metric(' ');
  CHECK(space.values[0] == 0 && space.values[2] == 8);
  qa_q3_atlas_metric upper = qa_q3_prop_metric('A');
  qa_q3_atlas_metric lower = qa_q3_prop_metric('a');
  CHECK(upper.values[0] == 5 && upper.values[2] == 18);
  CHECK(memcmp(upper.values, lower.values, sizeof upper.values) == 0);
  CHECK(qa_q3_prop_metric(31).values[2] == -1);
  CHECK(qa_q3_prop_metric('~').values[0] == 79);
  CHECK(qa_q3_prop_metric('`').values[2] == 5);
  CHECK(qa_q3_proportional_string_width("QUAKE") >
        qa_q3_proportional_string_width("I"));
  CHECK(qa_q3_proportional_string_width("I") == 8);
  CHECK(qa_q3_proportional_string_width("") == -3);
  CHECK(qa_q3_banner_string_width("AB") == 68);
  CHECK(qa_q3_banner_string_width("a") == -4);

  static qa_q3_registered_font small, normal, big;
  fill_font(&small, 1, 5);
  fill_font(&normal, 10, 10);
  fill_font(&big, 100, 20);
  qa_q3_font_set ui = {&small, &normal, &big, QA_Q3_FONT_UI, 0.5, 2};
  CHECK(qa_q3_text_width(&ui, "AA", 0.5, 0) == 1);
  CHECK(qa_q3_text_width(&ui, "AA", 1, 0) == 20);
  CHECK(qa_q3_text_width(&ui, "A", 2, 0) == 200);
  CHECK(qa_q3_text_width(&ui, "^1A", 1, 0) == 10);
  CHECK(qa_q3_text_width(&ui, "AAA", 1, 2) == 20);
  CHECK(qa_q3_text_height(&ui, "AA", 1, 0) == 10);
  qa_q3_font_set cgame = ui;
  cgame.profile = QA_Q3_FONT_CGAME;
  CHECK(qa_q3_text_width(&cgame, "A", 2, 0) == 20);
  CHECK_ABORTS((void)qa_q3_text_width(&ui, "A", NAN, 0));
  normal.glyph_count = 65;
  CHECK_ABORTS((void)qa_q3_text_width(&ui, "B", 1, 0));
  normal.glyph_count = 256;
}

struct draw_log {
  int stretches;
  int colors;
  qa_rect rects[32];
  qa_text_texture_rect uvs[32];
  qa_vec4 last_color;
};
static void log_set_color(void *ctx, const qa_vec4 *color) {
  struct draw_log *log = (struct draw_log *)ctx;
  log->colors++;
  if (color != NULL) log->last_color = *color;
}
static void log_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv,
                        const qa_picture_asset *picture) {
  (void)picture;
  struct draw_log *log = (struct draw_log *)ctx;
  if (log->stretches < 32) {
    log->rects[log->stretches] = rect;
    log->uvs[log->stretches] = uv;
  }
  log->stretches++;
}
static qa_draw2d log_draw(struct draw_log *log) {
  qa_text_draw_sink sink;
  memset(&sink, 0, sizeof sink);
  sink.ctx = log;
  sink.target = (qa_rect){0, 0, 640, 480};
  sink.set_color = log_set_color;
  sink.stretch_pixels = log_stretch;
  qa_draw2d draw = {sink, QA_COORD_PIXELS};
  return draw;
}
static bool fake_picture(void *ctx, const char *path, const char *mode,
                         qa_picture_asset *out) {
  (void)mode;
  struct draw_log *log = (struct draw_log *)ctx;
  log->colors++;
  memset(out, 0, sizeof *out);
  out->kind = QA_PICTURE_IMAGE;
  out->image.name = path;
  return true;
}
static const void *fake_font(void *ctx, const char *path, double size,
                             qa_font_print_fn print, void *print_ctx) {
  (void)path;
  (void)size;
  (void)print;
  (void)print_ctx;
  return ctx;
}
static void fake_print(void *ctx, const char *text) {
  (void)ctx;
  (void)text;
}

static void test_font_draw(void) {
  qa_picture_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_IMAGE;
  asset.image.name = "charset";
  struct draw_log log;
  qa_q3_legacy_fonts legacy;
  memset(&legacy, 0, sizeof legacy);
  legacy.proportional = asset;
  legacy.banner = asset;
  legacy.glow = asset;

  memset(&log, 0, sizeof log);
  qa_draw2d draw = log_draw(&log);
  qa_q3_draw_char(&draw, &asset, 10, 20, 16, 16, ' ');
  CHECK(log.stretches == 0);
  qa_q3_draw_char(&draw, &asset, 10, 20, 16, 16, 'A');
  CHECK(log.stretches == 1 && log.rects[0].x == 10 && log.uvs[0].s == 1.0 / 16 &&
        log.uvs[0].t == 4.0 / 16);

  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  qa_q3_fixed_text_options fixed = {0, 0, "A^1B", {1, 1, 1, 1}, 8, 8, 0, false, false};
  qa_q3_draw_cg_string(&draw, &asset, &fixed);
  CHECK(log.stretches == 2 && log.rects[1].x == 8);
  CHECK(log.last_color.x == 1 && log.last_color.y == 0);

  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  qa_q3_ui_text_options ui = {100, 0, "AB", {1, 1, 1, 1}, QA_UI_BLINK, 200};
  qa_q3_draw_ui_string(&draw, &asset, &ui);
  CHECK(log.stretches == 0 && log.colors == 0);
  ui.style = QA_UI_CENTER;
  qa_q3_draw_ui_string(&draw, &asset, &ui);
  CHECK(log.stretches == 2 && log.rects[0].x == 84 && log.rects[1].x == 100);

  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  ui = (qa_q3_ui_text_options){0, 0, "A", {1, 1, 1, 1}, QA_UI_LEFT, 0};
  qa_q3_draw_proportional_string(&draw, &legacy, &ui);
  CHECK(log.stretches == 1 && log.rects[0].width == 18 && log.rects[0].height == 27 &&
        log.uvs[0].s == 5.0 / 256);
  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  qa_q3_draw_banner_string(&draw, &legacy, &ui);
  CHECK(log.stretches == 1 && log.rects[0].width == 33 && log.rects[0].height == 36);

  static qa_q3_registered_font small, normal, big;
  fill_font(&small, 1, 5);
  fill_font(&normal, 10, 10);
  fill_font(&big, 100, 20);
  for (int i = 0; i < 256; i++) {
    normal.glyphs[i].picture = &asset;
    normal.glyphs[i].metrics.top = 8;
    normal.glyphs[i].metrics.image_width = 6;
    normal.glyphs[i].metrics.image_height = 10;
  }
  qa_q3_font_set set = {&small, &normal, &big, QA_Q3_FONT_UI, 0.5, 2};
  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  qa_q3_text_paint_options paint = {5, 20, 1, {1, 1, 1, 1}, "AB", 0, 0, 0};
  qa_q3_text_paint(&draw, &set, &paint);
  CHECK(log.stretches == 2 && log.rects[0].x == 5 && log.rects[0].y == 12 &&
        log.rects[1].x == 15);
  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  qa_q3_text_cursor cursor = {1, '_', 0};
  qa_q3_text_paint_with_cursor(&draw, &set, &paint, cursor);
  CHECK(log.stretches == 3);
  memset(&log, 0, sizeof log);
  draw = log_draw(&log);
  CHECK(qa_q3_text_paint_limit(&draw, &set, &paint, 0) == 0 && log.stretches == 0);
  CHECK(qa_q3_text_paint_limit(&draw, &set, &paint, 1000) == 25);

  memset(&log, 0, sizeof log);
  qa_font_asset_services services = {&log, fake_picture, fake_font};
  /* fake_font returns its ctx; swap ctx after the picture calls below. */
  qa_q3_ui_asset_registry registry;
  qa_q3_ui_asset_registry_init(&registry, services, fake_print, NULL);
  qa_q3_legacy_fonts loaded;
  CHECK(qa_q3_ui_asset_registry_load_legacy_fonts(&registry, &loaded));
  CHECK(log.colors == 4);
  CHECK(strcmp(loaded.charset.image.name, "gfx/2d/bigchars") == 0);
  CHECK(strcmp(loaded.banner.image.name, "menu/art/font2_prop.tga") == 0);
  registry.resources.ctx = &normal;
  CHECK(qa_q3_ui_asset_registry_register_font(&registry, NULL, 16) == &normal);
}

int main(void) {
  test_server_overlay();
  test_accessibility_overlay_and_catalog();
  test_localization_semantics();
  test_font_data();
  test_font_metrics();
  test_font_draw();
  printf("text_b: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
