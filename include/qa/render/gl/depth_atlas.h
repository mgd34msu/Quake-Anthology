/* Port of src/render/gl/depth-atlas.ts. Reduced: draw validates passes
 * but does not rasterize into the atlas FBO yet (see slice unresolved).
 * GPL-2.0-or-later. */
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
bool qa_depth_atlas_target_init(qa_depth_atlas_target *target, const qa_gl_context *context, qa_gl *gl, qa_gl_stage_program *program);
void qa_depth_atlas_target_draw(qa_depth_atlas_target *target, double texture, double width, double height, const qa_depth_atlas_pass *passes, size_t pass_count);
void qa_depth_atlas_target_close(qa_depth_atlas_target *target);
#endif
