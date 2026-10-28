/* Port of src/ui/settings/server.ts. The profile save/load is
 * synchronous (the landed ConfigStore is synchronous); busy is
 * still tracked, so it is only observable re-entrantly. Owner
 * write failures have no C error channel (the TS catch shows
 * "Unable to apply setting." only for non-Error throws); value
 * validation errors are pre-checked with parseServerSetting's
 * messages so Apply shows them instead of aborting.
 * GPL-2.0-or-later. */
#ifndef QA_UI_SETTINGS_SERVER_H
#define QA_UI_SETTINGS_SERVER_H

#include "qa/settings/config.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/rotation.h"

/** TS HostServerSettingsUi. bindings returns a borrowed array
 * valid until its next call. */
typedef struct qa_ui_host_server_settings {
  qa_ui_server_bindings_fn bindings;
  void *bindings_ctx;
  qa_config_store *store;
} qa_ui_host_server_settings;

/** TS registerServerSettingsMenu(controller, host). */
qa_ui_settings_menus qa_ui_register_server_settings_menu(
    qa_native_ui_controller *controller,
    const qa_ui_host_server_settings *host);

#endif
