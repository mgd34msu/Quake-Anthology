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
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/ui/common/legacy/menu.h"
#include "qa/ui/common/legacy/runtime.h"
#include "qa/ui/common/menu_theme.h"
#include "qa/ui/common/skin.h"
#include "qa/ui/hud/index.h"
#include "qa/ui/hud/powerups.h"
#include "qa/ui/hud/q1_wheel.h"
#include "qa/ui/hud/q2_native.h"
#include "qa/ui/hud/q2_rerelease_layout.h"
#include "qa/ui/library/inventory.h"
#include "qa/ui/library/match.h"
#include "qa/ui/library/menu.h"
#include "qa/ui/mods/menu.h"
#include "qa/ui/saves/menu.h"
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

/* ---- hud/q2-native + q2-rerelease-layout + hud/index +
 * library/{inventory,match,menu} + mods/menu + saves/menu ----
 * (appended section; ports tests/ui/hud/q2-native.test.ts,
 * tests/ui/hud/splitscreen-readability.test.ts (occupied rects),
 * tests/ui/library/menu.test.ts, tests/ui/library-menu.test.ts
 * (library + inventory), tests/ui/mods/menu.test.ts) */

static const char *q2_statusbar =
    "yb -24 xv 0 hnum xv 50 pic 0 if 2 xv 100 anum xv 150 pic 2 endif "
    "if 4 xv 200 rnum xv 250 pic 4 endif if 6 xv 296 pic 6 endif yb -50 "
    "if 7 xv 0 pic 7 xv 26 yb -42 stat_string 8 yb -50 endif if 9 xv 262 "
    "num 2 10 xv 296 pic 9 endif if 11 xv 148 pic 11 endif";

static bool op_is_pic(const qa_native_q2_hud_operations *ops, size_t i,
                      double x, double y, const char *name) {
  if (i >= ops->count) return false;
  const qa_native_q2_hud_operation *op = &ops->operations[i];
  return op->kind == QA_Q2_HUD_OP_PICTURE && near(op->x, x) && near(op->y, y)
         && op->name != NULL && strcmp(op->name, name) == 0;
}
static bool ops_have_pic(const qa_native_q2_hud_operations *ops, double x,
                         double y, const char *name) {
  for (size_t i = 0; i < ops->count; i++)
    if (op_is_pic(ops, i, x, y, name)) return true;
  return false;
}
static bool ops_have_text(const qa_native_q2_hud_operations *ops,
                          qa_native_q2_hud_operation_kind kind, double x,
                          double y, const char *text, bool alternate) {
  for (size_t i = 0; i < ops->count; i++) {
    const qa_native_q2_hud_operation *op = &ops->operations[i];
    if (op->kind == kind && near(op->x, x) && near(op->y, y)
        && op->text != NULL && strcmp(op->text, text) == 0
        && op->alternate == alternate)
      return true;
  }
  return false;
}

typedef struct q2_fixture {
  double stats[64];
  double inventory[256];
  qa_native_q2_config_string configs[40];
  char item_names[25][16];
  qa_native_q2_hud_frame frame;
} q2_fixture;

static void q2_classic_fixture(q2_fixture *f) {
  memset(f, 0, sizeof *f);
  f->stats[0] = 1;
  f->stats[1] = 100;
  f->stats[2] = 2;
  f->stats[3] = 50;
  f->stats[4] = 3;
  f->stats[5] = 75;
  f->configs[0] = (qa_native_q2_config_string){ 5, q2_statusbar };
  f->configs[1] = (qa_native_q2_config_string){ 545, "i_health" };
  f->configs[2] = (qa_native_q2_config_string){ 546, "a_shells" };
  f->configs[3] = (qa_native_q2_config_string){ 547, "i_combatarmor" };
  f->frame.protocol = (qa_q2_protocol_identity){ "q2-classic", 34, 0, 0 };
  f->frame.stats = f->stats;
  f->frame.stat_count = 32;
  f->frame.configstrings = f->configs;
  f->frame.configstring_count = 4;
  f->frame.layout = "";
  f->frame.inventory = f->inventory;
  f->frame.inventory_count = 0;
  f->frame.server_frame = 4;
  f->frame.time_milliseconds = 100;
}

static const char *q2_binding_q(void *ctx, const char *command) {
  (void)ctx;
  return strcmp(command, "use Item 23") == 0 ? "Q" : "";
}

static void test_q2_classic_hud(void) {
  q2_fixture f;
  q2_classic_fixture(&f);
  qa_native_q2_hud_operations ops;
  CHECK(qa_q2_native_hud_operations(&f.frame, 640, 480, NULL, NULL, NULL,
                                    NULL, NULL, &ops));
  CHECK(op_is_pic(&ops, 0, 162, 456, "num_1"));
  CHECK(op_is_pic(&ops, 1, 178, 456, "num_0"));
  CHECK(op_is_pic(&ops, 3, 210, 456, "i_health"));
  CHECK(ops_have_pic(&ops, 310, 456, "a_shells"));
  CHECK(ops_have_pic(&ops, 410, 456, "i_combatarmor"));
  bool has_inventory_pic = false;
  for (size_t i = 0; i < ops.count; i++)
    if (ops.operations[i].kind == QA_Q2_HUD_OP_PICTURE
        && strcmp(ops.operations[i].name, "inventory") == 0)
      has_inventory_pic = true;
  CHECK(!has_inventory_pic);
  qa_native_q2_hud_operations_free(&ops);
  /* Donor flashing/suppression + malformed index abort. */
  f.stats[1] = 12;
  f.stats[3] = -1;
  f.stats[5] = 0;
  f.stats[15] = 1;
  CHECK(qa_q2_layout_operations("hnum anum rnum", &f.frame, 320, 240, NULL,
                                NULL, &ops));
  CHECK(ops.count == 3 && op_is_pic(&ops, 0, 0, 0, "field_3")
        && ops.operations[1].name != NULL
        && strcmp(ops.operations[1].name, "anum_1") == 0
        && strcmp(ops.operations[2].name, "anum_2") == 0);
  qa_native_q2_hud_operations_free(&ops);
  CHECK_ABORTS(qa_q2_layout_operations("pic 999", &f.frame, 320, 240, NULL,
                                       NULL, &ops));
  /* svc_inventory scroll + exact use binding row. */
  q2_classic_fixture(&f);
  f.stats[13] = 2;
  f.stats[12] = 23;
  for (size_t i = 2; i < 27; i++) f.inventory[i] = (double)i;
  f.frame.inventory_count = 256;
  size_t at = 4;
  for (size_t i = 2; i < 27; i++) {
    snprintf(f.item_names[i - 2], sizeof f.item_names[i - 2], "Item %zu", i);
    f.configs[at++] =
        (qa_native_q2_config_string){ 1056 + (double)i, f.item_names[i - 2] };
  }
  f.frame.configstring_count = at;
  CHECK(qa_q2_native_hud_operations(&f.frame, 640, 480, q2_binding_q, NULL,
                                    NULL, NULL, NULL, &ops));
  CHECK(ops_have_pic(&ops, 192, 128, "inventory"));
  size_t item_rows = 0;
  bool first_ok = false, selected_ok = false;
  for (size_t i = 0; i < ops.count; i++) {
    const qa_native_q2_hud_operation *op = &ops.operations[i];
    if (op->kind == QA_Q2_HUD_OP_TEXT && strstr(op->text, "Item ") != NULL) {
      if (item_rows == 0)
        first_ok = strcmp(op->text, "        10 Item 10") == 0 && op->alternate;
      if (strcmp(op->text, "     Q  23 Item 23") == 0 && near(op->x, 216)
          && near(op->y, 264) && !op->alternate)
        selected_ok = true;
      item_rows++;
    }
  }
  CHECK(item_rows == 17 && first_ok && selected_ok);
  qa_native_q2_hud_operations_free(&ops);
  CHECK(qa_q2_native_hud_operations(&f.frame, 640, 480, NULL, NULL,
                                    "layout-overlay", NULL, NULL, &ops));
  CHECK(ops.count == 0);
  qa_native_q2_hud_operations_free(&ops);
}

