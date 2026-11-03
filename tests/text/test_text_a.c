/*
 * Port of the slice-a cases in tests/text/world.test.ts,
 * sound-captions.test.ts and presentation.test.ts (caption timeline,
 * world text store, classic charset/layout/draw commands, kfont
 * registry loads, live-language media captions). RangeError/Error
 * cases run in a child process and must abort. GPL-2.0-or-later.
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

#include "qa/contracts/identity.h"
#include "qa/formats/images/tga.h"
#include "qa/text.h"

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

static char *dup_bytes(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

static qa_renderer_image test_image(qa_identity_owner *owner, double ordinal,
                                    double width, double height) {
  qa_renderer_image image = {0};
  image.owner.identity = owner;
  image.owner.session = owner->session;
  image.owner.generation = 0;
  image.ordinal = ordinal;
  image.source_is_resource = false;
  image.source_name = "test";
  image.width = width;
  image.height = height;
  return image;
}

static qa_world_text source_text(void) {
  qa_world_text text = {0};
  text.content = "q2:rerelease:baseq2:test";
  text.text = "A\nBC";
  text.origin = (qa_vec3){32, 0, 8};
  text.color = (qa_vec4){1, 0.5, 0.25, 0.5};
  text.cell_size = 8;
  text.font_selected = false;
  text.depth_test = true;
  text.orientation_fixed = false;
  return text;
}

static void test_world_store(void) {
  qa_world_text source = source_text();
  qa_world_text_store store;
  qa_world_text_store_init(&store);
  size_t count = 0;

  qa_world_text submitted = source;
  CHECK(qa_world_text_store_submit(&store, &submitted, 4, 0.1));
  submitted.origin.x = -32; /* caller copy must not leak into the store */
  const qa_world_text *const *first =
      qa_world_text_store_snapshot(&store, 4, 1, &count);
  CHECK(count == 1 && first[0]->origin.x == 32 &&
        strcmp(first[0]->text, "A\nBC") == 0);
  qa_world_text_store_snapshot(&store, 4, 1, &count);
  CHECK(count == 1);
  qa_world_text_store_snapshot(&store, 4.09, 2, &count);
  CHECK(count == 1);
  qa_world_text_store_snapshot(&store, 4.1, 3, &count);
  CHECK(count == 0);

  CHECK(qa_world_text_store_submit(&store, &source, 4.2, 0));
  qa_world_text_store_snapshot(&store, 4.2, 4, &count);
  CHECK(count == 1);
  qa_world_text_store_snapshot(&store, 4.2, 4, &count);
  CHECK(count == 1);
  qa_world_text_store_snapshot(&store, 4.2, 5, &count);
  CHECK(count == 0);

  CHECK(qa_world_text_store_submit(&store, &source, 4.3, 0));
  qa_world_text positive = source;
  positive.text = "positive";
  CHECK(qa_world_text_store_submit(&store, &positive, 4.4, 0.1));
  const qa_world_text *const *late =
      qa_world_text_store_snapshot(&store, 4.8, 6, &count);
  CHECK(count == 1 && strcmp(late[0]->text, "A\nBC") == 0);
  qa_world_text_store_snapshot(&store, 4.8, 7, &count);
  CHECK(count == 0);

  CHECK(qa_world_text_store_submit(&store, &source, 5, 1));
  qa_world_text_store_clear(&store);
  qa_world_text_store_snapshot(&store, 5, 8, &count);
  CHECK(count == 0);

  qa_world_text bad = source;
  bad.has_distance_cull_factor = true;
  bad.distance_cull_factor = NAN;
  CHECK_ABORTS({
    qa_world_text_store s;
    qa_world_text_store_init(&s);
    qa_world_text_store_submit(&s, &bad, 0, 1);
  });
  bad.distance_cull_factor = -1;
  CHECK(qa_world_text_store_submit(&store, &bad, 0, 1));
  qa_world_text_store_destroy(&store);
}

struct capture {
  qa_render_command commands[32];
  size_t count;
};
static void capture_emit(void *ctx, const qa_render_command *command) {
  struct capture *cap = (struct capture *)ctx;
  cap->commands[cap->count++] = *command;
}
static void capture_material(void *ctx, const qa_material_text_draw *draw) {
  (void)ctx;
  (void)draw;
  failures++; /* unexpected material glyph */
}

