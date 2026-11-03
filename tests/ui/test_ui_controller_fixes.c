/*
 * Port of tests/ui/common/controller-fixes.test.ts (NativeUiController over
 * the settings menus) and its UI-019 menu localizer case. Expected values
 * from bun (7 pass).
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/input/key_codes.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/common/localization.h"
#include "qa/ui/common/skin.h"
#include "qa/ui/settings/index.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                     \
  } while (0)

typedef struct fixture {
  qa_identity_owner owner;
  qa_seat_id seat;
  double text_scale;
  char field[128];
  double slider;
  qa_ui_setting_binding bindings[2];
  qa_native_ui_controller *ui;
  qa_ui_settings_menus menus;
  char events[8][32];
  size_t event_count;
} fixture;

static qa_ui_skin skin(void *ctx) {
  (void)ctx;
  return qa_ui_default_skin("resource:test:font");
}
static double now(void *ctx) {
  (void)ctx;
  return 1000;
}
static const qa_input_binding *no_bindings(void *ctx, size_t *out) {
  (void)ctx;
  *out = 0;
  return NULL;
}
static void no_focus(void *ctx, qa_seat_input_focus focus, double now_ms) {
  (void)ctx;
  (void)focus;
  (void)now_ms;
}
static void no_sound(void *ctx, qa_ui_sound sound, qa_seat_id seat) {
  (void)ctx;
  (void)sound;
  (void)seat;
}
static void no_script(void *ctx, qa_legacy_ui_script script, qa_seat_id seat) {
  (void)ctx;
  (void)script;
  (void)seat;
}
static qa_native_ui_appearance appearance(void *ctx) {
  qa_native_ui_appearance value = {1, ((fixture *)ctx)->text_scale, false, NULL};
  return value;
}
static bool enabled(void *ctx) {
  (void)ctx;
  return true;
}
static double read_slider(void *ctx) { return ((fixture *)ctx)->slider; }
static void write_slider(void *ctx, double value) { ((fixture *)ctx)->slider = value; }
static const char *read_field(void *ctx) { return ((fixture *)ctx)->field; }
static void write_field(void *ctx, const char *value) {
  fixture *f = ctx;
  snprintf(f->field, sizeof f->field, "%s", value);
}

static void fixture_open(fixture *f, double text_scale) {
  memset(f, 0, sizeof *f);
  CHECK(qa_identity_owner_create(&f->owner, "controller-fixes"));
  f->seat = qa_identity_owner_seat(&f->owner, 0);
  f->text_scale = text_scale;
  snprintf(f->field, sizeof f->field, "original");
  f->slider = 3;
  qa_ui_setting_binding *slider = &f->bindings[0];
  slider->id = "ui:test:slider";
  slider->label = "Slider";
  slider->category = "input";
  slider->kind = "slider";
  slider->ctx = f;
  slider->enabled = enabled;
  slider->minimum = 0;
  slider->maximum = 20;
  slider->step = 1;
  slider->read_num = read_slider;
  slider->write_num = write_slider;
  qa_ui_setting_binding *field = &f->bindings[1];
  field->id = "ui:test:field";
  field->label = "Field";
  field->category = "input";
  field->kind = "text-entry";
  field->ctx = f;
  field->enabled = enabled;
  field->maximum_length = 40;
  field->read_str = read_field;
  field->write_str = write_field;
  qa_native_ui_options options;
  memset(&options, 0, sizeof options);
  options.ctx = f;
  options.seat = f->seat;
  options.skin = skin;
  options.now = now;
  options.bindings = no_bindings;
  options.focus = no_focus;
  options.sound = no_sound;
  options.execute_script = no_script;
  options.appearance = appearance;
  f->ui = qa_native_ui_controller_create(&options);
  CHECK(f->ui != NULL);
  f->menus = qa_ui_register_settings_menus(f->ui, f->bindings, 2, NULL, NULL, NULL, NULL);
}
static void fixture_close(fixture *f) {
  qa_ui_settings_menus_dispose(&f->menus);
  qa_native_ui_controller_destroy(f->ui);
  qa_identity_owner_destroy(&f->owner);
}
static qa_seat_input_event event_of(fixture *f, const char *kind, double time) {
  qa_seat_input_event event;
  memset(&event, 0, sizeof event);
  event.seat = f->seat;
  event.kind = kind;
  event.time_milliseconds = time;
  return event;
}
static void key(fixture *f, int code, bool down) {
  qa_seat_input_event event = event_of(f, "key", 1000);
  event.code = code;
  event.down = down;
  (void)qa_native_ui_controller_input(f->ui, event);
}
static void pad(fixture *f, double button, bool down, double time) {
  qa_seat_input_event event = event_of(f, "controller-button", time);
  event.device = 3;
  event.button = button;
  event.down = down;
  (void)qa_native_ui_controller_input(f->ui, event);
}
static bool focus_is(fixture *f, const char *id) {
  qa_seat_ui_state state = qa_native_ui_controller_state(f->ui);
  return state.focus.control != NULL && strcmp(state.focus.control, id) == 0;
}
static void focus_field(fixture *f) {
  for (int guard = 0; guard < 64 && !focus_is(f, "ui:test:field"); guard++) {
    key(f, QA_KEY_TAB, true);
    key(f, QA_KEY_TAB, false);
  }
  CHECK(focus_is(f, "ui:test:field"));
}
static qa_ui_draw_context context_of(fixture *f, double width, double height, double time) {
  qa_ui_draw_context context;
  memset(&context, 0, sizeof context);
  context.binding.seat = f->seat;
  context.binding.client = qa_identity_owner_client(&f->owner, 0, 0);
  context.binding.viewport = (qa_rect){0, 0, width, height};
  context.binding.safe_area = (qa_rect){0, 0, width, height};
  context.binding.hud_scale = 1;
  context.time_milliseconds = time;
  return context;
}

/* UI-006 a modifier released while no menu is open does not stay held. */
static void test_modifier_release(void) {
  fixture f;
  fixture_open(&f, 1);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  key(&f, QA_KEY_CONTROL, true);
  key(&f, QA_KEY_ESCAPE, true);
  key(&f, QA_KEY_CONTROL, false);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  key(&f, QA_KEY_TAB, true);
  key(&f, QA_KEY_TAB, false);
  focus_field(&f);
  key(&f, 117, true);
  CHECK(strcmp(f.field, "original") == 0);
  fixture_close(&f);
}

