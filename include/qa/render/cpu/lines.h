/* Port of src/render/cpu/lines.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_LINES_H
#define QA_RENDER_CPU_LINES_H
#include "qa/contracts/math.h"
#include "qa/render/cpu/lighting.h"
#include "qa/render/cpu/triangle_kernel.h"
typedef struct qa_line_fragment { double x, y, depth, eye_depth; qa_vec3 world_position, world_normal; qa_vec4 color; qa_vec2 tex_coord, tex_coord2; qa_line_texture_derivative tex_coord_derivative, tex_coord2_derivative; } qa_line_fragment;
typedef struct qa_line_scissor { double min_x, min_y, max_x, max_y; } qa_line_scissor;
typedef void (*qa_line_emit_fn)(void *ctx, qa_line_fragment fragment);
void qa_rasterize_aliased_line(qa_cpu_vertex first, qa_cpu_vertex second, double width, double height, double line_width, qa_line_scissor scissor, qa_line_emit_fn emit, void *emit_ctx);
#endif
