/*
 * Port of src/ui/mods/menu.ts: ModMenuRow, ModMenuService,
 * registerModMenu. The service is borrowed and must outlive the
 * menu. Search uses ASCII lowercasing and byte offsets (TS
 * toLocaleLowerCase / the /.{1,42}/gu wrap differ only for
 * non-ASCII text). GPL-2.0-or-later.
 */
#ifndef QA_UI_MODS_MENU_H
#define QA_UI_MODS_MENU_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/** TS ModMenuRow. unavailable NULL is TS null. */
typedef struct qa_ui_mod_menu_row {
  const char *id;
  const char *title;
  const char *source;
  bool enabled;
  const char *unavailable; /* nullable */
} qa_ui_mod_menu_row;

/** TS ModMenuService (one shared ctx). */
typedef struct qa_ui_mod_menu_service {
  void *ctx;
  const qa_ui_mod_menu_row *(*rows)(void *ctx, size_t *out_count);
  void (*set_enabled)(void *ctx, const char *id, bool enabled);
  void (*refresh)(void *ctx);
  const char *(*status)(void *ctx);
} qa_ui_mod_menu_service;

/** TS registerModMenu(controller, service). */
qa_ui_settings_menus qa_ui_register_mod_menu(
    qa_native_ui_controller *controller,
    const qa_ui_mod_menu_service *service);

#endif
