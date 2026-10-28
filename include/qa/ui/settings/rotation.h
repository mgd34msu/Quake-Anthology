/* Port of src/ui/settings/rotation.ts. bindings returns a borrowed
 * array valid until its next call. C accommodation: the landed
 * qa_rotation_map_name / qa_write_server_setting abort with the TS
 * messages, so the TS try/catch notice only ever shows the missing-
 * binding message set locally. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_ROTATION_H
#define QA_UI_SETTINGS_ROTATION_H

#include <stddef.h>

#include "qa/settings/server/types.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

typedef const qa_bound_server_setting *(*qa_ui_server_bindings_fn)(
    void *ctx, size_t *out_count);

/** TS registerMapRotationMenu(controller, bindings). */
qa_ui_settings_menus qa_ui_register_map_rotation_menu(
    qa_native_ui_controller *controller, qa_ui_server_bindings_fn bindings,
    void *bindings_ctx);

#endif
