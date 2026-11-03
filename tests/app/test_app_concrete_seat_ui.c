/*
 * The seat UI asset owners over real ApplicationAssets of the retail Q1 rerelease id1 content: ApplicationWeaponHudAssets
 * (weapon-hud.ts: wheel qpic icons, gfx.wad lumps, the load cache, the missing-picture error), ApplicationQ1Wheel
 * (q1-wheel.ts over id1/wwheel.txt and PlayerUi) and SeatGamePrompt (game-prompt.ts: catalogs from the mounted
 * loc_english.txt, paged controls, number keys). Reproduces the bun transcript of tests/app/oracles/app_concrete_seat_ui.ts
 * (app_concrete_seat_ui.txt) line for line. Every callback checks its sentinel ctx. Needs QTS_TEST_CORPUS.
 * GPL-2.0-or-later.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/game_prompt.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/q1_wheel.h"
#include "qa/app/bootstrap/weapon_hud.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)
#define N(value) QA_JS_NUMBER(value)

static char transcript[65536];
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *format, ...) {
  char line[8192];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  strncat(transcript, line, sizeof transcript - strlen(transcript) - 2);
  strcat(transcript, "\n");
}
/* JSON.stringify of a string (ASCII escapes) or null. */
static char *json(const char *text, char *buffer, size_t capacity) {
  if (text == NULL) { snprintf(buffer, capacity, "null"); return buffer; }
  size_t used = 0;
  buffer[used++] = '"';
  for (const char *p = text; *p != '\0' && used + 8 < capacity; p++) {
    if (*p == '\n') { buffer[used++] = '\\'; buffer[used++] = 'n'; }
    else if (*p == '"' || *p == '\\') { buffer[used++] = '\\'; buffer[used++] = *p; }
    else buffer[used++] = *p;
  }
  buffer[used++] = '"';
  buffer[used] = '\0';
  return buffer;
}

static qa_app_weapon_hud_assets *icons;
static void describe(const char *id, char *buffer, size_t capacity) {
  if (id == NULL) { snprintf(buffer, capacity, "null"); return; }
  const qa_picture_asset *picture = qa_app_weapon_hud_assets_picture(icons, id);
  char shape[1024];
  if (picture == NULL) snprintf(shape, sizeof shape, "-");
  else if (picture->kind == QA_PICTURE_IMAGE)
    snprintf(shape, sizeof shape, "image %s %sx%s", picture->image.name, N(picture->image.image.width), N(picture->image.image.height));
  else snprintf(shape, sizeof shape, "material %s", picture->material.name);
  snprintf(buffer, capacity, "%s aspect %s %s", id, N(qa_app_weapon_hud_assets_aspect(icons, &id)), shape);
}
typedef struct missing_load { qa_content_id content; qa_resource_id id; } missing_load;
static void missing_run(void *ctx) {
  missing_load *load = ctx;
  qa_weapon_hud_icon icon;
  memset(&icon, 0, sizeof icon);
  icon.kind = QA_WEAPON_HUD_ICON_IMAGE;
  icon.resource.content = load->content;
  icon.resource.path = "gfx/weapons/missing.lmp";
  load->id = qa_app_weapon_hud_assets_load(icons, icon);
}

/* ---- PlayerUi fixture ---- */
static const char *const weapon_ids[] = {"q1:weapon/axe", "q1:weapon/shotgun", "q1:weapon/supershotgun", "q1:weapon/nailgun",
    "q1:weapon/supernailgun", "q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher", "q1:weapon/lightning"};
