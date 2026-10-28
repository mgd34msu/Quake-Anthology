/* Port of src/app/bootstrap/component-drawings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_drawings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct {
  qa_presentation_owner owner;
  const char *graph_content;
  bool has_graph;
  double sequence;
  bool retired;
} owner_state;
struct qa_app_component_drawings {
  qa_app_component_drawings_host host;
  owner_state *owners; size_t owner_count, owner_cap;
};
static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
qa_app_component_drawings *qa_app_component_drawings_create(qa_app_component_drawings_host host) {
  qa_app_component_drawings *drawings = calloc(1, sizeof *drawings);
  if (!drawings) return NULL;
  drawings->host = host;
  return drawings;
}
void qa_app_component_drawings_destroy(qa_app_component_drawings *drawings) {
  if (!drawings) return;
  free(drawings->owners);
  free(drawings);
}
static owner_state *find_owner(qa_app_component_drawings *drawings, const char *provider) {
  for (size_t i = 0; i < drawings->owner_count; i++)
    if (strcmp(drawings->owners[i].owner.provider, provider) == 0) return &drawings->owners[i];
  return NULL;
}
void qa_app_component_drawings_receive(qa_app_component_drawings *drawings, const qa_app_drawing_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_drawing_event *source = &events[i];
    bool lifecycle = eq(source->kind, "presentation-owner");
    if (!lifecycle && !eq(source->kind, "debug-graph") &&
        !(eq(source->kind, "q2-rerelease") && (eq(source->event_kind, "world-text") || eq(source->event_kind, "debug-shapes")))) continue;
    if (!source->has_owner) continue;
    qa_presentation_owner owner = source->owner;
    owner_state *state = find_owner(drawings, owner.provider);
    if (state && state->owner.generation > owner.generation) continue;
    if (!state || state->owner.generation < owner.generation) {
      if (!state) {
        if (drawings->owner_count == drawings->owner_cap) {
          drawings->owner_cap = drawings->owner_cap ? drawings->owner_cap * 2 : 8;
          drawings->owners = realloc(drawings->owners, drawings->owner_cap * sizeof *drawings->owners);
        }
        state = &drawings->owners[drawings->owner_count++];
      }
      *state = (owner_state){owner, NULL, false, -1, false};
    }
    if (source->sequence <= state->sequence) continue;
    state->sequence = source->sequence;
    if (lifecycle) {
      drawings->host.clear_owner(drawings->host.ctx, owner.provider);
      state->has_graph = false;
      state->graph_content = NULL;
      if (eq(source->event_kind, "retired")) state->retired = true;
      continue;
    }
    if (!state->retired && eq(source->kind, "debug-graph")) {
      if (!state->has_graph) { state->has_graph = true; state->graph_content = source->content; }
      if (strcmp(state->graph_content, source->content) != 0)
        qa_range_error("Component debug graph changed source content");
      drawings->host.graph_add(drawings->host.ctx, owner.provider, source->content, source->graph_value, source->graph_color);
      continue;
    }
    if (state->retired || !eq(source->kind, "q2-rerelease")) continue;
    if (eq(source->event_kind, "world-text"))
      drawings->host.text_submit(drawings->host.ctx, owner.provider, source->world_text, source->content, source->seconds, source->text_lifetime);
    else if (eq(source->event_kind, "debug-shapes"))
      drawings->host.lines_submit(drawings->host.ctx, owner.provider, source->lines, source->line_count, source->seconds * 1000, source->line_lifetime_ms);
  }
}
const char *qa_app_component_drawings_prepare_graphs(qa_app_component_drawings *drawings) {
  for (size_t i = 0; i < drawings->owner_count; i++) {
    owner_state *state = &drawings->owners[i];
    if (state->retired || !state->has_graph || drawings->host.graph_has_palette(drawings->host.ctx, state->owner.provider)) continue;
    const void *palette = NULL;
    const char *error = drawings->host.load_palette(drawings->host.ctx, state->graph_content, &palette);
    if (error) return error;
    owner_state *after = find_owner(drawings, state->owner.provider);
    if (after == state && !state->retired && state->has_graph)
      drawings->host.graph_set_palette(drawings->host.ctx, state->owner.provider, palette);
  }
  return NULL;
}
double qa_app_component_drawings_draw_graphs(qa_app_component_drawings *drawings, qa_rect view,
                                             double debuggraph, double timegraph, double netgraph, double graph_height) {
  if ((debuggraph == 0 && timegraph == 0 && netgraph == 0) || !isfinite(graph_height) || trunc(graph_height) <= 0) return 0;
  double bottom = view.y + view.height;
  for (size_t i = 0; i < drawings->owner_count; i++) {
    owner_state *state = &drawings->owners[i];
    if (state->retired || !state->has_graph || !drawings->host.graph_has_palette(drawings->host.ctx, state->owner.provider)) continue;
    qa_rect rect = view;
    rect.height = bottom - view.y;
    drawings->host.graph_draw(drawings->host.ctx, state->owner.provider, rect, graph_height);
    bottom -= graph_height;
    if (bottom <= view.y) break;
  }
  return fmin(view.height, view.y + view.height - bottom);
}
size_t qa_app_component_drawings_snapshot(qa_app_component_drawings *drawings, double seconds, double frame, size_t *out_text_count) {
  size_t texts = 0, lines = 0;
  for (size_t i = 0; i < drawings->owner_count; i++) {
    if (drawings->owners[i].retired) continue;
    size_t text_count = 0;
    lines += drawings->host.snapshot(drawings->host.ctx, drawings->owners[i].owner.provider, seconds, frame, &text_count);
    texts += text_count;
  }
  *out_text_count = texts;
  return lines;
}
void qa_app_component_drawings_clear(qa_app_component_drawings *drawings) {
  for (size_t i = 0; i < drawings->owner_count; i++)
    drawings->host.clear_owner(drawings->host.ctx, drawings->owners[i].owner.provider);
  drawings->owner_count = 0;
}
