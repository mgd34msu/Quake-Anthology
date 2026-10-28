/*
 * Port of key cases from tests/ui (hud/powerups.test.ts,
 * hud/authored-wheel.test.ts, common/native-ui.test.ts) and
 * tests/compat/qvm/client-script.test.ts (legacy script engine)
 * for the ui/common + ui/hud modules ported so far. TypeScript
 * toThrow(RangeError) cases run in a child process and must abort.
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

#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
#include "qa/input/key_codes.h"
#include "qa/settings/server/profile.h"
#include "qa/text/draw2d.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/accessibility.h"
#include "qa/ui/settings/bindings.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/local_lobby.h"
#include "qa/ui/settings/rankings.h"
#include "qa/ui/settings/server.h"
#include "qa/ui/common/legacy/script/lexer.h"
#include "qa/ui/common/legacy/script/memory.h"
#include "qa/ui/common/legacy/script/precomp_memory.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include "qa/ui/common/legacy/script/source_memory.h"
#include "qa/ui/common/legacy/script/token_memory.h"
#include "qa/ui/common/legacy/team_arena/memory.h"
#include "qa/ui/common/accessibility.h"
#include "qa/ui/common/art_manifest.h"
#include "qa/ui/common/captions.h"
#include "qa/ui/common/layout.h"
#include "qa/ui/common/legacy/borders.h"
#include "qa/ui/common/menu_theme.h"
#include "qa/ui/common/skin.h"
#include "qa/ui/hud/powerups.h"
#include "qa/ui/hud/q1_wheel.h"
#include "qa/ui/hud/weapon.h"
#include "qa/ui/hud/wheel.h"

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

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }
static qa_rect rect(double x, double y, double w, double h) {
  qa_rect r = { x, y, w, h };
  return r;
}
static double measure8(void *ctx, const char *text, double scale) {
  (void)ctx;
  return (double)strlen(text) * 8 * scale;
}
static const char *identity_localize(void *ctx, const char *text) {
  (void)ctx;
  return text;
}

static void test_layout(void) {
  qa_ui_transform fit = qa_ui_fit(rect(0, 0, 1280, 720), 1);
  /* factor = min(2, 1.5) = 1.5; x = (1280-960)/2, y = 0 */
  CHECK(near(fit.scale, 1.5) && near(fit.x, 160) && near(fit.y, 0));
  qa_vec2 point = { 160 + 320 * 1.5, 240 * 1.5 };
  qa_vec2 back = qa_ui_point(point, fit);
  CHECK(near(back.x, 320) && near(back.y, 240));
  CHECK(qa_ui_contains(rect(10, 10, 20, 20), (qa_vec2){ 10, 29.5 }));
  CHECK(!qa_ui_contains(rect(10, 10, 20, 20), (qa_vec2){ 30, 15 }));
  qa_rect in = qa_ui_intersect(rect(0, 0, 100, 50), rect(50, 25, 100, 50));
  CHECK(near(in.x, 50) && near(in.y, 25) && near(in.width, 50)
        && near(in.height, 25));
  qa_rect none = qa_ui_intersect(rect(0, 0, 10, 10), rect(20, 20, 5, 5));
  CHECK(near(none.width, 0) && near(none.height, 0));
  qa_rect row = qa_ui_menu_row_default(2);
  CHECK(near(row.x, 64) && near(row.y, 92 + 56) && near(row.width, 512)
        && near(row.height, 28));
  CHECK_ABORTS(qa_ui_fit(rect(0, 0, 0, 480), 1));
  CHECK_ABORTS(qa_ui_fit(rect(0, 0, 640, 480), 0));
  CHECK_ABORTS(qa_ui_fit(rect(0, 0, 640, 480), NAN));
}

static void test_skin_and_accessibility(void) {
  qa_ui_skin skin = qa_ui_default_skin("resource:test:font");
  CHECK(near(skin.font_scale, 1.5) && near(skin.line_height, 14));
  CHECK(near(skin.colors.accent.y, 0.65));
  qa_ui_skin_colors hc =
      qa_ui_accessible_colors(skin.colors, true, NULL);
  CHECK(near(hc.text.x, 1) && near(hc.panel.w, 1) && near(hc.accent.z, 0));
  qa_ui_skin_colors mono =
      qa_ui_accessible_colors(skin.colors, false, "monochrome");
  CHECK(near(mono.accent.x, 1) && near(mono.focused.x, 0.25));
  qa_ui_skin_colors by =
      qa_ui_accessible_colors(skin.colors, false, "blue-yellow");
  CHECK(near(by.accent.y, 0.9) && near(by.focused.z, 0.42));
  qa_ui_skin_colors plain =
      qa_ui_accessible_colors(skin.colors, false, "standard");
  CHECK(near(plain.accent.y, 0.65));

  qa_ui_image_slice slice;
  memset(&slice, 0, sizeof slice);
  slice.resource = "resource:test:panel";
  slice.width = 100;
  slice.height = 100;
  slice.uv[1].x = 1;
  slice.uv[1].y = 1;
  slice.border_left = 10;
  slice.border_top = 10;
  slice.border_right = 10;
  slice.border_bottom = 10;
  qa_ui_draw_command out[9];
  qa_vec4 white = { 1, 1, 1, 1 };
  size_t count = qa_ui_nine_slice(&slice, rect(0, 0, 50, 40), white, out, 9);
  CHECK(count == 9);
  CHECK(out[0].kind == QA_UI_DRAW_IMAGE && near(out[0].rect.width, 10)
        && near(out[4].rect.x, 10) && near(out[4].rect.width, 30));
  slice.border_left = 60; /* 60 + 10 > width stays valid; 60+50 invalid */
  slice.border_right = 50;
  CHECK_ABORTS(qa_ui_nine_slice(&slice, rect(0, 0, 50, 40), white, out, 9));
}

static void test_art_and_theme(void) {
  CHECK(strcmp(qa_ui_menu_background.file,
               "assets/ui/menu-background.png") == 0);
  CHECK(qa_ui_menu_background.width == 1536
        && qa_ui_menu_focus_art.region_height == 633);
  CHECK(near(qa_ui_menu_text.title, 3.6) && near(qa_ui_menu_text.body, 2.3));
  qa_ui_skin skin = qa_ui_menu_skin("resource:test:font");
  CHECK(skin.has_title_font && near(skin.font_scale, 2.3)
        && near(skin.line_height, 24));
  CHECK(near(qa_ui_menu_panel_rect.width, 576));
}

static void test_captions(void) {
  qa_ui_caption_commands result;
  CHECK(qa_ui_get_caption_commands(NULL, 0, rect(0, 0, 320, 100),
                                   "resource:test:font", 1, measure8, NULL,
                                   0, 8, &result));
  CHECK(result.count == 0);
  qa_ui_caption_commands_free(&result);
  qa_active_caption caption;
  memset(&caption, 0, sizeof caption);
  caption.localized_speaker = "Guide";
  caption.localized_text = "hello world";
  CHECK(qa_ui_get_caption_commands(&caption, 1, rect(0, 0, 320, 100),
                                   "resource:test:font", 1, measure8, NULL,
                                   0, 8, &result));
  CHECK(result.count == 2);
  CHECK(result.commands[0].kind == QA_UI_DRAW_FILL
        && near(result.commands[0].color.w, 0.92));
  CHECK(result.commands[1].kind == QA_UI_DRAW_TEXT
        && strcmp(result.commands[1].text, "Guide: hello world") == 0);
  qa_ui_caption_commands_free(&result);
  /* Too small an area yields no commands. */
  CHECK(qa_ui_get_caption_commands(&caption, 1, rect(0, 0, 8, 8),
                                   "resource:test:font", 1, measure8, NULL,
                                   0, 8, &result));
  CHECK(result.count == 0);
  qa_ui_caption_commands_free(&result);
}