static qa_app_sim_player_ui_item player_items[8];
static char item_labels[8][8];
static qa_weapon_hud_status player_status;
static qa_app_sim_player_ui player_of(const char *active, bool has_status, qa_provider_reference source) {
  for (size_t i = 0; i < 8; i++) {
    snprintf(item_labels[i], sizeof item_labels[i], "W%zu", i);
    const char *id = weapon_ids[i];
    bool owned = strcmp(id, "q1:weapon/axe") == 0 || strcmp(id, "q1:weapon/shotgun") == 0 || strcmp(id, "q1:weapon/supershotgun") == 0
                 || strcmp(id, "q1:weapon/rocketlauncher") == 0;
    player_items[i] = (qa_app_sim_player_ui_item){id, item_labels[i], "weapon", (double)i, owned,
                                                  strcmp(id, "q1:weapon/supershotgun") != 0, true, (double)i * 3, 0};
  }
  memset(&player_status, 0, sizeof player_status);
  player_status.source = source;
  player_status.item = active != NULL ? active : "q1:weapon/axe";
  player_status.label = "";
  qa_app_sim_player_ui player;
  memset(&player, 0, sizeof player);
  player.weapon_status = has_status ? &player_status : NULL;
  player.active_weapon = active;
  player.items = player_items;
  player.item_count = 8;
  return player;
}
static void wheel_items_line(qa_app_application_q1_wheel *wheel, const qa_app_sim_player_ui *player, const char *label) {
  qa_app_q1_wheel_items_result items;
  if (!qa_app_application_q1_wheel_items(wheel, player, &items)) { out("%s null", label); return; }
  for (size_t i = 0; i < items.count; i++) {
    const qa_ui_wheel_item *item = &items.items[i];
    char id[256], name[256], icon[512], selected[512], count[64];
    snprintf(count, sizeof count, "%s", item->has_count ? N(item->count) : "null");
    out("wheel item {\"id\":%s,\"sourceOrdinal\":%s,\"sortOrder\":%s,\"label\":%s,\"owned\":%s,\"hasAmmo\":%s,\"count\":%s,"
        "\"warningCount\":%s,\"icon\":%s,\"selectedIcon\":%s}",
        json(item->id, id, sizeof id), N(item->source_ordinal), N(item->sort_order), json(item->label, name, sizeof name),
        item->owned ? "true" : "false", item->has_ammo ? "true" : "false", count, N(item->warning_count),
        json(item->icon, icon, sizeof icon), json(item->selected_icon, selected, sizeof selected));
  }
  qa_app_q1_wheel_items_result_destroy(&items);
}

/* ---- SeatGamePrompt fixture ---- */
typedef struct sentinel { int value; } sentinel;
static sentinel controller_ctx = {0xc017}, prompt_ctx = {0x9407}, focus_ctx = {0xf0c5};
static void expect(void *ctx, sentinel *wanted, const char *what) {
  if (ctx != wanted || ((sentinel *)ctx)->value != wanted->value) { fprintf(stderr, "wrong %s ctx\n", what); abort(); }
}
static qa_ui_skin c_skin(void *ctx) { expect(ctx, &controller_ctx, "skin"); qa_ui_skin skin; memset(&skin, 0, sizeof skin); return skin; }
static double c_now(void *ctx) { expect(ctx, &controller_ctx, "now"); return 0; }
static const qa_input_binding *c_bindings(void *ctx, size_t *count) { expect(ctx, &controller_ctx, "bindings"); *count = 0; return NULL; }
static void c_focus(void *ctx, qa_seat_input_focus focus, double now) { (void)focus; (void)now; expect(ctx, &controller_ctx, "focus"); }
static void c_sound(void *ctx, qa_ui_sound sound, qa_seat_id seat) { (void)sound; (void)seat; expect(ctx, &controller_ctx, "sound"); }
static void c_script(void *ctx, qa_legacy_ui_script script, qa_seat_id seat) { (void)script; (void)seat; expect(ctx, &controller_ctx, "script"); }
static double c_measure(void *ctx, const char *text, double scale) { (void)text; (void)scale; expect(ctx, &controller_ctx, "measure"); return 0; }
static qa_actor_id prompt_actor_value;
static double impulses[16];
static size_t impulse_count;
static qa_actor_id p_actor(void *ctx) { expect(ctx, &prompt_ctx, "actor"); return prompt_actor_value; }
static void p_impulse(void *ctx, double value) { expect(ctx, &prompt_ctx, "impulse"); impulses[impulse_count++] = value; }
static const char *p_language(void *ctx) { expect(ctx, &prompt_ctx, "language"); return "english"; }
static const char *focus_kind;
static const char *f_focus(void *ctx) { expect(ctx, &focus_ctx, "focus kind"); return focus_kind; }
static qa_app_seat_game_prompt *prompt;
static qa_native_ui_controller *controller;
static void show(const char *label) {
  qa_ui_menu menu = qa_app_seat_game_prompt_menu(prompt);
  char title[512], controls[4096] = "[", piece[1024], a[256], b[512];
  for (size_t i = 0; i < menu.control_count; i++) {
    snprintf(piece, sizeof piece, "%s[%s,%s,%s]", i == 0 ? "" : ",", json(menu.controls[i].id, a, sizeof a),
             json(menu.controls[i].label, b, sizeof b), menu.controls[i].enabled ? "true" : "false");
    strncat(controls, piece, sizeof controls - strlen(controls) - 2);
  }
  strcat(controls, "]");
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(controller);
  out("%s active %s title %s controls %s", label, active != NULL ? active : "null", json(menu.title, title, sizeof title), controls);
}
static qa_app_prompt_event event_of(qa_actor_id actor, const char *kind, const char *title, const qa_app_prompt_choice *choices,
                                    size_t count, const char *content) {
  qa_app_prompt_event event = {kind, actor, title, choices, count, content};
  return event;
}
static const char *impulse_list(char *buffer, size_t capacity) {
  snprintf(buffer, capacity, "[");
  for (size_t i = 0; i < impulse_count; i++) {
    strncat(buffer, i == 0 ? "" : ",", capacity - strlen(buffer) - 1);
    strncat(buffer, N(impulses[i]), capacity - strlen(buffer) - 1);
  }
  strncat(buffer, "]", capacity - strlen(buffer) - 1);
  return buffer;
}

