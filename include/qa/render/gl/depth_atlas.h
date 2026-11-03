/* Port of src/render/gl/depth-atlas.ts: Q2 rerelease gl_shadowmap.ts depth
 * passes using already projected caster geometry. The TS identity caches
 * (validated draws, packed positions) only avoid repeated work; C validates and
 * converts each draw, producing the same bytes. TS throws become qa_range_error
 * aborts (docs/PORTING.md). GPL-2.0-or-later. */
#ifndef QA_RENDER_GL_DEPTH_ATLAS_H
#define QA_RENDER_GL_DEPTH_ATLAS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/render.h"
#include "qa/platform/gl.h"
#include "qa/render/gl/programs.h"
typedef struct qa_depth_atlas_target qa_depth_atlas_target;
struct qa_depth_atlas_target { void *impl; };
/* False on OOM or when retaining the context's procedures fails. */
bool qa_depth_atlas_target_init(qa_depth_atlas_target *target, const qa_gl_context *context, qa_gl *gl, qa_gl_stage_program *program);
void qa_depth_atlas_target_draw(qa_depth_atlas_target *target, double texture, double width, double height, const qa_depth_atlas_pass *passes, size_t pass_count);
void qa_depth_atlas_target_close(qa_depth_atlas_target *target);
#endif