static void test_powerups(void) {
  qa_ui_skin skin = qa_ui_default_skin("resource:test:font");
  qa_active_powerup_timer timers[3];
  memset(timers, 0, sizeof timers);
  timers[0].label = "Quad Damage";
  timers[0].remaining_seconds = 30.25;
  timers[1].label = "Expired";
  timers[1].remaining_seconds = 0;
  timers[2].label = "Rebreather";
  timers[2].remaining_seconds = 20;
  qa_ui_powerup_commands result;
  CHECK(qa_ui_draw_powerup_timers(timers, 3, rect(0, 0, 640, 240), 200,
                                  skin, 1.5, measure8, NULL,
                                  identity_localize, NULL, &result));
  CHECK(result.count == 4); /* expired timer omitted */
  CHECK(result.commands[1].kind == QA_UI_DRAW_TEXT
        && strcmp(result.commands[1].text, "Quad Damage 31s") == 0);
  CHECK(strcmp(result.commands[3].text, "Rebreather 20s") == 0);
  qa_ui_powerup_commands_free(&result);
  CHECK(qa_ui_draw_powerup_timers(timers + 1, 1, rect(0, 0, 320, 240), 200,
                                  skin, 1.5, measure8, NULL,
                                  identity_localize, NULL, &result));
  CHECK(result.count == 0);
  qa_ui_powerup_commands_free(&result);
}

static void test_weapon(void) {
  qa_ui_hud_status_rows rows = qa_ui_hud_status_rows_for(1.5, 8);
  /* labelTop = max(25, 4 + 18 + 4) = 26; height = max(42, ceil(36.8)) */
  CHECK(near(rows.label_top, 26) && near(rows.height, 42));
  qa_ui_skin skin = qa_ui_default_skin("resource:test:font");
  qa_ui_common_weapon_hud data;
  memset(&data, 0, sizeof data);
  data.status.source.provider = "q2:official";
  data.status.label = "Shotgun";
  data.status.ammo_is_finite = true;
  data.status.ammo_count = 7;
  data.status.has_ammo_to_start = false; /* NO AMMO warning */
  data.warning = "none";
  data.measure_text = measure8;
  qa_ui_weapon_commands result;
  CHECK(qa_ui_draw_weapon_hud(&data, rect(0, 0, 160, 42), skin, 1.5, 0,
                              &result));
  bool saw_no_ammo = false, saw_count = false;
  for (size_t i = 0; i < result.count; i++) {
    if (result.commands[i].kind != QA_UI_DRAW_TEXT) continue;
    if (strcmp(result.commands[i].text, "NO AMMO") == 0) saw_no_ammo = true;
    if (strcmp(result.commands[i].text, "7") == 0) saw_count = true;
    /* Warning text uses the accent color. */
    if (saw_no_ammo)
      CHECK(near(result.commands[i].color.y, skin.colors.accent.y));
  }
  CHECK(saw_no_ammo && saw_count);
  qa_ui_weapon_commands_free(&result);
  /* q3: providers suppress the source NO AMMO warning. */
  data.status.source.provider = "q3:baseq3";
  CHECK(qa_ui_draw_weapon_hud(&data, rect(0, 0, 160, 42), skin, 1.5, 0,
                              &result));
  saw_no_ammo = false;
  for (size_t i = 0; i < result.count; i++)
    if (result.commands[i].kind == QA_UI_DRAW_TEXT
        && strcmp(result.commands[i].text, "NO AMMO") == 0)
      saw_no_ammo = true;
  CHECK(!saw_no_ammo);
  qa_ui_weapon_commands_free(&result);
}

/* ---- wheel ---- */

typedef struct wheel_fake {
  qa_ui_wheel_item items[3];
  double now;
  const char *active;
  const char *selected;
  int changed_count;
} wheel_fake;

static const qa_ui_wheel_item *fake_items(void *ctx, qa_ui_wheel_mode mode,
                                          size_t *out_count) {
  (void)mode;
  wheel_fake *fake = ctx;
  *out_count = 3;
  return fake->items;
}
static const char *fake_active(void *ctx) {
  wheel_fake *fake = ctx;
  return fake->active;
}
static void fake_select(void *ctx, const char *id, qa_ui_wheel_mode mode,
                        qa_seat_id seat) {
  (void)mode;
  (void)seat;
  wheel_fake *fake = ctx;
  fake->selected = id;
}
static double fake_now(void *ctx) {
  wheel_fake *fake = ctx;
  return fake->now;
}
static void fake_changed(void *ctx, qa_seat_id seat) {
  (void)seat;
  wheel_fake *fake = ctx;
  fake->changed_count++;
}

static void test_wheel(void) {
  wheel_fake fake;
  memset(&fake, 0, sizeof fake);
  const char *ids[3] = { "w:axe", "w:shotgun", "w:rocket" };
  for (int i = 0; i < 3; i++) {
    fake.items[i].id = ids[i];
    fake.items[i].source_ordinal = (double)i;
    fake.items[i].sort_order = (double)i;
    fake.items[i].label = ids[i];
    fake.items[i].owned = i != 2; /* rocket not owned */
    fake.items[i].has_ammo = true;
  }
  fake.active = "w:axe";
  qa_ui_weapon_wheel_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &fake;
  services.items = fake_items;
  services.active_item = fake_active;
  services.select = fake_select;
  services.now = fake_now;
  services.changed = fake_changed;
  qa_seat_weapon_wheel *wheel = qa_seat_weapon_wheel_create(&services, NULL);
  CHECK(wheel != NULL);
  CHECK(!qa_seat_weapon_wheel_is_open(wheel));
  CHECK(qa_seat_weapon_wheel_open(wheel, "weapons"));
  CHECK(qa_seat_weapon_wheel_holster(wheel));
  /* Slice 0 lies at angle 0, where dot = -y/distance: pointing
   * straight up (negative y) selects index 0. */
  qa_seat_input_event move;
  memset(&move, 0, sizeof move);
  move.kind = "mouse-motion";
  move.delta.y = -170;
  CHECK(qa_seat_weapon_wheel_input(wheel, move));
  fake.now = 100;
  qa_seat_weapon_wheel_update(wheel, 100);
  qa_ui_wheel_draw_state state = qa_seat_weapon_wheel_draw_state(wheel);
  CHECK(state.has_wheel && state.wheel.selected != NULL
        && strcmp(state.wheel.selected, "w:axe") == 0);
  qa_seat_weapon_wheel_close(wheel, true);
  CHECK(fake.selected != NULL && strcmp(fake.selected, "w:axe") == 0);
  /* Carousel cycles owned+ammo weapons only: axe -> shotgun. */
  fake.now = 200;
  qa_seat_weapon_wheel_cycle(wheel, 1);
  qa_ui_wheel_command command =
      qa_seat_weapon_wheel_command(wheel, false, 200 + 400);
  CHECK(command.holster);
  CHECK(fake.selected != NULL && strcmp(fake.selected, "w:shotgun") == 0);
  CHECK(near(qa_seat_weapon_wheel_weapon_lock_until(wheel), 600 + 300));
  qa_seat_weapon_wheel_destroy(wheel);
  CHECK_ABORTS(
      qa_ui_wheel_options bad = qa_ui_wheel_default_options();
      bad.radius = 100; /* selectionDistance 140 >= radius */
      qa_seat_weapon_wheel_destroy(
          qa_seat_weapon_wheel_create(&services, &bad)));
}