static const void *owner_identity;
int main(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL) { printf("skip: QTS_TEST_CORPUS not set\n0 checks, 0 failures\n"); return 0; }
  const char *argv[] = {"--game", "q1-rerelease-id1", "--map", "e1m1", "--dedicated", "--content-root", root};
  qa_content_root_search search = {NULL, NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, 7, search, NULL);
  CHECK(strcmp(command.kind, "run") == 0);
  qa_app_loaded_content *content = qa_app_load_application_content_native((qa_app_content_host){0}, command.options, NULL, false,
      NULL, NULL, NULL);
  memset(&command.options, 0, sizeof command.options);
  qa_app_application_command_destroy(&command);
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "seat-ui-oracle"));
  qa_renderer_resource_owner owner = {&owner_identity, ids.session, 0};
  qa_app_application_assets_options asset_options;
  memset(&asset_options, 0, sizeof asset_options);
  qa_app_application_assets *assets = qa_app_application_assets_create(content, owner, NULL, &asset_options);
  qa_content_id content_id = qa_app_loaded_content_recipe(content)->map.entities.content;
  qa_provider_reference source = {"q1:foundation", content_id};

  /* ApplicationWeaponHudAssets */
  icons = qa_app_weapon_hud_assets_create(assets);
  const char *hud_items[] = {"q1:weapon/rocketlauncher", "q1:weapon/axe", "q1:weapon/lightning"};
  for (size_t i = 0; i < 3; i++) {
    qa_weapon_hud_status status;
    memset(&status, 0, sizeof status);
    status.source = source; status.item = hud_items[i]; status.label = hud_items[i];
    qa_app_weapon_hud_prepared prepared = qa_app_weapon_hud_assets_prepare(icons, &status);
    char weapon[2048], ammo[2048];
    describe(prepared.has_weapon ? prepared.weapon : NULL, weapon, sizeof weapon);
    describe(prepared.has_ammo ? prepared.ammo : NULL, ammo, sizeof ammo);
    out("hud %s weapon %s ammo %s", hud_items[i], weapon, ammo);
  }
  qa_app_weapon_hud_prepared none = qa_app_weapon_hud_assets_prepare(icons, NULL);
  out("hud null {\"weapon\":%s,\"ammo\":%s}", none.has_weapon ? "?" : "null", none.has_ammo ? "?" : "null");
  qa_weapon_hud_status again_status;
  memset(&again_status, 0, sizeof again_status);
  again_status.source = source; again_status.item = "q1:weapon/rocketlauncher"; again_status.label = "";
  qa_app_weapon_hud_prepared again = qa_app_weapon_hud_assets_prepare(icons, &again_status);
  out("hud cached %s %s", again.weapon, again.ammo);
  out("hud aspect-null %s", N(qa_app_weapon_hud_assets_aspect(icons, NULL)));
  missing_load missing = {content_id, NULL};
  qa_caught_error caught;
  if (qa_try(missing_run, &missing, &caught)) out("hud missing loaded");
  else out("hud missing %s", caught.message);

  /* ApplicationQ1Wheel */
  qa_app_application_q1_wheel *wheel = qa_app_application_q1_wheel_create();
  qa_app_sim_player_ui player = player_of(NULL, true, source);
  wheel_items_line(wheel, &player, "wheel before");
  player = player_of("q1:weapon/rocketlauncher", true, source);
  qa_app_application_q1_wheel_prepare(wheel, assets, icons, &player);
  wheel_items_line(wheel, &player, "wheel items");
  const double switches[][2] = {{0, 1}, {5, 1}, {6, 0}, {7, 2}, {1.5, 3}, {-1, 1}, {3, 4}};
  for (size_t i = 0; i < sizeof switches / sizeof switches[0]; i++) {
    qa_item_id selected = qa_app_application_q1_wheel_switch_weapon(wheel, &player, switches[i][0], switches[i][1]);
    char first[32];
    snprintf(first, sizeof first, "%s", N(switches[i][0]));
    out("wheel switch %s %s %s", first, N(switches[i][1]), selected != NULL ? selected : "null");
  }
  player = player_of(NULL, false, source);
  qa_app_application_q1_wheel_prepare(wheel, assets, icons, &player);
  player = player_of(NULL, true, source);
  wheel_items_line(wheel, &player, "wheel cleared");
  qa_app_application_q1_wheel_destroy(wheel);

  /* SeatGamePrompt */
  qa_seat_id seat = qa_identity_owner_seat(&ids, 0);
  qa_actor_id actor = qa_identity_owner_actor(&ids, 1, 0), other = qa_identity_owner_actor(&ids, 2, 0);
  prompt_actor_value = actor;
  qa_native_ui_options controller_options;
  memset(&controller_options, 0, sizeof controller_options);
  controller_options.ctx = &controller_ctx; controller_options.seat = seat;
  controller_options.skin = c_skin; controller_options.now = c_now; controller_options.bindings = c_bindings;
  controller_options.focus = c_focus; controller_options.sound = c_sound; controller_options.execute_script = c_script;
  controller_options.measure_text = c_measure;
  controller = qa_native_ui_controller_create(&controller_options);
  CHECK(controller != NULL);
  prompt = qa_app_seat_game_prompt_create(seat, controller, (qa_app_seat_game_prompt_host){&prompt_ctx, p_actor, p_impulse, p_language});
  CHECK(prompt != NULL);
  const qa_app_prompt_choice other_choices[] = {{"x", 1}};
  qa_app_prompt_event event = event_of(other, "prompt", "$qc_exited", other_choices, 1, content_id);
  qa_app_seat_game_prompt_receive(prompt, &event, 1);
  focus_kind = "game";
  qa_app_seat_game_prompt_prepare(prompt, assets, f_focus, &focus_ctx);
  show("prompt other");
  const qa_app_prompt_choice choices[] = {{"$qc_suicides", 11}, {"Plain", 12}, {"$qc_ring_fade", 13}, {"Four", 14}, {"Five", 15}};
  event = event_of(actor, "prompt", "$qc_quad_fade", choices, 5, content_id);
  qa_app_seat_game_prompt_receive(prompt, &event, 1);
  focus_kind = "console";
  qa_app_seat_game_prompt_prepare(prompt, assets, f_focus, &focus_ctx);
  show("prompt console");
  focus_kind = "game";
  qa_app_seat_game_prompt_prepare(prompt, assets, f_focus, &focus_ctx);
  show("prompt game");
  qa_seat_input_event key;
  memset(&key, 0, sizeof key);
  key.kind = "key"; key.seat = qa_identity_owner_seat(&ids, 1); key.code = 50; key.down = true;
  out("prompt key-wrong-seat %s", qa_app_seat_game_prompt_input(prompt, &key) ? "true" : "false");
  key.seat = seat;
  char list[128];
  bool handled = qa_app_seat_game_prompt_input(prompt, &key);
  out("prompt key 2 %s impulses %s", handled ? "true" : "false", impulse_list(list, sizeof list));
  show("prompt after");
  const qa_app_prompt_choice only[] = {{"Only", 21}};
  event = event_of(actor, "prompt", "Again", only, 1, content_id);
  qa_app_seat_game_prompt_receive(prompt, &event, 1);
  qa_app_seat_game_prompt_prepare(prompt, assets, f_focus, &focus_ctx);
  show("prompt again");
  key.code = 57;
  handled = qa_app_seat_game_prompt_input(prompt, &key);
  out("prompt key 9 %s impulses %s", handled ? "true" : "false", impulse_list(list, sizeof list));
  event = event_of(actor, "clear-prompt", "", NULL, 0, content_id);
  qa_app_seat_game_prompt_receive(prompt, &event, 1);
  show("prompt cleared");
  qa_app_seat_game_prompt_destroy(prompt);
  qa_native_ui_controller_destroy(controller);

  qa_app_weapon_hud_assets_destroy(icons);
  qa_app_application_assets_close(assets);
  qa_app_application_assets_destroy(assets);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_identity_owner_destroy(&ids);

  FILE *file = fopen("tests/app/oracles/app_concrete_seat_ui.txt", "r");
  CHECK(file != NULL);
  if (file != NULL) {
    static char expected[65536];
    size_t length = fread(expected, 1, sizeof expected - 1, file);
    expected[length] = '\0';
    fclose(file);
    CHECK(strcmp(expected, transcript) == 0);
    if (strcmp(expected, transcript) != 0) fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, transcript);
  }
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