/* UI-007 a stick held through controller removal stops repeating. */
static void test_stick_release(void) {
  fixture f;
  fixture_open(&f, 1);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  qa_seat_input_event axis = event_of(&f, "controller-axis", 1000);
  axis.device = 7;
  axis.axis = "left-x";
  axis.value = 1;
  (void)qa_native_ui_controller_input(f.ui, axis);
  axis.value = 0;
  (void)qa_native_ui_controller_input(f.ui, axis);
  const double before = f.slider;
  size_t count = 0;
  (void)qa_native_ui_controller_draw(f.ui, context_of(&f, 640, 480, 5000), &count);
  CHECK(f.slider == before);
  fixture_close(&f);
}

/* UI-008 the largest Text size keeps row text inside its row. */
static void test_row_text_fits(void) {
  fixture f;
  fixture_open(&f, 2);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  size_t count = 0;
  const qa_ui_draw_command *commands = qa_native_ui_controller_draw(f.ui, context_of(&f, 640, 480, 1000), &count);
  const qa_ui_draw_command *label = NULL;
  for (size_t i = 0; i < count && label == NULL; i++)
    if (commands[i].kind == QA_UI_DRAW_TEXT && commands[i].text != NULL && strcmp(commands[i].text, "Slider") == 0)
      label = &commands[i];
  CHECK(label != NULL);
  if (label != NULL) {
    CHECK(label->origin.y >= 92);
    CHECK(label->origin.y + 8 * label->scale <= 92 + 28);
  }
  fixture_close(&f);
}

/* UI-018 an empty safe area draws nothing instead of throwing. */
static void test_empty_safe_area(void) {
  fixture f;
  fixture_open(&f, 1);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  size_t count = 99;
  (void)qa_native_ui_controller_draw(f.ui, context_of(&f, 0, 0, 1000), &count);
  CHECK(count == 0);
  fixture_close(&f);
}

