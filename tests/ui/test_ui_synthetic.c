/*
 * Port of tests/ui/common/legacy/synthetic.test.ts: the cgame menu runtime
 * over in-memory menu text. Host operations the menus under test must not
 * reach are left NULL (a call would crash the test). Expected values from
 * bun (5 pass).
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/input/key_codes.h"
#include "qa/ui/common/legacy/menu.h"
#include "qa/ui/common/legacy/runtime.h"
#include "qa/ui/common/legacy/team_arena/memory.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                     \
  } while (0)

static const char *menu_set = "{ loadMenu { \"ui/test.menu\" } }";
static const char *menu_text;

static bool source(const char *path, qa_ui_script_source *out) {
  const char *text = strcmp(path, "ui/test.txt") == 0     ? menu_set
                     : strcmp(path, "ui/test.menu") == 0 ? menu_text
                                                         : NULL;
  if (text == NULL) return false;
  memset(out, 0, sizeof *out);
  out->path = path;
  out->text = (const uint8_t *)text;
  out->text_length = strlen(text);
  return true;
}
static bool resolve_root(void *ctx, const char *path, qa_ui_script_source *out) {
  (void)ctx;
  return source(path, out);
}
static bool resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  (void)ctx;
  return source(request->requested_path, out);
}
static double zero_random(void *ctx) {
  (void)ctx;
  return 0;
}
static void memory_print(void *ctx, const char *text) {
  (void)ctx;
  (void)text;
}

typedef struct synthetic {
  qa_identity_owner owner;
  qa_ui_team_arena_memory *memory;
  qa_ui_menu_definitions definitions;
  qa_cvar_registry *cvars;
  qa_ui_runtime *runtime;
  char printed[4096];
} synthetic;

static void print_capture(void *ctx, const char *text) {
  synthetic *s = ctx;
  size_t used = strlen(s->printed);
  snprintf(s->printed + used, sizeof s->printed - used, "%s", text);
}
static bool no_picture(void *ctx, const char *path, qa_picture_asset *out) {
  (void)ctx;
  (void)path;
  (void)out;
  return false;
}
static void play_local(void *ctx, qa_ui_runtime_sound_arg sound) {
  (void)ctx;
  (void)sound;
}

static qa_q3_registered_font stub_font;

/* TS synthetic(menu). */
static void synthetic_open(synthetic *s, const char *menu) {
  memset(s, 0, sizeof *s);
  menu_text = menu;
  CHECK(qa_identity_owner_create(&s->owner, "synthetic-ui"));
  s->memory = qa_ui_team_arena_memory_create("qvm32", memory_print, NULL, QA_UI_TEAM_ARENA_MODULE_UI);
  qa_ui_menu_parse_host host;
  memset(&host, 0, sizeof host);
  host.kind = QA_UI_PARSE_HOST_RESOLVED;
  host.resolver.resolve_root = resolve_root;
  host.resolver.resolve = resolve;
  host.random = zero_random;
  qa_ui_menu_parse_options parse_options;
  memset(&parse_options, 0, sizeof parse_options);
  parse_options.has_memory = true;
  parse_options.memory.kind = QA_UI_MENU_MEMORY_QVM32;
  parse_options.memory.memory = s->memory;
  qa_ui_menu_load_plan plan = {"ui/test.txt"};
  qa_ui_load_menu_definitions(&host, &plan, NULL, &parse_options, &s->definitions);
  CHECK(s->definitions.diagnostic_count == 0);

  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = s->owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = qa_identity_owner_seat(&s->owner, 0);
  context.origin.client = qa_identity_owner_client(&s->owner, 0, 0);
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context = context;
  s->cvars = qa_cvar_registry_create(&cvar_options);

  memset(&stub_font, 0, sizeof stub_font);
  stub_font.glyph_scale = 1;
  stub_font.glyph_count = 256;
  for (int i = 0; i < 256; i++) {
    stub_font.glyphs[i].metrics.x_skip = 1;
    stub_font.glyphs[i].metrics.height = 1;
    stub_font.glyphs[i].metrics.pitch = 1;
    stub_font.glyphs[i].metrics.image_width = 1;
    stub_font.glyphs[i].metrics.image_height = 1;
    stub_font.glyphs[i].metrics.s2 = 1;
    stub_font.glyphs[i].metrics.t2 = 1;
  }
  qa_ui_runtime_options options;
  memset(&options, 0, sizeof options);
  options.definitions = s->definitions;
  options.cvars = s->cvars;
  options.print = print_capture;
  options.print_ctx = s;
  options.resources.handles.kind = QA_UI_HANDLES_DIAGNOSTIC;
  options.resources.registered_picture = no_picture;
  options.fonts.small = &stub_font;
  options.fonts.normal = &stub_font;
  options.fonts.big = &stub_font;
  options.fonts.profile = QA_Q3_FONT_UI;
  options.fonts.small_threshold = 0.25;
  options.fonts.big_threshold = 0.4;
  options.audio.play_local = play_local;
  s->runtime = qa_ui_runtime_create(&options);
  CHECK(s->runtime != NULL);
}
static void synthetic_dispose(synthetic *s) {
  qa_ui_runtime_retire(s->runtime);
  qa_ui_runtime_destroy(s->runtime);
  qa_cvar_registry_destroy(s->cvars);
  qa_ui_menu_definitions_destroy(&s->definitions);
  qa_ui_team_arena_memory_destroy(s->memory);
  qa_identity_owner_destroy(&s->owner);
}
static qa_ui_key_event key(int32_t code) {
  qa_ui_key_event event = {QA_UI_KEY_EVENT_KEY, code, true};
  return event;
}