static void test_q1_wheel(void) {
  qa_ui_q1_wheel_parse parsed;
  CHECK(qa_ui_parse_q1_weapon_wheel(
      "slot 9 {\nimpulse 227\nweaponnum 65536\n}\n"
      "slot 10 {\nimpulse 228\n}\nslot 11 {\nimpulse 22\n}", &parsed));
  CHECK(parsed.error_count == 0 && parsed.slot_count == 3);
  CHECK(parsed.slots[0].has_impulse && near(parsed.slots[0].impulse, 227)
        && parsed.slots[0].has_weapon_bits
        && near(parsed.slots[0].weapon_bits, 65536));
  CHECK(near(parsed.slots[2].impulse, 22));
  qa_ui_q1_wheel_parse_free(&parsed);
  CHECK(qa_ui_parse_q1_weapon_wheel("bogus 1 {", &parsed));
  CHECK(parsed.slot_count == 0 && parsed.error_count == 1
        && strcmp(parsed.errors[0], "line 1: expected slot N {") == 0);
  qa_ui_q1_wheel_parse_free(&parsed);
  CHECK(qa_ui_parse_q1_weapon_wheel(
      "slot 1 {\nimpulse 1 2\nunknownkey a b\n}", &parsed));
  CHECK(parsed.error_count == 1
        && strcmp(parsed.errors[0], "slot 1: impulse expects one value")
               == 0);
  CHECK(parsed.slot_count == 1 && parsed.slots[0].unknown_count == 1
        && strcmp(parsed.slots[0].unknown[0].key, "unknownkey") == 0
        && parsed.slots[0].unknown[0].value_count == 2);
  qa_ui_q1_wheel_parse_free(&parsed);
}

/* ---- borders ---- */

typedef struct sink_log {
  qa_rect rects[8];
  size_t count;
  int colors;
} sink_log;

static void log_set_color(void *ctx, const qa_vec4 *color) {
  (void)color;
  sink_log *log = ctx;
  log->colors++;
}
static void log_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv,
                        const qa_picture_asset *picture) {
  (void)uv;
  (void)picture;
  sink_log *log = ctx;
  if (log->count < 8) log->rects[log->count] = rect;
  log->count++;
}

static void test_borders(void) {
  sink_log log;
  memset(&log, 0, sizeof log);
  qa_draw2d draw;
  memset(&draw, 0, sizeof draw);
  draw.commands.ctx = &log;
  draw.commands.target = rect(0, 0, 640, 480);
  draw.commands.set_color = log_set_color;
  draw.commands.stretch_pixels = log_stretch;
  draw.space = QA_COORD_PIXELS;
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  qa_vec4 color = { 1, 0, 0, 1 };
  qa_ui_draw_cg_rect(&draw, rect(10, 20, 100, 50), 2, color, &picture);
  CHECK(log.count == 4 && log.colors == 2);
  /* Pixels space: adjust is identity; size 2 at scale 1. */
  CHECK(near(log.rects[0].y, 20) && near(log.rects[0].height, 2));
  CHECK(near(log.rects[1].y, 68) && near(log.rects[3].x, 108));
}

/* ---- legacy script engine ---- */

static qa_ui_script_token *lex_next(qa_ui_script_lexer *lexer) {
  qa_ui_script_token *token = NULL;
  if (qa_ui_script_lexer_next(lexer, &token)) return token;
  return NULL;
}
static bool token_eq(qa_ui_script_token *token, const char *text) {
  return token != NULL && token->text_length == strlen(text)
         && memcmp(token->text, text, token->text_length) == 0;
}

static void test_script_lexer(void) {
  /* TS hex digits are 0-9a-f plus 'A' only, so 0x1F would split. */
  const char *source = "foo 0x1f 010 1.5 \"a\" 'b' += >=";
  qa_ui_script_lexer *lexer = qa_ui_script_lexer_create_text(
      (const uint8_t *)source, strlen(source), "test", NULL);
  CHECK(lexer != NULL);
  qa_ui_script_token *token = lex_next(lexer);
  CHECK(token != NULL && token->kind == QA_UI_SCRIPT_KIND_NAME
        && token_eq(token, "foo") && token->length == 3);
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && token->kind == QA_UI_SCRIPT_KIND_NUMBER
        && token->integer_value == 31
        && (token->flags & QA_UI_SCRIPT_NUMBER_HEX) != 0);
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && token->integer_value == 8
        && (token->flags & QA_UI_SCRIPT_NUMBER_OCTAL) != 0);
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && near(token->float_value, 1.5)
        && token->integer_value == 1
        && (token->flags & QA_UI_SCRIPT_NUMBER_FLOAT) != 0);
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && token->kind == QA_UI_SCRIPT_KIND_STRING
        && token->value_length == 1 && token->value[0] == 'a');
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && token->kind == QA_UI_SCRIPT_KIND_LITERAL);
  qa_ui_script_token_release(token);
  CHECK(qa_ui_script_lexer_expect_token_string(lexer, "+="));
  token = lex_next(lexer);
  CHECK(token != NULL && token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION
        && token->punctuation == QA_UI_SCRIPT_PUNCT_GREATER_OR_EQUAL);
  /* unreadLast replays the retained token. */
  qa_ui_script_lexer_unread_last(lexer);
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && token_eq(token, ">="));
  qa_ui_script_token_release(token);
  CHECK(lex_next(lexer) == NULL);
  qa_ui_script_lexer_destroy(lexer);

  char *stripped = qa_ui_script_strip_double_quotes("\"abc\"");
  CHECK(stripped != NULL && strcmp(stripped, "abc") == 0);
  free(stripped);
  char *name = qa_ui_script_number_subtype_name(
      QA_UI_SCRIPT_NUMBER_HEX | QA_UI_SCRIPT_NUMBER_UNSIGNED
      | QA_UI_SCRIPT_NUMBER_INTEGER);
  CHECK(name != NULL && strcmp(name, "hex unsigned integer") == 0);
  free(name);
  CHECK(strcmp(qa_ui_script_token_type_name(QA_UI_SCRIPT_TOKEN_NUMBER),
               "number") == 0);
  CHECK_ABORTS(qa_ui_script_strip_double_quotes("\""));
  CHECK_ABORTS(qa_ui_script_number_subtype_name(QA_UI_SCRIPT_NUMBER_FLOAT));
  CHECK_ABORTS(
      qa_ui_script_lexer_options bad;
      memset(&bad, 0, sizeof bad);
      bad.has_max_token_length = true;
      bad.max_token_length = 3;
      qa_ui_script_lexer_destroy(qa_ui_script_lexer_create_text(
          (const uint8_t *)"x", 1, "t", &bad)));
  CHECK_ABORTS(qa_ui_source_script_allocate(-1, "p", NULL));
  /* Uncaught lexer failure aborts (TS ScriptLanguageError). */
  CHECK_ABORTS(
      qa_ui_script_lexer *bad = qa_ui_script_lexer_create_text(
          (const uint8_t *)"\"open", 5, "t", NULL);
      qa_ui_script_token *doomed = NULL;
      (void)qa_ui_script_lexer_next(bad, &doomed));
}

