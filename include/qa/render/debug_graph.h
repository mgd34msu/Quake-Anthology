/* Port of src/render/debug-graph.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_DEBUG_GRAPH_H
#define QA_RENDER_DEBUG_GRAPH_H
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/text/draw2d.h"
typedef struct qa_debug_graph_settings { double debuggraph, timegraph, netgraph, height, scale, shift; } qa_debug_graph_settings;
typedef struct qa_debug_graph_bar { qa_rect rect; double color; } qa_debug_graph_bar;
typedef struct qa_debug_graph_packet { double dropped, suppressed, ping_milliseconds; } qa_debug_graph_packet;
/* TS throws Error("Incomplete debug graph palette") -> qa_range_error. */
qa_vec4 qa_debug_graph_color(qa_palette palette, double index);
typedef struct qa_source_debug_graph { float values[1024]; int32_t colors[1024]; int current; } qa_source_debug_graph;
void qa_source_debug_graph_init(qa_source_debug_graph *g);
void qa_source_debug_graph_add(qa_source_debug_graph *g, double value, double color);
void qa_source_debug_graph_add_frame(qa_source_debug_graph *g, double seconds, qa_debug_graph_settings s);
void qa_source_debug_graph_add_network(qa_source_debug_graph *g, qa_debug_graph_packet packet, qa_debug_graph_settings s);
/* Writes up to capacity bars; returns total bar count (TS array length). */
size_t qa_source_debug_graph_bars(const qa_source_debug_graph *g, qa_rect rect, qa_debug_graph_settings s, qa_debug_graph_bar *out, size_t capacity);
typedef qa_vec4 (*qa_debug_graph_palette_color_fn)(void *ctx, double index);
typedef void (*qa_debug_graph_fill_fn)(void *ctx, qa_rect rect, qa_vec4 color, const qa_picture_asset *white);
/* TS draw(draw: Draw2D,...): Draw2D fillRect becomes fn+ctx. */
void qa_source_debug_graph_draw(const qa_source_debug_graph *g, qa_debug_graph_fill_fn fill, void *fill_ctx, qa_rect rect, qa_debug_graph_settings s, qa_debug_graph_palette_color_fn palette_color, void *palette_ctx, const qa_picture_asset *white);
#endif
