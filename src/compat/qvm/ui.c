/* See include/qa/compat/qvm/ui.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/ui.h"

#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

/* Landed QvmUiExport / QvmUiMenu values (TS abi.ts). */
enum {
  UI_INIT = QA_QVM_UI_EXPORT_UI_INIT, UI_SHUTDOWN = QA_QVM_UI_EXPORT_UI_SHUTDOWN,
  UI_KEY_EVENT = QA_QVM_UI_EXPORT_UI_KEY_EVENT, UI_MOUSE_EVENT = QA_QVM_UI_EXPORT_UI_MOUSE_EVENT,
  UI_REFRESH = QA_QVM_UI_EXPORT_UI_REFRESH, UI_IS_FULLSCREEN = QA_QVM_UI_EXPORT_UI_IS_FULLSCREEN,
  UI_SET_ACTIVE_MENU = QA_QVM_UI_EXPORT_UI_SET_ACTIVE_MENU,
  UI_CONSOLE_COMMAND = QA_QVM_UI_EXPORT_UI_CONSOLE_COMMAND,
  UI_DRAW_CONNECT_SCREEN = QA_QVM_UI_EXPORT_UI_DRAW_CONNECT_SCREEN,
  UI_HASUNIQUECDKEY = QA_QVM_UI_EXPORT_UI_HASUNIQUECDKEY,
  UIMENU_NONE = QA_QVM_UI_MENU_UIMENU_NONE, UIMENU_MAIN = QA_QVM_UI_MENU_UIMENU_MAIN,
  UIMENU_INGAME = QA_QVM_UI_MENU_UIMENU_INGAME, UIMENU_NEED_CD = QA_QVM_UI_MENU_UIMENU_NEED_CD,
  UIMENU_BAD_CD_KEY = QA_QVM_UI_MENU_UIMENU_BAD_CD_KEY, UIMENU_TEAM = QA_QVM_UI_MENU_UIMENU_TEAM,
  UIMENU_POSTGAME = QA_QVM_UI_MENU_UIMENU_POSTGAME,
};

static int32_t menu_number(const char *menu) {
  if (strcmp(menu, "none") == 0) return UIMENU_NONE;
  if (strcmp(menu, "main") == 0) return UIMENU_MAIN;
  if (strcmp(menu, "ingame") == 0) return UIMENU_INGAME;
  if (strcmp(menu, "need-cd") == 0) return UIMENU_NEED_CD;
  if (strcmp(menu, "bad-cd-key") == 0) return UIMENU_BAD_CD_KEY;
  if (strcmp(menu, "team") == 0) return UIMENU_TEAM;
  if (strcmp(menu, "postgame") == 0) return UIMENU_POSTGAME;
  qa_range_error("Unknown Q3 menu command %s", menu);
}

static void finish_init(qa_qvm_ui *ui) {
  const qa_execution_profile *profile = qa_qvm_module_get_profile(ui->module);
  if (profile->api.as.q3.kind != QA_Q3_API_UI)
    qa_range_error("QvmUi execution profile has the wrong role");
  ui->api = profile->api.as.q3;
}

void qa_qvm_ui_init(qa_qvm_ui *ui, qa_seat_id seat, const qa_qvm_module_options *options,
                    qa_qvm_ui_assert_current_fn assert_current, void *assert_ctx) {
  memset(ui, 0, sizeof *ui);
  ui->seat = seat;
  ui->assert_current_operation = assert_current;
  ui->assert_ctx = assert_ctx;
  if (strcmp(options->artifact->role, "ui") != 0) qa_range_error("QvmUi requires a ui artifact");
  assert_current(assert_ctx);
  qa_qvm_module_init(&ui->owned_module, options);
  ui->module = &ui->owned_module;
  ui->owns_module = true;
  finish_init(ui);
}

void qa_qvm_ui_init_module(qa_qvm_ui *ui, qa_seat_id seat, qa_qvm_module *module,
                           qa_qvm_ui_assert_current_fn assert_current, void *assert_ctx) {
  memset(ui, 0, sizeof *ui);
  ui->seat = seat;
  ui->assert_current_operation = assert_current;
  ui->assert_ctx = assert_ctx;
  assert_current(assert_ctx);
  ui->module = module;
  ui->owns_module = false;
  finish_init(ui);
}

static void assert_validate(void *ctx) {
  qa_qvm_ui *ui = ctx;
  ui->assert_current_operation(ui->assert_ctx);
  if (ui->retired) qa_range_error("UI module has been retired");
}

void qa_qvm_ui_create(qa_qvm_ui *ui, qa_seat_id seat, const qa_qvm_module_options *options,
                      qa_qvm_ui_assert_current_fn assert_current, void *assert_ctx) {
  memset(ui, 0, sizeof *ui);
  ui->seat = seat;
  ui->assert_current_operation = assert_current;
  ui->assert_ctx = assert_ctx;
  ui->module = &ui->owned_module;
  ui->owns_module = true;
  if (strcmp(options->artifact->role, "ui") != 0) qa_range_error("QvmUi requires a ui artifact");
  assert_current(assert_ctx);
  /* TS retires the module when QvmUi construction fails; C failures
   * abort, so only the success path remains. */
  qa_qvm_module_create(&ui->owned_module, options, assert_validate, ui);
  finish_init(ui);
}

void qa_qvm_ui_destroy(qa_qvm_ui *ui) {
  if (ui->owns_module) qa_qvm_module_destroy(&ui->owned_module);
  memset(ui, 0, sizeof *ui);
}

