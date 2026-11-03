/* Port of src/app/bootstrap/game-prompt.ts: SeatGamePrompt, the Q1 composition
 * prompt menu (paged choices, number-key routing) localized through catalogs
 * loaded from ApplicationAssets providers. Async prepare -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_GAME_PROMPT_H
#define QA_APP_BOOTSTRAP_GAME_PROMPT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
#include "qa/ui/common/controller.h"
extern const char qa_app_game_prompt_menu[]; /* "menu:application:prompt" */
typedef struct qa_app_prompt_choice { const char *label; double impulse; } qa_app_prompt_choice;
/* The Q1 composition prompt/clear-prompt event and its source content; the caller
 * unwraps addon events (source.event.kind === "addon" ? source.event.event : source.event). */
typedef struct qa_app_prompt_event {
  const char *kind; /* "prompt"|"clear-prompt"|other */
  qa_actor_id actor;
  const char *title;
  const qa_app_prompt_choice *choices; size_t choice_count;
  const char *content;
} qa_app_prompt_event;
typedef struct qa_app_seat_game_prompt qa_app_seat_game_prompt;
/* TS constructor closures: actor(), impulse(value), language() (NULL is the "english" default). */
typedef struct qa_app_seat_game_prompt_host {
  void *ctx;
  qa_actor_id (*actor)(void *ctx);
  void (*impulse)(void *ctx, double value);
  const char *(*language)(void *ctx);
} qa_app_seat_game_prompt_host;
qa_app_seat_game_prompt *qa_app_seat_game_prompt_create(qa_seat_id seat, qa_native_ui_controller *controller, qa_app_seat_game_prompt_host host);
void qa_app_seat_game_prompt_destroy(qa_app_seat_game_prompt *prompt);
void qa_app_seat_game_prompt_receive(qa_app_seat_game_prompt *prompt, const qa_app_prompt_event *events, size_t count);
/* TS prepare(assets, focus): assets is Pick<ApplicationAssets, "provider">; focus() returns the seat focus kind. */
void qa_app_seat_game_prompt_prepare(qa_app_seat_game_prompt *prompt, qa_app_application_assets *assets,
    const char *(*focus)(void *ctx), void *focus_ctx);
bool qa_app_seat_game_prompt_input(qa_app_seat_game_prompt *prompt, const qa_seat_input_event *event);
void qa_app_seat_game_prompt_clear(qa_app_seat_game_prompt *prompt);
void qa_app_seat_game_prompt_close(qa_app_seat_game_prompt *prompt);
/* The menu factory the prompt registered for gamePromptMenu (title, controls()): the controls stay valid until the
 * next call. */
qa_ui_menu qa_app_seat_game_prompt_menu(qa_app_seat_game_prompt *prompt);
size_t qa_app_seat_game_prompt_page(const qa_app_seat_game_prompt *prompt);
const char *qa_app_seat_game_prompt_title(const qa_app_seat_game_prompt *prompt);
size_t qa_app_seat_game_prompt_choice_count(const qa_app_seat_game_prompt *prompt);
const char *qa_app_seat_game_prompt_choice_label(const qa_app_seat_game_prompt *prompt, size_t index);
#endif