static void test_atlas_and_layout(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "text-a-atlas"));
  qa_renderer_image image = test_image(&owner, 0, 128, 128);
  qa_text_atlas charset;
  CHECK(qa_classic_charset(image, NULL, true, &charset));
  CHECK(charset.line_height == 8 && charset.glyph_count == 256);
  const qa_atlas_glyph *letter_a = qa_text_atlas_glyph(&charset, 'A');
  CHECK(letter_a != NULL && letter_a->x == 8 && letter_a->y == 32 &&
        letter_a->width == 8 && letter_a->advance == 8 && letter_a->color);
  CHECK_ABORTS({
    qa_text_atlas bad;
    qa_classic_charset(test_image(&owner, 1, 100, 128), "bad", true, &bad);
  });

  qa_text_font_selection selection = {0};
  selection.classic = &charset;
  qa_resolved_text_glyph resolved = qa_resolve_text_glyph(&selection, 'A', false);
  CHECK(resolved.codepoint == 65 && resolved.visible);
  qa_resolved_text_glyph space = qa_resolve_text_glyph(&selection, ' ', false);
  CHECK(!space.visible);
  /* Beyond Latin-1 with no unicode atlas falls back to classic '?'. */
  qa_resolved_text_glyph fallback =
      qa_resolve_text_glyph(&selection, 0x410, false);
  CHECK(fallback.codepoint == 63 && fallback.visible);
  qa_text_texture_rect uv = qa_glyph_uv(&resolved);
  CHECK(uv.s == 8.0 / 128 && uv.t2 == 40.0 / 128);

  size_t point_count = 0;
  int64_t *points = qa_text_codepoints("abca\xc3\xa9", 6, &point_count);
  CHECK(points != NULL && point_count == 4 && points[3] == 0xe9);
  free(points);

  /* Tinted glyphs take the Q3 palette color (presentation.test.ts
   * uses the "tinted" classic charset the same way). */
  qa_text_atlas tinted;
  CHECK(qa_classic_charset(image, "classic", false, &tinted));
  qa_text_font_selection tinted_selection = {0};
  tinted_selection.classic = &tinted;
  qa_text_layout_options options = {0};
  options.font = tinted_selection;
  options.scale = 1;
  options.color = (qa_vec4){1, 1, 1, 1};
  options.color_codes = QA_COLOR_CODES_Q3;
  options.text = "^1AB CD\nE";
  options.text_length = strlen(options.text);
  options.has_max_width = true;
  options.max_width = 24;
  qa_text_layout layout;
  CHECK(qa_layout_text(&options, &layout));
  CHECK(layout.line_count == 3); /* "AB" / "CD" / "E" after the wrap */
  CHECK(layout.glyph_count == 6);
  const qa_positioned_glyph *first_glyph = &layout.lines[0].glyphs[0];
  CHECK(first_glyph->glyph.codepoint == 'A');
  CHECK(first_glyph->color.x == 1 && first_glyph->color.y == 0 &&
        first_glyph->color.z == 0 && first_glyph->color.w == 1);
  CHECK(first_glyph->rect.width == 8 && layout.height == 24);
  CHECK_ABORTS({
    qa_text_layout_options bad = options;
    bad.scale = 0;
    qa_text_layout out;
    qa_layout_text(&bad, &out);
  });

  /* Draw through a TextCommandSink: first stretch-pic lands at the
   * target offset plus the layout origin, as in presentation.test.ts. */
  struct capture capture = {0};
  qa_seat_id seat = qa_identity_owner_seat(&owner, 1);
  qa_rect target = {640, 0, 640, 480};
  qa_text_command_sink sink;
  qa_text_command_sink_init(&sink, seat, target, capture_emit, &capture,
                            capture_material, NULL);
  qa_draw2d draw = {qa_text_command_sink_sink(&sink), QA_COORD_PIXELS};
  qa_draw_text_layout(&draw, &layout, (qa_vec2){10, 20}, 0);
  double pic_x = -1;
  for (size_t i = 0; i < capture.count; i++) {
    if (capture.commands[i].kind == QA_RENDER_COMMAND_STRETCH_PIC) {
      pic_x = capture.commands[i].rect.x;
      break;
    }
  }
  CHECK(pic_x == 650);
  qa_text_layout_destroy(&layout);

  qa_rect viewport = {0, 0, 1600, 900};
  qa_draw2d scaled = {qa_text_command_sink_sink(&sink), QA_COORD_BASE_UI_640};
  scaled.commands.target = viewport;
  CHECK(qa_draw2d_scale_x(&scaled) == (double)(float)(900 * (double)(float)(1.0 / 480)));
  CHECK(fabs(qa_draw2d_bias_x(&scaled) - 200) < 0.01);

  qa_text_atlas_destroy(&tinted);
  qa_text_atlas_destroy(&charset);
  qa_identity_owner_destroy(&owner);
}

