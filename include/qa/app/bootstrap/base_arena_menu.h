/* Port of src/app/bootstrap/base-arena-menu.ts. Menu control
 * construction is controller-host work; the menu ids, difficulty
 * table, result-identity gating and service routing are kept here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_BASE_ARENA_MENU_H
#define QA_APP_BOOTSTRAP_BASE_ARENA_MENU_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/base_arena_postgame.h"
#include "qa/app/bootstrap/base_arena_selection.h"
#include "qa/ui/common/controller.h"
extern const char qa_app_arena_progress_root[]; /* "menu:application:arena-progress" */
typedef struct qa_app_arena_progress_row { const char *label, *value; } qa_app_arena_progress_row;
typedef struct qa_app_base_arena_menu_service {
  void *ctx;
  const qa_app_arena_selection *(*selection)(void *ctx);
  double (*skill)(void *ctx);
  void (*play)(void *ctx, const char *map, double skill);
  bool (*result)(void *ctx, const qa_app_arena_postgame_presentation **out); /* false = TS null */
  const char *(*player_name)(void *ctx, double client);
  const qa_app_arena_progress_row *(*progress)(void *ctx, size_t *out_count);
  void (*retry)(void *ctx);
  void (*next)(void *ctx);
  void (*quit)(void *ctx);
  void (*reset)(void *ctx);
} qa_app_base_arena_menu_service;
typedef struct qa_app_base_arena_menus qa_app_base_arena_menus;
qa_app_base_arena_menus *qa_app_base_arena_menus_create(qa_native_ui_controller *controller, qa_app_base_arena_menu_service service);
void qa_app_base_arena_menus_destroy(qa_app_base_arena_menus *menus);
const char *qa_app_base_arena_menus_root(const qa_app_base_arena_menus *menus);
void qa_app_base_arena_menus_update(qa_app_base_arena_menus *menus);
void qa_app_base_arena_menus_close(qa_app_base_arena_menus *menus);
/* Difficulty table (TS difficulties 1..5). */
const char *qa_app_arena_difficulty_label(double skill);
#endif