static void test_script_storage(void) {
  const char *text = "alpha /* comment */ beta\n";
  qa_ui_source_script_storage *storage =
      qa_ui_script_allocate_source((int64_t)strlen(text), "stored", NULL);
  CHECK(storage != NULL);
  qa_ui_source_script_copy_text(storage, (const uint8_t *)text,
                                strlen(text));
  char *path = qa_ui_source_script_path(storage);
  CHECK(path != NULL && strcmp(path, "stored") == 0);
  free(path);
  qa_ui_source_script_compress(storage);
  char *compressed = qa_ui_source_script_text(storage);
  CHECK(compressed != NULL && strcmp(compressed, "alpha beta") == 0);
  free(compressed);
  qa_ui_source_script_reset(storage);
  qa_ui_script_lexer *lexer =
      qa_ui_script_lexer_create_storage(storage, "stored", NULL);
  CHECK(lexer != NULL);
  qa_ui_script_token *token = lex_next(lexer);
  CHECK(token != NULL && token_eq(token, "alpha"));
  qa_ui_script_token_release(token);
  token = lex_next(lexer);
  CHECK(token != NULL && token_eq(token, "beta"));
  qa_ui_script_token_release(token);
  qa_ui_script_lexer_destroy(lexer); /* also destroys the storage */

  /* x87 float profile round-trips through token_t bytes. */
  qa_ui_source_token_memory *memory =
      qa_ui_source_token_memory_create_local();
  qa_ui_source_token_set_float_value(memory, 1.5);
  CHECK(near(qa_ui_source_token_float_value(memory), 1.5));
  qa_ui_source_token_set_float_value(memory, -0.0);
  CHECK(qa_ui_source_token_float_value(memory) == 0
        && signbit(qa_ui_source_token_float_value(memory)));
  qa_ui_source_token_write_string(memory, "tok", 3);
  char buffer[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(memory, buffer);
  CHECK(strcmp(buffer, "tok") == 0);
  qa_ui_source_token_memory_destroy(memory);
  CHECK_ABORTS(
      qa_ui_source_token_memory *m =
          qa_ui_source_token_memory_create_local();
      qa_ui_source_token_set_string_byte(m, 1024, 65));
}

static bool script_resolve(void *ctx,
                           const qa_ui_script_include_request *request,
                           qa_ui_script_source *out) {
  (void)ctx;
  memset(out, 0, sizeof *out);
  if (strcmp(request->requested_path, "nested.inc") == 0) {
    out->path = "nested.inc";
    out->text = (const uint8_t *)"\"middle\"\n#include \"deep.inc\"\n";
    out->text_length = strlen((const char *)out->text);
    return true;
  }
  if (strcmp(request->requested_path, "deep.inc") == 0) {
    out->path = "deep.inc";
    out->text = (const uint8_t *)"\"deep\"\n";
    out->text_length = strlen((const char *)out->text);
    return true;
  }
  if (strcmp(request->requested_path, "child") == 0) {
    out->path = "child";
    out->text = (const uint8_t *)"\"middle\"";
    out->text_length = strlen((const char *)out->text);
    return true;
  }
  return false;
}

static void collect_reader_tokens(qa_ui_script_source_reader *reader,
                                  char texts[][64], size_t *count,
                                  size_t capacity) {
  *count = 0;
  qa_ui_script_token_record record;
  while (qa_ui_script_source_reader_next(reader, &record)) {
    qa_ui_script_token *token = record.token;
    const char *text = token->kind == QA_UI_SCRIPT_KIND_STRING
                           ? token->value
                           : token->text;
    size_t length = token->kind == QA_UI_SCRIPT_KIND_STRING
                        ? token->value_length
                        : token->text_length;
    if (*count < capacity && length < 64) {
      memcpy(texts[*count], text, length);
      texts[*count][length] = '\0';
    }
    (*count)++;
    qa_ui_script_token_record_release(&record);
  }
}

static void test_script_preprocessor(void) {
  /* Port of tests/compat/qvm/client-script.test.ts fixture flow. */
  qa_ui_script_global_defines *globals =
      qa_ui_script_global_defines_create(NULL, NULL, NULL);
  CHECK(globals != NULL);
  CHECK(qa_ui_script_global_defines_add(globals, "VALUE 7"));
  const char *root_text =
      "#define TWICE(x) ((x)+(x))\n\"before\"\n#include \"nested.inc\"\n"
      "#define KEEP 1\n\"after\"\nVALUE TWICE(3)\n";
  qa_ui_script_source root;
  memset(&root, 0, sizeof root);
  root.path = "root.menu";
  root.text = (const uint8_t *)root_text;
  root.text_length = strlen(root_text);
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.globals = globals;
  qa_ui_script_source_reader *reader = qa_ui_script_source_reader_open(
      &root, script_resolve, NULL, &options);
  CHECK(reader != NULL);
  qa_ui_script_token_record record;
  CHECK(qa_ui_script_source_reader_next(reader, &record));
  CHECK(record.token->kind == QA_UI_SCRIPT_KIND_STRING);
  CHECK(record.token->value_length == 21
        && memcmp(record.token->value, "beforemiddledeepafter", 21) == 0);
  qa_ui_script_token_record_release(&record);
  CHECK(qa_ui_script_source_reader_next(reader, &record));
  CHECK(record.token->kind == QA_UI_SCRIPT_KIND_NUMBER
        && record.integer_value == 7 && near(record.float_value, 7));
  qa_ui_script_token_record_release(&record);
  char texts[16][64];
  size_t count = 0;
  collect_reader_tokens(reader, texts, &count, 16);
  const char *expected[8] = {"(", "(", "3", ")", "+", "(", "3", ")"};
  /* one more ")" closes TWICE's outer parens */
  CHECK(count == 9);
  for (size_t i = 0; i < 8 && i < count; i++)
    CHECK(strcmp(texts[i], expected[i]) == 0);
  if (count == 9) CHECK(strcmp(texts[8], ")") == 0);
  char filename[128];
  int64_t line = 0;
  qa_ui_script_source_reader_position(reader, filename, sizeof filename,
                                      &line);
  CHECK(strcmp(filename, "root.menu") == 0);
  CHECK(line > 1);
  qa_ui_script_source_reader_destroy(reader);

  /* Conditionals and expression evaluation. */
  const char *cond_text =
      "#define A 2\n#if A * 3 == 6\nyes\n#else\nno\n#endif\n"
      "#ifdef A\non\n#endif\n#ifndef A\noff\n#endif\n";
  memset(&root, 0, sizeof root);
  root.path = "cond";
  root.text = (const uint8_t *)cond_text;
  root.text_length = strlen(cond_text);
  memset(&options, 0, sizeof options);
  reader = qa_ui_script_source_reader_open(&root, script_resolve, NULL,
                                           &options);
  collect_reader_tokens(reader, texts, &count, 16);
  CHECK(count == 2 && strcmp(texts[0], "yes") == 0
        && strcmp(texts[1], "on") == 0);
  qa_ui_script_source_reader_destroy(reader);

  /* Adjacent strings across an include (client-script async case). */
  const char *async_text =
      "\"first\"\n#include \"child\"\n#define KEEP 1\n\"last\"";
  memset(&root, 0, sizeof root);
  root.path = "root";
  root.text = (const uint8_t *)async_text;
  root.text_length = strlen(async_text);
  reader = qa_ui_script_source_reader_open(&root, script_resolve, NULL,
                                           &options);
  CHECK(qa_ui_script_source_reader_next_async(reader, NULL, NULL,
                                              &record));
  CHECK(record.token->text_length == 17
        && memcmp(record.token->text, "\"firstmiddlelast\"", 17) == 0);
  qa_ui_script_token_record_release(&record);
  qa_ui_script_source_reader_destroy(reader);

  /* Shared globals survive an include failure in another reader. */
  memset(&root, 0, sizeof root);
  root.path = "other";
  root.text = (const uint8_t *)"VALUE";
  root.text_length = 5;
  memset(&options, 0, sizeof options);
  options.globals = globals;
  reader = qa_ui_script_source_reader_open(&root, script_resolve, NULL,
                                           &options);
  CHECK(qa_ui_script_source_reader_next(reader, &record));
  CHECK(token_eq(record.token, "7"));
  qa_ui_script_token_record_release(&record);
  qa_ui_script_source_reader_destroy(reader);
  qa_ui_script_global_defines_destroy(globals);

  /* Missing include is an uncaught source failure. */
  CHECK_ABORTS(
      qa_ui_script_source bad;
      memset(&bad, 0, sizeof bad);
      bad.path = "error.menu";
      bad.text = (const uint8_t *)"#include \"absent.inc\"\n";
      bad.text_length = strlen((const char *)bad.text);
      qa_ui_script_source_reader *doomed =
          qa_ui_script_source_reader_open(&bad, script_resolve, NULL,
                                          NULL);
      qa_ui_script_token_record dropped;
      (void)qa_ui_script_source_reader_next(doomed, &dropped));
  /* expect* helpers turn the same failure into false + diagnostic. */
  memset(&root, 0, sizeof root);
  root.path = "error.menu";
  root.text = (const uint8_t *)"#include \"absent.inc\"\n";
  root.text_length = strlen((const char *)root.text);
  reader = qa_ui_script_source_reader_open(&root, script_resolve, NULL,
                                           NULL);
  CHECK(!qa_ui_script_source_reader_expect_token_string(reader, "x"));
  size_t diag_count = 0;
  const qa_ui_script_diagnostic *diags =
      qa_ui_script_source_reader_diagnostics(reader, &diag_count);
  bool saw_missing = false;
  for (size_t i = 0; i < diag_count; i++)
    if (strstr(diags[i].message, "absent.inc") != NULL
        && strstr(diags[i].message, "not found") != NULL)
      saw_missing = true;
  CHECK(saw_missing);
  qa_ui_script_source_reader_destroy(reader);
}

typedef struct checkpoint_store {
  qa_ui_script_allocation *allocations[256];
  uint8_t *bytes[256];
  size_t lengths[256];
  size_t count;
  qa_ui_script_allocation restored[256];
  bool created[256];
} checkpoint_store;

static int64_t checkpoint_reference(void *ctx,
                                    qa_ui_script_allocation *allocation) {
  checkpoint_store *store = ctx;
  for (size_t i = 0; i < store->count; i++)
    if (store->allocations[i] == allocation) return (int64_t)i;
  size_t index = store->count++;
  store->allocations[index] = allocation;
  store->bytes[index] = malloc(allocation->length);
  memcpy(store->bytes[index], allocation->bytes, allocation->length);
  store->lengths[index] = allocation->length;
  return (int64_t)index;
}

static qa_ui_script_allocation checkpoint_allocation(void *ctx,
                                                     int64_t id) {
  checkpoint_store *store = ctx;
  if (!store->created[id]) {
    store->restored[id].bytes = malloc(store->lengths[id]);
    memcpy(store->restored[id].bytes, store->bytes[id],
           store->lengths[id]);
    store->restored[id].length = store->lengths[id];
    store->restored[id].record = NULL;
    store->created[id] = true;
  }
  return store->restored[id];
}

static void test_script_reader_checkpoint(void) {
  const char *root_text =
      "#define TWICE(x) ((x)+(x))\n\"before\"\n#include \"nested.inc\"\n"
      "7 TWICE(3)\n";
  qa_ui_script_source root;
  memset(&root, 0, sizeof root);
  root.path = "root.menu";
  root.text = (const uint8_t *)root_text;
  root.text_length = strlen(root_text);
  qa_ui_script_source_reader *reader = qa_ui_script_source_reader_open(
      &root, script_resolve, NULL, NULL);
  CHECK(reader != NULL);
  qa_ui_script_token_record record;
  CHECK(qa_ui_script_source_reader_next(reader, &record));
  qa_ui_script_token_record_release(&record);
  checkpoint_store store;
  memset(&store, 0, sizeof store);
  qa_ui_script_memory_capture capture = {&store, checkpoint_reference};
  qa_save_value *state =
      qa_ui_script_source_reader_capture_save_state(reader, &capture);
  CHECK(state != NULL);
  qa_ui_script_memory_restore restore = {&store, checkpoint_allocation};
  qa_ui_script_source_reader *restored =
      qa_ui_script_source_reader_restore_save_state(state, script_resolve,
                                                    NULL, NULL, &restore);
  CHECK(restored != NULL);
  char left[16][64], right[16][64];
  size_t left_count = 0, right_count = 0;
  collect_reader_tokens(reader, left, &left_count, 16);
  collect_reader_tokens(restored, right, &right_count, 16);
  CHECK(left_count == right_count);
  CHECK(left_count == 10); /* 7 ( ( 3 ) + ( 3 ) ) */
  for (size_t i = 0; i < left_count && i < right_count; i++)
    CHECK(strcmp(left[i], right[i]) == 0);
  if (left_count == 10) CHECK(strcmp(left[0], "7") == 0);
  qa_ui_script_source_reader_destroy(reader);
  qa_ui_script_source_reader_destroy(restored);
  qa_save_value_free(state);
  for (size_t i = 0; i < store.count; i++) free(store.bytes[i]);
}

static void test_script_globals_checkpoint(void) {
  qa_ui_script_global_defines *globals =
      qa_ui_script_global_defines_create(NULL, NULL, NULL);
  CHECK(qa_ui_script_global_defines_add(globals, "VALUE 7"));
  qa_save_value *owned =
      qa_ui_script_global_defines_capture_owned_save_state(globals);
  CHECK(owned != NULL);
  qa_ui_script_global_defines *restored =
      qa_ui_script_global_defines_create(NULL, NULL, NULL);
  qa_ui_script_global_defines_restore_owned_save_state(restored, owned);
  qa_save_value_free(owned);
  qa_ui_script_source root;
  memset(&root, 0, sizeof root);
  root.path = "other";
  root.text = (const uint8_t *)"VALUE";
  root.text_length = 5;
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.globals = restored;
  qa_ui_script_source_reader *reader = qa_ui_script_source_reader_open(
      &root, script_resolve, NULL, &options);
  qa_ui_script_token_record record;
  CHECK(qa_ui_script_source_reader_next(reader, &record));
  CHECK(token_eq(record.token, "7"));
  qa_ui_script_token_record_release(&record);
  qa_ui_script_source_reader_destroy(reader);
  qa_ui_script_global_defines_destroy(globals);
  qa_ui_script_global_defines_destroy(restored);

  /* Eager preprocessor over the same sources. */
  qa_ui_script_preprocessor *pre = qa_ui_script_preprocessor_create(
      &root, script_resolve, NULL, NULL);
  CHECK(pre != NULL);
  size_t all_count = 0;
  qa_ui_script_token *const *all =
      qa_ui_script_preprocessor_all(pre, &all_count);
  CHECK(all_count == 1 && token_eq(all[0], "VALUE"));
  qa_ui_script_preprocessor_destroy(pre);
}

/* ---- team-arena memory ---- */

typedef struct print_sink {
  char text[2048];
  size_t length;
} print_sink;

static void sink_print(void *ctx, const char *text) {
  print_sink *sink = ctx;
  size_t length = strlen(text);
  if (sink->length + length < sizeof sink->text) {
    memcpy(sink->text + sink->length, text, length + 1);
    sink->length += length;
  }
}

static void test_team_arena_memory(void) {
  print_sink sink;
  memset(&sink, 0, sizeof sink);
  qa_ui_team_arena_memory *memory = qa_ui_team_arena_memory_create(
      "qvm32", sink_print, &sink, QA_UI_TEAM_ARENA_MODULE_UI);
  CHECK(memory != NULL);
  int64_t offset = -1;
  CHECK(qa_ui_team_arena_memory_allocate(memory, 8, &offset)
        && offset == 0);
  CHECK(qa_ui_team_arena_memory_allocate(memory, 1, &offset)
        && offset == 16);
  CHECK(qa_ui_team_arena_memory_allocated_bytes(memory) == 32);
  qa_ui_string_reference *first =
      qa_ui_team_arena_memory_string_alloc_reference(memory, "Hello");
  qa_ui_string_reference *again =
      qa_ui_team_arena_memory_string_alloc_reference(memory, "Hello");
  CHECK(first != NULL && first == again); /* deduped */
  char *read = qa_ui_string_reference_read(first);
  CHECK(read != NULL && strcmp(read, "Hello") == 0);
  free(read);
  CHECK(qa_ui_team_arena_memory_string_bytes(memory) == 6);
  CHECK(qa_ui_team_arena_memory_string_alloc(memory, NULL) == NULL);
  qa_ui_team_arena_memory_report(memory);
  CHECK(strstr(sink.text, "Memory/String Pool Info") != NULL);
  CHECK(strstr(sink.text, "String Pool is") != NULL);
  qa_ui_memory_allocation *record =
      qa_ui_team_arena_memory_borrow(memory, 0, 32);
  CHECK(record != NULL);
  qa_ui_memory_allocation_set_int32(record, 0, -42);
  CHECK(qa_ui_memory_allocation_get_int32(record, 0) == -42);
  qa_ui_memory_allocation_set_float32(record, 4, 1.5);
  CHECK(near(qa_ui_memory_allocation_get_float32(record, 4), 1.5));
  qa_ui_memory_allocation_set_string_text(record, 8, "ptr");
  char *value = qa_ui_memory_allocation_get_string(record, 8);
  CHECK(value != NULL && strcmp(value, "ptr") == 0);
  free(value);
  CHECK(!qa_ui_memory_allocation_is_null_pointer(record, 8));
  qa_ui_memory_allocation_destroy(record);
  qa_ui_memory_allocation *menu =
      qa_ui_team_arena_memory_menu_record(memory, 3);
  CHECK(menu != NULL && qa_ui_memory_allocation_size(menu) == 644);
  qa_ui_memory_allocation_destroy(menu);
  CHECK(!qa_ui_team_arena_memory_allocate(memory, 1024 * 1024, &offset));
  CHECK(qa_ui_team_arena_memory_out_of_memory(memory));
  qa_ui_team_arena_memory_destroy(memory);
  CHECK_ABORTS(
      qa_ui_team_arena_memory *m = qa_ui_team_arena_memory_create(
          "qvm32", sink_print, &sink, QA_UI_TEAM_ARENA_MODULE_UI);
      qa_ui_team_arena_memory_menu_record(m, 64));
  CHECK_ABORTS(
      qa_ui_memory_allocation *z = qa_ui_memory_allocation_zeroed(8);
      qa_ui_memory_allocation_get_int32(z, 8));
}

/* ---- settings menus (ui/settings + ui/common/controller) ---- */

static char *dup_str(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static qa_ui_skin test_skin(void *ctx) {
  (void)ctx;
  return qa_ui_default_skin("resource:test:font");
}
static double test_now(void *ctx) { (void)ctx; return 0; }
static const qa_input_binding *test_bindings(void *ctx, size_t *out) {
  (void)ctx;
  *out = 0;
  return NULL;
}
static void test_noop_focus(void *ctx, qa_seat_input_focus focus, double now) {
  (void)ctx;
  (void)focus;
  (void)now;
}
static void test_noop_sound(void *ctx, qa_ui_sound sound, qa_seat_id seat) {
  (void)ctx;
  (void)sound;
  (void)seat;
}
static void test_noop_script(void *ctx, qa_legacy_ui_script script,
                             qa_seat_id seat) {
  (void)ctx;
  (void)script;
  (void)seat;
}

typedef struct test_ui_harness {
  qa_identity_owner identity;
  qa_seat_id seat;
  qa_native_ui_controller *controller;
} test_ui_harness;
static void harness_init(test_ui_harness *h) {
  CHECK(qa_identity_owner_create(&h->identity, "ui-test"));
  h->seat = qa_identity_owner_seat(&h->identity, 0);
  qa_native_ui_options options;
  memset(&options, 0, sizeof options);
  options.seat = h->seat;
  options.skin = test_skin;
  options.now = test_now;
  options.bindings = test_bindings;
  options.focus = test_noop_focus;
  options.sound = test_noop_sound;
  options.execute_script = test_noop_script;
  options.measure_text = measure8;
  h->controller = qa_native_ui_controller_create(&options);
  CHECK(h->controller != NULL);
}
static void harness_destroy(test_ui_harness *h) {
  qa_native_ui_controller_destroy(h->controller);
  qa_identity_owner_destroy(&h->identity);
}
static void send_key(test_ui_harness *h, int code) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = h->seat;
  event.kind = "key";
  event.code = code;
  event.down = true;
  (void)qa_native_ui_controller_input(h->controller, event);
  event.down = false;
  (void)qa_native_ui_controller_input(h->controller, event);
}
static void send_text(test_ui_harness *h, const char *text) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = h->seat;
  event.kind = "text";
  event.text = text;
  (void)qa_native_ui_controller_input(h->controller, event);
}
static bool draw_has(test_ui_harness *h, const char *needle) {
  qa_ui_draw_context context;
  memset(&context, 0, sizeof context);
  context.binding.seat = h->seat;
  context.binding.viewport = rect(0, 0, 640, 480);
  context.binding.safe_area = rect(0, 0, 640, 480);
  context.binding.hud_scale = 1;
  size_t count = 0;
  const qa_ui_draw_command *commands =
      qa_native_ui_controller_draw(h->controller, context, &count);
  for (size_t i = 0; i < count; i++)
    if (commands[i].kind == QA_UI_DRAW_TEXT && commands[i].text != NULL
        && strstr(commands[i].text, needle) != NULL)
      return true;
  return false;
}
static bool menu_is(test_ui_harness *h, const char *id) {
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(h->controller);
  return id == NULL ? active == NULL
                    : active != NULL && strcmp(active, id) == 0;
}

