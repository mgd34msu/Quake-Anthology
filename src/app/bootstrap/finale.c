/* Port of src/app/bootstrap/finale.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/finale.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/base/rules.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/foundation/js.h"
typedef struct {
  bool has_owner; qa_presentation_owner owner;
  char *content; char *source_text; double started; bool banner;
} state_t;
struct qa_app_source_finale {
  qa_app_finale_host host;
  state_t *state;
  bool prepared;
  double banner_width, banner_height;
  char *message;
  double revision;
  char **loaded; size_t loaded_count, loaded_cap;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
qa_app_source_finale *qa_app_source_finale_create(qa_app_finale_host host) {
  qa_app_source_finale *finale = calloc(1, sizeof *finale);
  if (!finale) return NULL;
  finale->host = host;
  finale->message = dup("");
  return finale;
}
static void state_free(state_t *state) { if (state) { free(state->source_text); free(state->content); } free(state); }
void qa_app_source_finale_destroy(qa_app_source_finale *finale) {
  if (!finale) return;
  state_free(finale->state);
  free(finale->message);
  for (size_t i = 0; i < finale->loaded_count; i++) free(finale->loaded[i]);
  free(finale->loaded);
  free(finale);
}
bool qa_app_source_finale_active(const qa_app_source_finale *finale) { return finale->state != NULL; }
static void finale_begin(qa_app_source_finale *finale, const qa_app_sim_presentation_event *source, const char *text, bool banner) {
  state_free(finale->state);
  finale->state = calloc(1, sizeof *finale->state);
  finale->state->has_owner = source->has_owner;
  if (source->has_owner) finale->state->owner = source->owner;
  finale->state->content = dup(source->content);
  finale->state->source_text = dup(text);
  finale->state->started = source->seconds;
  finale->state->banner = banner;
  finale->prepared = false;
}
void qa_app_source_finale_receive(qa_app_source_finale *finale, const qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *source = &events[i];
    if (eq(source->kind, "presentation-owner")
        && qa_same_presentation_owner(finale->state != NULL && finale->state->has_owner ? &finale->state->owner : NULL,
                                      source->presentation_owner.owner)) {
      finale->revision++;
      if (eq(source->presentation_owner.kind, "retired")) {
        state_free(finale->state);
        finale->state = NULL;
        finale->prepared = false;
        free(finale->message);
        finale->message = dup("");
      }
    } else if (eq(source->kind, "q1-level") && eq(source->q1_level->kind, "finale"))
      finale_begin(finale, source, source->q1_level->text, true);
    else if (eq(source->kind, "q1") && eq(source->q1->kind, "finale") && source->q1->stage <= 4)
      finale_begin(finale, source, source->q1->text, source->q1->stage >= 4);
  }
}
const char *qa_app_source_finale_prepare(qa_app_source_finale *finale) {
  state_t *state = finale->state;
  double revision = finale->revision;
  if (!state) return NULL;
  bool known = false;
  for (size_t i = 0; i < finale->loaded_count; i++) if (strcmp(finale->loaded[i], state->content) == 0) known = true;
  double width = finale->banner_width, height = finale->banner_height;
  if (!known) {
    const char *error = finale->host.load_banner(finale->host.ctx, state->content, &width, &height);
    /* TS: the banner is decoration; the host warns once, the finale ends and the game continues. */
    if (error) {
      if (finale->state == state) { state_free(finale->state); finale->state = NULL; }
      return NULL;
    }
    if (finale->loaded_count == finale->loaded_cap) {
      finale->loaded_cap = finale->loaded_cap ? finale->loaded_cap * 2 : 8;
      finale->loaded = realloc(finale->loaded, finale->loaded_cap * sizeof *finale->loaded);
    }
    finale->loaded[finale->loaded_count++] = dup(state->content);
  }
  char *message = NULL;
  const char *error = finale->host.resolve_message(finale->host.ctx, state->content, state->source_text, &message);
  if (error) return error;
  if (finale->state != state || finale->revision != revision) { free(message); return NULL; }
  finale->banner_width = width;
  finale->banner_height = height;
  finale->prepared = true;
  free(finale->message);
  finale->message = message;
  return NULL;
}
void qa_app_source_finale_draw(qa_app_source_finale *finale, double seconds) {
  state_t *state = finale->state;
  if (!state || !finale->prepared) return;
  double draw_width = finale->host.width, draw_height = finale->host.height;
  double scale = qa_js_min(draw_width / 320, draw_height / 200), height = draw_height / scale;
  if (state->banner)
    finale->host.draw_pic(finale->host.ctx, (draw_width - finale->banner_width * scale) / 2, 16 * scale,
                          finale->banner_width * scale, finale->banner_height * scale);
  size_t line_count = 1;
  for (const char *p = finale->message; *p; p++) if (*p == '\n') line_count++;
  double y = (line_count <= 4 ? trunc(height * 0.35) : 48) * scale;
  long remaining = (long)qa_js_max(0, trunc(8 * (seconds - state->started))) + 1;
  const char *cursor = finale->message;
  for (;;) {
    const char *newline = strchr(cursor, '\n');
    size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
    if (length > 40) length = 40; /* slice(0, 40) */
    char line[41];
    memcpy(line, cursor, length);
    line[length] = '\0';
    double width = finale->host.text_width(finale->host.ctx, line, scale);
    finale->host.draw_text(finale->host.ctx, line, scale, remaining, trunc((draw_width - width) / 2), y);
    remaining -= (long)length;
    if (remaining <= 0 || !newline) break;
    y += 8 * scale;
    cursor = newline + 1;
  }
}
void qa_app_source_finale_resize(qa_app_source_finale *finale, double width, double height) {
  finale->host.width = width;
  finale->host.height = height;
}
const char *qa_app_source_finale_content(const qa_app_source_finale *finale) {
  return finale->state != NULL ? finale->state->content : NULL;
}
