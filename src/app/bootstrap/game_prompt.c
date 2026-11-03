/* Port of src/app/bootstrap/game-prompt.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/game_prompt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/text/localization.h"
#include "qa/text/localization_resources.h"
#include "qa/ui/common/layout.h"
const char qa_app_game_prompt_menu[] = "menu:application:prompt";
/* TS PendingPrompt: { content, value: Prompt }, owned copy. */
typedef struct pending_t { char *content, *title; qa_actor_id actor; qa_app_prompt_choice *choices; size_t choice_count; } pending_t;
/* TS catalogs: Map<string, Promise<LocalizationCatalog>>; the reader keeps the provider's mounts. */
typedef struct catalog_entry { char *key; qa_content_mounted *mounts; qa_localization_catalog catalog; } catalog_entry;
typedef struct choice_ctx { struct qa_app_seat_game_prompt *prompt; size_t index; } choice_ctx;
struct qa_app_seat_game_prompt {
  qa_seat_id seat;
  qa_native_ui_controller *controller;
  qa_app_seat_game_prompt_host host;
  int registration;
  pending_t *pending, *prepared;
  char *prepared_language, *title;
  double page;
  qa_app_prompt_choice *choices; size_t choice_count; /* localized labels, owned */
  catalog_entry **catalogs; size_t catalog_count, catalog_cap;
  /* The menu factory's controls (rebuilt per call). */
  qa_ui_control *controls; char **control_text; size_t control_count, control_text_count;
  choice_ctx choice_ctxs[4];
  bool closed;
};
static char *dup_text(const char *s) {
  if (s == NULL) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p == NULL) qa_range_error("Out of memory");
  memcpy(p, s, n);
  return p;
}
static const char *language_of(qa_app_seat_game_prompt *prompt) {
  return prompt->host.language != NULL ? prompt->host.language(prompt->host.ctx) : "english";
}
static void free_controls(qa_app_seat_game_prompt *prompt) {
  for (size_t i = 0; i < prompt->control_text_count; i++) free(prompt->control_text[i]);
  free(prompt->control_text); free(prompt->controls);
  prompt->control_text = NULL; prompt->controls = NULL;
  prompt->control_count = prompt->control_text_count = 0;
}
static char *control_text(qa_app_seat_game_prompt *prompt, char *text) {
  prompt->control_text[prompt->control_text_count++] = text;
  return text;
}
static char *format(const char *fmt, const char *a, const char *b) {
  size_t length = strlen(fmt) + strlen(a) + strlen(b) + 1;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  snprintf(out, length, fmt, a, b);
  return out;
}
static void choose(qa_app_seat_game_prompt *prompt, double index);
static void choice_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  choice_ctx *choice = ctx;
  choose(choice->prompt, (double)choice->index);
}
static void previous_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  ((qa_app_seat_game_prompt *)ctx)->page--;
}
static void next_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  ((qa_app_seat_game_prompt *)ctx)->page++;
}
static void noop_menu(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
/* TS controls(). */
static void build_controls(qa_app_seat_game_prompt *prompt) {
  free_controls(prompt);
  double first = prompt->page * 4;
  size_t start = first < 0 ? 0 : (size_t)first;
  size_t end = start + 4 < prompt->choice_count ? start + 4 : prompt->choice_count;
  size_t visible = start < end ? end - start : 0;
  bool paged = prompt->choice_count > 4;
  prompt->control_count = visible + (paged ? 2 : 0);
  prompt->controls = calloc(prompt->control_count == 0 ? 1 : prompt->control_count, sizeof *prompt->controls);
  prompt->control_text = calloc(prompt->control_count * 2 + 1, sizeof *prompt->control_text);
  if (prompt->controls == NULL || prompt->control_text == NULL) qa_range_error("Out of memory");
  for (size_t row = 0; row < visible; row++) {
    size_t index = start + row;
    char number_id[32], number_label[32];
    snprintf(number_id, sizeof number_id, "%s", QA_JS_NUMBER(first + (double)row));
    snprintf(number_label, sizeof number_label, "%s", QA_JS_NUMBER(first + (double)row + 1));
    qa_ui_control *control = &prompt->controls[row];
    control->kind = "button";
    control->id = control_text(prompt, format("ui:game-prompt:%s%s", number_id, ""));
    control->label = control_text(prompt, format("%s. %s", number_label, prompt->choices[index].label));
    qa_ui_menu_row_options options = {false, true, false, true, 0, 220, 0, 44};
    control->rect = qa_ui_menu_row((double)row, options);
    control->enabled = true; control->visible = true;
    prompt->choice_ctxs[row].prompt = prompt; prompt->choice_ctxs[row].index = index;
    control->ctx = &prompt->choice_ctxs[row];
    control->activate_seat = choice_activate;
  }
  if (paged) {
    qa_ui_control *previous = &prompt->controls[visible], *next = &prompt->controls[visible + 1];
    previous->kind = "button"; previous->id = "ui:game-prompt:previous"; previous->label = "Previous";
    qa_ui_menu_row_options previous_options = {false, true, true, false, 0, 408, 248, 0};
    previous->rect = qa_ui_menu_row(0, previous_options);
    previous->enabled = prompt->page > 0; previous->visible = true;
    previous->ctx = prompt; previous->activate_seat = previous_activate;
    char page[32], pages[32];
    snprintf(page, sizeof page, "%s", QA_JS_NUMBER(prompt->page + 1));
    snprintf(pages, sizeof pages, "%s", QA_JS_NUMBER(ceil((double)prompt->choice_count / 4)));
    next->kind = "button"; next->id = "ui:game-prompt:next";
    next->label = control_text(prompt, format("Next (%s/%s)", page, pages));
    qa_ui_menu_row_options next_options = {true, true, true, false, 328, 408, 248, 0};
    next->rect = qa_ui_menu_row(0, next_options);
    next->enabled = first + 4 < (double)prompt->choice_count; next->visible = true;
    next->ctx = prompt; next->activate_seat = next_activate;
  }
}
static qa_ui_menu make_menu(void *ctx) {
  qa_app_seat_game_prompt *prompt = ctx;
  build_controls(prompt);
  qa_ui_menu out;
  memset(&out, 0, sizeof out);
  out.id = qa_app_game_prompt_menu;
  out.title = prompt->title;
  out.full_screen = false;
  out.controls = prompt->controls; out.control_count = prompt->control_count;
  out.ctx = prompt; out.open = noop_menu; out.close = noop_menu;
  return out;
}
qa_app_seat_game_prompt *qa_app_seat_game_prompt_create(qa_seat_id seat, qa_native_ui_controller *controller, qa_app_seat_game_prompt_host host) {
  qa_app_seat_game_prompt *prompt = calloc(1, sizeof *prompt);
  if (prompt == NULL) return NULL;
  prompt->seat = seat;
  prompt->controller = controller;
  prompt->host = host;
  prompt->title = dup_text("");
  prompt->prepared_language = dup_text("");
  prompt->registration = qa_native_ui_controller_register(controller, qa_app_game_prompt_menu, make_menu, prompt);
  return prompt;
}
static void pending_free(pending_t *pending) {
  if (pending == NULL) return;
  for (size_t i = 0; i < pending->choice_count; i++) free((char *)pending->choices[i].label);
  free(pending->content); free(pending->title); free(pending->choices); free(pending);
}
static void set_pending(qa_app_seat_game_prompt *prompt, pending_t *pending) {
  if (prompt->pending != NULL && prompt->pending != prompt->prepared) pending_free(prompt->pending);
  prompt->pending = pending;
}
static void set_prepared(qa_app_seat_game_prompt *prompt, pending_t *prepared) {
  if (prompt->prepared != NULL && prompt->prepared != prompt->pending && prompt->prepared != prepared) pending_free(prompt->prepared);
  prompt->prepared = prepared;
}
static void free_choices(qa_app_seat_game_prompt *prompt) {
  for (size_t i = 0; i < prompt->choice_count; i++) free((char *)prompt->choices[i].label);
  free(prompt->choices);
  prompt->choices = NULL; prompt->choice_count = 0;
}
void qa_app_seat_game_prompt_destroy(qa_app_seat_game_prompt *prompt) {
  if (prompt == NULL) return;
  qa_app_seat_game_prompt_close(prompt);
  free(prompt->prepared_language); free(prompt->title);
  free_choices(prompt);
  free_controls(prompt);
  free(prompt->catalogs);
  free(prompt);
}
void qa_app_seat_game_prompt_receive(qa_app_seat_game_prompt *prompt, const qa_app_prompt_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_prompt_event *event = &events[i];
    bool is_prompt = event->kind != NULL && strcmp(event->kind, "prompt") == 0;
    bool is_clear = event->kind != NULL && strcmp(event->kind, "clear-prompt") == 0;
    if ((!is_prompt && !is_clear) || !qa_same_actor(event->actor, prompt->host.actor(prompt->host.ctx))) continue;
    if (is_clear) { qa_app_seat_game_prompt_clear(prompt); continue; }
    pending_t *pending = calloc(1, sizeof *pending);
    if (pending == NULL) qa_range_error("Out of memory");
    pending->content = dup_text(event->content);
    pending->title = dup_text(event->title);
    pending->actor = event->actor;
    pending->choices = calloc(event->choice_count == 0 ? 1 : event->choice_count, sizeof *pending->choices);
    if (pending->choices == NULL) qa_range_error("Out of memory");
    pending->choice_count = event->choice_count;
    for (size_t j = 0; j < event->choice_count; j++) {
      pending->choices[j].label = dup_text(event->choices[j].label);
      pending->choices[j].impulse = event->choices[j].impulse;
    }
    set_pending(prompt, pending);
  }
}
/* (await provider.mounts.open(path))?.bytes ?? null */
static uint8_t *catalog_read(void *ctx, const char *path, size_t *out_length) {
  catalog_entry *entry = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(entry->mounts, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  if (opened.bytes == NULL) {
    uint8_t *empty = malloc(1);
    if (empty == NULL) qa_range_error("Out of memory");
    return empty;
  }
  return opened.bytes;
}
/* TS catalog(content, language, assets). */
static qa_localization_catalog *catalog_for(qa_app_seat_game_prompt *prompt, const char *content, const char *language,
                                            qa_app_application_assets *assets) {
  char *key = format("%s:%s", content, language);
  for (size_t i = 0; i < prompt->catalog_count; i++)
    if (strcmp(prompt->catalogs[i]->key, key) == 0) { free(key); return &prompt->catalogs[i]->catalog; }
  catalog_entry *entry = calloc(1, sizeof *entry);
  if (entry == NULL) qa_range_error("Out of memory");
  entry->key = key;
  if (prompt->catalog_count == prompt->catalog_cap) {
    prompt->catalog_cap = prompt->catalog_cap ? prompt->catalog_cap * 2 : 4;
    catalog_entry **grown = realloc(prompt->catalogs, prompt->catalog_cap * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    prompt->catalogs = grown;
  }
  prompt->catalogs[prompt->catalog_count++] = entry;
  entry->mounts = qa_app_application_assets_provider(assets, content).mounts;
  qa_load_localization_resources(prompt->seat, language, catalog_read, entry, QA_LOC_Q1_RERELEASE, &entry->catalog);
  return &entry->catalog;
}
static char *localize(qa_localization_catalog *catalog, const char *text) {
  char *out = qa_localization_table_localize(&catalog->table, text, NULL, 0, true, 1024, NULL);
  if (out == NULL) qa_range_error("Out of memory");
  return out;
}
void qa_app_seat_game_prompt_prepare(qa_app_seat_game_prompt *prompt, qa_app_application_assets *assets,
    const char *(*focus)(void *ctx), void *focus_ctx) {
  pending_t *pending = prompt->pending;
  char *language = dup_text(language_of(prompt));
  if (pending != NULL && !qa_same_actor(pending->actor, prompt->host.actor(prompt->host.ctx))) {
    free(language);
    qa_app_seat_game_prompt_clear(prompt);
    return;
  }
  if (pending != NULL && (pending != prompt->prepared || strcmp(language, prompt->prepared_language) != 0)) {
    qa_cleanup language_guard;
    qa_cleanup_push(&language_guard, free, language);
    qa_localization_catalog *catalog = catalog_for(prompt, pending->content, language, assets);
    qa_cleanup_pop(&language_guard);
    if (strcmp(language_of(prompt), language) != 0 || prompt->pending != pending
        || !qa_same_actor(pending->actor, prompt->host.actor(prompt->host.ctx))) {
      free(language);
      return;
    }
    prompt->page = 0;
    free(prompt->title);
    prompt->title = localize(catalog, pending->title);
    free_choices(prompt);
    prompt->choices = calloc(pending->choice_count == 0 ? 1 : pending->choice_count, sizeof *prompt->choices);
    if (prompt->choices == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < pending->choice_count; i++) {
      prompt->choices[i].label = localize(catalog, pending->choices[i].label);
      prompt->choices[i].impulse = pending->choices[i].impulse;
      prompt->choice_count = i + 1;
    }
    set_prepared(prompt, pending);
    free(prompt->prepared_language);
    prompt->prepared_language = language;
    language = NULL;
  }
  free(language);
  if (prompt->pending != NULL && strcmp(focus(focus_ctx), "game") == 0)
    qa_native_ui_controller_open_menu(prompt->controller, qa_app_game_prompt_menu);
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(prompt->controller);
  if (prompt->pending == NULL && active != NULL && strcmp(active, qa_app_game_prompt_menu) == 0)
    qa_native_ui_controller_close_menu(prompt->controller);
}
bool qa_app_seat_game_prompt_input(qa_app_seat_game_prompt *prompt, const qa_seat_input_event *event) {
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(prompt->controller);
  if (!qa_same_seat(event->seat, prompt->seat) || active == NULL || strcmp(active, qa_app_game_prompt_menu) != 0) return false;
  if (event->kind != NULL && strcmp(event->kind, "key") == 0 && event->code >= 49 && event->code <= 57) {
    if (event->down && !event->repeat) choose(prompt, event->code - 49);
    return true;
  }
  return false;
}
/* TS choose(index): this.choices[index] (the localized choices). */
static void choose(qa_app_seat_game_prompt *prompt, double index) {
  if (prompt->pending == NULL || prompt->pending != prompt->prepared
      || !qa_same_actor(prompt->pending->actor, prompt->host.actor(prompt->host.ctx)))
    return;
  if (index < 0 || index >= (double)prompt->choice_count || index != floor(index)) return;
  double impulse = prompt->choices[(size_t)index].impulse;
  qa_app_seat_game_prompt_clear(prompt);
  prompt->host.impulse(prompt->host.ctx, impulse);
}
void qa_app_seat_game_prompt_clear(qa_app_seat_game_prompt *prompt) {
  pending_t *pending = prompt->pending, *prepared = prompt->prepared;
  prompt->pending = NULL;
  prompt->prepared = NULL;
  pending_free(pending);
  if (prepared != pending) pending_free(prepared);
  free(prompt->title);
  prompt->title = dup_text("");
  prompt->page = 0;
  free_choices(prompt);
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(prompt->controller);
  if (active != NULL && strcmp(active, qa_app_game_prompt_menu) == 0) qa_native_ui_controller_close_menu(prompt->controller);
}
void qa_app_seat_game_prompt_close(qa_app_seat_game_prompt *prompt) {
  if (prompt == NULL || prompt->closed) return;
  prompt->closed = true;
  qa_app_seat_game_prompt_clear(prompt);
  qa_native_ui_controller_unregister(prompt->controller, prompt->registration);
  for (size_t i = 0; i < prompt->catalog_count; i++) {
    qa_localization_catalog_destroy(&prompt->catalogs[i]->catalog);
    free(prompt->catalogs[i]->key);
    free(prompt->catalogs[i]);
  }
  prompt->catalog_count = 0;
}
qa_ui_menu qa_app_seat_game_prompt_menu(qa_app_seat_game_prompt *prompt) { return make_menu(prompt); }
size_t qa_app_seat_game_prompt_page(const qa_app_seat_game_prompt *prompt) { return (size_t)prompt->page; }
const char *qa_app_seat_game_prompt_title(const qa_app_seat_game_prompt *prompt) { return prompt->title; }
size_t qa_app_seat_game_prompt_choice_count(const qa_app_seat_game_prompt *prompt) { return prompt->choice_count; }
const char *qa_app_seat_game_prompt_choice_label(const qa_app_seat_game_prompt *prompt, size_t index) {
  return index < prompt->choice_count ? prompt->choices[index].label : NULL;
}