static qa_vec2 rr_measure6(void *ctx, const char *text) {
  (void)ctx;
  double widest = 0, lines = 1, current = 0;
  for (const char *p = text;; p++) {
    if (*p == '\n' || *p == '\0') {
      if (current * 6 > widest) widest = current * 6;
      current = 0;
      if (*p == '\0') break;
      lines++;
    } else {
      current++;
    }
  }
  return (qa_vec2){ widest, lines * 10 };
}
static char rr_localize_buffer[256];
static const char *rr_localize(void *ctx, const char *text,
                               const char *const *args, size_t arg_count) {
  (void)ctx;
  if (strcmp(text, "$g_pc_goals") == 0) return "Goals";
  if (strcmp(text, "$m_eou_press_button") == 0) return "Press a button";
  if (strcmp(text, "$g_score_time") == 0) {
    snprintf(rr_localize_buffer, sizeof rr_localize_buffer, "Time: %s",
             arg_count > 0 ? args[0] : "");
    return rr_localize_buffer;
  }
  if (strcmp(text, "{}: 2/4") == 0) return "Goals: 2/4";
  if (strcmp(text, "$level") == 0) return "Level";
  if (strcmp(text, "$score") == 0) return "Score";
  if (strcmp(text, "$boss") == 0) return "Boss";
  if (strcmp(text, "$story") == 0) return "First line\nSecond line";
  if (strcmp(text, "$shells") == 0) return "Shells";
  return text;
}

static void test_q2_rerelease_hud(void) {
  q2_fixture f;
  memset(&f, 0, sizeof f);
  qa_native_q2_hud_table table;
  qa_native_q2_hud_table_init(&table);
  qa_native_q2_hud_environment env;
  memset(&env, 0, sizeof env);
  env.table = &table;
  env.use_font = true;
  env.font_line_height = 10;
  env.measure = rr_measure6;
  env.localize = rr_localize;
  f.frame.protocol = (qa_q2_protocol_identity){ "q2-rerelease", 1038, 0, 0 };
  f.frame.stats = f.stats;
  f.frame.stat_count = 64;
  f.frame.layout = "";
  f.frame.has_frame_time_milliseconds = true;
  f.frame.frame_time_milliseconds = 50;
  const char *layout =
      "xv 265 yv 164 loc_rstring2 1 \"{}: 2/4\" \"$g_pc_goals\" if 0 ifgef 0 "
      "loc_string 0 \"hidden\" endif endif ifgef 8 yb -48 xv 0 loc_cstring2 0 "
      "\"$m_eou_press_button\" endif xr -8 yt 8 time_limit 1208";
  qa_native_q2_hud_operations ops;
  f.frame.server_frame = 7;
  CHECK(qa_q2_layout_operations(layout, &f.frame, 640, 480, NULL, &env, &ops));
  CHECK(ops_have_text(&ops, QA_Q2_HUD_OP_FONT_TEXT, 365, 283, "Goals: 2/4",
                      true));
  bool any_hidden = false;
  for (size_t i = 0; i < ops.count; i++)
    if (ops.operations[i].text != NULL
        && (strcmp(ops.operations[i].text, "hidden") == 0
            || strcmp(ops.operations[i].text, "Press a button") == 0))
      any_hidden = true;
  CHECK(!any_hidden);
  qa_native_q2_hud_operations_free(&ops);
  f.frame.server_frame = 8;
  CHECK(qa_q2_layout_operations(layout, &f.frame, 640, 480, NULL, &env, &ops));
  CHECK(ops_have_text(&ops, QA_Q2_HUD_OP_FONT_TEXT, 278, 431,
                      "Press a button", true));
  CHECK(ops_have_text(&ops, QA_Q2_HUD_OP_FONT_TEXT, 566, 7, "Time: 01:00",
                      true));
  qa_native_q2_hud_operations_free(&ops);
  CHECK_ABORTS(qa_q2_layout_operations("loc_string 8 \"bad\"", &f.frame, 320,
                                       240, NULL, &env, &ops));
  CHECK_ABORTS(qa_q2_layout_operations("endif", &f.frame, 320, 240, NULL,
                                       &env, &ops));
  /* Table shared between authored status and svc_layout. */
  f.stats[13] = 1;
  f.configs[0] = (qa_native_q2_config_string){
    5, "start_table 2 \"$level\" \"$score\" table_row 2 \"Outer Base\" \"12\""
  };
  f.frame.configstrings = f.configs;
  f.frame.configstring_count = 1;
  f.frame.layout = "xv 160 yt 0 draw_table";
  CHECK(qa_q2_native_hud_operations(&f.frame, 640, 480, NULL, NULL, NULL,
                                    NULL, &env, &ops));
  bool fill_ok = false;
  for (size_t i = 0; i < ops.count; i++) {
    const qa_native_q2_hud_operation *op = &ops.operations[i];
    if (op->kind == QA_Q2_HUD_OP_FILL && near(op->x, 272) && near(op->y, 8)
        && near(op->width, 96) && near(op->height, 18))
      fill_ok = true;
  }
  CHECK(fill_ok);
  CHECK(ops_have_text(&ops, QA_Q2_HUD_OP_FONT_TEXT, 287, 7, "Level", true));
  CHECK(ops_have_text(&ops, QA_Q2_HUD_OP_FONT_TEXT, 356, 16, "12", false));
  qa_native_q2_hud_operations_free(&ops);
  /* lives_num + packed health bars + dogtag. */
  memset(f.stats, 0, sizeof f.stats);
  f.stats[49] = 2;
  f.stats[52] = 255 | (128 << 8);
  f.configs[0] = (qa_native_q2_config_string){ 12104, "$boss" };
  f.configs[1] =
      (qa_native_q2_config_string){ 11582, "Marine\\male/grunt\\eagle" };
  f.frame.configstring_count = 2;
  CHECK(qa_q2_layout_operations(
      "xl 0 yt 20 lives_num 49 health_bars xl 10 yt 80 dogtag 0", &f.frame,
      320, 240, NULL, &env, &ops));
  CHECK(op_is_pic(&ops, 0, 2, 20, "anum_0"));
  bool bar_ok = false, dogtag_ok = false;
  for (size_t i = 0; i < ops.count; i++) {
    const qa_native_q2_hud_operation *op = &ops.operations[i];
    if (op->kind == QA_Q2_HUD_OP_FILL && near(op->x, 80) && near(op->y, 30)
        && near(op->width, 160) && near(op->height, 4)
        && near(op->color.x, 1))
      bar_ok = true;
    if (op->kind == QA_Q2_HUD_OP_SIZED_PICTURE && near(op->x, 10)
        && near(op->y, 80) && near(op->width, 198)
        && strcmp(op->name, "/tags/eagle.pcx") == 0)
      dogtag_ok = true;
  }
  CHECK(bar_ok && dogtag_ok);
  qa_native_q2_hud_operations_free(&ops);
  qa_native_q2_hud_table_destroy(&table);
}