/* UI-017 a held d-pad direction repeats like the stick and stops on release. */
static void test_dpad_repeat(void) {
  fixture f;
  fixture_open(&f, 1);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  const double start = f.slider;
  pad(&f, 14, true, 1000);
  size_t count = 0;
  (void)qa_native_ui_controller_draw(f.ui, context_of(&f, 640, 480, 1400), &count);
  CHECK(f.slider >= start + 2);
  pad(&f, 14, false, 1500);
  const double stopped = f.slider;
  (void)qa_native_ui_controller_draw(f.ui, context_of(&f, 640, 480, 9000), &count);
  CHECK(f.slider == stopped);
  fixture_close(&f);
}

static void accept_event(void *ctx, qa_physical_input input) {
  fixture *f = ctx;
  if (f->event_count < 8)
    snprintf(f->events[f->event_count++], sizeof f->events[0], "accept %s",
             input.kind == QA_PHYSICAL_INPUT_CONTROLLER_BUTTON ? (input.button == 6 ? "6" : input.button == 1 ? "1" : "?") : "?");
}
static void cancel_event(void *ctx) {
  fixture *f = ctx;
  if (f->event_count < 8) snprintf(f->events[f->event_count++], sizeof f->events[0], "cancel");
}
static void press(fixture *f, double button) {
  pad(f, button, true, 1000);
  pad(f, button, false, 1000);
}

/* UI-017 a pad edits a focused text field and the Back+B chord abandons a binding capture. */
static void test_pad_text_and_chord(void) {
  fixture f;
  fixture_open(&f, 1);
  qa_native_ui_controller_open_menu(f.ui, "menu:settings:input:0");
  focus_field(&f);
  press(&f, 3);
  CHECK(strcmp(f.field, "origina") == 0);
  press(&f, 10);
  CHECK(strcmp(f.field, "originb") == 0);
  press(&f, 9);
  press(&f, 9);
  CHECK(strcmp(f.field, "origin&") == 0);
  press(&f, 2);
  CHECK(strcmp(f.field, "origin&a") == 0);
  qa_native_ui_controller_capture_binding(f.ui, accept_event, cancel_event, &f);
  pad(&f, 6, true, 1000);
  pad(&f, 1, true, 1000);
  pad(&f, 1, false, 1000);
  pad(&f, 6, false, 1000);
  CHECK(f.event_count == 1 && strcmp(f.events[0], "cancel") == 0);
  qa_native_ui_controller_capture_binding(f.ui, accept_event, cancel_event, &f);
  pad(&f, 6, true, 1000);
  pad(&f, 6, false, 1000);
  CHECK(f.event_count == 2 && strcmp(f.events[1], "accept 6") == 0);
  fixture_close(&f);
}

/* UI-019 the menu localizer translates by language and falls back to the literal. */
static const qa_ui_menu_catalog_entry german_entries[] = {{"Slider", "Regler"}};
static const qa_ui_menu_catalog german[] = {{"german", german_entries, 1}};
static qa_ui_menu_catalogs catalogs(void *ctx) {
  (void)ctx;
  qa_ui_menu_catalogs value = {german, 1};
  return value;
}
static const char *current_language;
static char *language(void *ctx) {
  (void)ctx;
  return strdup(current_language);
}
static void test_localizer(void) {
  current_language = "english";
  qa_ui_menu_localizer localizer = {NULL, catalogs, language};
  CHECK(strcmp(qa_ui_menu_localize(&localizer, "Slider"), "Slider") == 0);
  current_language = "german";
  CHECK(strcmp(qa_ui_menu_localize(&localizer, "Slider"), "Regler") == 0);
  CHECK(strcmp(qa_ui_menu_localize(&localizer, "Field"), "Field") == 0);
  current_language = "french";
  CHECK(strcmp(qa_ui_menu_localize(&localizer, "Slider"), "Slider") == 0);
}

int main(void) {
  test_modifier_release();
  test_stick_release();
  test_row_text_fits();
  test_empty_safe_area();
  test_dpad_repeat();
  test_pad_text_and_chord();
  test_localizer();
  if (failures) {
    fprintf(stderr, "test_ui_controller_fixes: %d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_ui_controller_fixes: %d checks ok\n", checks);
  return 0;
}
