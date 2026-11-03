/*
 * Port of src/ui/saves/menu.ts: SavedGameMenuService,
 * registerSavedGameMenus.
 * C accommodations:
 *  - TS Promise<void> service operations take a settle closure the
 *    service calls exactly once, at once or later (an application
 *    queues save/load/restart and runs them in its step): error NULL
 *    resolves, otherwise the text is the rejection's message. The
 *    menu stays busy ("Working...") until the operation settles, as
 *    the TS run() awaits it; menus disposed while an operation is
 *    pending keep their state until it settles (TS GC).
 *  - Mirror (TS owner not yet ported, removed when it lands):
 *    qa_ui_startup_save_list mirrors StartupSaveList/StartupSaveRow
 *    (src/app/bootstrap/startup-saves.ts).
 *  - The service is borrowed and must outlive the menus; a NULL
 *    service is the TS undefined. Its recovery.restart is carried
 *    for shape fidelity (the TS menu never calls it).
 * GPL-2.0-or-later.
 */
#ifndef QA_UI_SAVES_MENU_H
#define QA_UI_SAVES_MENU_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/ui.h"
#include "qa/ui/common/controller.h"

/* Mirror: TS StartupSaveRow (src/app/bootstrap/startup-saves.ts).
 * unavailable NULL is TS null. */
typedef struct qa_ui_startup_save_row {
  const char *id;
  const char *label;
  const char *map;
  const char *game;
  double saved_at_milliseconds;
  const char *unavailable; /* nullable */
} qa_ui_startup_save_row;

/* Mirror: TS StartupSaveList (src/app/bootstrap/startup-saves.ts).
 * error NULL is TS null; rows are borrowed. */
typedef struct qa_ui_startup_save_list {
  const qa_ui_startup_save_row *rows;
  size_t row_count;
  const char *error; /* nullable */
} qa_ui_startup_save_list;

/** TS SavedGameMenuService (one shared ctx). unavailable action is
 * the exact TS string ("save"|"load"); its NULL return is TS null.
 * save's overwrite NULL is TS null. */
/** The settlement of a TS Promise<void>: fn(ctx, NULL) resolves,
 * fn(ctx, message) rejects (the message is borrowed for the call). */
typedef struct qa_ui_saves_settle {
  void (*fn)(void *ctx, const char *error);
  void *ctx;
} qa_ui_saves_settle;
typedef struct qa_ui_saved_game_menu_service {
  void *ctx;
  bool has_recovery;
  void (*recovery_restart)(void *ctx, qa_ui_saves_settle settle);
  qa_ui_startup_save_list (*list)(void *ctx);
  void (*refresh)(void *ctx, qa_ui_saves_settle settle);
  const char *(*unavailable)(void *ctx, const char *action);
  void (*save)(void *ctx, const char *name, const char *overwrite, qa_ui_saves_settle settle);
  void (*load)(void *ctx, const char *id, qa_ui_saves_settle settle);
} qa_ui_saved_game_menu_service;

/** TS registerSavedGameMenus result {save, load, dispose()}. */
typedef struct qa_ui_saved_game_menus qa_ui_saved_game_menus;
struct qa_ui_saved_game_menus {
  qa_ui_menu_id save;
  qa_ui_menu_id load;
  void *state;
  void (*dispose_fn)(qa_ui_saved_game_menus *menus);
};
void qa_ui_saved_game_menus_dispose(qa_ui_saved_game_menus *menus);

/** TS registerSavedGameMenus(controller, service). */
qa_ui_saved_game_menus qa_ui_register_saved_game_menus(
    qa_native_ui_controller *controller,
    const qa_ui_saved_game_menu_service *service);

#endif
