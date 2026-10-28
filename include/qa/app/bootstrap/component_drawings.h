/* Port of src/app/bootstrap/component-drawings.ts. World text/line
 * stores and debug-graph drawing are host rows keyed by the owner
 * token this module hands out; generation/sequence gating is kept
 * here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_DRAWINGS_H
#define QA_APP_BOOTSTRAP_COMPONENT_DRAWINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/presentation.h"
#include "qa/contracts/render.h"
/* Presentation event fields read here. */
typedef struct qa_app_drawing_event {
  const char *kind; const char *event_kind;
  bool has_owner; qa_presentation_owner owner;
  const char *content;
  double sequence, seconds;
  double graph_value; double graph_color; /* debug-graph */
  const void *world_text; double text_lifetime; /* world-text */
  const void *lines; size_t line_count; double line_lifetime_ms; /* debug-shapes */
} qa_app_drawing_event;
typedef struct qa_app_component_drawings qa_app_component_drawings;
typedef struct qa_app_component_drawings_host {
  void *ctx;
  /* Store operations keyed by the owner token (provider text). */
  void (*clear_owner)(void *ctx, const char *provider);
  void (*graph_add)(void *ctx, const char *provider, const char *content, double value, double color);
  void (*text_submit)(void *ctx, const char *provider, const void *text, const char *content, double seconds, double lifetime);
  void (*lines_submit)(void *ctx, const char *provider, const void *lines, size_t count, double milliseconds, double lifetime_ms);
  bool (*graph_has_palette)(void *ctx, const char *provider);
  void (*graph_set_palette)(void *ctx, const char *provider, const void *palette);
  const char *(*load_palette)(void *ctx, const char *content, const void **out_palette);
  void (*graph_draw)(void *ctx, const char *provider, qa_rect view, double height);
  size_t (*snapshot)(void *ctx, const char *provider, double seconds, double frame, size_t *text_count);
} qa_app_component_drawings_host;
qa_app_component_drawings *qa_app_component_drawings_create(qa_app_component_drawings_host host);
void qa_app_component_drawings_destroy(qa_app_component_drawings *drawings);
void qa_app_component_drawings_receive(qa_app_component_drawings *drawings, const qa_app_drawing_event *events, size_t count);
const char *qa_app_component_drawings_prepare_graphs(qa_app_component_drawings *drawings);
/* settings: debuggraph/timegraph/netgraph/height as doubles. */
double qa_app_component_drawings_draw_graphs(qa_app_component_drawings *drawings, qa_rect view,
                                             double debuggraph, double timegraph, double netgraph, double graph_height);
/* Returns total line count; *out_text_count set. */
size_t qa_app_component_drawings_snapshot(qa_app_component_drawings *drawings, double seconds, double frame, size_t *out_text_count);
void qa_app_component_drawings_clear(qa_app_component_drawings *drawings);
#endif
