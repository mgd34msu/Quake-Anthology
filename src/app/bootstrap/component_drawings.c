/* Port of src/app/bootstrap/component-drawings.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_drawings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/rerelease/types.h"
#include "qa/debug/world.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* TS DrawingOwner. owner.provider and graph_content are owned copies. */
typedef struct drawing_owner {
  qa_presentation_owner owner;
  qa_world_debug_line_store lines;
  qa_world_text_store text;
  bool has_graph; char *graph_content; qa_source_debug_graph samples; bool has_palette; qa_palette palette;
  double sequence;
  bool retired;
} drawing_owner;
struct qa_app_component_drawings {
  drawing_owner **owners; size_t owner_count, owner_cap; /* TS Map insertion order */
  const qa_world_text **text; size_t text_cap;
  qa_debug_line *lines; size_t line_cap;
};

static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { void *r = realloc(p, n ? n : 1); if (!r) qa_range_error("Out of memory"); return r; }
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *r = xmalloc(n); memcpy(r, s, n); return r; }

qa_app_component_drawings *qa_app_component_drawings_create(void) {
  qa_app_component_drawings *drawings = calloc(1, sizeof *drawings);
  if (drawings == NULL) qa_range_error("Out of memory");
  return drawings;
}
static void graph_drop(drawing_owner *state) {
  free(state->graph_content);
  state->graph_content = NULL; state->has_graph = false; state->has_palette = false;
}
static void owner_free(drawing_owner *state) {
  qa_world_debug_line_store_destroy(&state->lines);
  qa_world_text_store_destroy(&state->text);
  graph_drop(state);
  free((char *)state->owner.provider);
  free(state);
}
static drawing_owner *owner_new(qa_presentation_owner owner) {
  drawing_owner *state = calloc(1, sizeof *state);
  if (state == NULL || !qa_world_debug_line_store_init_default(&state->lines)) qa_range_error("Out of memory");
  qa_world_text_store_init(&state->text);
  state->owner.provider = xstrdup(owner.provider);
  state->owner.generation = owner.generation;
  state->sequence = -1;
  return state;
}
void qa_app_component_drawings_clear(qa_app_component_drawings *drawings) {
  for (size_t i = 0; i < drawings->owner_count; i++) owner_free(drawings->owners[i]);
  drawings->owner_count = 0;
}
void qa_app_component_drawings_destroy(qa_app_component_drawings *drawings) {
  if (drawings == NULL) return;
  qa_app_component_drawings_clear(drawings);
  free(drawings->owners); free(drawings->text); free(drawings->lines);
  free(drawings);
}
static size_t owner_index(const qa_app_component_drawings *drawings, qa_provider_id provider) {
  for (size_t i = 0; i < drawings->owner_count; i++)
    if (strcmp(drawings->owners[i]->owner.provider, provider) == 0) return i;
  return drawings->owner_count;
}
/* TS DebugLine lifetimes are checked safe integers; the store takes int64. */
static int64_t line_lifetime(double now_milliseconds, double lifetime) {
  if (!isfinite(now_milliseconds) || !isfinite(lifetime) || trunc(lifetime) != lifetime || fabs(lifetime) > 9007199254740991.0
      || lifetime < 0 || lifetime > 4294967295.0)
    qa_range_error("Invalid debug line lifetime");
  return (int64_t)lifetime;
}

void qa_app_component_drawings_receive(qa_app_component_drawings *drawings, const qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *source = &events[i];
    bool lifecycle = eq(source->kind, "presentation-owner");
    if (!lifecycle && !eq(source->kind, "debug-graph")
        && (!eq(source->kind, "q2-rerelease")
            || (!eq(source->q2_rerelease->kind, "world-text") && !eq(source->q2_rerelease->kind, "debug-shapes")))) continue;
    const qa_presentation_owner *owner = lifecycle ? &source->presentation_owner.owner : source->has_owner ? &source->owner : NULL;
    if (owner == NULL) continue;
    size_t index = owner_index(drawings, owner->provider);
    drawing_owner *state = index < drawings->owner_count ? drawings->owners[index] : NULL;
    if (state != NULL && state->owner.generation > owner->generation) continue;
    if (state == NULL || state->owner.generation < owner->generation) {
      drawing_owner *replacement = owner_new(*owner);
      if (state != NULL) { owner_free(state); drawings->owners[index] = replacement; }
      else {
        if (drawings->owner_count == drawings->owner_cap) {
          drawings->owner_cap = drawings->owner_cap ? drawings->owner_cap * 2 : 8;
          drawings->owners = xrealloc(drawings->owners, drawings->owner_cap * sizeof *drawings->owners);
        }
        drawings->owners[drawings->owner_count++] = replacement;
      }
      state = replacement;
    }
    if (source->sequence <= state->sequence) continue;
    state->sequence = source->sequence;
    if (lifecycle) {
      qa_world_debug_line_store_clear(&state->lines); qa_world_text_store_clear(&state->text); graph_drop(state);
      if (eq(source->presentation_owner.kind, "retired")) state->retired = true;
      continue;
    }
    if (!state->retired && eq(source->kind, "debug-graph")) {
      if (!state->has_graph) {
        state->has_graph = true; state->graph_content = xstrdup(source->content);
        qa_source_debug_graph_init(&state->samples); state->has_palette = false;
      }
      if (strcmp(state->graph_content, source->content) != 0) qa_raise("Error", 0, 0, "Component debug graph changed source content");
      qa_source_debug_graph_add(&state->samples, source->debug_graph.value, source->debug_graph.color);
      continue;
    }
    if (state->retired || !eq(source->kind, "q2-rerelease")) continue;
    const qa_q2_rerelease_event *event = source->q2_rerelease;
    if (eq(event->kind, "world-text")) {
      qa_world_text text = event->text_value;
      text.content = source->content;
      if (!qa_world_text_store_submit(&state->text, &text, source->seconds, event->lifetime)) qa_range_error("Out of memory");
    } else if (eq(event->kind, "debug-shapes")) {
      double now = source->seconds * 1000;
      if (!qa_world_debug_line_store_submit(&state->lines, event->lines, event->line_count, now,
                                            line_lifetime(now, event->lifetime_milliseconds)))
        qa_range_error("Out of memory");
    }
  }
}

