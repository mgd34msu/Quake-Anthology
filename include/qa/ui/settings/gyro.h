/* Port of src/ui/settings/gyro.ts. save() is synchronous and
 * returns an error message (borrowed) or NULL. GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_GYRO_H
#define QA_UI_SETTINGS_GYRO_H

#include <stdbool.h>

#include "qa/input/router.h"
#include "qa/platform/controller.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/** TS GyroSettingsUi. device false is TS null. */
typedef struct qa_ui_gyro_settings {
  void *ctx;
  qa_input_router *router;
  bool (*device)(void *ctx, qa_controller_device *out);
  bool (*busy)(void *ctx);
  const char *(*message)(void *ctx);
  const char *(*save)(void *ctx);
} qa_ui_gyro_settings;

/** title NULL selects "Gyro controls". */
qa_ui_settings_menus qa_ui_register_gyro_settings_menu(
    qa_native_ui_controller *controller,
    const qa_ui_gyro_settings *settings, const char *title);

#endif
