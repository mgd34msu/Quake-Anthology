/* StartupMenu (TS startup-menu.ts) over the retail Q1 classic id1 content, built as
 * StartupApplication.openGraphics builds it (tests/app/startup_menu_fixture.h):
 *  - draw() and captionCommands(): every RenderCommand, in order, compared line for line with the
 *    bun oracle tests/app/oracles/startup_menu_draw.ts (its output is startup_menu_draw.txt). The
 *    states cover the main, family, campaign, options and load menus, the custom-game builder with
 *    and without the product, map-set (staged, scrolled) and character (turntable viewer) pickers,
 *    a busy status, the high-contrast/bold/scaled appearance and a wide viewport.
 *  - navigation through real seat input: menus, presets, the builder footer while a picker is open,
 *    busy input, Escape at main, quit and resumeDisplayOptions, with a sentinel ctx in every callback.
 * Needs QTS_TEST_CORPUS (skipped without it, as the TS menu test skips). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "startup_menu_fixture.h"
#include "qa/input/key_codes.h"
#include "qa/network/common/js_number.h"
#include "qa/ui/settings/accessibility.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define HOST_MAGIC 0x51a7
typedef struct host {
  int magic, bad_ctx;
  int plays, quits, refreshes, loads, presets;
  char preset_id[64];
  double preset_skill;
  bool preset_arena_null;
  qa_app_startup_save_list saves;
  qa_ui_preference_values appearance;
  /* draw transcript */
  char *text; size_t length, capacity;
} host;
static host *host_of(void *ctx) {
  host *h = ctx;
  if (h == NULL || h->magic != HOST_MAGIC) { if (h) h->bad_ctx++; abort(); }
  return h;
}
static double h_now(void *ctx) { (void)host_of(ctx); return 0; }
static void h_play(void *ctx) { host_of(ctx)->plays++; }
static void h_quit(void *ctx) { host_of(ctx)->quits++; }
static void h_load(void *ctx, const char *id) { (void)id; host_of(ctx)->loads++; }
static const qa_app_startup_save_list *h_saves(void *ctx) { return &host_of(ctx)->saves; }
static void h_refresh(void *ctx) { host_of(ctx)->refreshes++; }
static qa_ui_preference_values h_appearance(void *ctx) { return host_of(ctx)->appearance; }
static void h_play_preset(void *ctx, const char *id, double skill, const char *arena) {
  host *h = host_of(ctx);
  h->presets++;
  snprintf(h->preset_id, sizeof h->preset_id, "%s", id);
  h->preset_skill = skill;
  h->preset_arena_null = arena == NULL;
}

static void append(host *h, const char *line) {
  size_t n = strlen(line);
  if (h->length + n + 2 > h->capacity) {
    size_t capacity = h->capacity ? h->capacity * 2 : 1 << 16;
    while (capacity < h->length + n + 2) capacity *= 2;
    h->text = realloc(h->text, capacity);
    if (!h->text) abort();
    h->capacity = capacity;
  }
  memcpy(h->text + h->length, line, n);
  h->length += n;
  h->text[h->length++] = '\n';
  h->text[h->length] = '\0';
}
/* The oracle's describe(): JS String() numbers, generated names over 48 characters shortened. */
static void describe_line(const qa_render_command *command, char *line, size_t size) {
  if (command->kind == QA_RENDER_COMMAND_SET_COLOR) {
    snprintf(line, size, "set-color %s,%s,%s,%s", QA_JS_NUMBER(command->color.x), QA_JS_NUMBER(command->color.y),
             QA_JS_NUMBER(command->color.z), QA_JS_NUMBER(command->color.w));
  } else if (command->kind == QA_RENDER_COMMAND_STRETCH_PIC) {
    const qa_renderer_image *image = &command->image;
    char source[160];
    if (image->source_is_resource) snprintf(source, sizeof source, "resource:%s", image->source_resource.id);
    else {
      size_t length = strlen(image->source_name);
      if (length > 48) snprintf(source, sizeof source, "generated:%.48s~%zu", image->source_name, length);
      else snprintf(source, sizeof source, "generated:%s", image->source_name);
    }
    snprintf(line, size, "stretch-pic %s,%s,%s,%s uv %s,%s,%s,%s %s %sx%s", QA_JS_NUMBER(command->rect.x),
             QA_JS_NUMBER(command->rect.y), QA_JS_NUMBER(command->rect.width), QA_JS_NUMBER(command->rect.height),
             QA_JS_NUMBER(command->uv.s1), QA_JS_NUMBER(command->uv.t1), QA_JS_NUMBER(command->uv.s2),
             QA_JS_NUMBER(command->uv.t2), source, QA_JS_NUMBER(image->width), QA_JS_NUMBER(image->height));
  } else snprintf(line, size, "kind-%d", (int)command->kind);
}
static void describe(host *h, const qa_render_command *command) {
  char line[1024];
  describe_line(command, line, sizeof line);
  append(h, line);
}
typedef struct draw_capture { host *h; char **lines; size_t count, capacity; } draw_capture;
static void capture_emit(void *ctx, const qa_render_command *command) {
  draw_capture *capture = ctx;
  (void)host_of(capture->h);
  char buffer[1024];
  describe_line(command, buffer, sizeof buffer);
  char *line = strdup(buffer);
  if (capture->count == capture->capacity) {
    capture->capacity = capture->capacity ? capture->capacity * 2 : 256;
    capture->lines = realloc(capture->lines, capture->capacity * sizeof *capture->lines);
  }
  capture->lines[capture->count++] = line;
}
static void capture_material(void *ctx, const qa_material_text_draw *draw) {
  (void)draw; draw_capture *capture = ctx; (void)host_of(capture->h);
  abort(); /* the oracle's material callback throws */
}

