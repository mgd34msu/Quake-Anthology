/*
 * Port of src/ui/library/match.ts: registerMatchMenu. Commands go
 * through the seat's existing source dispatcher and permission
 * checks (the command callback). GPL-2.0-or-later.
 */
#ifndef QA_UI_LIBRARY_MATCH_H
#define QA_UI_LIBRARY_MATCH_H

#include <stddef.h>

#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/** TS command(name, args) callback. */
typedef void (*qa_ui_match_command_fn)(void *ctx, const char *name,
                                       const char *const *args,
                                       size_t arg_count);

/** TS registerMatchMenu(controller, command). */
qa_ui_settings_menus qa_ui_register_match_menu(
    qa_native_ui_controller *controller, qa_ui_match_command_fn command,
    void *command_ctx);

#endif
