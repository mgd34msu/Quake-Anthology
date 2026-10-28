/* Port of src/app/bootstrap/startup-menu.ts. Menu tree construction,
 * campaign/lobby/saves services and main-loop/menu-state types
 * (sibling bootstrap + ui slices) are host fn+ctx; menu lifetime,
 * registration, event routing and graphic navigation ordering are
 * kept here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_MENU_H
#define QA_APP_BOOTSTRAP_STARTUP_MENU_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/common.h"
#include "qa/contracts/render.h"
/* qa_menu_key_event mirror below (input barrel sibling name). */
#include "qa/platform/sdl.h"
/* Mirror: TS MenuKeyEvent (input/core.ts). */
typedef struct qa_menu_key_event { const char *key; bool down; } qa_menu_key_event;
#include "qa/ui/common/controller.h"
typedef struct qa_app_startup_menu_host {
  void *ctx;
  const char *(*build_menus)(void *ctx);
  qa_native_ui_controller *(*controller)(void *ctx);
  bool (*handle_event)(void *ctx, const qa_sdl_event *event);
  void (*graphic_navigation)(void *ctx, const qa_menu_key_event *event);
  void (*record_command)(void *ctx, const char *command);
  void (*commands_append)(void *ctx, const char *text, const qa_command_context *source);
  void (*navigate)(void *ctx, const char *target);
  void (*set_display_mode)(void *ctx, bool hidden);
  void (*enter_game)(void *ctx);
  void (*leave_game)(void *ctx);
  void (*prepare_saves)(void *ctx);
  void (*close_saves)(void *ctx);
  const char *(*resolve)(void *ctx);
  void (*close_menu)(void *ctx);
  bool (*open)(void *ctx);
  void (*pump)(void *ctx);
  int (*run_frame)(void *ctx); /* -1 continue, 0 selected, 1 failed */
  void (*close_services)(void *ctx);
} qa_app_startup_menu_host;
typedef struct qa_app_startup_menu qa_app_startup_menu;
qa_app_startup_menu *qa_app_startup_menu_create(qa_app_startup_menu_host host);
void qa_app_startup_menu_destroy(qa_app_startup_menu *m);
void qa_app_startup_menu_graphic_navigation(qa_app_startup_menu *m, const qa_menu_key_event *event);
bool qa_app_startup_menu_handle(qa_app_startup_menu *m, const qa_sdl_event *event);
void qa_app_startup_menu_command(qa_app_startup_menu *m, const char *command);
void qa_app_startup_menu_set_display_mode(qa_app_startup_menu *m, bool hidden);
bool qa_app_startup_menu_open(qa_app_startup_menu *m);
void qa_app_startup_menu_pump(qa_app_startup_menu *m);
void qa_app_startup_menu_prepare_frame(qa_app_startup_menu *m, qa_render_frame frame);
void qa_app_startup_menu_navigate(qa_app_startup_menu *m, const char *target);
void qa_app_startup_menu_close(qa_app_startup_menu *m);
#endif