static void current(qa_qvm_ui *ui) {
  ui->assert_current_operation(ui->assert_ctx);
  if (ui->retired) qa_range_error("UI module has been retired");
}

static int32_t call(qa_qvm_ui *ui, const int32_t *words, size_t count) {
  current(ui);
  int32_t result = qa_qvm_module_call_async(ui->module, words, count, 0, assert_validate, ui);
  current(ui);
  return result;
}

void qa_qvm_ui_init_export(qa_qvm_ui *ui, bool connecting) {
  int32_t words[] = {UI_INIT, connecting ? 1 : 0};
  (void)call(ui, words, 2);
}

void qa_qvm_ui_shutdown(qa_qvm_ui *ui) {
  int32_t words[] = {UI_SHUTDOWN};
  (void)call(ui, words, 1);
}

void qa_qvm_ui_retire(qa_qvm_ui *ui) {
  ui->retired = true;
  qa_qvm_module_retire(ui->module);
}

void qa_qvm_ui_key_event(qa_qvm_ui *ui, int32_t key, bool down) {
  int32_t words[] = {UI_KEY_EVENT, key, down ? 1 : 0};
  (void)call(ui, words, 3);
}

void qa_qvm_ui_mouse_event(qa_qvm_ui *ui, int32_t dx, int32_t dy) {
  int32_t words[] = {UI_MOUSE_EVENT, dx, dy};
  (void)call(ui, words, 3);
}

void qa_qvm_ui_refresh(qa_qvm_ui *ui, int32_t time) {
  int32_t words[] = {UI_REFRESH, time};
  (void)call(ui, words, 2);
}

bool qa_qvm_ui_is_fullscreen(qa_qvm_ui *ui) {
  int32_t words[] = {UI_IS_FULLSCREEN};
  return call(ui, words, 1) != 0;
}

void qa_qvm_ui_set_active_menu(qa_qvm_ui *ui, const char *menu) {
  if (strcmp(qa_qvm_module_get_abi_profile(ui->module), "q3-modern") != 0 &&
      menu_number(menu) > UIMENU_BAD_CD_KEY)
    qa_range_error("Legacy UI does not implement menu %s", menu);
  int32_t words[] = {UI_SET_ACTIVE_MENU, menu_number(menu)};
  (void)call(ui, words, 2);
}

bool qa_qvm_ui_console_command(qa_qvm_ui *ui, int32_t time, const char *const *arguments, size_t argument_count) {
  current(ui);
  int32_t words[] = {UI_CONSOLE_COMMAND, time};
  bool result = qa_qvm_module_command_async(ui->module, words, 2, arguments, argument_count,
                                            assert_validate, ui) != 0;
  current(ui);
  return result;
}

void qa_qvm_ui_draw_connect_screen(qa_qvm_ui *ui, bool overlay) {
  int32_t words[] = {UI_DRAW_CONNECT_SCREEN, overlay ? 1 : 0};
  (void)call(ui, words, 2);
}

bool qa_qvm_ui_has_unique_cd_key(qa_qvm_ui *ui) {
  if (strcmp(qa_qvm_module_get_abi_profile(ui->module), "q3-modern") != 0)
    qa_range_error("Legacy UI does not export UI_HASUNIQUECDKEY");
  int32_t words[] = {UI_HASUNIQUECDKEY};
  return call(ui, words, 1) != 0;
}

static void exports_init(void *ctx, bool connecting) { qa_qvm_ui_init_export(ctx, connecting); }
static void exports_shutdown(void *ctx) { qa_qvm_ui_shutdown(ctx); }
static void exports_key_event(void *ctx, double key, bool down) {
  qa_qvm_ui_key_event(ctx, qa_int32(key), down);
}
static void exports_mouse_event(void *ctx, double dx, double dy) {
  qa_qvm_ui_mouse_event(ctx, qa_int32(dx), qa_int32(dy));
}
static void exports_refresh(void *ctx, double time) { qa_qvm_ui_refresh(ctx, qa_int32(time)); }
static bool exports_is_fullscreen(void *ctx) { return qa_qvm_ui_is_fullscreen(ctx); }
static void exports_set_active_menu(void *ctx, qa_q3_menu_command menu) {
  qa_qvm_ui_set_active_menu(ctx, menu);
}
static bool exports_console_command(void *ctx, double time, qa_string_view_list arguments) {
  return qa_qvm_ui_console_command(ctx, qa_int32(time), arguments.values, arguments.count);
}
static void exports_draw_connect_screen(void *ctx, bool overlay) {
  qa_qvm_ui_draw_connect_screen(ctx, overlay);
}
static bool exports_has_unique_cd_key(void *ctx) { return qa_qvm_ui_has_unique_cd_key(ctx); }

qa_q3_ui_exports qa_qvm_ui_as_exports(qa_qvm_ui *ui) {
  qa_q3_ui_exports exports;
  memset(&exports, 0, sizeof exports);
  exports.ctx = ui;
  exports.api = ui->api;
  exports.seat = ui->seat;
  exports.init = exports_init;
  exports.shutdown = exports_shutdown;
  exports.key_event = exports_key_event;
  exports.mouse_event = exports_mouse_event;
  exports.refresh = exports_refresh;
  exports.is_fullscreen = exports_is_fullscreen;
  exports.set_active_menu = exports_set_active_menu;
  exports.console_command = exports_console_command;
  exports.draw_connect_screen = exports_draw_connect_screen;
  exports.has_unique_cd_key = exports_has_unique_cd_key;
  return exports;
}