static void test_kfont_parse(void) {
  const char *text =
      "texture \"fonts/qfont.png\"\n"
      "unicode\n"
      "mapchar {\n"
      "65 0 0 8 8 0\n"
      "66 8 0 6 10 0\n"
      "1040 16 0 9 12 0\n"
      "}\n";
  qa_parsed_kfont parsed;
  CHECK(qa_parse_kfont(text, &parsed));
  CHECK(strcmp(parsed.texture_token, "fonts/qfont.png") == 0);
  CHECK(parsed.line_height == 12);
  CHECK(parsed.glyph_count == 3);
  const qa_kfont_char *cyrillic = qa_parsed_kfont_glyph(&parsed, 1040);
  CHECK(cyrillic != NULL && cyrillic->w == 9);
  qa_kfont font = qa_kfont_from_parsed(&parsed, "fonts/qfont");
  const qa_kfont_char *b = qa_scr_kfont_lookup(&font, 66);
  CHECK(b != NULL && b->w == 6);
  CHECK(qa_scr_kfont_lookup(&font, 1040) == NULL);
  CHECK(qa_kfont_glyph(&font, 1040) != NULL);
  CHECK(qa_kfont_glyph(&font, 67) == NULL);
  qa_parsed_kfont_destroy(&parsed);

  qa_parsed_kfont missing;
  CHECK(!qa_parse_kfont("mapchar {\n65 0 0 8 8 0\n}\n", &missing));

}

struct registry_files {
  qa_identity_owner *owner;
  uint8_t *tga;
  size_t tga_length;
  int released;
};
static uint8_t *registry_read(void *ctx, const char *path, size_t *out_length) {
  struct registry_files *files = (struct registry_files *)ctx;
  const char *kfont =
      "texture \"fonts/qfont.tga\"\nmapchar {\n65 0 0 8 8 0\n66 8 0 6 8 0\n}\n";
  if (strcmp(path, "fonts/qfont.kfont") == 0) {
    *out_length = strlen(kfont);
    return (uint8_t *)dup_bytes(kfont);
  }
  if (strcmp(path, "fonts/qfont.tga") == 0) {
    uint8_t *copy = (uint8_t *)malloc(files->tga_length);
    if (copy != NULL) memcpy(copy, files->tga, files->tga_length);
    *out_length = files->tga_length;
    return copy;
  }
  return NULL;
}
static qa_renderer_image registry_register(void *ctx, const char *name,
                                           const qa_render_image *content) {
  struct registry_files *files = (struct registry_files *)ctx;
  (void)name;
  return test_image(files->owner, 7, content->levels[0].width,
                    content->levels[0].height);
}
static void registry_release(void *ctx, qa_renderer_image image) {
  (void)image;
  ((struct registry_files *)ctx)->released++;
}

static void test_registry_kfont(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "text-a-registry"));
  uint8_t pixels[32 * 16 * 4] = {0};
  for (size_t i = 0; i < sizeof pixels; i += 4) pixels[i + 3] = 255;
  qa_image_level level = {32, 16, {pixels, sizeof pixels}};
  size_t tga_length = 0;
  uint8_t *tga = qa_encode_tga(&level, &tga_length);
  CHECK(tga != NULL);
  struct registry_files files = {&owner, tga, tga_length, 0};
  qa_font_image_services services = {&files, registry_read, registry_register,
                                     registry_release};
  qa_text_font_registry registry;
  qa_text_font_registry_init(&registry, services);
  bool oom = false;
  const qa_text_atlas *font =
      qa_text_font_registry_load_kfont(&registry, "fonts/qfont.kfont", &oom);
  CHECK(!oom && font != NULL);
  CHECK(font != NULL && font->glyph_count == 2 && font->line_height == 8);
  const qa_text_atlas *again =
      qa_text_font_registry_load_kfont(&registry, "fonts/qfont.kfont", &oom);
  CHECK(again == font); /* cached */
  const qa_text_atlas *absent =
      qa_text_font_registry_load_kfont(&registry, "fonts/none.kfont", &oom);
  CHECK(absent == NULL && !oom);
  CHECK_ABORTS((void)qa_text_font_registry_load_truetype(
      &registry, "fonts/x.ttf", 0, NULL, 0, &oom));
  qa_text_font_registry_close(&registry);
  CHECK(files.released == 1);
  free(tga);
  qa_identity_owner_destroy(&owner);
}