static void test_hud_index(void) {
  qa_ui_hud_rects rects;
  CHECK(qa_ui_hud_vital_rects_for(2, 1, 42, &rects));
  CHECK(rects.count == 2 && near(rects.rects[0].x, 160)
        && near(rects.rects[0].y, 434) && near(rects.rects[0].width, 156));
  qa_ui_hud_rects_free(&rects);
  test_ui_harness h;
  harness_init(&h);
  qa_seat_hud_messages *messages = qa_seat_hud_messages_create(h.seat);
  CHECK(messages != NULL);
  qa_source_time zero = { QA_SOURCE_TIME_MILLISECONDS, 0 };
  qa_source_time five_s = { QA_SOURCE_TIME_SECONDS, 5 };
  qa_seat_hud_messages_notify(messages, h.seat, "hello", true, zero, five_s);
  qa_seat_hud_messages_center_print(messages, h.seat, "centered", zero, five_s,
                                    true, 125);
  qa_seat_hud_active active;
  CHECK(qa_seat_hud_messages_active(messages, 100, &active));
  CHECK(active.notification_count == 1 && active.has_center_print
        && strcmp(active.center_print.text, "centered") == 0);
  qa_seat_hud_active_free(&active);
  /* Queued non-instant print starts after the current one ends. */
  qa_seat_hud_messages_center_print(messages, h.seat, "next", zero,
                                    (qa_source_time){ QA_SOURCE_TIME_MILLISECONDS, 1000 },
                                    false, 100);
  CHECK(qa_seat_hud_messages_active(messages, 5100, &active));
  CHECK(active.has_center_print
        && strcmp(active.center_print.text, "next") == 0
        && near(active.center_print.starts.value, 5000)
        && near(active.center_print.duration.value, 1400));
  qa_seat_hud_active_free(&active);
  qa_ui_hud_point_of_interest point;
  memset(&point, 0, sizeof point);
  point.id = 7;
  point.image = "resource:test:poi";
  point.expires_milliseconds = 10000;
  CHECK(qa_seat_hud_messages_add_point(messages, h.seat, point, 0, 64));
  CHECK(qa_seat_hud_messages_active(messages, 0, &active));
  CHECK(active.point_count == 1);
  qa_seat_hud_active_free(&active);
  qa_seat_hud_messages_remove_point(messages, 7);
  CHECK(qa_seat_hud_messages_active(messages, 0, &active));
  CHECK(active.point_count == 0);
  qa_seat_hud_active_free(&active);
  qa_seat_id other = qa_identity_owner_seat(&h.identity, 1);
  CHECK_ABORTS(qa_seat_hud_messages_notify(messages, other, "x", false, zero,
                                           five_s));
  /* drawCommonHud over empty data: clip, crosshair fills, clip. */
  qa_ui_common_hud_data data = qa_ui_empty_hud_data(h.seat);
  qa_ui_seat_preferences prefs;
  qa_ui_seat_preferences_init(&prefs, h.seat, NULL);
  qa_ui_common_hud_draw_options options;
  memset(&options, 0, sizeof options);
  options.skin = qa_ui_default_skin("resource:test:font");
  options.measure_text = measure8;
  options.preferences = qa_ui_seat_preferences_values(&prefs);
  options.messages = messages;
  options.localize = identity_localize;
  qa_ui_draw_context context;
  memset(&context, 0, sizeof context);
  context.binding.seat = h.seat;
  context.binding.viewport = rect(0, 0, 640, 480);
  context.binding.safe_area = rect(0, 0, 640, 480);
  context.binding.hud_scale = 1;
  context.time_milliseconds = 100;
  qa_ui_hud_commands commands;
  CHECK(qa_ui_draw_common_hud(context, &data, &options, &commands));
  CHECK(commands.count >= 4
        && commands.commands[0].kind == QA_UI_DRAW_CLIP
        && commands.commands[commands.count - 1].kind == QA_UI_DRAW_CLIP
        && !commands.commands[commands.count - 1].has_clip_rect);
  qa_ui_hud_commands_free(&commands);
  CHECK(qa_ui_hud_vital_occupied_rects(context, 3, 1, 1.5, 8, &rects));
  CHECK(rects.count == 3 && rects.rects[0].x < rects.rects[1].x);
  qa_ui_hud_rects_free(&rects);
  qa_ui_common_hud_data wrong = qa_ui_empty_hud_data(other);
  CHECK_ABORTS(qa_ui_draw_common_hud(context, &wrong, &options, &commands));
  qa_seat_hud_messages_destroy(messages);
  harness_destroy(&h);
}

static void send_click(test_ui_harness *h, double x, double y) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = h->seat;
  event.kind = "mouse-motion";
  event.position = (qa_vec2){ x, y };
  (void)qa_native_ui_controller_input(h->controller, event);
  memset(&event, 0, sizeof event);
  event.seat = h->seat;
  event.kind = "mouse-button";
  event.button = 1;
  event.down = true;
  (void)qa_native_ui_controller_input(h->controller, event);
  event.down = false;
  (void)qa_native_ui_controller_input(h->controller, event);
}