static qa_command_context test_context(qa_identity_owner *owner) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner->session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  return context;
}
static qa_cvar_registry *make_registry(qa_identity_owner *owner,
                                       qa_command_dialect dialect) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = dialect;
  options.context = test_context(owner);
  return qa_cvar_registry_create(&options);
}
static void cvar_register(qa_cvar_registry *r, const char *name,
                          const char *value) {
  qa_cvar_snapshot out;
  if (qa_cvar_registry_register(r, name, value, 0, &out))
    qa_cvar_read_free(&out);
}
static void cvar_set(qa_cvar_registry *r, const char *name,
                     const char *value) {
  qa_cvar_snapshot out;
  if (qa_cvar_registry_set(r, name, value, false, &out))
    qa_cvar_read_free(&out);
}

static void test_settings_core(void) {
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "settings-test"));
  qa_cvar_registry *cvars = make_registry(&identity, QA_COMMAND_DIALECT_Q3);
  CHECK(cvars != NULL);
  qa_ui_register_accessibility_settings(cvars);
  qa_ui_preference_values prefs = qa_ui_read_preferences(cvars, 0);
  CHECK(near(prefs.hud_scale, 1) && prefs.captions && prefs.crosshair);
  CHECK(strcmp(prefs.typeface, "standard") == 0);
  prefs.hud_scale = 1.25;
  prefs.captions = false;
  prefs.typeface = "bold";
  qa_ui_write_preferences(cvars, 0, prefs);
  prefs = qa_ui_read_preferences(cvars, 0);
  CHECK(near(prefs.hud_scale, 1.25) && !prefs.captions);
  CHECK(strcmp(prefs.typeface, "bold") == 0);
  /* Out-of-range writes are rejected by the bound validator. */
  cvar_set(cvars, "ui_seat1_hudScale", "9");
  CHECK(near(qa_ui_read_preferences(cvars, 0).hud_scale, 1.25));

  cvar_register(cvars, "test_toggle", "0");
  qa_ui_cvar_setting_spec spec;
  memset(&spec, 0, sizeof spec);
  spec.name = "test_toggle";
  spec.label = "Toggle";
  spec.category = "video";
  spec.kind = "toggle";
  qa_ui_setting_binding binding;
  CHECK(qa_ui_bind_cvar_setting(cvars, &spec, NULL, &binding));
  CHECK(strcmp(binding.id, "ui:settings:test_toggle") == 0);
  CHECK(!binding.read_bool(binding.ctx));
  binding.write_bool(binding.ctx, true);
  CHECK(binding.read_bool(binding.ctx));
  qa_ui_cvar_setting_binding_dispose(&binding);
  qa_ui_cvar_setting_spec missing = spec;
  missing.name = "test_missing";
  CHECK_ABORTS(qa_ui_bind_cvar_setting(cvars, &missing, NULL, &binding));

  qa_ui_seat_preferences seat_prefs;
  qa_seat_id seat = qa_identity_owner_seat(&identity, 1);
  qa_ui_seat_preferences_init(&seat_prefs, seat, NULL);
  size_t pref_count = 0;
  const qa_ui_setting_binding *pref_bindings =
      qa_ui_seat_preferences_bindings(&seat_prefs, &pref_count);
  CHECK(pref_count == 11);
  qa_ui_preference_values changed = qa_ui_seat_preferences_values(&seat_prefs);
  changed.hud_scale = 0.5;
  qa_ui_seat_preferences_set_values(&seat_prefs, changed);
  CHECK(near(qa_ui_seat_preferences_values(&seat_prefs).hud_scale, 0.5));
  pref_bindings[10].activate(pref_bindings[10].ctx); /* reset */
  CHECK(near(qa_ui_seat_preferences_values(&seat_prefs).hud_scale, 1));
  CHECK_ABORTS(pref_bindings[0].write_str(pref_bindings[0].ctx, "comic"));
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&identity);
}

