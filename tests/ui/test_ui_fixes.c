/*
 * UI small-defect fixes; run under ASan with leak detection for the
 * ownership pins.
 *  - Preprocessor #define expansion re-snapshots tokens; each snapshot used to
 *    pin the previous one's chain (about 1.4 KB per expansion). Expected
 *    tokens from bun (src/ui/common/legacy/script/preprocessor.ts readSource):
 *    "#define F(x) x+1\nF(2) F(3)\n" -> 2 + 1 3 + 1.
 *  - setbackground (src/ui/common/legacy/runtime.ts scriptSetBackground):
 *    the window keeps {kind: "shader", path} of the last call; the runtime
 *    owns each reference it creates.
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/ui/common/legacy/menu.h"
#include "qa/ui/common/legacy/runtime.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include "qa/ui/common/legacy/team_arena/memory.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(cond)) {                                                                     \
      failures++;                                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                                  \
  } while (0)

static int sentinel;
#define SENTINEL ((void *)&sentinel)

/* ---- preprocessor #define expansion ---- */
static bool no_include(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  (void)request;
  (void)out;
  CHECK(ctx == SENTINEL);
  return false;
}
static void test_define_expansion(void) {
  const char *text = "#define F(x) x+1\nF(2) F(3)\n";
  qa_ui_script_source root;
  memset(&root, 0, sizeof root);
  root.path = "fixes.menu";
  root.text = (const uint8_t *)text;
  root.text_length = strlen(text);
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  qa_ui_script_source_reader *reader = qa_ui_script_source_reader_open(&root, no_include, SENTINEL, &options);
  CHECK(reader != NULL);
  if (reader == NULL) return;
  const char *expected[] = {"2", "+", "1", "3", "+", "1"};
  size_t count = 0;
  qa_ui_script_token_record record;
  while (qa_ui_script_source_reader_next(reader, &record)) {
    if (count < 6) CHECK(strcmp(record.token->text, expected[count]) == 0);
    count++;
    qa_ui_script_token_record_release(&record);
  }
  CHECK(count == 6);
  qa_ui_script_source_reader_destroy(reader);
}

/* ---- runtime setbackground ---- */
static const char *menu_text =
    "menuDef {\n"
    "  name \"main\"\n"
    "  rect 0 0 640 480\n"
    "  visible 1\n"
    "  itemDef {\n"
    "    name \"panel\"\n"
    "    type 1\n"
    "    rect 100 100 120 24\n"
    "    visible 1\n"
    "    action { setbackground \"gfx/one\" ; setbackground \"gfx/two\" }\n"
    "  }\n"
    "}\n";
static const char *set_text = "{ loadMenu { \"ui/fixes.menu\" } }";
static bool resolve_named(const char *path, qa_ui_script_source *out) {
  const char *text = strcmp(path, "ui/menus.txt") == 0 ? set_text : strcmp(path, "ui/fixes.menu") == 0 ? menu_text : NULL;
  if (text == NULL) return false;
  memset(out, 0, sizeof *out);
  out->path = path;
  out->text = (const uint8_t *)text;
  out->text_length = strlen(text);
  return true;
}
static bool resolve_root(void *ctx, const char *path, qa_ui_script_source *out) {
  (void)ctx;
  return resolve_named(path, out);
}
static bool resolve_include(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  (void)ctx;
  return resolve_named(request->requested_path, out);
}
static double fixed_random(void *ctx) { (void)ctx; return 7; }
static void quiet_print(void *ctx, const char *text) { (void)ctx; (void)text; }
typedef struct picture_log { int registered; } picture_log;
static bool register_picture(void *ctx, const char *path, qa_picture_asset *out) {
  ((picture_log *)ctx)->registered++;
  memset(out, 0, sizeof *out);
  out->kind = QA_PICTURE_IMAGE;
  out->image.name = path;
  return true;
}
static bool no_picture(void *ctx, const char *path, qa_picture_asset *out) {
  (void)ctx;
  (void)path;
  (void)out;
  return false;
}
static void register_font(void *ctx, const char *path, double point_size) {
  (void)ctx;
  (void)path;
  (void)point_size;
}
static void test_setbackground(void) {
  picture_log log = {0};
  qa_ui_team_arena_memory *memory = qa_ui_team_arena_memory_create("qvm32", quiet_print, &log, QA_UI_TEAM_ARENA_MODULE_UI);
  CHECK(memory != NULL);
  qa_ui_menu_parse_host host;
  memset(&host, 0, sizeof host);
  host.kind = QA_UI_PARSE_HOST_RESOLVED;
  host.resolver.resolve_root = resolve_root;
  host.resolver.resolve = resolve_include;
  host.random = fixed_random;
  qa_ui_menu_parse_options parse_options;
  memset(&parse_options, 0, sizeof parse_options);
  parse_options.has_memory = true;
  parse_options.memory.kind = QA_UI_MENU_MEMORY_QVM32;
  parse_options.memory.memory = memory;
  qa_ui_menu_load_plan plan = {"ui/menus.txt"};
  qa_ui_menu_definitions definitions;
  qa_ui_load_menu_definitions(&host, &plan, NULL, &parse_options, &definitions);
  CHECK(definitions.menu_count == 1);

  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "ui-fixes"));
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = owner.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context = context;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);

  static qa_q3_registered_font font;
  memset(&font, 0, sizeof font);
  font.glyph_scale = 1;
  font.glyph_count = 256;
  qa_ui_runtime_options options;
  memset(&options, 0, sizeof options);
  options.definitions = definitions;
  options.cvars = cvars;
  options.resources.handles.kind = QA_UI_HANDLES_DIAGNOSTIC;
  options.resources.ctx = &log;
  options.resources.register_font = register_font;
  options.resources.register_picture = register_picture;
  options.resources.registered_picture = no_picture;
  options.fonts.small = &font;
  options.fonts.normal = &font;
  options.fonts.big = &font;
  options.fonts.profile = QA_Q3_FONT_UI;
  options.fonts.small_threshold = 0.25;
  options.fonts.big_threshold = 0.4;
  qa_ui_runtime *runtime = qa_ui_runtime_create(&options);
  CHECK(runtime != NULL);
  if (runtime != NULL && definitions.menu_count == 1) {
    qa_ui_item_definition *panel = qa_ui_menu_item_at(definitions.menus[0], 0);
    for (int pass = 0; pass < 2; pass++) {
      qa_ui_runtime_run_item_script(runtime, "main", "panel", qa_ui_item_action(panel));
      qa_ui_shader_reference *background = qa_ui_window_background(&panel->window);
      CHECK(background != NULL && background->path != NULL && strcmp(background->path, "gfx/two") == 0);
    }
    CHECK(log.registered >= 4);
    qa_ui_runtime_destroy(runtime);
  }
  qa_cvar_registry_destroy(cvars);
  qa_ui_menu_definitions_destroy(&definitions);
  qa_ui_team_arena_memory_destroy(memory);
  qa_identity_owner_destroy(&owner);
}

int main(void) {
  test_define_expansion();
  test_setbackground();
  if (failures) {
    fprintf(stderr, "%d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_ui_fixes: %d checks ok\n", checks);
  return 0;
}
