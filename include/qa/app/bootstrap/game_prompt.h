/* Port of src/app/bootstrap/game-prompt.ts. Localization catalog
 * loading is a host step; prompt paging, key routing and menu
 * open/close gating are kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_GAME_PROMPT_H
#define QA_APP_BOOTSTRAP_GAME_PROMPT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
#include "qa/ui/common/controller.h"
extern const char qa_app_game_prompt_menu[]; /* "menu:application:prompt" */
typedef struct qa_app_prompt_choice { const char *label; double impulse; } qa_app_prompt_choice;
/* Q1 composition prompt event fields read here. */
typedef struct qa_app_prompt_event {
  const char *kind; /* "prompt"|"clear-prompt"|other */
  qa_actor_id actor;
  const char *title;
  const qa_app_prompt_choice *choices; size_t choice_count;
  const char *content;
} qa_app_prompt_event;
typedef struct qa_app_seat_game_prompt qa_app_seat_game_prompt;
typedef struct qa_app_seat_game_prompt_host {
  void *ctx;
  qa_actor_id (*actor)(void *ctx);
  void (*impulse)(void *ctx, double value);
  const char *(*language)(void *ctx); /* may be NULL -> "english" */
  /* Localize title+labels for content/language; fills malloc'd text. */
  const char *(*localize)(void *ctx, const char *content, const char *language, const char *text, char **out);
  const char *(*focus_kind)(void *ctx); /* SeatInputFocus.kind */
} qa_app_seat_game_prompt_host;
qa_app_seat_game_prompt *qa_app_seat_game_prompt_create(qa_seat_id seat, qa_native_ui_controller *controller, qa_app_seat_game_prompt_host host);
void qa_app_seat_game_prompt_destroy(qa_app_seat_game_prompt *prompt);
/* TS receive(): composition events already unwrapped by the caller
 * (addon events pass their inner event + content). */
void qa_app_seat_game_prompt_receive(qa_app_seat_game_prompt *prompt, const qa_app_prompt_event *events, size_t count);
const char *qa_app_seat_game_prompt_prepare(qa_app_seat_game_prompt *prompt);
bool qa_app_seat_game_prompt_input(qa_app_seat_game_prompt *prompt, const qa_seat_input_event *event);
void qa_app_seat_game_prompt_clear(qa_app_seat_game_prompt *prompt);
void qa_app_seat_game_prompt_close(qa_app_seat_game_prompt *prompt);
/* Page controls for the menu factory. */
size_t qa_app_seat_game_prompt_page(const qa_app_seat_game_prompt *prompt);
void qa_app_seat_game_prompt_set_page(qa_app_seat_game_prompt *prompt, size_t page);
const char *qa_app_seat_game_prompt_title(const qa_app_seat_game_prompt *prompt);
size_t qa_app_seat_game_prompt_choice_count(const qa_app_seat_game_prompt *prompt);
const char *qa_app_seat_game_prompt_choice_label(const qa_app_seat_game_prompt *prompt, size_t index);
void qa_app_seat_game_prompt_choose(qa_app_seat_game_prompt *prompt, size_t index);
#endif