typedef struct fake_primary {
  qa_ui_primary_input_settings values;
} fake_primary;
static qa_ui_primary_input_settings fake_primary_read(void *ctx) {
  return ((fake_primary *)ctx)->values;
}
static void fake_primary_write(void *ctx, qa_ui_primary_input_settings values,
                               unsigned mask) {
  fake_primary *f = ctx;
  if (mask & QA_UI_PRIMARY_SENSITIVITY) f->values.sensitivity = values.sensitivity;
  if (mask & QA_UI_PRIMARY_PITCH) f->values.pitch = values.pitch;
  if (mask & QA_UI_PRIMARY_YAW) f->values.yaw = values.yaw;
  if (mask & QA_UI_PRIMARY_INVERT) f->values.invert_mouse = values.invert_mouse;
  if (mask & QA_UI_PRIMARY_ALWAYS_RUN) f->values.always_run = values.always_run;
}

static void test_settings_factories(void) {
  fake_primary fake = { { 3, -0.011, -0.011, false, false } };
  qa_ui_primary_input_service service = { &fake, fake_primary_read,
                                          fake_primary_write };
  qa_ui_setting_bindings bindings =
      qa_ui_bind_primary_input_settings(&service);
  CHECK(bindings.count == 5);
  CHECK(strcmp(bindings.items[1].id, "ui:input:mouse-yaw") == 0);
  /* |yaw| / 0.022 * 100 = 50; writing 200 keeps the negative sign. */
  CHECK(near(bindings.items[1].read_num(bindings.items[1].ctx), 50));
  bindings.items[1].write_num(bindings.items[1].ctx, 200);
  CHECK(fake.values.yaw < 0 && near(fake.values.yaw, -0.044));
  bindings.items[0].write_num(bindings.items[0].ctx, 7);
  CHECK(near(fake.values.sensitivity, 7));
  qa_ui_setting_bindings_free(&bindings);

  qa_input_binding_target action_target;
  memset(&action_target, 0, sizeof action_target);
  action_target.is_action = true;
  action_target.action = "jump";
  qa_ui_binding_action action;
  memset(&action, 0, sizeof action);
  action.id = "jump";
  action.label = "Jump";
  action.target = action_target;
  CHECK(qa_ui_binding_matches_action(action_target, &action));
  qa_input_binding_target command_target;
  memset(&command_target, 0, sizeof command_target);
  command_target.text = "jump";
  CHECK(!qa_ui_binding_matches_action(command_target, &action));
}

