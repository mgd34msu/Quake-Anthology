/* Port of src/app/bootstrap/game-prompt.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/game_prompt.h"
#include <stdlib.h>
#include <string.h>
const char qa_app_game_prompt_menu[] = "menu:application:prompt";
typedef struct { char *content, *title; qa_actor_id actor; qa_app_prompt_choice *choices; size_t choice_count; char **labels; } pending_t;
struct qa_app_seat_game_prompt {
  qa_seat_id seat;
  qa_native_ui_controller *controller;
  qa_app_seat_game_prompt_host host;
  int registration;
  pending_t *pending, *prepared;
  char *prepared_language, *title;
  size_t page;
  char **choice_labels; size_t choice_count;
  bool closed;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static qa_ui_menu make_menu(void *ctx) {
  qa_app_seat_game_prompt *prompt = ctx;
  qa_ui_menu out = {0};
  out.id = qa_app_game_prompt_menu;
  out.title = prompt->title ? prompt->title : "";
  out.full_screen = false;
  return out;
}
qa_app_seat_game_prompt *qa_app_seat_game_prompt_create(qa_seat_id seat, qa_native_ui_controller *controller, qa_app_seat_game_prompt_host host) {
  qa_app_seat_game_prompt *prompt = calloc(1, sizeof *prompt);
  if (!prompt) return NULL;
  prompt->seat = seat;
  prompt->controller = controller;
  prompt->host = host;
  prompt->title = dup("");
  prompt->prepared_language = dup("");
  prompt->registration = qa_native_ui_controller_register(controller, qa_app_game_prompt_menu, make_menu, prompt);
  return prompt;
}
static void pending_free(pending_t *pending) {
  if (!pending) return;
  free(pending->content);
  free(pending->title);
  free(pending->choices);
  free(pending);
}
void qa_app_seat_game_prompt_destroy(qa_app_seat_game_prompt *prompt) {
  if (!prompt) return;
  qa_app_seat_game_prompt_close(prompt);
  pending_free(prompt->pending == prompt->prepared ? NULL : prompt->pending);
  pending_free(prompt->prepared);
  free(prompt->prepared_language);
  free(prompt->title);
  for (size_t i = 0; i < prompt->choice_count; i++) free(prompt->choice_labels[i]);
  free(prompt->choice_labels);
  free(prompt);
}
void qa_app_seat_game_prompt_receive(qa_app_seat_game_prompt *prompt, const qa_app_prompt_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_prompt_event *event = &events[i];
    bool is_prompt = event->kind && strcmp(event->kind, "prompt") == 0;
    bool is_clear = event->kind && strcmp(event->kind, "clear-prompt") == 0;
    if ((!is_prompt && !is_clear) || !qa_same_actor(event->actor, prompt->host.actor(prompt->host.ctx))) continue;
    if (is_clear) qa_app_seat_game_prompt_clear(prompt);
    else {
      pending_t *pending = calloc(1, sizeof *pending);
      pending->content = dup(event->content);
      pending->title = dup(event->title);
      pending->actor = event->actor;
      pending->choices = malloc((event->choice_count ? event->choice_count : 1) * sizeof *pending->choices);
      pending->choice_count = event->choice_count;
      for (size_t j = 0; j < event->choice_count; j++) pending->choices[j] = event->choices[j];
      if (prompt->pending && prompt->pending != prompt->prepared) pending_free(prompt->pending);
      prompt->pending = pending;
    }
  }
}
const char *qa_app_seat_game_prompt_prepare(qa_app_seat_game_prompt *prompt) {
  const char *language = prompt->host.language ? prompt->host.language(prompt->host.ctx) : "english";
  pending_t *pending = prompt->pending;
  if (pending && !qa_same_actor(pending->actor, prompt->host.actor(prompt->host.ctx))) {
    qa_app_seat_game_prompt_clear(prompt);
    return NULL;
  }
  if (pending && (pending != prompt->prepared || strcmp(language, prompt->prepared_language) != 0)) {
    char *title = NULL;
    const char *error = prompt->host.localize(prompt->host.ctx, pending->content, language, pending->title, &title);
    if (error) return error;
    if (strcmp(prompt->host.language ? prompt->host.language(prompt->host.ctx) : "english", language) != 0 ||
        prompt->pending != pending || !qa_same_actor(pending->actor, prompt->host.actor(prompt->host.ctx))) {
      free(title);
      return NULL;
    }
    prompt->page = 0;
    free(prompt->title);
    prompt->title = title;
    for (size_t i = 0; i < prompt->choice_count; i++) free(prompt->choice_labels[i]);
    free(prompt->choice_labels);
    prompt->choice_count = pending->choice_count;
    prompt->choice_labels = calloc(prompt->choice_count ? prompt->choice_count : 1, sizeof *prompt->choice_labels);
    for (size_t i = 0; i < pending->choice_count; i++) {
      char *label = NULL;
      error = prompt->host.localize(prompt->host.ctx, pending->content, language, pending->choices[i].label, &label);
      if (error) return error;
      prompt->choice_labels[i] = label;
    }
    if (prompt->prepared && prompt->prepared != pending) pending_free(prompt->prepared);
    prompt->prepared = pending;
    free(prompt->prepared_language);
    prompt->prepared_language = dup(language);
  }
  if (prompt->pending && strcmp(prompt->host.focus_kind(prompt->host.ctx), "game") == 0)
    qa_native_ui_controller_open_menu(prompt->controller, qa_app_game_prompt_menu);
  if (!prompt->pending) {
    qa_ui_menu_id active = qa_native_ui_controller_active_menu(prompt->controller);
    if (active && strcmp(active, qa_app_game_prompt_menu) == 0) qa_native_ui_controller_close_menu(prompt->controller);
  }
  return NULL;
}
void qa_app_seat_game_prompt_choose(qa_app_seat_game_prompt *prompt, size_t index) {
  if (!prompt->pending || prompt->pending != prompt->prepared || !qa_same_actor(prompt->pending->actor, prompt->host.actor(prompt->host.ctx))) return;
  if (index >= prompt->pending->choice_count) return;
  double impulse = prompt->pending->choices[index].impulse;
  qa_app_seat_game_prompt_clear(prompt);
  prompt->host.impulse(prompt->host.ctx, impulse);
}
bool qa_app_seat_game_prompt_input(qa_app_seat_game_prompt *prompt, const qa_seat_input_event *event) {
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(prompt->controller);
  if (!qa_same_seat(event->seat, prompt->seat) || !active || strcmp(active, qa_app_game_prompt_menu) != 0) return false;
  if (event->kind && strcmp(event->kind, "key") == 0 && event->code >= 49 && event->code <= 57) {
    if (event->down && !event->repeat) qa_app_seat_game_prompt_choose(prompt, (size_t)(event->code - 49));
    return true;
  }
  return false;
}
void qa_app_seat_game_prompt_clear(qa_app_seat_game_prompt *prompt) {
  if (prompt->pending && prompt->pending != prompt->prepared) pending_free(prompt->pending);
  if (prompt->prepared) pending_free(prompt->prepared);
  prompt->pending = NULL;
  prompt->prepared = NULL;
  free(prompt->title);
  prompt->title = dup("");
  prompt->page = 0;
  for (size_t i = 0; i < prompt->choice_count; i++) free(prompt->choice_labels[i]);
  free(prompt->choice_labels);
  prompt->choice_labels = NULL;
  prompt->choice_count = 0;
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(prompt->controller);
  if (active && strcmp(active, qa_app_game_prompt_menu) == 0) qa_native_ui_controller_close_menu(prompt->controller);
}
void qa_app_seat_game_prompt_close(qa_app_seat_game_prompt *prompt) {
  if (!prompt || prompt->closed) return;
  prompt->closed = true;
  qa_app_seat_game_prompt_clear(prompt);
  qa_native_ui_controller_unregister(prompt->controller, prompt->registration);
}
size_t qa_app_seat_game_prompt_page(const qa_app_seat_game_prompt *prompt) { return prompt->page; }
void qa_app_seat_game_prompt_set_page(qa_app_seat_game_prompt *prompt, size_t page) { prompt->page = page; }
const char *qa_app_seat_game_prompt_title(const qa_app_seat_game_prompt *prompt) { return prompt->title; }
size_t qa_app_seat_game_prompt_choice_count(const qa_app_seat_game_prompt *prompt) { return prompt->choice_count; }
const char *qa_app_seat_game_prompt_choice_label(const qa_app_seat_game_prompt *prompt, size_t index) {
  return index < prompt->choice_count ? prompt->choice_labels[index] : NULL;
}