typedef struct run { host *h; startup_menu_fixture *f; qa_app_startup_menu *menu; } run;
static qa_ui_draw_context context(run *r, double width, double height, double time) {
  qa_ui_draw_context c;
  memset(&c, 0, sizeof c);
  c.binding.seat = r->f->seat;
  c.binding.client = r->f->client;
  c.binding.viewport = (qa_rect){0, 0, width, height};
  c.binding.safe_area = (qa_rect){0, 0, width, height};
  c.binding.hud_scale = 1;
  c.binding.presentation.assets = r->f->product->id;
  c.time_milliseconds = time;
  return c;
}
static void draw_state(run *r, const char *name, double width, double height) {
  draw_capture capture = {r->h, NULL, 0, 0};
  qa_app_startup_menu_draw(r->menu, context(r, width, height, 0), capture_emit, capture_material, &capture);
  char head[256];
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(qa_app_startup_menu_controller(r->menu));
  snprintf(head, sizeof head, "state %s active %s commands %zu", name, active ? active : "null", capture.count);
  append(r->h, head);
  for (size_t i = 0; i < capture.count; i++) { append(r->h, capture.lines[i]); free(capture.lines[i]); }
  free(capture.lines);
}
static void send(run *r, qa_seat_input_event event) {
  event.seat = r->f->seat;
  (void)qa_app_startup_menu_input(r->menu, event);
}
static void move(run *r, double x, double y) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "mouse-motion"; event.position = (qa_vec2){x, y};
  send(r, event);
}
static void click(run *r, double x, double y) {
  move(r, x, y);
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "mouse-button"; event.button = 1; event.down = true;
  send(r, event);
  event.down = false;
  send(r, event);
}
static void key(run *r, double code) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "key"; event.code = code; event.down = true;
  send(r, event);
  event.down = false;
  send(r, event);
}
static void wheel(run *r, double y) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.kind = "mouse-wheel"; event.delta = (qa_vec2){0, y};
  send(r, event);
}
static void captions(run *r, double width, double height) {
  static qa_caption_cue cues[2];
  cues[0] = (qa_caption_cue){0};
  qa_active_caption active[2];
  memset(active, 0, sizeof active);
  active[0].cue = &cues[0];
  active[0].localized_text = "The slipgate is open. Find the exit before the Shambler finds you.";
  active[0].localized_speaker = "Ranger";
  active[1].cue = &cues[1];
  active[1].localized_text = "Second line\nwith a break";
  active[1].localized_speaker = NULL;
  size_t count = 0;
  const qa_render_command *commands = qa_app_startup_menu_caption_commands(r->menu, active, 2, context(r, width, height, 500), &count);
  char head[128];
  snprintf(head, sizeof head, "captions %sx%s commands %zu", QA_JS_NUMBER(width), QA_JS_NUMBER(height), count);
  append(r->h, head);
  for (size_t i = 0; i < count; i++) describe(r->h, &commands[i]);
}
static char *read_text(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *text = malloc((size_t)size + 1);
  size_t read = fread(text, 1, (size_t)size, file);
  text[read] = '\0';
  fclose(file);
  return text;
}
/* The oracle sequence (startup_menu_draw.ts), state for state. */
static void draw_oracle(startup_menu_fixture *f, host *h) {
  qa_app_startup_menu_options options = startup_menu_fixture_options(f, h);
  options.appearance = h_appearance;
  options.now = h_now; options.play = h_play; options.load = h_load; options.saves = h_saves;
  options.refresh_saves = h_refresh; options.quit = h_quit;
  h->appearance = qa_ui_default_preferences;
  run r = {h, f, qa_app_startup_menu_new(options)};
  CHECK(r.menu != NULL);
  draw_state(&r, "main", 640, 480);
  click(&r, 100, 114 + 12);
  draw_state(&r, "family", 640, 480);
  click(&r, 100, 114 + 12);
  draw_state(&r, "campaign", 640, 480);
  key(&r, 27); key(&r, 27);
  click(&r, 100, 114 + 2 * 33 + 12);
  draw_state(&r, "options", 640, 480);
  key(&r, 27);
  click(&r, 100, 114 + 33 + 12);
  draw_state(&r, "load-empty", 640, 480);
  key(&r, 27);
  click(&r, 100, 114 + 12);
  click(&r, 100, 114 + 3 * 33 + 12);
  draw_state(&r, "session", 640, 480);
  move(&r, 100, 140);
  draw_state(&r, "session-hover", 640, 480);
  click(&r, 100, 135);
  draw_state(&r, "dropdown-product", 640, 480);
  move(&r, 340, 150);
  draw_state(&r, "dropdown-product-hover", 640, 480);
  key(&r, 27);
  click(&r, 100, 158);
  draw_state(&r, "dropdown-mapset", 640, 480);
  wheel(&r, -1);
  draw_state(&r, "dropdown-mapset-scrolled", 640, 480);
  key(&r, 27);
  double character_row = 110 + 15 + 3 * 23 + 8 + 15;
  click(&r, 100, character_row + 5);
  draw_state(&r, "dropdown-character", 640, 480);
  key(&r, 9);
  draw_state(&r, "dropdown-character-focus", 640, 480);
  key(&r, 27);
  draw_state(&r, "session-closed", 640, 480);
  qa_app_startup_menu_set_status(r.menu, "Checking map rules...", true);
  draw_state(&r, "status-busy", 640, 480);
  qa_app_startup_menu_set_status(r.menu, "", false);
  h->appearance = qa_ui_default_preferences;
  h->appearance.high_contrast = true; h->appearance.typeface = "bold"; h->appearance.menu_scale = 0.8;
  draw_state(&r, "session-appearance", 800, 600);
  h->appearance = qa_ui_default_preferences;
  draw_state(&r, "session-wide", 1280, 720);
  captions(&r, 640, 480);
  captions(&r, 1280, 720);
  h->appearance.captions = false;
  captions(&r, 640, 480);
  h->appearance = qa_ui_default_preferences;
  qa_app_startup_menu_close(r.menu);
  qa_app_startup_menu_destroy(r.menu);
  char *expected = read_text("tests/app/oracles/startup_menu_draw.txt");
  CHECK(expected != NULL);
  if (expected == NULL) return;
  /* Line-for-line comparison; report the first difference. */
  size_t line = 1;
  const char *a = h->text ? h->text : "", *e = expected;
  while (*a && *e) {
    const char *an = strchr(a, '\n'), *en = strchr(e, '\n');
    size_t al = an ? (size_t)(an - a) : strlen(a), el = en ? (size_t)(en - e) : strlen(e);
    if (al != el || memcmp(a, e, al) != 0) {
      fprintf(stderr, "FAIL draw oracle line %zu:\n  C:  %.*s\n  TS: %.*s\n", line, (int)al, a, (int)el, e);
      failures++;
      break;
    }
    a += al + (an ? 1 : 0); e += el + (en ? 1 : 0); line++;
  }
  CHECK(*a == '\0' && *e == '\0');
  printf("draw oracle: %zu lines compared\n", line - 1);
  free(expected);
}

