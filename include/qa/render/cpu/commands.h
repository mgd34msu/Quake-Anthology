/* Port of src/render/cpu/commands.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_CPU_COMMANDS_H
#define QA_RENDER_CPU_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/render/cpu/rasterizer.h"
/* TS CpuPresenter = Pick<SdlWindow,"backend"|"drawableSize"|"present">. */
typedef struct qa_cpu_presenter { void *ctx; const char *backend; void (*drawable_size)(void *ctx, double *width, double *height); void (*present)(void *ctx, const uint8_t *pixels, size_t length); } qa_cpu_presenter;
typedef struct qa_cpu_render_target { qa_software_renderer *backend; const qa_cpu_presenter *presenter; qa_vec4 color; } qa_cpu_render_target;
/* presenter NULL = TS null. Non-"cpu" presenter aborts (TS Error). */
void qa_cpu_render_target_init(qa_cpu_render_target *target, qa_software_renderer *backend, const qa_cpu_presenter *presenter);
void qa_cpu_render_target_execute(qa_cpu_render_target *target, qa_render_frame frame);
void qa_cpu_render_target_present(qa_cpu_render_target *target);
void qa_cpu_render_target_close(qa_cpu_render_target *target);
#endif