static void test_captions(void) {
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "captions"));
  qa_localization_catalog first, second;
  qa_localization_catalog_init(&first, qa_identity_owner_seat(&owner, 0),
                               QA_LOC_Q1_RERELEASE);
  qa_localization_catalog_init(&second, qa_identity_owner_seat(&owner, 1),
                               QA_LOC_Q1_RERELEASE);
  const char *en = "line=\"Follow me\"";
  const char *fr = "line=\"Suivez-moi\"";
  qa_localization_table_reload(&first.table,
                               (qa_byte_view){(const uint8_t *)en, strlen(en)},
                               NULL);
  qa_localization_table_reload(&second.table,
                               (qa_byte_view){(const uint8_t *)fr, strlen(fr)},
                               NULL);
  qa_caption_timeline a, b;
  qa_caption_timeline_init(&a, first.seat, &first);
  qa_caption_timeline_init(&b, second.seat, &second);
  const char *srt =
      "1\n00:00:01,000 --> 00:00:02,000\n$line\n\n"
      "2\n00:00:01,500 --> 00:00:03,000\nDoors opening";
  qa_caption_cue *cues = NULL;
  size_t cue_count = 0;
  CHECK(qa_parse_subtitle_text(srt, strlen(srt), "clip", NULL, NULL, &cues, &cue_count));
  CHECK(cue_count == 2 && strcmp(cues[0].id, "clip:0") == 0 &&
        cues[0].start_milliseconds == 1000 &&
        cues[0].duration_milliseconds == 1000);
  CHECK(qa_caption_timeline_replace(&a, cues, cue_count));
  CHECK(qa_caption_timeline_replace(&b, cues, cue_count));
  qa_caption_cues_free(cues, cue_count);

  qa_active_caption *active = NULL;
  size_t active_count = 0;
  CHECK(qa_caption_timeline_active_at(&a, 999, &active, &active_count));
  CHECK(active_count == 0);
  qa_active_captions_free(active, active_count);
  CHECK(qa_caption_timeline_active_at(&a, 1000, &active, &active_count));
  CHECK(active_count == 1 && strcmp(active[0].localized_text, "Follow me") == 0);
  qa_active_captions_free(active, active_count);
  CHECK(qa_caption_timeline_active_at(&b, 1000, &active, &active_count));
  CHECK(active_count == 1 && strcmp(active[0].localized_text, "Suivez-moi") == 0);
  qa_active_captions_free(active, active_count);
  CHECK(qa_caption_timeline_active_at(&a, 1500, &active, &active_count));
  CHECK(active_count == 2);
  qa_active_captions_free(active, active_count);
  CHECK(qa_caption_timeline_active_at(&a, 2000, &active, &active_count));
  CHECK(active_count == 1);
  qa_active_captions_free(active, active_count);
  CHECK(qa_caption_timeline_active_at(&a, 1000, &active, &active_count));
  CHECK(active_count == 1); /* seeking back replays the cue */
  qa_active_captions_free(active, active_count);

  CHECK_ABORTS(qa_caption_timeline_active_at(&a, NAN, &active, &active_count));
  CHECK_ABORTS({
    qa_caption_timeline wrong;
    qa_caption_timeline_init(&wrong, first.seat, &second);
  });
  CHECK_ABORTS({
    qa_caption_cue *bad = NULL;
    size_t n = 0;
    const char *text = "1\n00:99:00,000 --> 00:00:02,000\nX";
    qa_parse_subtitle_text(text, strlen(text), "bad", NULL, NULL, &bad, &n);
  });

  /* WebVTT voice speaker extraction. */
  const char *vtt = "WEBVTT\n\n00:00:00.000 --> 00:00:02.000\n<v Ann>Hi</v>";
  CHECK(qa_parse_subtitle_text(vtt, strlen(vtt), "v", NULL, NULL, &cues, &cue_count));
  CHECK(cue_count == 1 && cues[0].speaker != NULL &&
        strcmp(cues[0].speaker, "Ann") == 0 &&
        strcmp(cues[0].text, "Hi") == 0);
  qa_caption_cues_free(cues, cue_count);

  qa_caption_timeline_destroy(&a);
  qa_caption_timeline_destroy(&b);
  qa_localization_catalog_destroy(&first);
  qa_localization_catalog_destroy(&second);
  qa_identity_owner_destroy(&owner);
}

struct media_files {
  int failures;
};
static uint8_t *media_read(void *ctx, const char *path, size_t *out_length) {
  (void)ctx;
  size_t length = strlen(path);
  if (length < 4 || strcmp(path + length - 4, ".srt") != 0) return NULL;
  char buffer[128];
  snprintf(buffer, sizeof buffer,
           "1\n00:00:00,000 --> 00:00:02,000\n%s\n",
           strstr(path, "_fr") != NULL ? "Bonjour" : "Hello");
  *out_length = strlen(buffer);
  return (uint8_t *)dup_bytes(buffer);
}
static const char *live_language = "english";
static const char *read_live(void *ctx) {
  (void)ctx;
  return live_language;
}
static void live_failed(void *ctx, const char *error) {
  (void)error;
  ((struct media_files *)ctx)->failures++;
}