typedef struct fake_library {
  qa_ui_library_entry entries[2];
  int refreshes;
  char opened[8][32];
  size_t opened_count;
} fake_library;
static const qa_ui_library_entry *fake_library_entries(void *ctx,
                                                       size_t *out) {
  fake_library *f = ctx;
  *out = 2;
  return f->entries;
}
static const char *fake_library_status(void *ctx) {
  (void)ctx;
  return "";
}
static void fake_library_refresh(void *ctx) { ((fake_library *)ctx)->refreshes++; }
static void fake_library_activate(void *ctx, const char *id) {
  fake_library *f = ctx;
  snprintf(f->opened[f->opened_count], sizeof f->opened[0], "%s", id);
  f->opened_count++;
}

typedef struct fake_commands {
  char log[16][96];
  size_t count;
} fake_commands;
static void fake_command_record(void *ctx, const char *name,
                                const char *const *args, size_t arg_count) {
  fake_commands *f = ctx;
  char *line = f->log[f->count];
  snprintf(line, sizeof f->log[0], "%s", name);
  for (size_t i = 0; i < arg_count; i++) {
    strncat(line, " ", sizeof f->log[0] - strlen(line) - 1);
    strncat(line, args[i], sizeof f->log[0] - strlen(line) - 1);
  }
  f->count++;
}
static const qa_ui_hud_inventory_item fake_inv_items[1] = {
  { "q2:item_quad", "Quad damage", 1, true, NULL, NULL }
};
static const qa_ui_hud_inventory_item *fake_inv_list(void *ctx, size_t *out) {
  (void)ctx;
  *out = 1;
  return fake_inv_items;
}

static void test_library_inventory_match_menus(void) {
  test_ui_harness h;
  harness_init(&h);
  /* library: keyboard selection does not activate; Enter opens. */
  fake_library library;
  memset(&library, 0, sizeof library);
  library.entries[0] = (qa_ui_library_entry){ "one", "Demo one", NULL, NULL };
  library.entries[1] = (qa_ui_library_entry){ "two", "Demo two", NULL, NULL };
  qa_ui_library_menu_service service;
  memset(&service, 0, sizeof service);
  service.ctx = &library;
  service.entries = fake_library_entries;
  service.status = fake_library_status;
  service.refresh = fake_library_refresh;
  service.activate = fake_library_activate;
  qa_ui_settings_menus menus =
      qa_ui_register_library_menu(h.controller, "menu:test:library", "Demos",
                                  &service);
  qa_native_ui_controller_open_menu(h.controller, menus.root);
  CHECK(library.refreshes == 1);
  CHECK(draw_has(&h, "Demo one"));
  send_key(&h, QA_KEY_TAB);
  send_key(&h, QA_KEY_DOWN);
  CHECK(library.opened_count == 0);
  send_key(&h, QA_KEY_ENTER);
  CHECK(library.opened_count == 1 && strcmp(library.opened[0], "two") == 0);
  qa_ui_settings_menus_dispose(&menus);
  /* inventory: activate uses the item; close dispatches putaway. */
  fake_commands commands;
  memset(&commands, 0, sizeof commands);
  qa_ui_settings_menus inv_menus = qa_ui_register_inventory_menu(
      h.controller, fake_inv_list, NULL, fake_command_record, &commands);
  qa_native_ui_controller_open_menu(h.controller, inv_menus.root);
  CHECK(draw_has(&h, "Quad damage"));
  send_key(&h, QA_KEY_ENTER);
  CHECK(commands.count == 1
        && strcmp(commands.log[0], "use q2:item_quad") == 0);
  send_key(&h, QA_KEY_ESCAPE);
  CHECK(commands.count == 2 && strcmp(commands.log[1], "putaway") == 0);
  qa_ui_settings_menus_dispose(&inv_menus);
  /* match: Join team dispatches through the command callback. */
  memset(&commands, 0, sizeof commands);
  qa_ui_settings_menus match_menus = qa_ui_register_match_menu(
      h.controller, fake_command_record, &commands);
  qa_native_ui_controller_open_menu(h.controller, match_menus.root);
  CHECK(draw_has(&h, "Join team"));
  send_click(&h, 100, 145); /* Join team button (64,130,496,30) */
  CHECK(commands.count == 1 && strcmp(commands.log[0], "team red") == 0);
  qa_ui_settings_menus_dispose(&match_menus);
  harness_destroy(&h);
}

typedef struct fake_mods {
  qa_ui_mod_menu_row rows[3];
  bool enabled[3];
  int refreshes;
} fake_mods;
static const qa_ui_mod_menu_row *fake_mods_rows(void *ctx, size_t *out) {
  fake_mods *f = ctx;
  for (size_t i = 0; i < 3; i++) f->rows[i].enabled = f->enabled[i];
  *out = 3;
  return f->rows;
}
static void fake_mods_set_enabled(void *ctx, const char *id, bool enabled) {
  fake_mods *f = ctx;
  for (size_t i = 0; i < 3; i++)
    if (strcmp(f->rows[i].id, id) == 0) f->enabled[i] = enabled;
}
static void fake_mods_refresh(void *ctx) { ((fake_mods *)ctx)->refreshes++; }
static const char *fake_mods_status(void *ctx) {
  (void)ctx;
  return "";
}

typedef struct fake_saves {
  qa_ui_startup_save_row rows[1];
  int saves;
  int refreshes;
  char saved_name[64];
  bool queue;                 /* settle save later, as the application's step does */
  bool pending;
  qa_ui_saves_settle settle;  /* the queued save's settlement */
} fake_saves;
static qa_ui_startup_save_list fake_saves_list(void *ctx) {
  fake_saves *f = ctx;
  return (qa_ui_startup_save_list){ f->rows, 1, NULL };
}
static void fake_saves_refresh(void *ctx, qa_ui_saves_settle settle) {
  fake_saves *f = ctx;
  f->refreshes++;
  settle.fn(settle.ctx, NULL);
}
static const char *fake_saves_unavailable(void *ctx, const char *action) {
  (void)ctx;
  (void)action;
  return NULL;
}
static void fake_saves_save(void *ctx, const char *name,
                            const char *overwrite, qa_ui_saves_settle settle) {
  (void)overwrite;
  fake_saves *f = ctx;
  f->saves++;
  snprintf(f->saved_name, sizeof f->saved_name, "%s", name);
  if (f->queue) {
    f->pending = true;
    f->settle = settle;
    return;
  }
  settle.fn(settle.ctx, NULL);
}
static void fake_saves_load(void *ctx, const char *id,
                            qa_ui_saves_settle settle) {
  (void)ctx;
  (void)id;
  settle.fn(settle.ctx, NULL);
}

