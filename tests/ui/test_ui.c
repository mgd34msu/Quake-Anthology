/*
 * Port of key cases from tests/ui (hud/powerups.test.ts,
 * hud/authored-wheel.test.ts, common/native-ui.test.ts) for the
 * ui/common + ui/hud modules ported so far. TypeScript
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

#include "qa/contracts/ui.h"
#include "qa/text/draw2d.h"
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
  if (failures > 0) {
    fprintf(stderr, "test_ui: %d of %d checks FAILED\n", failures, checks);
    return 1;
  }
  printf("test_ui: all %d checks passed\n", checks);
  return 0;
}
