/* Port of src/ui/common/localization.ts: MenuCatalogs and
 * createMenuLocalizer. The returned closure becomes a localizer struct;
 * its result is borrowed (the catalog translation or the input text).
 * GPL-2.0-or-later. */
#ifndef QA_UI_COMMON_LOCALIZATION_H
#define QA_UI_COMMON_LOCALIZATION_H
#include <stddef.h>
/* One ReadonlyMap<string, string> entry. */
typedef struct qa_ui_menu_catalog_entry { const char *text, *translation; } qa_ui_menu_catalog_entry;
/* One MenuCatalogs entry: language -> catalog. */
typedef struct qa_ui_menu_catalog {
  const char *language;
  const qa_ui_menu_catalog_entry *entries; size_t entry_count;
} qa_ui_menu_catalog;
/* TS MenuCatalogs = ReadonlyMap<string, ReadonlyMap<string, string>>. */
typedef struct qa_ui_menu_catalogs { const qa_ui_menu_catalog *catalogs; size_t count; } qa_ui_menu_catalogs;
/* TS createMenuLocalizer(catalogs, language). language returns malloc'd
 * text the localizer frees. */
typedef struct qa_ui_menu_localizer {
  void *ctx;
  qa_ui_menu_catalogs (*catalogs)(void *ctx);
  char *(*language)(void *ctx);
} qa_ui_menu_localizer;
/* The TS closure call: "english" keeps text; unknown languages and
 * strings missing from a catalog fall back to the source literal. */
const char *qa_ui_menu_localize(const qa_ui_menu_localizer *localizer, const char *text);
#endif