static void test_media_captions(void) {
  char **paths = NULL;
  size_t path_count = 0;
  CHECK(qa_subtitle_paths("intro.cin", "french", &paths, &path_count));
  CHECK(path_count == 4 && strcmp(paths[0], "intro_fr.srt") == 0 &&
        strcmp(paths[1], "intro_fr.vtt") == 0 &&
        strcmp(paths[2], "intro.srt") == 0);
  qa_subtitle_paths_free(paths, path_count);
  CHECK(qa_subtitle_paths("movies/intro.cin", "english", &paths, &path_count));
  CHECK(path_count == 2 && strcmp(paths[0], "movies/intro.srt") == 0);
  qa_subtitle_paths_free(paths, path_count);

  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "movie-language"));
  struct media_files files = {0};
  live_language = "english";
  qa_live_language live = {&files, read_live, live_failed};
  qa_seat_media_captions captions;
  qa_seat_media_captions_init(&captions, qa_identity_owner_seat(&owner, 0),
                              media_read, &files, NULL, false, &live);
  CHECK(qa_seat_media_captions_prepare(&captions, "intro.cin", "english"));
  qa_caption_playback_state state = {"intro.cin", 100, "playing"};
  qa_caption_preferences preferences = {true, true, true};
  qa_active_caption *active = NULL;
  size_t active_count = 0;
  CHECK(qa_seat_media_captions_active(&captions, state, preferences, &active,
                                      &active_count));
  CHECK(active_count == 1 && strcmp(active[0].localized_text, "Hello") == 0);
  qa_active_captions_free(active, active_count);
  live_language = "french";
  CHECK(qa_seat_media_captions_active(&captions, state, preferences, &active,
                                      &active_count));
  CHECK(active_count == 0); /* locale swapped; this call reports none */
  qa_active_captions_free(active, active_count);
  CHECK(qa_seat_media_captions_prepare(&captions, "intro.cin", "french"));
  CHECK(qa_seat_media_captions_active(&captions, state, preferences, &active,
                                      &active_count));
  CHECK(active_count == 1 && strcmp(active[0].localized_text, "Bonjour") == 0);
  qa_active_captions_free(active, active_count);
  CHECK(files.failures == 0);
  state.status = "ended";
  CHECK(qa_seat_media_captions_active(&captions, state, preferences, &active,
                                      &active_count));
  CHECK(active_count == 0);
  qa_active_captions_free(active, active_count);
  qa_seat_media_captions_clear(&captions);
  qa_seat_media_captions_destroy(&captions);
  qa_identity_owner_destroy(&owner);
}

struct mounted_files {
  int opens;
};
static bool mounted_open(void *ctx, const char *path, qa_opened_resource *out) {
  ((struct mounted_files *)ctx)->opens++;
  if (strcmp(path, "fonts/font.dat") != 0) return false;
  static const uint8_t bytes[] = {1, 2, 3, 4};
  memset(out, 0, sizeof *out);
  out->bytes.bytes = bytes;
  out->bytes.length = sizeof bytes;
  out->reference.byte_length = (int64_t)sizeof bytes;
  return true;
}

static void test_mounted_reader(void) {
  struct mounted_files files = {0};
  qa_mounted_content content = {&files, mounted_open};
  qa_mounted_font_reader reader;
  qa_mounted_font_reader_init(&reader, content);
  qa_font_file_reader services = qa_mounted_font_reader_as_reader(&reader);
  CHECK(services.read_file_length(services.ctx, "fonts/font.dat") == 4);
  CHECK(services.read_file_length(services.ctx, "fonts/none.dat") == -1);
  qa_retained_font_file file;
  CHECK(services.read_file_retained(services.ctx, "fonts/font.dat", &file));
  CHECK(file.length == 4 && file.bytes.length == 4 && file.bytes.bytes[2] == 3);
  CHECK(!services.read_file_retained(services.ctx, "fonts/none.dat", &file));
  CHECK(files.opens == 2); /* one cached open per path */
  services.free_file(services.ctx, file);
  qa_mounted_font_reader_close(&reader);
}

int main(void) {
  test_world_store();
  test_atlas_and_layout();
  test_kfont_parse();
  test_registry_kfont();
  test_captions();
  test_media_captions();
  test_mounted_reader();
  printf("text_a: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