typedef struct fake_ranking {
  qa_ranking_service_state service;
  qa_ranking_player_state player;
} fake_ranking;
static qa_ranking_service_state fake_ranking_service(void *ctx) {
  return ((fake_ranking *)ctx)->service;
}
static qa_ranking_player_state fake_ranking_player(void *ctx) {
  return ((fake_ranking *)ctx)->player;
}
static const char *fake_ranking_noop(void *ctx) {
  (void)ctx;
  return NULL;
}
static const char *fake_ranking_submit(void *ctx,
                                       qa_ranking_account_request request) {
  (void)ctx;
  (void)request;
  return NULL;
}

static void test_ranking_view(void) {
  fake_ranking fake;
  memset(&fake, 0, sizeof fake);
  qa_ui_ranking_account_actions actions;
  memset(&actions, 0, sizeof actions);
  actions.ctx = &fake;
  actions.service = fake_ranking_service;
  actions.player = fake_ranking_player;
  actions.submit = fake_ranking_submit;
  actions.reset = fake_ranking_noop;
  actions.spectate = fake_ranking_noop;
  fake.service.kind = "disabled";
  CHECK(strcmp(qa_ui_ranking_account_view_for(&actions).kind, "disabled") == 0);
  fake.service.kind = "unavailable";
  fake.service.reason = "offline";
  qa_ui_ranking_account_view view = qa_ui_ranking_account_view_for(&actions);
  CHECK(strcmp(view.kind, "unavailable") == 0);
  CHECK(strcmp(view.message, "offline") == 0);
  fake.service.kind = "starting";
  CHECK(strcmp(qa_ui_ranking_account_view_for(&actions).kind, "busy") == 0);
  fake.service.kind = "active";
  fake.player.kind = "spectator";
  view = qa_ui_ranking_account_view_for(&actions);
  CHECK(strcmp(view.kind, "account") == 0);
  CHECK(strcmp(view.player.kind, "spectator") == 0);
}

typedef struct fake_server_owner {
  char desired[64], effective[64];
} fake_server_owner;
static qa_server_setting_values fake_server_read(void *ctx,
                                                 qa_server_setting_target t) {
  (void)t;
  fake_server_owner *owner = ctx;
  qa_server_setting_values values = { dup_str(owner->desired),
                                      dup_str(owner->effective) };
  return values;
}
static void fake_server_write(void *ctx, qa_server_setting_target t,
                              const char *value, qa_server_apply_at at) {
  (void)t;
  (void)at;
  fake_server_owner *owner = ctx;
  snprintf(owner->desired, sizeof owner->desired, "%s", value);
}
static qa_server_setting_definition fraglimit_def;
static qa_bound_server_setting fraglimit_binding;
static const qa_bound_server_setting *fake_server_bindings(void *ctx,
                                                           size_t *out) {
  (void)ctx;
  *out = 1;
  return &fraglimit_binding;
}

