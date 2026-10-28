/*
 * Port of src/ui/library/menu.ts: LibraryEntry, LibraryMenuService,
 * registerLibraryMenu. The service is borrowed and must outlive
 * the menus; its create/stop labels are re-read on every factory
 * call, as in the TS. scope NULL (or returning NULL) is the TS
 * absent scope ("" view key). Search lowercases with ASCII
 * tolower over bytes (TS toLowerCase differs only for non-ASCII).
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_LIBRARY_MENU_H
#define QA_UI_LIBRARY_MENU_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/ui.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/settings/index.h"

/** TS LibraryEntry. detail/unavailable NULL are TS undefined. */
typedef struct qa_ui_library_entry {
  const char *id;
  const char *label;
  const char *detail;     /* optional */
  const char *unavailable; /* optional */
} qa_ui_library_entry;

/** TS LibraryMenuService (one shared ctx). */
typedef struct qa_ui_library_menu_service {
  void *ctx;
  const char *(*scope)(void *ctx); /* optional */
  const qa_ui_library_entry *(*entries)(void *ctx, size_t *out_count);
  const char *(*status)(void *ctx);
  void (*refresh)(void *ctx);
  void (*activate)(void *ctx, const char *id);
  bool has_create;
  const char *create_label;
  void (*create_submit)(void *ctx, const char *name);
  bool has_stop;
  const char *stop_label;
  void (*stop_activate)(void *ctx);
} qa_ui_library_menu_service;

/** TS registerLibraryMenu(controller, id, title, service). The
 * title string is copied; the service is borrowed. */
qa_ui_settings_menus qa_ui_register_library_menu(
    qa_native_ui_controller *controller, qa_ui_menu_id id, const char *title,
    const qa_ui_library_menu_service *service);

#endif