static void test_mods_saves_menus(void) {
  test_ui_harness h;
  harness_init(&h);
  /* mods: Tab to the list, Enter toggles; unavailable stays off. */
  fake_mods mods;
  memset(&mods, 0, sizeof mods);
  mods.rows[0] =
      (qa_ui_mod_menu_row){ "q1:weather", "Storm effects", "Quake", false, NULL };
  mods.rows[1] = (qa_ui_mod_menu_row){ "q2:monsters", "Monster variety",
                                       "Quake II", false, NULL };
  mods.rows[2] = (qa_ui_mod_menu_row){
    "q2:expansion", "Expansion enemies", "Quake II rerelease", false,
    "Requires installed expansion content."
  };
  qa_ui_mod_menu_service mod_service;
  memset(&mod_service, 0, sizeof mod_service);
  mod_service.ctx = &mods;
  mod_service.rows = fake_mods_rows;
  mod_service.set_enabled = fake_mods_set_enabled;
  mod_service.refresh = fake_mods_refresh;
  mod_service.status = fake_mods_status;
  qa_ui_settings_menus mod_menus =
      qa_ui_register_mod_menu(h.controller, &mod_service);
  qa_native_ui_controller_open_menu(h.controller, mod_menus.root);
  CHECK(mods.refreshes == 1);
  CHECK(draw_has(&h, "Disabled"));
  send_key(&h, QA_KEY_TAB);
  send_key(&h, QA_KEY_ENTER);
  CHECK(mods.enabled[0] && !mods.enabled[1]);
  CHECK(draw_has(&h, "Disable"));
  send_key(&h, QA_KEY_END);
  CHECK(draw_has(&h, "Requires installed expansion content."));
  send_key(&h, QA_KEY_ENTER);
  CHECK(!mods.enabled[2]);
  qa_ui_settings_menus_dispose(&mod_menus);
  /* saves with no service: remote-games reason is shown. */
  qa_ui_saved_game_menus no_saves =
      qa_ui_register_saved_game_menus(h.controller, NULL);
  qa_native_ui_controller_open_menu(h.controller, no_saves.load);
  CHECK(draw_has(&h, "Remote games cannot be saved or loaded here."));
  qa_ui_saved_game_menus_dispose(&no_saves);
  /* saves: New picks the first free "Save %03d" draft and writes. */
  fake_saves saves;
  memset(&saves, 0, sizeof saves);
  saves.rows[0] = (qa_ui_startup_save_row){ "slot-1", "Save 001", "base1",
                                            "q2", 0, NULL };
  qa_ui_saved_game_menu_service save_service;
  memset(&save_service, 0, sizeof save_service);
  save_service.ctx = &saves;
  save_service.list = fake_saves_list;
  save_service.refresh = fake_saves_refresh;
  save_service.unavailable = fake_saves_unavailable;
  save_service.save = fake_saves_save;
  save_service.load = fake_saves_load;
  qa_ui_saved_game_menus save_menus =
      qa_ui_register_saved_game_menus(h.controller, &save_service);
  qa_native_ui_controller_open_menu(h.controller, save_menus.save);
  CHECK(draw_has(&h, "New saved game"));
  CHECK(draw_has(&h, "Save 001 - base1"));
  send_click(&h, 100, 100); /* New saved game (menuRow 0) */
  CHECK(menu_is(&h, "menu:saves:name"));
  CHECK(draw_has(&h, "Save 002"));
  send_click(&h, 100, 184); /* Save game (menuRow 3) */
  CHECK(saves.saves == 1 && strcmp(saves.saved_name, "Save 002") == 0);
  /* A queued save (TS saveMenu settles it in step): the menu stays busy with
   * "Working..." until the promise settles; a rejection shows its message. */
  saves.queue = true;
  qa_native_ui_controller_open_menu(h.controller, save_menus.save);
  send_click(&h, 100, 100); /* New saved game */
  CHECK(menu_is(&h, "menu:saves:name"));
  int refreshes = saves.refreshes;
  send_click(&h, 100, 184); /* Save game */
  CHECK(saves.saves == 2 && saves.pending);
  CHECK(draw_has(&h, "Working..."));
  send_click(&h, 100, 184); /* busy: the second write is refused */
  CHECK(saves.saves == 2);
  saves.pending = false;
  saves.settle.fn(saves.settle.ctx, "Disk full");
  CHECK(saves.refreshes == refreshes); /* rejected: no refresh, menu stays */
  CHECK(menu_is(&h, "menu:saves:name"));
  CHECK(draw_has(&h, "Disk full"));
  send_click(&h, 100, 184); /* Save game again, resolved later */
  CHECK(saves.saves == 3 && saves.pending);
  saves.pending = false;
  saves.settle.fn(saves.settle.ctx, NULL);
  CHECK(saves.refreshes == refreshes + 1); /* resolved: refresh, then close */
  CHECK(!menu_is(&h, "menu:saves:name"));
  /* disposed while a save is pending: the settle still runs (TS GC). */
  qa_native_ui_controller_open_menu(h.controller, save_menus.save);
  send_click(&h, 100, 100);
  send_click(&h, 100, 184);
  CHECK(saves.pending);
  qa_ui_saved_game_menus_dispose(&save_menus);
  saves.pending = false;
  saves.settle.fn(saves.settle.ctx, NULL);
  harness_destroy(&h);
}
/* ---- end appended hud/library/mods/saves section ---- */

/* ---- legacy menu/runtime/input (ui/common/legacy/{menu,runtime,input}.ts;
 * synthetic counterpart of tests/ui/common/legacy/retail.test.ts) ---- */

static const char *legacy_menu_text =
    "assetGlobalDef {\n"
    "  fadeClamp 0.75\n"
    "  fadeCycle 10\n"
    "  fadeAmount 0.1\n"
    "  shadowColor 0 0 0 0.25\n"
    "  cursor \"ui/cursor.tga\"\n"
    "}\n"
    "menuDef {\n"
    "  name \"main\"\n"
    "  rect 0 0 640 480\n"
    "  visible 1\n"
    "  onOpen { setCvar ui_opened 1 }\n"
    "  itemDef {\n"
    "    name \"play\"\n"
    "    group \"grpMenu\"\n"
    "    type 1\n"
    "    rect 100 100 120 24\n"
    "    visible 1\n"
    "    text \"Play\"\n"
    "    action { show confirm ; hide grpMenu }\n"
    "  }\n"
    "  itemDef {\n"
    "    name \"confirm\"\n"
    "    type 1\n"
    "    rect 100 140 120 24\n"
    "    text \"Confirm\"\n"
    "    action { exec \"map_restart\" ; close \"main\" }\n"
    "  }\n"
    "  itemDef {\n"
    "    name \"volume\"\n"
    "    type 12\n"
    "    rect 100 180 120 24\n"
    "    visible 1\n"
    "    cvar \"s_volume\"\n"
    "    cvarFloatList { \"Low\" 0.25 \"High\" 0.75 }\n"
    "  }\n"
    "  itemDef {\n"
    "    name \"ext\"\n"
    "    type 1\n"
    "    rect 100 220 120 24\n"
    "    visible 1\n"
    "    text \"Ext\"\n"
    "    action { myExternal 42 }\n"
    "  }\n"
    "}\n";

