/*
 * Port of src/compat/qvm/ui.ts. Async exports are synchronous
 * (docs/PORTING.md). Constructing from options builds and owns a
 * module; constructing from a module borrows it - retire retires the
 * module either way, as in the TS. Menu keeps the TS Q3MenuCommand
 * strings.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_UI_H
#define QA_COMPAT_QVM_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/module.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"

typedef void (*qa_qvm_ui_assert_current_fn)(void *ctx);

typedef struct qa_qvm_ui {
  qa_seat_id seat;
  qa_q3_api_identity api;
  qa_qvm_module *module;
  qa_qvm_module owned_module;
  bool owns_module;
  bool retired;
  qa_qvm_ui_assert_current_fn assert_current_operation;
  void *assert_ctx;
} qa_qvm_ui;

void qa_qvm_ui_init(qa_qvm_ui *ui, qa_seat_id seat, const qa_qvm_module_options *options,
                    qa_qvm_ui_assert_current_fn assert_current, void *assert_ctx);
void qa_qvm_ui_init_module(qa_qvm_ui *ui, qa_seat_id seat, qa_qvm_module *module,
                           qa_qvm_ui_assert_current_fn assert_current, void *assert_ctx);
void qa_qvm_ui_create(qa_qvm_ui *ui, qa_seat_id seat, const qa_qvm_module_options *options,
                      qa_qvm_ui_assert_current_fn assert_current, void *assert_ctx);
void qa_qvm_ui_destroy(qa_qvm_ui *ui);
/* Fills the Q3UiExports table (contracts/ui.h) for this instance. */
qa_q3_ui_exports qa_qvm_ui_as_exports(qa_qvm_ui *ui);
void qa_qvm_ui_init_export(qa_qvm_ui *ui, bool connecting);
void qa_qvm_ui_shutdown(qa_qvm_ui *ui);
void qa_qvm_ui_retire(qa_qvm_ui *ui);
void qa_qvm_ui_key_event(qa_qvm_ui *ui, int32_t key, bool down);
void qa_qvm_ui_mouse_event(qa_qvm_ui *ui, int32_t dx, int32_t dy);
void qa_qvm_ui_refresh(qa_qvm_ui *ui, int32_t time);
bool qa_qvm_ui_is_fullscreen(qa_qvm_ui *ui);
void qa_qvm_ui_set_active_menu(qa_qvm_ui *ui, const char *menu);
bool qa_qvm_ui_console_command(qa_qvm_ui *ui, int32_t time, const char *const *arguments, size_t argument_count);
void qa_qvm_ui_draw_connect_screen(qa_qvm_ui *ui, bool overlay);
bool qa_qvm_ui_has_unique_cd_key(qa_qvm_ui *ui);

#endif