static void test_server_menu(void) {
  test_ui_harness h;
  harness_init(&h);
  (void)system("rm -rf /tmp/qa-ui-server-test");
  qa_config_store store;
  CHECK(qa_config_store_init(&store, "/tmp/qa-ui-server-test"));
  fake_server_owner owner = { "5", "5" };
  memset(&fraglimit_def, 0, sizeof fraglimit_def);
  fraglimit_def.id = "server:fraglimit";
  fraglimit_def.label = "Frag limit";
  fraglimit_def.description = "Frags needed to end the match.";
  fraglimit_def.apply_at = QA_SERVER_APPLY_LIVE;
  fraglimit_def.default_value = "0";
  fraglimit_def.target.name = "fraglimit";
  fraglimit_def.kind = QA_SERVER_SETTING_SLIDER;
  fraglimit_def.minimum = 0;
  fraglimit_def.maximum = 100;
  fraglimit_def.step = 1;
  fraglimit_def.integer = true;
  fraglimit_binding.definition = &fraglimit_def;
  fraglimit_binding.owner.ctx = &owner;
  fraglimit_binding.owner.read = fake_server_read;
  fraglimit_binding.owner.write = fake_server_write;
  qa_ui_host_server_settings host = { fake_server_bindings, NULL, &store };
  qa_ui_settings_menus menus =
      qa_ui_register_server_settings_menu(h.controller, &host);
  CHECK(strcmp(menus.root, "menu:server:settings") == 0);
  qa_native_ui_controller_open_menu(h.controller, menus.root);
  CHECK(menu_is(&h, "menu:server:settings"));
  CHECK(draw_has(&h, "Frag limit: 5"));
  send_key(&h, QA_KEY_ENTER);
  CHECK(menu_is(&h, "menu:server:detail"));
  /* Draft is "5": replace it with invalid text and Apply. */
  send_key(&h, QA_KEY_HOME);
  send_key(&h, QA_KEY_DELETE);
  send_text(&h, "x");
  send_key(&h, QA_KEY_DOWN);
  send_key(&h, QA_KEY_ENTER);
  CHECK(strcmp(owner.desired, "5") == 0);
  CHECK(draw_has(&h, "outside its allowed range"));
  send_key(&h, QA_KEY_UP);
  send_key(&h, QA_KEY_HOME);
  send_key(&h, QA_KEY_DELETE);
  send_text(&h, "7");
  send_key(&h, QA_KEY_DOWN);
  send_key(&h, QA_KEY_ENTER);
  CHECK(strcmp(owner.desired, "7") == 0);
  send_key(&h, QA_KEY_ESCAPE);
  CHECK(menu_is(&h, "menu:server:settings"));
  /* Profiles: root focus is the item; Down reaches the profiles button. */
  send_key(&h, QA_KEY_DOWN);
  send_key(&h, QA_KEY_ENTER);
  CHECK(menu_is(&h, "menu:server:profiles"));
  send_key(&h, QA_KEY_DOWN); /* Save */
  send_key(&h, QA_KEY_ENTER);
  char *saved = qa_config_store_load_text(&store, "servers/default.json",
                                          NULL);
  CHECK(saved != NULL && strstr(saved, "server:fraglimit") != NULL);
  free(saved);
  snprintf(owner.desired, sizeof owner.desired, "3");
  send_key(&h, QA_KEY_DOWN); /* Load */
  send_key(&h, QA_KEY_ENTER);
  CHECK(strcmp(owner.desired, "7") == 0);
  qa_ui_settings_menus_dispose(&menus);
  qa_config_store_destroy(&store);
  harness_destroy(&h);
}

typedef struct fake_lobby_app {
  qa_ui_application_local_lobby app;
  qa_lobby lobby;
  qa_lobby_member members[2];
  const qa_lobby *list_items[1];
  bool has_lobby;
  bool last_ready;
  int ready_calls;
} fake_lobby_app;
static fake_lobby_app *lobby_app_of(void *ctx) {
  return (fake_lobby_app *)((char *)ctx - offsetof(fake_lobby_app, app));
}
static const qa_lobby *fake_lobby_current(void *ctx) {
  fake_lobby_app *fake = lobby_app_of(ctx);
  return fake->has_lobby ? &fake->lobby : NULL;
}
static const qa_lobby *const *fake_lobby_list(void *ctx, size_t *out) {
  fake_lobby_app *fake = lobby_app_of(ctx);
  *out = 1;
  return fake->list_items;
}
static const char *fake_lobby_host(void *ctx, const char *name,
                                   double capacity,
                                   qa_ui_local_lobby_selection selection,
                                   double seats) {
  (void)ctx;
  (void)name;
  (void)capacity;
  (void)selection;
  (void)seats;
  return NULL;
}
static const char *fake_lobby_join(void *ctx, const char *id, double seats) {
  (void)ctx;
  (void)id;
  (void)seats;
  return NULL;
}
static const char *fake_lobby_ready(void *ctx, bool value) {
  fake_lobby_app *fake = lobby_app_of(ctx);
  fake->last_ready = value;
  fake->ready_calls++;
  fake->members[0].ready = value;
  return NULL;
}
static const char *fake_lobby_start(void *ctx) { (void)ctx; return NULL; }
static const char *fake_lobby_leave(void *ctx) { (void)ctx; return NULL; }
static const qa_ui_application_local_lobby *fake_lobby_owner(void *ctx) {
  fake_lobby_app *fake = ctx;
  return fake->app.ctx == NULL ? NULL : &fake->app;
}
static const char *fake_host_selection(void *ctx,
                                       qa_ui_local_lobby_selection *out) {
  (void)ctx;
  out->composition = NULL;
  return NULL;
}
static double fake_read_seats(void *ctx) { (void)ctx; return 2; }

static void test_local_lobby_menu(void) {
  test_ui_harness h;
  harness_init(&h);
  fake_lobby_app fake;
  memset(&fake, 0, sizeof fake);
  fake.members[0].account.id = (char *)"acct-1";
  fake.members[0].account.name = (char *)"Alice";
  fake.members[0].seats = 1;
  fake.members[1].account.id = (char *)"acct-2";
  fake.members[1].account.name = (char *)"Bob";
  fake.members[1].seats = 2;
  fake.members[1].ready = true;
  fake.lobby.id = (char *)"lobby-1";
  fake.lobby.owner = (char *)"acct-1";
  fake.lobby.name = (char *)"Fun";
  fake.lobby.capacity = 4;
  fake.lobby.members = fake.members;
  fake.lobby.member_count = 2;
  fake.lobby.phase = "open";
  fake.list_items[0] = &fake.lobby;
  fake.has_lobby = true;
  fake.app.ctx = &fake.app;
  fake.app.account_id = "acct-1";
  fake.app.current = fake_lobby_current;
  fake.app.list = fake_lobby_list;
  fake.app.host = fake_lobby_host;
  fake.app.join = fake_lobby_join;
  fake.app.ready = fake_lobby_ready;
  fake.app.start = fake_lobby_start;
  fake.app.leave = fake_lobby_leave;
  qa_ui_settings_menus menus = qa_ui_register_local_lobby_menu(
      h.controller, fake_lobby_owner, &fake, fake_host_selection, NULL,
      fake_read_seats, NULL);
  qa_native_ui_controller_open_menu(h.controller, menus.root);
  CHECK(menu_is(&h, "menu:local:lobby"));
  CHECK(draw_has(&h, "Alice: Not ready (1 seat)"));
  CHECK(draw_has(&h, "Bob: Ready (2 seats)"));
  send_key(&h, QA_KEY_ENTER); /* focus starts on Ready */
  CHECK(fake.ready_calls == 1 && fake.last_ready);
  fake.has_lobby = false;
  CHECK(draw_has(&h, "Join Fun (3/4)"));
  fake.app.ctx = NULL; /* owner becomes unavailable */
  CHECK(draw_has(&h, "No local lobby service is configured."));
  qa_ui_settings_menus_dispose(&menus);
  harness_destroy(&h);
}

int main(void) {
  test_layout();
  test_skin_and_accessibility();
  test_art_and_theme();
  test_captions();
  test_powerups();
  test_weapon();
  test_wheel();
  test_q1_wheel();
  test_borders();
  test_script_lexer();
  test_script_storage();
  test_script_preprocessor();
  test_script_reader_checkpoint();
  test_script_globals_checkpoint();
  test_team_arena_memory();
  test_settings_core();
  test_settings_factories();
  test_ranking_view();
  test_server_menu();
  test_local_lobby_menu();
  if (failures > 0) {
    fprintf(stderr, "test_ui: %d of %d checks FAILED\n", failures, checks);
    return 1;
  }
  printf("test_ui: all %d checks passed\n", checks);
  return 0;
}