const char *qa_app_component_drawings_prepare_graphs(qa_app_component_drawings *drawings,
                                                     qa_app_drawings_load_palette_fn load_palette, void *load_ctx) {
  for (size_t i = 0; i < drawings->owner_count; i++) {
    drawing_owner *state = drawings->owners[i];
    if (state->retired || !state->has_graph || state->has_palette) continue;
    char *content = state->graph_content;
    qa_palette palette; const char *error = NULL;
    if (!load_palette(load_ctx, content, &palette, &error)) return error;
    /* TS: !state.retired && state.graph === graph && owners.get(provider) === state. */
    size_t index = owner_index(drawings, state->owner.provider);
    if (!state->retired && state->has_graph && state->graph_content == content && index < drawings->owner_count
        && drawings->owners[index] == state) {
      state->palette = palette; state->has_palette = true;
    }
  }
  return NULL;
}

typedef struct graph_draw { const qa_draw2d *draw; const qa_palette *palette; } graph_draw;
static void graph_fill(void *ctx, qa_rect rect, qa_vec4 color, const qa_picture_asset *white) {
  qa_draw2d_fill_rect(((graph_draw *)ctx)->draw, rect, color, white);
}
static qa_vec4 graph_color(void *ctx, double index) { return qa_debug_graph_color(*((graph_draw *)ctx)->palette, index); }

double qa_app_component_drawings_draw_graphs(qa_app_component_drawings *drawings, const qa_draw2d *draw, qa_rect view,
                                             qa_debug_graph_settings settings, const qa_picture_asset *white) {
  if ((settings.debuggraph == 0 && settings.timegraph == 0 && settings.netgraph == 0) || !isfinite(settings.height)
      || trunc(settings.height) <= 0) return 0;
  double bottom = view.y + view.height;
  for (size_t i = 0; i < drawings->owner_count; i++) {
    drawing_owner *state = drawings->owners[i];
    if (state->retired || !state->has_graph || !state->has_palette) continue;
    qa_rect rect = view;
    rect.height = bottom - view.y;
    graph_draw ctx = {draw, &state->palette};
    qa_source_debug_graph_draw(&state->samples, graph_fill, &ctx, rect, settings, graph_color, &ctx, white);
    bottom -= settings.height;
    if (bottom <= view.y) break;
  }
  return qa_js_min(view.height, view.y + view.height - bottom);
}

qa_app_component_drawings_snapshot qa_app_component_drawings_snapshot_at(qa_app_component_drawings *drawings,
                                                                         double seconds, double frame) {
  size_t text_count = 0, line_count = 0;
  for (size_t i = 0; i < drawings->owner_count; i++) {
    drawing_owner *state = drawings->owners[i];
    if (state->retired) continue;
    size_t texts = 0, lines = 0;
    const qa_world_text *const *text = qa_world_text_store_snapshot(&state->text, seconds, frame, &texts);
    const qa_debug_line *line = qa_world_debug_line_store_snapshot(&state->lines, seconds * 1000, frame, &lines);
    if (text_count + texts > drawings->text_cap) {
      drawings->text_cap = (text_count + texts) * 2;
      drawings->text = xrealloc(drawings->text, drawings->text_cap * sizeof *drawings->text);
    }
    if (line_count + lines > drawings->line_cap) {
      drawings->line_cap = (line_count + lines) * 2;
      drawings->lines = xrealloc(drawings->lines, drawings->line_cap * sizeof *drawings->lines);
    }
    for (size_t t = 0; t < texts; t++) drawings->text[text_count++] = text[t];
    if (lines > 0) memcpy(&drawings->lines[line_count], line, lines * sizeof *line);
    line_count += lines;
  }
  qa_app_component_drawings_snapshot result = {drawings->text, text_count, drawings->lines, line_count};
  return result;
}
