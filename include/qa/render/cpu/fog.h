/* Port of src/render/cpu/fog.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_FOG_H
#define QA_RENDER_CPU_FOG_H
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/materials/legacy_fog.h"
#include "qa/render/cpu/triangle_kernel.h"
double qa_eye_depth_from_window_depth(qa_mat4 projection, double window_depth, const double depth_range[2]);
double qa_eye_depth_from_window_depth_default(qa_mat4 projection, double window_depth);
/* output: "truecolor"|"classic-indexed". */
void qa_apply_q1_depth_fog(qa_cpu_framebuffer *frame, qa_rect viewport, qa_mat4 projection, qa_q1_fog fog, double sky_fraction, const char *output, double clear_depth);
void qa_apply_q2_depth_fog(qa_cpu_framebuffer *frame, qa_q2_fog_operation operation, double alpha_bits);
#endif