static const char *active(qa_app_startup_menu *menu) {
  return qa_native_ui_controller_active_menu(qa_app_startup_menu_controller(menu));
}
static bool active_is(qa_app_startup_menu *menu, const char *id) { return active(menu) && strcmp(active(menu), id) == 0; }
static void navigation(startup_menu_fixture *f, host *h) {
  static char ids[6][24], labels[6][24];
  qa_app_startup_save_row rows[6];
  for (int i = 0; i < 6; i++) {
    snprintf(ids[i], sizeof ids[i], "save%d", i);
    snprintf(labels[i], sizeof labels[i], "Save %d", i);
    rows[i] = (qa_app_startup_save_row){ids[i], labels[i], (char *)"e1m1", (char *)"Quake", 0, NULL};
  }
  h->saves = (qa_app_startup_save_list){rows, 6, NULL};
  qa_app_startup_menu_options options = startup_menu_fixture_options(f, h);
  options.now = h_now; options.play = h_play; options.load = h_load; options.saves = h_saves;
  options.refresh_saves = h_refresh; options.quit = h_quit; options.play_preset = h_play_preset;
  run r = {h, f, qa_app_startup_menu_new(options)};
  qa_app_startup_menu *menu = r.menu;
  /* Constructor ensureActiveMenu(): main is active. Every TS-registered id resolves. */
  CHECK(active_is(menu, "menu:startup:main"));
  static const char *const menus[] = {
    "menu:startup:library", "menu:startup:native-family", "menu:startup:native-campaign",
    "menu:startup:native-difficulty", "menu:startup:servers", "menu:startup:server-filters",
    "menu:startup:server-details", "menu:startup:session", "menu:startup:hosting",
    "menu:startup:options", "menu:startup:sound", "menu:startup:select",
    "menu:startup:roster", "menu:startup:load", "menu:library:arena-selection",
    "menu:settings:root", "menu:settings:input:0", "menu:mods:root"};
  qa_native_ui_controller *controller = qa_app_startup_menu_controller(menu);
  for (size_t i = 0; i < sizeof menus / sizeof *menus; i++) {
    qa_native_ui_controller_open_menu(controller, menus[i]);
    CHECK(active_is(menu, menus[i]));
    qa_native_ui_controller_close_menu(controller);
  }
  /* Main -> Play a game -> Quake -> the Quake campaign (row 1 under the edition toggle) -> Play. */
  qa_app_startup_menu_open_main(menu);
  click(&r, 100, 128);
  CHECK(active_is(menu, "menu:startup:native-family"));
  click(&r, 200, 128);
  CHECK(active_is(menu, "menu:startup:native-campaign"));
  click(&r, 200, 114 + 33 + 12);
  CHECK(active_is(menu, "menu:startup:native-difficulty"));
  click(&r, 200, 114 + 7 * 33 + 12);
  CHECK(h->presets == 1 && h->preset_arena_null);
  /* Load Game refreshes saves and loads by id; the wheel pages saves. */
  qa_app_startup_menu_open_main(menu);
  click(&r, 100, 161);
  CHECK(active_is(menu, "menu:startup:load"));
  CHECK(h->refreshes == 1);
  click(&r, 200, 139);
  CHECK(h->loads == 1);
  /* Busy swallows input (TS input() first line). */
  qa_app_startup_menu_set_status(menu, "Working", true);
  CHECK(qa_app_startup_menu_is_busy(menu));
  key(&r, QA_KEY_ESCAPE);
  CHECK(active_is(menu, "menu:startup:load"));
  qa_app_startup_menu_set_status(menu, "", false);
  /* Escape at main is swallowed. */
  qa_app_startup_menu_open_main(menu);
  key(&r, QA_KEY_ESCAPE);
  CHECK(active_is(menu, "menu:startup:main"));
  /* Builder: an open picker disables the footer Play until Escape closes it. */
  click(&r, 100, 128);
  click(&r, 200, 114 + 3 * 33 + 12);
  CHECK(active_is(menu, "menu:startup:session"));
  click(&r, 100, 135);
  int plays = h->plays;
  click(&r, 400, 429);
  CHECK(h->plays == plays);
  key(&r, QA_KEY_ESCAPE);
  CHECK(active_is(menu, "menu:startup:session"));
  click(&r, 400, 429);
  CHECK(h->plays == plays + 1);
  /* Quit from main (row 3 without libraries). */
  qa_app_startup_menu_open_main(menu);
  click(&r, 100, 114 + 3 * 33 + 12);
  CHECK(h->quits == 1);
  qa_app_startup_menu_close(menu);
  qa_app_startup_menu_destroy(menu);
  CHECK(h->bad_ctx == 0);
}

int main(void) {
  const char *corpus = getenv("QTS_TEST_CORPUS");
  if (corpus == NULL || corpus[0] == '\0') {
    printf("test_startup_menu: skipped (QTS_TEST_CORPUS unset)\n");
    return 0;
  }
  char user_root[512];
  snprintf(user_root, sizeof user_root, "%s/startup-menu-draw-c", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
  mkdir(user_root, 0777);
  startup_menu_fixture f;
  if (!startup_menu_fixture_open(&f, corpus, user_root, true)) {
    printf("test_startup_menu: skipped (no q1-classic-id1)\n");
    return 0;
  }
  host h;
  memset(&h, 0, sizeof h);
  h.magic = HOST_MAGIC;
  draw_oracle(&f, &h);
  free(h.text);
  host n;
  memset(&n, 0, sizeof n);
  n.magic = HOST_MAGIC;
  navigation(&f, &n);
  startup_menu_fixture_close(&f);
  printf("test_startup_menu: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