static const char *legacy_set_text = "{ loadMenu { \"ui/test.menu\" } }";

static bool legacy_resolve_named(const char *path, qa_ui_script_source *out) {
  const char *text = NULL;
  if (strcmp(path, "ui/menus.txt") == 0 || strcmp(path, "ui/hud.txt") == 0)
    text = legacy_set_text;
  else if (strcmp(path, "ui/test.menu") == 0)
    text = legacy_menu_text;
  if (text == NULL) return false;
  memset(out, 0, sizeof *out);
  out->path = path;
  out->text = (const uint8_t *)text;
  out->text_length = strlen(text);
  return true;
}

static bool legacy_resolve_root(void *ctx, const char *path,
                                qa_ui_script_source *out) {
  (void)ctx;
  return legacy_resolve_named(path, out);
}

static bool legacy_resolve_include(void *ctx,
                                   const qa_ui_script_include_request *request,
                                   qa_ui_script_source *out) {
  (void)ctx;
  if (legacy_resolve_named(request->requested_path, out)) return true;
  /* posix join(dirname(from), requested) */
  const char *slash = strrchr(request->from_path, '/');
  if (slash == NULL) return false;
  size_t dir_length = (size_t)(slash - request->from_path) + 1;
  char joined[256];
  snprintf(joined, sizeof joined, "%.*s%s", (int)dir_length,
           request->from_path, request->requested_path);
  static char joined_path[256];
  snprintf(joined_path, sizeof joined_path, "%s", joined);
  return legacy_resolve_named(joined_path, out);
}

static double legacy_random(void *ctx) {
  (void)ctx;
  return 7;
}

typedef struct legacy_host_log {
  int prints, play_local, backgrounds, stops, restarts, external;
  int fonts, pictures, pauses;
  bool overstrike;
  char external_token[32];
} legacy_host_log;

static void legacy_print(void *ctx, const char *text) {
  (void)text;
  ((legacy_host_log *)ctx)->prints++;
}
static void legacy_play_local(void *ctx, qa_ui_runtime_sound_arg sound) {
  (void)sound;
  ((legacy_host_log *)ctx)->play_local++;
}
static void legacy_start_background(void *ctx, const char *path) {
  (void)path;
  ((legacy_host_log *)ctx)->backgrounds++;
}
static void legacy_stop_background(void *ctx) {
  ((legacy_host_log *)ctx)->stops++;
}
static void legacy_register_font(void *ctx, const char *path,
                                 double point_size) {
  (void)path;
  (void)point_size;
  ((legacy_host_log *)ctx)->fonts++;
}
static bool legacy_register_picture(void *ctx, const char *path,
                                    qa_picture_asset *out) {
  ((legacy_host_log *)ctx)->pictures++;
  memset(out, 0, sizeof *out);
  out->kind = QA_PICTURE_IMAGE;
  out->image.name = path;
  return true;
}
static bool legacy_no_picture(void *ctx, const char *path,
                              qa_picture_asset *out) {
  (void)ctx;
  (void)path;
  (void)out;
  return false;
}
static bool legacy_no_sound(void *ctx, const char *path, qa_pcm_sound *out) {
  (void)ctx;
  (void)path;
  (void)out;
  return false;
}
static qa_q3_pres_scene_model legacy_register_model(void *ctx,
                                                    const char *path) {
  (void)ctx;
  (void)path;
  return qa_q3_pres_default_model;
}
static bool legacy_no_model(void *ctx, const char *path,
                            qa_q3_pres_scene_model *out) {
  (void)ctx;
  (void)path;
  (void)out;
  return false;
}
static qa_ui_cinematic_asset legacy_prepare_cinematic(void *ctx,
                                                      const char *path) {
  (void)ctx;
  qa_ui_cinematic_asset asset = {path};
  return asset;
}
static bool legacy_cine_play(void *ctx, qa_ui_cinematic_asset asset,
                             qa_rect rect, qa_ui_cinematic_instance *out) {
  (void)ctx;
  (void)asset;
  (void)rect;
  (void)out;
  return false;
}
static void legacy_cine_noop(void *ctx, double handle) {
  (void)ctx;
  (void)handle;
}
static void legacy_cine_run(void *ctx, double handle, double time) {
  (void)ctx;
  (void)handle;
  (void)time;
}
static void legacy_cine_draw(void *ctx, double handle, qa_rect rect,
                             const qa_draw2d *draw) {
  (void)ctx;
  (void)handle;
  (void)rect;
  (void)draw;
}
static void legacy_paint_model(void *ctx,
                               const qa_ui_model_paint_request *request) {
  (void)ctx;
  (void)request;
}
static double legacy_feeder_count(void *ctx, double feeder) {
  (void)ctx;
  (void)feeder;
  return 0;
}
static bool legacy_feeder_item(void *ctx, double feeder, double index,
                               double column,
                               qa_ui_runtime_feeder_item *out) {
  (void)ctx;
  (void)feeder;
  (void)index;
  (void)column;
  (void)out;
  return false;
}
static bool legacy_feeder_image(void *ctx, double feeder, double index,
                                qa_picture_asset *out) {
  (void)ctx;
  (void)feeder;
  (void)index;
  (void)out;
  return false;
}
static void legacy_feeder_select(void *ctx, double feeder, double index) {
  (void)ctx;
  (void)feeder;
  (void)index;
}
static bool legacy_od_visible(void *ctx, double flags) {
  (void)ctx;
  (void)flags;
  return true;
}
static double legacy_od_zero(void *ctx, double owner_draw) {
  (void)ctx;
  (void)owner_draw;
  return 0;
}
static double legacy_od_width(void *ctx, double owner_draw, double scale) {
  (void)ctx;
  (void)owner_draw;
  (void)scale;
  return 0;
}
static qa_ui_owner_draw_key_result legacy_od_key(void *ctx, double owner_draw,
                                                 double flags, double special,
                                                 double key) {
  (void)ctx;
  (void)owner_draw;
  (void)flags;
  (void)key;
  qa_ui_owner_draw_key_result result = {false, special};
  return result;
}
static void legacy_od_paint(void *ctx,
                            const qa_ui_owner_draw_paint_request *request) {
  (void)ctx;
  (void)request;
}
static void legacy_external(void *ctx, qa_ui_script_cursor *cursor,
                            qa_ui_external_script_context context) {
  legacy_host_log *log = ctx;
  (void)context;
  log->external++;
  qa_ui_menu_script_token token;
  if (qa_ui_script_cursor_next(cursor, &token)) {
    snprintf(log->external_token, sizeof log->external_token, "%s",
             token.text);
    qa_ui_menu_script_token_destroy(&token);
  }
}
static qa_vec4 legacy_team_color(void *ctx) {
  (void)ctx;
  qa_vec4 color = {1, 0, 0, 1};
  return color;
}
static void legacy_stretch_count(void *ctx, qa_rect rect,
                                 qa_text_texture_rect uv,
                                 const qa_picture_asset *picture) {
  (void)rect;
  (void)uv;
  (void)picture;
  (*(int *)ctx)++;
}
static void legacy_set_color_count(void *ctx, const qa_vec4 *color) {
  (void)ctx;
  (void)color;
}

