/* StartupMenu class body (TS startup-menu.ts): menu graph
 * construction over the landed NativeUiController and navigation
 * state transitions, driven through real seat input events against
 * a fake StartupSelectionModel. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/startup_menu.h"
#include "qa/contracts/identity.h"
#include "qa/input/key_codes.h"

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

typedef struct fake {
  qa_identity_owner identity;
  qa_seat_id seat;
  int plays, quits, refreshes, arena_refreshes;
  char loaded[64], preset_id[64], arena[64], selected[128];
  double preset_skill;
  bool preset_arena_null, toggles[4];
  qa_app_startup_save_list saves;
  qa_app_startup_save_row save_rows[6];
} fake;

static double fake_now(void *ctx) { (void)ctx; return 0; }
static double fake_measure(void *ctx, const char *text, double scale) {
  (void)ctx; return (double)strlen(text) * scale;
}
static const qa_app_startup_menu_choice difficulties[] = {
  {"0", "Easy", NULL, NULL, NULL}, {"1", "Normal", NULL, NULL, NULL},
  {"2", "Hard", NULL, NULL, NULL}};
static const qa_app_startup_menu_preset presets[] = {
  {"q1-id1", "Quake", NULL, "q1", "classic", "1", difficulties, 3},
  {"q1-id1-rr", "Quake", NULL, "q1", "rerelease", "1", difficulties, 3},
  {"q2-base", "Quake II", NULL, "q2", "classic", "1", difficulties, 3},
  {"q3-baseq3", "Quake III Arena", NULL, "q3", "classic", "2", difficulties, 3}};
static const qa_app_startup_menu_preset *fake_presets(void *ctx, size_t *out) {
  (void)ctx; *out = 4; return presets;
}
static const qa_app_startup_menu_choice product_choices[] = {
  {"q1-id1", "Quake", NULL, "Quake", "classic"},
  {"q1-id1-rr", "Quake", NULL, "Quake", "rerelease"},
  {"q2-base", "Quake II", NULL, "Quake II", "classic"}};
static const qa_app_startup_menu_choice two[] = {
  {"a", "A", NULL, NULL, NULL}, {"b", "B", NULL, NULL, NULL}};
static const qa_app_startup_menu_choice enemies[] = {
  {"native", "Native", NULL, NULL, NULL}, {"custom", "Custom", NULL, NULL, NULL}};
static const qa_app_startup_menu_row rows[] = {
  {"product", "Game", "q1-id1", product_choices, 3},
  {"mapProduct", "Map set", "q1-id1", product_choices, 3},
  {"map", "Map", "a", two, 2},
  {"enemies", "Monsters", "native", enemies, 2},
  {"environment", "Environment", "a", two, 2},
  {"doppler", "Doppler", "a", two, 2},
  {"seats", "Players", "a", two, 2},
  {"mode", "Mode", "a", two, 2},
  {"rules", "Rules", "a", two, 2},
  {"skill", "Difficulty", "a", two, 2},
  {"grapple", "Hook", "a", two, 2},
  {"grappleStyle", "Hook style", "a", two, 2},
  {"grenades", "Grenades", "a", two, 2},
  {"character", "Character", "a", two, 2},
  {"movement", "Movement", "a", two, 2},
  {"weapons", "Weapons", "a", two, 2}};
static const qa_app_startup_menu_row *fake_rows(void *ctx, size_t *out) {
  (void)ctx; *out = sizeof rows / sizeof *rows; return rows;
}
static void fake_select(void *ctx, const char *field, const char *id) {
  fake *f = ctx; snprintf(f->selected, sizeof f->selected, "%s=%s", field, id);
}
static qa_app_startup_menu_row fake_source_row(void *ctx) {
  (void)ctx; return (qa_app_startup_menu_row){"source", "Source", "a", two, 2};
}
static const char *fake_no_error(void *ctx) { (void)ctx; return NULL; }
static const char *fake_refresh_arenas(void *ctx) { ((fake *)ctx)->arena_refreshes++; return NULL; }
static qa_app_startup_menu_hosting fake_hosting(void *ctx) {
  (void)ctx; return (qa_app_startup_menu_hosting){"offline", 27910, false, 0};
}
static const char *fake_mode(void *ctx) { (void)ctx; return "singleplayer"; }
static const char *fake_family(void *ctx) { (void)ctx; return "q1"; }
static void fake_play(void *ctx) { ((fake *)ctx)->plays++; }
static void fake_quit(void *ctx) { ((fake *)ctx)->quits++; }
static void fake_play_preset(void *ctx, const char *id, double skill, const char *arena) {
  fake *f = ctx;
  snprintf(f->preset_id, sizeof f->preset_id, "%s", id);
  f->preset_skill = skill;
  f->preset_arena_null = arena == NULL;
  if (arena) snprintf(f->arena, sizeof f->arena, "%s", arena);
}
static void fake_load(void *ctx, const char *id) {
  snprintf(((fake *)ctx)->loaded, sizeof ((fake *)ctx)->loaded, "%s", id);
}
static const qa_app_startup_save_list *fake_saves(void *ctx) { return &((fake *)ctx)->saves; }
static void fake_refresh_saves(void *ctx) { ((fake *)ctx)->refreshes++; }
static bool toggle_read(void *ctx) { return *(bool *)ctx; }
static void toggle_write(void *ctx, bool value) { *(bool *)ctx = value; }
static bool toggle_enabled(void *ctx) { (void)ctx; return true; }

static qa_ui_setting_binding make_toggle(fake *f, int slot, const char *id, const char *category) {
  qa_ui_setting_binding binding = {0};
  binding.id = id; binding.label = id; binding.category = category;
  binding.kind = "toggle"; binding.ctx = &f->toggles[slot];
  binding.enabled = toggle_enabled;
  binding.read_bool = toggle_read; binding.write_bool = toggle_write;
  return binding;
}

static void send(fake *f, qa_app_startup_menu *menu, qa_seat_input_event event) {
  event.seat = f->seat;
  (void)qa_app_startup_menu_input(menu, event);
}
static void click(fake *f, qa_app_startup_menu *menu, double x, double y) {
  qa_seat_input_event event = {0};
  event.kind = "mouse-motion"; event.position = (qa_vec2){x, y};
  send(f, menu, event);
  memset(&event, 0, sizeof event);
  event.kind = "mouse-button"; event.button = 1; event.down = true;
  send(f, menu, event);
  event.down = false;
  send(f, menu, event);
}
static void key(fake *f, qa_app_startup_menu *menu, double code) {
  qa_seat_input_event event = {0};
  event.kind = "key"; event.code = code; event.down = true;
  send(f, menu, event);
  event.down = false;
  send(f, menu, event);
}
static const char *active(qa_app_startup_menu *menu) {
  return qa_native_ui_controller_active_menu(qa_app_startup_menu_controller(menu));
}

int main(void) {
  fake f = {0};
  CHECK(qa_identity_owner_create(&f.identity, "startup-menu-test"));
  f.seat = qa_identity_owner_seat(&f.identity, 0);
  for (int i = 0; i < 6; i++) {
    static char ids[6][16], labels[6][16];
    snprintf(ids[i], sizeof ids[i], "save%d", i);
    snprintf(labels[i], sizeof labels[i], "Save %d", i);
    f.save_rows[i] = (qa_app_startup_save_row){ids[i], labels[i], (char *)"e1m1", (char *)"Quake", 0, NULL};
  }
  f.saves = (qa_app_startup_save_list){f.save_rows, 6, NULL};
  qa_ui_setting_binding settings[3] = {
    make_toggle(&f, 0, "ui:test:audio", "audio"),
    make_toggle(&f, 1, "ui:test:display", "display"),
    make_toggle(&f, 2, "ui:test:accessibility", "accessibility")};
  qa_app_startup_menu_options options = {0};
  options.ctx = &f;
  options.seat = f.seat;
  options.skin_font = "resource:test:font";
  options.now = fake_now;
  options.measure = fake_measure;
  options.presets = fake_presets;
  options.rows = fake_rows;
  options.select = fake_select;
  options.monster_source_row = fake_source_row;
  options.prepare_monster_roster = fake_no_error;
  options.refresh_base_arenas = fake_refresh_arenas;
  options.hosting = fake_hosting;
  options.mode = fake_mode;
  options.product_family = fake_family;
  options.play = fake_play;
  options.play_preset = fake_play_preset;
  options.load = fake_load;
  options.saves = fake_saves;
  options.refresh_saves = fake_refresh_saves;
  options.quit = fake_quit;
  options.settings = settings;
  options.setting_count = 3;
  qa_app_startup_menu *menu = qa_app_startup_menu_new(options);
  CHECK(menu != NULL);
  if (!menu) return 1;
  qa_native_ui_controller *controller = qa_app_startup_menu_controller(menu);
  CHECK(controller != NULL);
  /* Constructor ensureActiveMenu(): main is active. */
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:main") == 0);
  /* Every TS-registered menu id resolves (unknown ids abort). */
  static const char *const ids[] = {
    "menu:startup:library", "menu:startup:native-family", "menu:startup:native-campaign",
    "menu:startup:native-difficulty", "menu:startup:servers", "menu:startup:server-filters",
    "menu:startup:server-details", "menu:startup:session", "menu:startup:hosting",
    "menu:startup:options", "menu:startup:sound", "menu:startup:select",
    "menu:startup:roster", "menu:startup:load", "menu:library:arena-selection",
    "menu:settings:root", "menu:settings:display:0", "menu:settings:input:0",
    "menu:settings:accessibility:0"};
  for (size_t i = 0; i < sizeof ids / sizeof *ids; i++) {
    qa_native_ui_controller_open_menu(controller, ids[i]);
    CHECK(active(menu) && strcmp(active(menu), ids[i]) == 0);
    qa_native_ui_controller_close_menu(controller);
  }
  /* Main -> Play a game -> Quake -> unified campaign -> Play. */
  qa_app_startup_menu_open_main(menu);
  click(&f, menu, 100, 128);
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:native-family") == 0);
  click(&f, menu, 200, 128);
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:native-campaign") == 0);
  /* Edition toggle occupies row 0, so the unified Quake preset is row 1. */
  click(&f, menu, 200, 161);
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:native-difficulty") == 0);
  click(&f, menu, 200, 359); /* Play at row 7 */
  CHECK(strcmp(f.preset_id, "q1-id1") == 0);
  CHECK(f.preset_skill == 1 && f.preset_arena_null);
  /* Load Game refreshes saves and loads by id. */
  qa_app_startup_menu_open_main(menu);
  click(&f, menu, 100, 161);
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:load") == 0);
  CHECK(f.refreshes == 1);
  click(&f, menu, 200, 139);
  CHECK(strcmp(f.loaded, "save0") == 0);
  /* Busy swallows input (TS input() first line). */
  qa_app_startup_menu_set_status(menu, "Working", true);
  CHECK(qa_app_startup_menu_is_busy(menu));
  qa_seat_input_event escape = {0};
  escape.seat = f.seat; escape.kind = "key"; escape.code = QA_KEY_ESCAPE; escape.down = true;
  CHECK(qa_app_startup_menu_input(menu, escape));
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:load") == 0);
  qa_app_startup_menu_set_status(menu, "", false);
  /* Escape at main is swallowed and main reopens after close. */
  qa_app_startup_menu_open_main(menu);
  key(&f, menu, QA_KEY_ESCAPE);
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:main") == 0);
  /* Session builder: dropdown disables footer Play until Escape. */
  click(&f, menu, 100, 128);              /* Play a game */
  click(&f, menu, 200, 227);              /* Custom game (family row 3) */
  CHECK(active(menu) && strcmp(active(menu), "menu:startup:session") == 0);
  click(&f, menu, 100, 136);              /* Game picker field */
  int plays_before = f.plays;
  click(&f, menu, 400, 429);              /* Play footer while dropdown open */
  CHECK(f.plays == plays_before);
  key(&f, menu, QA_KEY_ESCAPE);
  click(&f, menu, 400, 429);
  CHECK(f.plays == plays_before + 1);
  /* Quit from main (row 3 without libraries). */
  qa_app_startup_menu_open_main(menu);
  click(&f, menu, 100, 227);
  CHECK(f.quits == 1);
  /* resumeDisplayOptions stacks options then display. */
  qa_app_startup_menu_resume_display_options(menu);
  CHECK(active(menu) && strcmp(active(menu), "menu:settings:display:0") == 0);
  qa_app_startup_menu_destroy(menu);
  qa_identity_owner_destroy(&f.identity);
  printf("test_startup_menu: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