/* UI-003 Up and Down in a menu with no other focusable item do not throw. */
static void test_focus_wraps_once(void) {
  synthetic s;
  synthetic_open(&s,
                 "{ menuDef { name \"m\" rect 0 0 640 480 visible 1\n"
                 "    itemDef { name \"only\" type 1 rect 10 10 100 20 visible 1 text \"A\" }\n"
                 "    itemDef { name \"decor\" type 0 rect 10 40 100 20 visible 1 decoration text \"B\" } } }");
  CHECK(qa_ui_runtime_activate(s.runtime, "m"));
  const int32_t codes[] = {QA_KEY_UP, QA_KEY_UP, QA_KEY_DOWN, QA_KEY_DOWN};
  for (size_t i = 0; i < 4; i++) (void)qa_ui_runtime_handle_key(s.runtime, key(codes[i]), 20, 20);
  synthetic_dispose(&s);
}

/* UI-015 a menu whose onOpen opens itself stops at the nesting limit. */
static void test_nesting_limit(void) {
  synthetic s;
  synthetic_open(&s, "{ menuDef { name \"m\" rect 0 0 640 480 visible 1 onOpen { open m } } }");
  CHECK(qa_ui_runtime_activate(s.runtime, "m"));
  CHECK(strstr(s.printed, "nesting limit") != NULL);
  synthetic_dispose(&s);
}

/* UI-023 a bind item with text and an edit field are inert in the cgame context. */
static void test_bind_and_edit_inert(void) {
  synthetic s;
  synthetic_open(&s,
                 "{ menuDef { name \"m\" rect 0 0 640 480 visible 1\n"
                 "    itemDef { name \"bind\" type 13 rect 10 10 100 20 visible 1 text \"Fire\" cvar \"+attack\" }\n"
                 "    itemDef { name \"edit\" type 4 rect 10 40 100 20 visible 1 text \"Name\" cvar \"name\" maxChars 8 } } }");
  CHECK(qa_ui_runtime_activate(s.runtime, "m"));
  (void)qa_ui_runtime_handle_key(s.runtime, key(QA_KEY_ENTER), 20, 50);
  synthetic_dispose(&s);
}

/* UI-005 repeated lookups of three colliding strings do not consume the pool. */
static void test_colliding_strings(void) {
  qa_ui_team_arena_memory *memory =
      qa_ui_team_arena_memory_create("qvm32", memory_print, NULL, QA_UI_TEAM_ARENA_MODULE_UI);
  const char *names[] = {"ui_map3", "team_game1", "g_mode2"};
  for (size_t i = 0; i < 3; i++) free(qa_ui_team_arena_memory_string_alloc(memory, names[i]));
  const int64_t before = qa_ui_team_arena_memory_string_bytes(memory);
  bool same = true;
  for (int round = 0; round < 100; round++)
    for (size_t i = 0; i < 3; i++) {
      char *value = qa_ui_team_arena_memory_string_alloc(memory, names[i]);
      same = same && value != NULL && strcmp(value, names[i]) == 0;
      free(value);
    }
  CHECK(same);
  CHECK(qa_ui_team_arena_memory_string_bytes(memory) == before);
  qa_ui_team_arena_memory_destroy(memory);
}

/* UI-016 Enter on the focused yes/no item toggles it without the pointer over it. */
static void test_enter_toggles_yes_no(void) {
  synthetic s;
  synthetic_open(&s,
                 "{ menuDef { name \"m\" rect 0 0 640 480 visible 1\n"
                 "    itemDef { name \"first\" type 11 rect 10 10 100 20 visible 1 text \"Other\" cvar \"other_flag\" }\n"
                 "    itemDef { name \"yn\" type 11 rect 10 40 100 20 visible 1 text \"Flag\" cvar \"my_flag\" } } }");
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(s.cvars, "my_flag", "0", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  if (qa_cvar_registry_register(s.cvars, "other_flag", "0", 0, &snapshot)) qa_cvar_read_free(&snapshot);
  CHECK(qa_ui_runtime_activate(s.runtime, "m"));
  (void)qa_ui_runtime_pointer_move(s.runtime, 500, 400);
  (void)qa_ui_runtime_handle_key(s.runtime, key(QA_KEY_DOWN), 500, 400);
  (void)qa_ui_runtime_handle_key(s.runtime, key(QA_KEY_ENTER), 500, 400);
  char *value = qa_cvar_registry_variable_string(s.cvars, "my_flag");
  CHECK(value != NULL && strcmp(value, "1") == 0);
  free(value);
  synthetic_dispose(&s);
}

int main(void) {
  test_focus_wraps_once();
  test_nesting_limit();
  test_bind_and_edit_inert();
  test_colliding_strings();
  test_enter_toggles_yes_no();
  if (failures) {
    fprintf(stderr, "test_ui_synthetic: %d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_ui_synthetic: %d checks ok\n", checks);
  return 0;
}