static qa_ui_menu_parse_host legacy_host(void) {
  qa_ui_menu_parse_host host;
  memset(&host, 0, sizeof host);
  host.kind = QA_UI_PARSE_HOST_RESOLVED;
  host.resolver.resolve_root = legacy_resolve_root;
  host.resolver.resolve = legacy_resolve_include;
  host.random = legacy_random;
  return host;
}

static void test_legacy_menu_runtime(void) {
  legacy_host_log log;
  memset(&log, 0, sizeof log);
  qa_ui_team_arena_memory *memory = qa_ui_team_arena_memory_create(
      "qvm32", legacy_print, &log, QA_UI_TEAM_ARENA_MODULE_UI);
  CHECK(memory != NULL);
  qa_ui_menu_parse_host host = legacy_host();
  qa_ui_menu_parse_options parse_options;
  memset(&parse_options, 0, sizeof parse_options);
  parse_options.has_memory = true;
  parse_options.memory.kind = QA_UI_MENU_MEMORY_QVM32;
  parse_options.memory.memory = memory;
  qa_ui_menu_load_plan plan = {"ui/menus.txt"};
  qa_ui_menu_definitions definitions;
  qa_ui_load_menu_definitions(&host, &plan, NULL, &parse_options,
                              &definitions);
  CHECK(definitions.diagnostic_count == 0);
  CHECK(definitions.menu_count == 1);
  CHECK(definitions.loaded_file_count == 2);
  CHECK(definitions.registration_kind == QA_UI_REGISTRATION_DEFERRED);
  CHECK(definitions.registration_event_count == 1);
  CHECK(near(definitions.assets.fade_clamp, 0.75));
  CHECK(near(definitions.assets.shadow_fade_clamp, 0.25));
  CHECK(definitions.assets.cursor != NULL
        && strcmp(definitions.assets.cursor->path, "ui/cursor.tga") == 0);
  qa_ui_menu_definition *menu = definitions.menus[0];
  CHECK(qa_ui_menu_item_count(menu) == 4);
  char *menu_name = qa_ui_window_name(&menu->window);
  CHECK(menu_name != NULL && strcmp(menu_name, "main") == 0);
  free(menu_name);
  qa_ui_item_definition *play = qa_ui_menu_item_at(menu, 0);
  char *play_text = qa_ui_item_text(play);
  CHECK(play_text != NULL && strcmp(play_text, "Play") == 0);
  free(play_text);
  qa_rect play_rect = qa_ui_window_rect(&play->window);
  CHECK(near(play_rect.x, 100) && near(play_rect.y, 100));
  CHECK((qa_ui_window_flags(&play->window) & QA_UI_WINDOW_VISIBLE) != 0);
  qa_ui_item_definition *confirm = qa_ui_menu_item_at(menu, 1);
  CHECK((qa_ui_window_flags(&confirm->window) & QA_UI_WINDOW_VISIBLE) == 0);
  qa_ui_item_definition *volume = qa_ui_menu_item_at(menu, 2);
  CHECK(qa_ui_item_behavior(volume) == QA_UI_BEHAVIOR_MULTI);
  qa_ui_multi_definition multi;
  CHECK(qa_ui_item_multi_data(volume, &multi));
  CHECK(qa_ui_multi_count(&multi) == 2);
  char *high = qa_ui_multi_label(&multi, 1);
  CHECK(high != NULL && strcmp(high, "High") == 0);
  free(high);
  CHECK(near(qa_ui_multi_number_value(&multi, 1), 0.75));
  CHECK_ABORTS((void)qa_ui_multi_label(&multi, 32));
  CHECK_ABORTS((void)qa_ui_menu_item_at(menu, 96));

  /* The default HUD plan parses the same set through COM text. */
  qa_ui_team_arena_memory *hud_memory = qa_ui_team_arena_memory_create(
      "qvm32", legacy_print, &log, QA_UI_TEAM_ARENA_MODULE_UI);
  qa_ui_menu_parse_options hud_options = parse_options;
  hud_options.memory.memory = hud_memory;
  qa_ui_menu_load_plan hud_plan = qa_ui_default_hud_menu_plan();
  qa_ui_menu_definitions hud_definitions;
  qa_ui_load_menu_definitions(&host, &hud_plan, NULL, &hud_options,
                              &hud_definitions);
  CHECK(hud_definitions.menu_count == 1);
  qa_ui_menu_definitions_destroy(&hud_definitions);
  qa_ui_team_arena_memory_destroy(hud_memory);

  /* Missing set file is a ScriptLanguageError abort. */
  qa_ui_menu_load_plan missing = {"ui/nope.txt"};
  qa_ui_menu_definitions dropped;
  CHECK_ABORTS(qa_ui_load_menu_definitions(&host, &missing, NULL,
                                           &parse_options, &dropped));

  /* Runtime over the parsed definitions. */
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "legacy-ui"));
  qa_seat_id seat = qa_identity_owner_seat(&owner, 1);
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = seat;
  context.origin.client = qa_identity_owner_client(&owner, 1, 0);
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context = context;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, "s_volume", "0.25", 0, &snapshot))
    qa_cvar_read_free(&snapshot);
  if (qa_cvar_registry_register(cvars, "ui_opened", "0", 0, &snapshot))
    qa_cvar_read_free(&snapshot);

  static qa_q3_registered_font fake_font;
  memset(&fake_font, 0, sizeof fake_font);
  fake_font.glyph_scale = 1;
  fake_font.glyph_count = 256;
  for (int i = 0; i < 256; i++) {
    fake_font.glyphs[i].metrics.x_skip = 8;
    fake_font.glyphs[i].metrics.height = 10;
  }
  qa_ui_runtime_options options;
  memset(&options, 0, sizeof options);
  options.definitions = definitions;
  options.cvars = cvars;
  options.resources.handles.kind = QA_UI_HANDLES_DIAGNOSTIC;
  options.resources.ctx = &log;
  options.resources.register_font = legacy_register_font;
  options.resources.register_picture = legacy_register_picture;
  options.resources.registered_picture = legacy_no_picture;
  options.resources.register_sound = legacy_no_sound;
  options.resources.registered_sound = legacy_no_sound;
  options.resources.register_model = legacy_register_model;
  options.resources.registered_model = legacy_no_model;
  options.resources.prepare_cinematic = legacy_prepare_cinematic;
  options.fonts.small = &fake_font;
  options.fonts.normal = &fake_font;
  options.fonts.big = &fake_font;
  options.fonts.profile = QA_Q3_FONT_UI;
  options.fonts.small_threshold = 0.25;
  options.fonts.big_threshold = 0.4;
  options.audio.ctx = &log;
  options.audio.play_local = legacy_play_local;
  options.audio.start_background = legacy_start_background;
  options.audio.stop_background = legacy_stop_background;
  options.cinematics.ctx = &log;
  options.cinematics.play = legacy_cine_play;
  options.cinematics.run = legacy_cine_run;
  options.cinematics.draw = legacy_cine_draw;
  options.cinematics.stop = legacy_cine_noop;
  options.paint_model = legacy_paint_model;
  options.feeder.ctx = &log;
  options.feeder.count = legacy_feeder_count;
  options.feeder.item = legacy_feeder_item;
  options.feeder.image = legacy_feeder_image;
  options.feeder.select = legacy_feeder_select;
  options.owner_draw.ctx = &log;
  options.owner_draw.visible = legacy_od_visible;
  options.owner_draw.width = legacy_od_width;
  options.owner_draw.value = legacy_od_zero;
  options.owner_draw.handle_key = legacy_od_key;
  options.owner_draw.paint = legacy_od_paint;
  options.owner_draw.close_cinematic = legacy_cine_noop;
  options.external_script.ctx = &log;
  options.external_script.run = legacy_external;
  options.get_team_color = legacy_team_color;
  qa_ui_runtime *runtime = qa_ui_runtime_create(&options);
  CHECK(runtime != NULL);
  CHECK(qa_ui_runtime_activate(runtime, "main"));
  char *opened = qa_cvar_registry_variable_string(cvars, "ui_opened");
  CHECK(opened != NULL && strcmp(opened, "1") == 0);
  free(opened);
  qa_ui_runtime_snapshot snap = qa_ui_runtime_take_snapshot(runtime);
  CHECK(snap.focused_menu != NULL && strcmp(snap.focused_menu, "main") == 0);
  CHECK(snap.menu_count == 1 && snap.menus[0].item_count == 4);
  CHECK(((int)snap.menus[0].items[0].flags & QA_UI_WINDOW_VISIBLE) != 0);
  CHECK(((int)snap.menus[0].items[1].flags & QA_UI_WINDOW_VISIBLE) == 0);
  qa_ui_runtime_snapshot_destroy(&snap);

  /* Click play -> show confirm, hide play group (the TS LegacyUiSeat is
     gone; keys go straight to the runtime). */
  int stretches = 0;
  qa_draw2d draw;
  memset(&draw, 0, sizeof draw);
  draw.commands.ctx = &stretches;
  draw.commands.seat = seat;
  draw.commands.target = rect(640, 0, 640, 480);
  draw.commands.set_color = legacy_set_color_count;
  draw.commands.stretch_pixels = legacy_stretch_count;
  draw.space = QA_COORD_TEAM_UI_640;
  qa_ui_runtime_frame frame = {1000, 16, &draw};
  qa_ui_runtime_run_frame(runtime, frame, 60);
  qa_ui_key_event click = {QA_UI_KEY_EVENT_KEY, QA_KEY_MOUSE1, true};
  qa_ui_runtime_set_display_cursor(runtime, 110, 110);
  CHECK(qa_ui_runtime_pointer_move(runtime, 110, 110));
  CHECK(qa_ui_runtime_handle_key(runtime, click, 110, 110));
  snap = qa_ui_runtime_take_snapshot(runtime);
  CHECK(((int)snap.menus[0].items[1].flags & QA_UI_WINDOW_VISIBLE) != 0);
  CHECK(((int)snap.menus[0].items[0].flags & QA_UI_WINDOW_VISIBLE) == 0);
  qa_ui_runtime_snapshot_destroy(&snap);
  /* The cgame menu runtime has no command host: exec is an Error. */
  qa_ui_runtime_set_display_cursor(runtime, 110, 150);
  CHECK(qa_ui_runtime_pointer_move(runtime, 110, 150));
  CHECK_ABORTS((void)qa_ui_runtime_handle_key(runtime, click, 110, 150));

  /* Multi cycles the cvar; unknown script commands go external. */
  CHECK(qa_ui_runtime_activate(runtime, "main"));
  qa_ui_runtime_set_display_cursor(runtime, 110, 190);
  CHECK(qa_ui_runtime_pointer_move(runtime, 110, 190));
  qa_ui_key_event mouse_down = {QA_UI_KEY_EVENT_KEY, QA_KEY_MOUSE1, true};
  CHECK(qa_ui_runtime_handle_key(runtime, mouse_down, 110, 190));
  CHECK(near(qa_cvar_registry_variable_value(cvars, "s_volume"), 0.75));
  qa_ui_item_definition *ext = qa_ui_menu_item_at(menu, 3);
  qa_ui_runtime_run_item_script(runtime, "main", "ext",
                                qa_ui_item_action(ext));
  CHECK(log.external == 1);
  CHECK(strcmp(log.external_token, "42") == 0);

  /* Error cases abort with the TS messages. */
  qa_ui_key_event bad_key = {QA_UI_KEY_EVENT_KEY, 40000, true};
  CHECK_ABORTS((void)qa_ui_runtime_handle_key(runtime, bad_key, 0, 0));
  CHECK_ABORTS((void)qa_ui_runtime_pointer_move(runtime, NAN, 0));
  CHECK_ABORTS(qa_ui_runtime_set_display_time(runtime, 2147483648.0));

  qa_ui_runtime_retire(runtime);
  CHECK_ABORTS((void)qa_ui_runtime_take_snapshot(runtime));

  qa_ui_runtime_destroy(runtime);
  qa_cvar_registry_destroy(cvars);
  qa_ui_menu_definitions_destroy(&definitions);
  qa_ui_team_arena_memory_destroy(memory);
  qa_identity_owner_destroy(&owner);
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
  test_q2_classic_hud();
  test_q2_rerelease_hud();
  test_hud_index();
  test_library_inventory_match_menus();
  test_mods_saves_menus();
  test_legacy_menu_runtime();
  if (failures > 0) {
    fprintf(stderr, "test_ui: %d of %d checks FAILED\n", failures, checks);
    return 1;
  }
  printf("test_ui: all %d checks passed\n", checks);
  return 0;
}
