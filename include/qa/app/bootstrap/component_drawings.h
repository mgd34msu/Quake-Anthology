/* Port of src/app/bootstrap/component-drawings.ts (ComponentDrawings):
 * each viewing seat consumes source drawing events with their activation
 * and server clock, over the real WorldDebugLineStore, WorldTextStore and
 * SourceDebugGraph. C accommodations: the async prepareGraphs loader is a
 * synchronous fn+ctx that returns false with a borrowed error where the TS
 * promise rejects; snapshot returns views owned by the drawings, valid until
 * the next receive, snapshot, clear or destroy. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_COMPONENT_DRAWINGS_H
#define QA_APP_BOOTSTRAP_COMPONENT_DRAWINGS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/render.h"
#include "qa/debug/shapes.h"
#include "qa/render/debug_graph.h"
#include "qa/text/draw2d.h"
#include "qa/text/world.h"

typedef struct qa_app_component_drawings qa_app_component_drawings;

/* TS loadPalette: (content) => Promise<Palette>. The palette is borrowed:
 * its bytes must stay valid while the drawings keep it (until the owner is
 * replaced, cleared or destroyed). */
typedef bool (*qa_app_drawings_load_palette_fn)(void *ctx, qa_content_id content, qa_palette *out, const char **out_error);

/* TS snapshot() result. */
typedef struct qa_app_component_drawings_snapshot {
  const qa_world_text *const *text; size_t text_count;
  const qa_debug_line *lines; size_t line_count;
} qa_app_component_drawings_snapshot;

qa_app_component_drawings *qa_app_component_drawings_create(void);
void qa_app_component_drawings_destroy(qa_app_component_drawings *drawings);
void qa_app_component_drawings_receive(qa_app_component_drawings *drawings, const qa_app_sim_presentation_event *events, size_t count);
/* NULL on success, else the loader's error. */
const char *qa_app_component_drawings_prepare_graphs(qa_app_component_drawings *drawings,
                                                     qa_app_drawings_load_palette_fn load_palette, void *load_ctx);
double qa_app_component_drawings_draw_graphs(qa_app_component_drawings *drawings, const qa_draw2d *draw, qa_rect view,
                                             qa_debug_graph_settings settings, const qa_picture_asset *white);
qa_app_component_drawings_snapshot qa_app_component_drawings_snapshot_at(qa_app_component_drawings *drawings,
                                                                         double seconds, double frame);
void qa_app_component_drawings_clear(qa_app_component_drawings *drawings);
#endif
