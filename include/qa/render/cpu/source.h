/* Port of src/render/cpu/source.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_SOURCE_H
#define QA_RENDER_CPU_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
/* TS SourcePrimitiveMode exact strings. */
typedef const char *qa_source_primitive_mode;
qa_source_primitive_mode qa_source_primitive_mode_for(double requested, bool compiled_arrays);
typedef struct qa_source_strip_emitter { void *ctx; void (*begin)(void *ctx); void (*element)(void *ctx, double index); void (*end)(void *ctx); } qa_source_strip_emitter;
void qa_emit_source_triangle_strips(const double *indices, size_t count, qa_source_strip_emitter emit);
typedef struct qa_source_stage_cell { qa_vec4 color; qa_vec2 tex_coord, tex_coord2, raw_tex_coord, raw_tex_coord2; } qa_source_stage_cell;
/* TS SourceStageData: kind exact strings "generic-single"|"vertex-lit"|"dlight"|"fog"|"generic-pair"|"lightmapped-pair". */
typedef struct qa_source_stage_data { double state_bits; const char *kind; qa_draw_batch batch; const qa_source_stage_cell *scratch; size_t scratch_count; } qa_source_stage_data;
typedef struct qa_prepared_source_draw { void *ctx; void (*begin)(void *ctx); void (*prepare_texture)(void *ctx, double unit); void (*apply_texture)(void *ctx, double unit, qa_texture_binding op); void (*finish_textures)(void *ctx); void (*draw)(void *ctx, double primitives); void (*cleanup)(void *ctx); } qa_prepared_source_draw;
#endif
